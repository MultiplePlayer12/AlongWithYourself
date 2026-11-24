// Copyright Epic Games, Inc. All Rights Reserved.

#include "Room/RoomShell.h"

#include "AlongWithYourself.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Room/RoomBall.h"
#include "UObject/ConstructorHelpers.h"

ARoomShell::ARoomShell()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Actor 原点 = 立方体形心，旋转时保持不动
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 旋转枢轴：外壳 + 内部碰撞体都挂在这里，绕形心转
	ShellPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShellPivot"));
	ShellPivot->SetupAttachment(Root);

	// 玩家视角：钉在侧墙内侧的一台固定机位，跟着枢轴一起翻
	FaceCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FaceCamera"));
	FaceCamera->SetupAttachment(ShellPivot);
	FaceCamera->SetMobility(EComponentMobility::Movable);
	FaceCamera->SetRelativeLocation(FVector(0.f, -200.f, 150.f));
	FaceCamera->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	FaceCamera->SetFieldOfView(CameraFOV);
	FaceCamera->bUsePawnControlRotation = false;
	FaceCamera->SetActive(true);

	// 让「这个 Actor 当视点」时能自动找到上面那台摄像机
	bFindCameraComponentWhenViewTarget = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	// ---- 六个可见面 ----
	ShellFaces.Reserve(6);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FName CompName(*FString::Printf(TEXT("ShellFace_%d"), Index));

		UStaticMeshComponent* Face = CreateDefaultSubobject<UStaticMeshComponent>(CompName);
		Face->SetupAttachment(ShellPivot);
		Face->SetMobility(EComponentMobility::Movable);
		Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Face->SetCastShadow(true);
		Face->bCastDynamicShadow = true;
		Face->SetGenerateOverlapEvents(false);

		if (CubeMesh.Succeeded())
		{
			Face->SetStaticMesh(CubeMesh.Object);
		}

		ShellFaces.Add(Face);
	}

	// ---- 不可见碰撞体：0 地板、1 天花板、2.. 围墙分段 ----
	// 关键：挂在 ShellPivot 上，与外壳整体一起转。
	// 这样盒子翻到任何角度，角色都站在「当前朝下的那一面」上，永远贴着内壁，不会穿出去。
	BoundaryColliders.Reserve(MaxBoundaryColliders);
	for (int32 Index = 0; Index < MaxBoundaryColliders; ++Index)
	{
		const FName CompName(*FString::Printf(TEXT("Boundary_%d"), Index));

		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(CompName);
		Box->SetupAttachment(ShellPivot);
		Box->SetMobility(EComponentMobility::Movable);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Box->SetGenerateOverlapEvents(false);
		Box->SetCanEverAffectNavigation(false);
		Box->SetHiddenInGame(true);
		Box->ShapeColor = FColor(80, 200, 255);

		BoundaryColliders.Add(Box);
	}

	if (CubeMesh.Succeeded())
	{
		WallMesh = CubeMesh.Object;
	}
}

void ARoomShell::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	Rebuild();
}

void ARoomShell::BeginPlay()
{
	Super::BeginPlay();

	// 先按当前参数建一次（此时玩家 Pawn 可能还没生成）
	Rebuild();

	// 优先按真实角色身高修正一次尺寸
	if (bAutoSizeToCharacter)
	{
		if (const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0))
		{
			bSizedToCharacter = true;
			SetCharacterHeight(PlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f);
		}
	}
	else
	{
		bSizedToCharacter = true;
	}

	// 尺寸确定后再放球，保证球落在正确的内部空间里
	SpawnBallIfMissing();
}

ARoomBall* ARoomShell::SpawnBallIfMissing()
{
	if (Ball)
	{
		return Ball;
	}

	if (!bSpawnBall)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 关卡里已经手动摆了一个球就不再重复生成
	for (TActorIterator<ARoomBall> It(World); It; ++It)
	{
		Ball = *It;
		return Ball;
	}

	const float Radius = FMath::Max(BallRadius, 2.f);
	const float Thickness = FMath::Clamp(WallThickness, 1.f, RoomSize * 0.08f);
	const float InnerHeight = FMath::Max(RoomSize - 2.f * Thickness, Radius * 3.f);

	// 出生高度：按房间比例，夹在「至少两个球径」和「别顶到天花板」之间
	const float DropHeight = FMath::Clamp(RoomSize * BallSpawnHeightRatio, Radius * 2.f, InnerHeight - Radius * 1.2f);

	// 稍微偏开中心，免得正好砸在出生点上的角色头顶
	const FVector SpawnLocation = GetInnerFloorLocation() + FVector(RoomSize * 0.15f, 0.f, DropHeight);
	const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);

	UClass* ClassToSpawn = BallClass ? BallClass.Get() : ARoomBall::StaticClass();

	// 用 Deferred 生成：先改好半径 / 材质，再让 BeginPlay 打开物理，避免先掉一下再变尺寸
	ARoomBall* NewBall = World->SpawnActorDeferred<ARoomBall>(
		ClassToSpawn,
		SpawnTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!NewBall)
	{
		return nullptr;
	}

	NewBall->SetBallRadius(Radius);
	UGameplayStatics::FinishSpawningActor(NewBall, SpawnTransform);

	Ball = NewBall;

	return Ball;
}

void ARoomShell::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 心跳：每秒一行，用来确认 Tick 本身还在跑（诊断卡顿用）
	if (bLogBalance)
	{
		BalanceHeartbeat += DeltaSeconds;
		if (BalanceHeartbeat >= 1.f)
		{
			BalanceHeartbeat = 0.f;
			UE_LOG(LogAlongWithYourself, Log,
				TEXT("[Tick] dt=%.4f 帧=%llu 倾角=%.1f° 求解器=%d 角速度=%.1f°/s"),
				DeltaSeconds,
				static_cast<uint64>(GFrameCounter),
				TiltDegrees,
				bEnableInertialRotation ? 1 : 0,
				AngularVelocityDeg.Size());
		}
	}

	// 角色若比房间晚生成，这里补一次缩放（只做一次）
	if (bAutoSizeToCharacter && !bSizedToCharacter)
	{
		if (const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0))
		{
			bSizedToCharacter = true;
			SetCharacterHeight(PlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f);
		}
	}

	// ------------------------------------------------------------------ 姿态更新
	if (bEnableInertialRotation)
	{
		// 物理驱动：角色 / 小球的重量把盒子拽向「重心垂在形心正下方」的姿态
		UpdateInertialRotation(DeltaSeconds);
	}
	else if (!CurrentRotation.Equals(TargetRotation, 1.e-4f))
	{
		// 旧路径：鼠标拖出来的目标姿态，球面插值，任意轴都走最短路径
		CurrentRotation = FMath::QInterpTo(CurrentRotation, TargetRotation, DeltaSeconds, FMath::Max(RotationInterpSpeed, 0.01f));
		CurrentRotation.Normalize();
		ShellPivot->SetRelativeRotation(CurrentRotation);
	}

	RescueBallIfOutside();
	RescuePawnIfOutside();
}

FVector ARoomShell::ComputeContentCenterOfMass(float& OutTotalMass) const
{
	// 空盒子的重心就在形心上，对力矩没有贡献，所以这里只统计「内容物」：
	// 角色（按体重）+ 小球（按质量）。谁在哪边，重心就偏向哪边。
	float SumMass = 0.f;
	FVector SumMoment = FVector::ZeroVector;

	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (const ACharacter* PlayerCharacter = Cast<ACharacter>(PlayerPawn))
		{
			const UCapsuleComponent* Capsule = PlayerCharacter->GetCapsuleComponent();
			if (Capsule)
			{
				// 人体重心大约在身高的 55%，比胶囊中心略低一点
				const FVector BodyCenter = Capsule->GetComponentLocation()
					- Capsule->GetUpVector() * (Capsule->GetScaledCapsuleHalfHeight() * 0.25f);

				const float Mass = FMath::Max(CharacterMass, 1.f);
				SumMoment += BodyCenter * Mass;
				SumMass += Mass;
			}
		}
	}

	if (Ball)
	{
		const float Mass = FMath::Max(BallMass, 0.f);
		if (Mass > 0.f)
		{
			SumMoment += Ball->GetActorLocation() * Mass;
			SumMass += Mass;
		}
	}

	OutTotalMass = SumMass;

	if (SumMass <= KINDA_SMALL_NUMBER)
	{
		return ShellPivot->GetComponentLocation();
	}

	return SumMoment / SumMass;
}

void ARoomShell::UpdateInertialRotation(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f || !ShellPivot)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float GravityZ = FMath::Abs(World ? World->GetGravityZ() : -980.f);

	// ---- 1. 内容物重心 → 相对形心的偏移 r ----
	float TotalMass = 0.f;
	const FVector WorldCOM = ComputeContentCenterOfMass(TotalMass);
	const FVector PivotLocation = ShellPivot->GetComponentLocation();
	const FVector R = WorldCOM - PivotLocation;

	CenterOfMassLocal = ShellPivot->GetComponentTransform().InverseTransformPosition(WorldCOM);

	// ---- 2. 重力力矩 τ = r × F（力矩天然过形心方向，纯转动，不动位置）----
	const float TotalWeight = TotalMass * GravityZ;		// kg·cm/s²
	FVector Torque = FVector::CrossProduct(R, FVector(0.f, 0.f, -TotalWeight));

	// ---- 3. 可选的「落地回正」力矩：让盒子像被地板托住，只倾有限角度 ----
	if (UprightStiffness > 0.f && TotalWeight > KINDA_SMALL_NUMBER)
	{
		const FVector BoxUp = ShellPivot->GetUpVector();
		const float LeanAngle = FMath::Acos(FMath::Clamp(BoxUp | FVector::UpVector, -1.f, 1.f));
		const FVector LeanAxis = FVector::CrossProduct(BoxUp, FVector::UpVector).GetSafeNormal();

		if (!LeanAxis.IsNearlyZero() && LeanAngle > KINDA_SMALL_NUMBER)
		{
			Torque += LeanAxis * (UprightStiffness * LeanAngle * TotalWeight * RoomSize * 0.5f);
		}
	}

	// ---- 4. 绕形心的转动惯量（kg·cm²）----
	// 薄壁立方体：I ≈ M·a²/6；内容物按质点 m·r² 叠加。
	float Inertia = FMath::Max(ShellMass, 1.f) * RoomSize * RoomSize / 6.f;
	Inertia += TotalMass * R.SizeSquared();
	Inertia = FMath::Max(Inertia, 1000.f);

	// ---- 5. 角加速度 → 角速度（世界空间，单位 rad/s）----
	FVector AngularVelocityRad = AngularVelocityDeg * (PI / 180.f);
	AngularVelocityRad += (Torque / Inertia) * (DeltaSeconds * FMath::Max(InertialResponse, 0.f));
	AngularVelocityRad *= FMath::Exp(-FMath::Max(AngularDamping, 0.f) * DeltaSeconds);

	const float MaxRad = FMath::DegreesToRadians(FMath::Clamp(MaxAngularSpeed, 1.f, 720.f));
	if (AngularVelocityRad.SizeSquared() > MaxRad * MaxRad)
	{
		AngularVelocityRad = AngularVelocityRad.GetSafeNormal() * MaxRad;
	}

	AngularVelocityDeg = AngularVelocityRad * (180.f / PI);

	// ---- 6. 角速度积分成姿态四元数 ----
	const float Speed = AngularVelocityRad.Size();
	const FQuat RootQuat = Root ? Root->GetComponentQuat() : FQuat::Identity;
	const FQuat OldWorldQuat = RootQuat * CurrentRotation;
	FQuat WorldQuat = OldWorldQuat;

	if (Speed > 1.e-5f)
	{
		// 世界角速度左乘：绕「过形心的世界轴」转，形心不动
		WorldQuat = FQuat(AngularVelocityRad / Speed, Speed * DeltaSeconds) * WorldQuat;
		WorldQuat.Normalize();
	}

	// ---- 6.5 倾角限位：把超出部分转回来，并把角速度清零 ----
	if (MaxTiltAngle > 0.f && MaxTiltAngle < 179.f)
	{
		const FVector BoxUp = WorldQuat.GetUpVector();
		const float CurrentTilt = FMath::Acos(FMath::Clamp(BoxUp | FVector::UpVector, -1.f, 1.f));
		const float LimitRad = FMath::DegreesToRadians(MaxTiltAngle);

		if (CurrentTilt > LimitRad)
		{
			const FVector FixAxis = FVector::CrossProduct(BoxUp, FVector::UpVector).GetSafeNormal();
			if (!FixAxis.IsNearlyZero())
			{
				WorldQuat = FQuat(FixAxis, CurrentTilt - LimitRad) * WorldQuat;
				WorldQuat.Normalize();

				// 顶到限位就不再累积角速度，否则下一帧会越冲越猛
				AngularVelocityRad = FVector::ZeroVector;
				AngularVelocityDeg = FVector::ZeroVector;
			}
		}
	}

	CurrentRotation = RootQuat.Inverse() * WorldQuat;
	CurrentRotation.Normalize();
	TargetRotation = CurrentRotation;
	ShellPivot->SetRelativeRotation(CurrentRotation);

	// 房间里的角色要跟着这块地板一起被搬走，否则房间只是绕着它空转。
	// 但**不能**在这里直接搬：CharacterMovement 的预处理 Tick 随后会把他摆回去。
	// 这里只累积，等角色自己在 TG_PostPhysics 里消费（见 ApplyPendingCarryToPawn）。
	if (bManualCarryPlayer)
	{
		PendingCarryQuat = (WorldQuat * OldWorldQuat.Inverse()) * PendingCarryQuat;
		PendingCarryQuat.Normalize();
	}

	TiltDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(ShellPivot->GetUpVector() | FVector::UpVector, -1.f, 1.f)));

	// ---- 7. 验证日志 ----
	if (bLogBalance)
	{
		BalanceLogTimer += DeltaSeconds;
		if (BalanceLogTimer >= 0.5f)
		{
			BalanceLogTimer = 0.f;

			FString CharLocal = TEXT("n/a");
			FString BaseName = TEXT("无");
			float CharSpeed = 0.f;
			if (const ACharacter* PlayerCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				CharLocal = ShellPivot->GetComponentTransform()
					.InverseTransformPosition(PlayerCharacter->GetActorLocation()).ToCompactString();

				// 站在旋转地面上是否在「滑动」，看这个：静摩擦生效时应该一直 ≈0
				CharSpeed = PlayerCharacter->GetVelocity().Size2D();

				if (const UPrimitiveComponent* MovementBase = PlayerCharacter->GetMovementBase())
				{
					BaseName = MovementBase->GetName();
				}
			}

			UE_LOG(LogAlongWithYourself, Log,
				TEXT("[Balance] 倾角 %.1f° | 角速度 %.1f°/s | 重心(局部) %s | r=%.0fcm | 质量 %.0fkg | 角色(局部) %s | 滑速 %.1fcm/s | 基座 %s"),
				TiltDegrees,
				AngularVelocityDeg.Size(),
				*CenterOfMassLocal.ToCompactString(),
				R.Size(),
				TotalMass,
				*CharLocal,
				CharSpeed,
				*BaseName);
		}
	}
	else
	{
		BalanceLogTimer = 0.f;
	}
}

void ARoomShell::ApplyPendingCarryToPawn()
{
	if (!bManualCarryPlayer)
	{
		PendingCarryQuat = FQuat::Identity;
		return;
	}

	const FQuat Delta = PendingCarryQuat;
	PendingCarryQuat = FQuat::Identity;

	CarryPlayerWithRotation(Delta);
}

void ARoomShell::ClampPawnToRoomFloor()
{
	if (!bClampPawnToFloor || !ShellPivot)
	{
		return;
	}

	ACharacter* PlayerCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!PlayerCharacter)
	{
		return;
	}

	const UCharacterMovementComponent* Move = PlayerCharacter->GetCharacterMovement();
	const UCapsuleComponent* Capsule = PlayerCharacter->GetCapsuleComponent();
	if (!Move || !Capsule || !Move->IsMovingOnGround())
	{
		return;			// 空中 / 跳起 / 下落时不干预
	}

	const FQuat PivotQuat = ShellPivot->GetComponentQuat();
	const FVector Pivot = ShellPivot->GetComponentLocation();
	const FVector Local = PivotQuat.Inverse().RotateVector(PlayerCharacter->GetActorLocation() - Pivot);

	const float Half = RoomSize * 0.5f;
	const float Thickness = FMath::Clamp(WallThickness, 1.f, RoomSize * 0.08f);
	const float ExpectedZ = -Half + Thickness + Capsule->GetScaledCapsuleHalfHeight();

	// 只有「踩的就是这个房间自己的面」才管；站在小球上（脚底命中球）就放过，
	// 否则会把人从球上生拉下来。
	if (Move->CurrentFloor.HitResult.GetActor() != this)
	{
		return;
	}

	// 硬约束：站在地板上就不可能比「地板 + 胶囊半高」更高。
	// 多出来的那点高度全是参照系打架的产物 —— 盒子一转，人就顺着内壁一点点往上蹭，
	// 几秒钟就爬到墙中间，盒子的重心跟着被顶到枢轴以上，于是又被转成滚筒。
	if (Local.Z <= ExpectedZ + 0.5f)
	{
		return;
	}

	const FVector Clamped = Local + FVector(0.f, 0.f, ExpectedZ - Local.Z);
	PlayerCharacter->SetActorLocation(
		Pivot + PivotQuat.RotateVector(Clamped), false, nullptr, ETeleportType::TeleportPhysics);
}

void ARoomShell::CarryPlayerWithRotation(const FQuat& WorldDeltaQuat)
{
	if (!bManualCarryPlayer || WorldDeltaQuat.Equals(FQuat::Identity, 1.e-9f))
	{
		return;
	}

	ACharacter* PlayerCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!PlayerCharacter || !ShellPivot)
	{
		return;
	}

	// 位移和朝向都要跟着盒子一起转：
	//  - 位置绕形心旋转，人才会真的「站在会转的地板上」；
	//  - 朝向同样转，胶囊才垂直于当前的内壁（否则盒子倾 65° 时人还世界竖直，
	//    在墙上只能用胶囊底部的球尖点着地，看着像躺在空中）。
	const FVector Pivot = ShellPivot->GetComponentLocation();
	const FVector OldLocation = PlayerCharacter->GetActorLocation();
	const FVector NewLocation = Pivot + WorldDeltaQuat.RotateVector(OldLocation - Pivot);
	const FQuat NewQuat = (WorldDeltaQuat * PlayerCharacter->GetActorQuat()).GetNormalized();

	PlayerCharacter->SetActorLocationAndRotation(
		NewLocation, NewQuat, false, nullptr, ETeleportType::TeleportPhysics);
}

void ARoomShell::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (FaceCamera && FaceCamera->IsActive())
	{
		FaceCamera->GetCameraView(DeltaTime, OutResult);
		return;
	}

	Super::CalcCamera(DeltaTime, OutResult);
}

void ARoomShell::UpdateFaceCamera()
{
	if (!FaceCamera)
	{
		return;
	}

	const float Half = FMath::Max(RoomSize, 100.f) * 0.5f;
	const float Thickness = FMath::Clamp(WallThickness, 1.f, RoomSize * 0.08f);
	const float Inner = Half - Thickness;
	const float Distance = FMath::Max(Inner - FMath::Clamp(CameraWallInset, 1.f, Inner * 0.6f), 10.f);
	const float EyeZ = -Inner + FMath::Max(CameraEyeHeight, 10.f);

	// 视线方向（盒子局部空间，水平指向房间中心）
	FVector LookDirection = FVector(0.f, 1.f, 0.f);
	switch (CameraWall)
	{
	case 1: LookDirection = FVector(0.f, -1.f, 0.f); break;		// +Y 墙
	case 2: LookDirection = FVector(1.f, 0.f, 0.f); break;		// -X 墙
	case 3: LookDirection = FVector(-1.f, 0.f, 0.f); break;		// +X 墙
	default: LookDirection = FVector(0.f, 1.f, 0.f); break;		// -Y 墙
	}

	// 站在墙上、朝房间中心：位置 = -视线方向 * 距离
	FaceCamera->SetRelativeLocation(-LookDirection * Distance + FVector(0.f, 0.f, EyeZ));
	FaceCamera->SetRelativeRotation(LookDirection.Rotation());
	FaceCamera->SetFieldOfView(FMath::Clamp(CameraFOV, 30.f, 150.f));
	FaceCamera->SetActive(true);
}

void ARoomShell::AddRotation(const FVector& WorldAxis, float AngleDegrees)
{
	const FVector Axis = WorldAxis.GetSafeNormal();
	if (Axis.IsNearlyZero() || FMath::IsNearlyZero(AngleDegrees))
	{
		return;
	}

	// 世界轴 → 枢轴父空间（Root）的局部轴：只改姿态，形心（Actor 原点）不动
	FQuat ParentQuat = FQuat::Identity;
	if (const USceneComponent* Parent = ShellPivot->GetAttachParent())
	{
		ParentQuat = Parent->GetComponentQuat();
	}
	const FVector LocalAxis = ParentQuat.UnrotateVector(Axis);
	const FQuat DeltaQuat(LocalAxis, FMath::DegreesToRadians(AngleDegrees));

	TargetRotation = DeltaQuat * TargetRotation;
	TargetRotation.Normalize();

	// 物理驱动模式下这一步是「立刻生效」：调试命令 / 蓝图调用可以一把把盒子掰过去，
	// 之后重力力矩会接着把它拽回重心平衡的姿态（松手后自己摆回来，正好能验证求解器）。
	if (bEnableInertialRotation)
	{
		CurrentRotation = DeltaQuat * CurrentRotation;
		CurrentRotation.Normalize();
		TargetRotation = CurrentRotation;
		AngularVelocityDeg = FVector::ZeroVector;
		ShellPivot->SetRelativeRotation(CurrentRotation);
	}
}

void ARoomShell::AddYaw(float DeltaDegrees)
{
	AddRotation(FVector::UpVector, DeltaDegrees);
}

void ARoomShell::ResetRotation()
{
	TargetRotation = FQuat::Identity;
	CurrentRotation = FQuat::Identity;
	AngularVelocityDeg = FVector::ZeroVector;
	ShellPivot->SetRelativeRotation(FRotator::ZeroRotator);
}

void ARoomShell::RebuildRoom()
{
	Rebuild();
}

void ARoomShell::SetCharacterHeight(float Height)
{
	CharacterHeight = FMath::Max(Height, 1.f);

	if (bAutoSizeToCharacter)
	{
		SetRoomSize(CharacterHeight * SizeMultiplier);
	}
}

void ARoomShell::SetRoomSize(float NewSize)
{
	RoomSize = FMath::Max(NewSize, 100.f);
	Rebuild();
}

FVector ARoomShell::GetInnerFloorLocation() const
{
	const float Half = RoomSize * 0.5f;
	const float T = FMath::Clamp(WallThickness, 1.f, RoomSize * 0.08f);

	// 碰撞体挂在枢轴下，内部地板跟着盒子转
	return ShellPivot->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, -Half + T));
}

void ARoomShell::Rebuild()
{
	const float Size = FMath::Max(RoomSize, 100.f);
	const float Half = Size * 0.5f;
	const float Thickness = FMath::Clamp(WallThickness, 1.f, Size * 0.08f);

	// 材质：优先用指定材质，否则尝试工程里的金属材质，最后退回引擎默认材质
	UMaterialInterface* Material = WallMaterial;
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Room/M_RoomMetal.M_RoomMetal"));
	}
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	// ---------------- 可见外壳 ----------------
	// 用六块 100x100x100 的 Cube 拼成空心立方体：Scale = 目标尺寸 / 100
	struct FFaceDef
	{
		FVector Center;
		FVector Size;
	};

	const FFaceDef Faces[6] =
	{
		{ FVector(0.f, 0.f, -Half + Thickness * 0.5f), FVector(Size, Size, Thickness) },	// 地面
		{ FVector(0.f, 0.f,  Half - Thickness * 0.5f), FVector(Size, Size, Thickness) },	// 天花板
		{ FVector( Half - Thickness * 0.5f, 0.f, 0.f), FVector(Thickness, Size, Size) },	// +X
		{ FVector(-Half + Thickness * 0.5f, 0.f, 0.f), FVector(Thickness, Size, Size) },	// -X
		{ FVector(0.f,  Half - Thickness * 0.5f, 0.f), FVector(Size, Thickness, Size) },	// +Y
		{ FVector(0.f, -Half + Thickness * 0.5f, 0.f), FVector(Size, Thickness, Size) }		// -Y
	};

	for (int32 Index = 0; Index < ShellFaces.Num(); ++Index)
	{
		UStaticMeshComponent* Face = ShellFaces[Index];
		if (!Face)
		{
			continue;
		}

		if (WallMesh && Face->GetStaticMesh() != WallMesh)
		{
			Face->SetStaticMesh(WallMesh);
		}

		Face->SetRelativeLocation(Faces[Index].Center);
		Face->SetRelativeRotation(FRotator::ZeroRotator);
		Face->SetRelativeScale3D(Faces[Index].Size / 100.f);
		Face->SetCastShadow(bShellCastsShadow);
		Face->bCastDynamicShadow = bShellCastsShadow;

		if (Material)
		{
			Face->SetMaterial(0, Material);
		}
	}

	// ---------------- 不可见碰撞体 ----------------
	const float FloorTop = -Half + Thickness;
	const float CeilingBottom = Half - Thickness;

	// 地板
	UBoxComponent* FloorCollider = BoundaryColliders.IsValidIndex(0) ? BoundaryColliders[0].Get() : nullptr;
	if (FloorCollider)
	{
		FloorCollider->SetBoxExtent(FVector(Half, Half, Thickness * 0.5f));
		FloorCollider->SetRelativeLocation(FVector(0.f, 0.f, FloorTop - Thickness * 0.5f));
		FloorCollider->SetRelativeRotation(FRotator::ZeroRotator);
		FloorCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// 天花板
	UBoxComponent* CeilingCollider = BoundaryColliders.IsValidIndex(1) ? BoundaryColliders[1].Get() : nullptr;
	if (CeilingCollider)
	{
		CeilingCollider->SetBoxExtent(FVector(Half, Half, Thickness * 0.5f));
		CeilingCollider->SetRelativeLocation(FVector(0.f, 0.f, CeilingBottom + Thickness * 0.5f));
		CeilingCollider->SetRelativeRotation(FRotator::ZeroRotator);
		CeilingCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// 围墙：用一圈多边形分段近似「内切圆」，无论立方体转到什么角度都在内壁以内
	const int32 NumSegments = FMath::Clamp(BoundarySegments, 4, MaxBoundaryColliders - 2);
	const float Radius = FMath::Max(Half - Thickness - FMath::Max(BoundaryInset, 0.f), Size * 0.15f);
	const float SegmentLength = 2.f * Radius * FMath::Tan(PI / static_cast<float>(NumSegments)) + 40.f;
	const float WallHalfHeight = FMath::Max((CeilingBottom - FloorTop) * 0.5f, 10.f);
	const float WallCenterZ = (CeilingBottom + FloorTop) * 0.5f;

	for (int32 Index = 0; Index < NumSegments; ++Index)
	{
		if (!BoundaryColliders.IsValidIndex(2 + Index))
		{
			break;
		}

		UBoxComponent* Wall = BoundaryColliders[2 + Index];

		const float AngleDeg = 360.f / static_cast<float>(NumSegments) * Index;
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);

		Wall->SetBoxExtent(FVector(8.f, SegmentLength * 0.5f, WallHalfHeight));
		Wall->SetRelativeLocation(FVector(FMath::Cos(AngleRad) * Radius, FMath::Sin(AngleRad) * Radius, WallCenterZ));
		Wall->SetRelativeRotation(FRotator(0.f, AngleDeg, 0.f));
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// 多余的分段关掉
	for (int32 Index = 2 + NumSegments; Index < BoundaryColliders.Num(); ++Index)
	{
		if (UBoxComponent* Wall = BoundaryColliders[Index])
		{
			Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// 摄像机坐标依赖房间尺寸 / 墙厚，最后统一摆一次
	UpdateFaceCamera();
}

void ARoomShell::RescueBallIfOutside()
{
	if (!bRescueBall || !Ball)
	{
		return;
	}

	const float Half = RoomSize * 0.5f;
	const float Thickness = FMath::Clamp(WallThickness, 1.f, RoomSize * 0.08f);
	const float Limit = FMath::Max(Half - Thickness - FMath::Max(BallRadius, 2.f), 10.f);

	const FTransform ShellTransform = ShellPivot->GetComponentTransform();
	const FVector Local = ShellTransform.InverseTransformPosition(Ball->GetActorLocation());

	if (FMath::Abs(Local.X) <= Limit && FMath::Abs(Local.Y) <= Limit && FMath::Abs(Local.Z) <= Limit)
	{
		return;
	}

	// 被快速翻转甩到盒外：夹回内壁以内，并停掉速度，避免它一路往下掉
	const FVector Clamped(
		FMath::Clamp(Local.X, -Limit, Limit),
		FMath::Clamp(Local.Y, -Limit, Limit),
		FMath::Clamp(Local.Z, -Limit, Limit));

	Ball->SetActorLocation(ShellTransform.TransformPosition(Clamped), false, nullptr, ETeleportType::TeleportPhysics);

	if (UPrimitiveComponent* BallRoot = Cast<UPrimitiveComponent>(Ball->GetRootComponent()))
	{
		if (BallRoot->IsSimulatingPhysics())
		{
			BallRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
			BallRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
}

void ARoomShell::RescuePawnIfOutside()
{
	if (!bRescuePawn || !ShellPivot)
	{
		return;
	}

	ACharacter* PlayerCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!PlayerCharacter)
	{
		return;
	}

	const UCapsuleComponent* Capsule = PlayerCharacter->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	const float Half = RoomSize * 0.5f;
	const float Thickness = FMath::Clamp(WallThickness, 1.f, RoomSize * 0.08f);
	const float Inner = Half - Thickness;

	// 角色允许活动的范围：要按「不可见围墙碰撞体」算，而不是按可见外壳算。
	// 墙碰撞体是个 12 边形，内表面在 R·cos(π/N) - 8 处；夹到这个半径以内，
	// 角色才不会和围墙发生穿透 —— 一旦穿透，穿透修正会顺着墙把人往上顶，人会浮起来。
	const float BoundaryRadius = FMath::Max(Inner - FMath::Max(BoundaryInset, 0.f), RoomSize * 0.15f);
	const int32 NumSegments = FMath::Clamp(BoundarySegments, 4, MaxBoundaryColliders - 2);
	const float WallFaceRadius = BoundaryRadius * FMath::Cos(PI / static_cast<float>(NumSegments)) - 8.f;

	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalf = Capsule->GetScaledCapsuleHalfHeight();
	const float XYLimit = FMath::Max(FMath::Min(WallFaceRadius, Inner) - CapsuleRadius, 10.f);
	const float ZMin = -Inner + CapsuleHalf;
	const float ZMax = FMath::Max(Inner - CapsuleHalf, ZMin);

	const FTransform ShellTransform = ShellPivot->GetComponentTransform();
	const FVector Local = ShellTransform.InverseTransformPosition(PlayerCharacter->GetActorLocation());

	const FVector Clamped(
		FMath::Clamp(Local.X, -XYLimit, XYLimit),
		FMath::Clamp(Local.Y, -XYLimit, XYLimit),
		FMath::Clamp(Local.Z, ZMin, ZMax));

	// 1cm 以内不动，免得跟 CharacterMovement 的正常移动打架
	if (Clamped.Equals(Local, 1.f))
	{
		return;
	}

	// 硬夹回盒内：倾斜 + 贴墙 + 角色一直往墙里走时，光靠扫掠碰撞偶尔会被挤进围墙，
	// 这里每帧兜一次底，保证角色永远在盒子里、脚不会陷进地板。
	PlayerCharacter->SetActorLocation(
		ShellTransform.TransformPosition(Clamped), false, nullptr, ETeleportType::TeleportPhysics);
}

void ARoomShell::RebuildInEditor()
{
	Rebuild();
}
