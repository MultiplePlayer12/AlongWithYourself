// Copyright Epic Games, Inc. All Rights Reserved.

#include "AlongWithYourselfCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "PhysicsEngine/BodyInstance.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Room/RoomShell.h"
#include "AlongWithYourself.h"

AAlongWithYourselfCharacter::AAlongWithYourselfCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// 角色的 Tick 放到 PostPhysics：房间每帧会把「绕形心的搬运量」攒起来，
	// 只有等移动组件在这一帧解算完之后再搬，位移才不会被覆盖掉。
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = WalkBrakingDeceleration;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// 行走摩擦（引擎默认 8）
	GetCharacterMovement()->GroundFriction = CharacterGroundFriction;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// ------------------------------------------------------------------ 重力
	// CMC 每帧做 v.z -= 980 * GravityScale * dt，再拿胶囊往下扫一遍找地面。
	// 这里显式写死一遍，免得蓝图里被改掉之后「角色怎么不往下掉」。
	GetCharacterMovement()->GravityScale = GravityScale;
	GetCharacterMovement()->bApplyGravityWhileJumping = true;

	// ------------------------------------------------------------------ 默认 hitbox
	// 胶囊半高 90、半径 35，下面的偏移都按「胶囊局部坐标」给（原点 = 胶囊中心，Z 向上）。
	// 覆盖范围：脚底 -89 ~ 头顶 +83，基本贴合人物身体轮廓。
	auto AddHitbox = [this](const TCHAR* Part, FVector Offset, FVector Extent, float Multiplier)
	{
		FCharacterHitboxDef Def;
		Def.PartName = FName(Part);
		Def.Offset = Offset;
		Def.Extent = Extent;
		Def.DamageMultiplier = Multiplier;
		HitboxDefs.Add(Def);
	};

	AddHitbox(TEXT("Head"),    FVector(0.f,   0.f,  72.f), FVector(11.f, 10.f, 11.f), 2.00f);
	AddHitbox(TEXT("Chest"),   FVector(0.f,   0.f,  34.f), FVector(15.f, 20.f, 17.f), 1.00f);
	AddHitbox(TEXT("Abdomen"), FVector(0.f,   0.f,   6.f), FVector(14.f, 18.f, 12.f), 1.00f);
	AddHitbox(TEXT("Pelvis"),  FVector(0.f,   0.f, -20.f), FVector(14.f, 17.f, 12.f), 0.90f);
	AddHitbox(TEXT("Thigh_L"), FVector(0.f, -10.f, -50.f), FVector(10.f,  8.f, 16.f), 0.75f);
	AddHitbox(TEXT("Thigh_R"), FVector(0.f,  10.f, -50.f), FVector(10.f,  8.f, 16.f), 0.75f);
	AddHitbox(TEXT("Calf_L"),  FVector(0.f, -10.f, -77.f), FVector( 9.f,  7.f, 12.f), 0.75f);
	AddHitbox(TEXT("Calf_R"),  FVector(0.f,  10.f, -77.f), FVector( 9.f,  7.f, 12.f), 0.75f);
	AddHitbox(TEXT("Arm_L"),   FVector(0.f, -26.f,  34.f), FVector( 7.f,  7.f, 16.f), 0.70f);
	AddHitbox(TEXT("Arm_R"),   FVector(0.f,  26.f,  34.f), FVector( 7.f,  7.f, 16.f), 0.70f);

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AAlongWithYourselfCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 重力 / 质量先落到位，再生成 hitbox（hitbox 要挂在胶囊上，得等组件都注册好）
	ApplyPhysicsSettings();
	RebuildHitboxes();

	// 自动找到场景里的空心立方体，并按本角色身高 × 倍数缩放它
	if (!RoomShell)
	{
		for (TActorIterator<ARoomShell> It(GetWorld()); It; ++It)
		{
			RoomShell = *It;
			break;
		}
	}

	if (RoomShell)
	{
		// 角色身高 = 胶囊体总高
		const float CharacterHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f;
		RoomShell->SetCharacterHeight(CharacterHeight);
	}

	// 立方体很大，开局就把镜头拉到盒外（此时可能还没被 Possess，俯仰角留到 Tick / PossessedBy 补）
	ApplyOverviewCamera();

	// 视角改到「钉在墙上的固定机位」
	ApplyRoomCameraView();
}

void AAlongWithYourselfCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	ApplyOverviewCamera();
	ApplyRoomCameraView();
}

void AAlongWithYourselfCharacter::ApplyRoomCameraView()
{
	if (!bUseRoomCamera || !RoomShell || !RoomShell->GetFaceCamera())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;			// 还没被 Possess，Tick 里会再试
	}

	// 把房间 Actor 设为视点：它的 CalcCamera 会返回墙上那台摄像机
	if (PC->GetViewTarget() != RoomShell)
	{
		PC->SetViewTarget(RoomShell);

		UE_LOG(LogAlongWithYourself, Log,
			TEXT("[Camera] 视角已切到墙面机位（墙 %d，眼高 %.0fcm）"),
			RoomShell->CameraWall,
			RoomShell->CameraEyeHeight);
	}
}

void AAlongWithYourselfCharacter::ApplyOverviewCamera()
{
	// 用墙面机位时，角色自己那套弹簧臂完全不参与，别再改控制器俯仰角
	if (bUseRoomCamera || !bUseOverviewCamera || !CameraBoom)
	{
		return;
	}

	// 镜头距离按房间尺寸等比缩放（换成 3 倍身高后房间只有 546cm，臂长也要一起收）
	const float RoomSize = RoomShell ? RoomShell->GetRoomSize() : 1000.f;

	// 拉开镜头并把碰撞检测关掉：弹簧臂已经伸到立方体外面了，
	// 再让墙 / 球把镜头顶回来只会让画面忽远忽近。
	CameraBoom->TargetArmLength = FMath::Max(RoomSize * OverviewCameraArmFactor, 200.f);
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = false;

	// 瞄准点抬高到立方体中心，盒子才能居中而不是挤在画面上半部分
	CameraBoom->TargetOffset = FVector(0.f, 0.f, RoomSize * OverviewCameraTargetOffsetFactor);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;			// 还没被 Possess，Tick 里会再试
	}

	FRotator ControlRotation = PC->GetControlRotation();
	ControlRotation.Pitch = FMath::Clamp(OverviewCameraPitch, -89.f, 0.f);
	ControlRotation.Roll = 0.f;
	PC->SetControlRotation(ControlRotation);

	bOverviewCameraApplied = true;
}

void AAlongWithYourselfCharacter::ApplyPhysicsSettings()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = FMath::Clamp(GravityScale, 0.f, 10.f);
		Move->bApplyGravityWhileJumping = true;

		// 摩擦：行走时真正起作用的是这两个（物理材质的 Friction 对 CMC 行走无效）。
		Move->GroundFriction = FMath::Max(CharacterGroundFriction, 0.f);
		Move->BrakingDecelerationWalking = FMath::Max(WalkBrakingDeceleration, 0.f);

		// 可行走坡度：太陡角色会顺着房间内壁「爬墙」，把盒子压成仓鼠轮
		Move->SetWalkableFloorAngle(FMath::Clamp(MaxWalkableSlopeAngle, 44.f, 89.f));

		// 让胶囊跟着房间的倾斜一起倒。
		// 关掉 bOrientRotationToMovement：它每帧只按移动方向改 yaw（还把 pitch/roll 清零），
		// 盒子倾 45° 时人仍然世界竖直，脚底就只有一个点搭在斜面上，看着像躺在空中。
		// 现在朝向由房间的搬运负责（CarryPlayerWithRotation 会一并旋转 Actor）。
		Move->bOrientRotationToMovement = false;
		Move->bIgnoreBaseRotation = true;
	}

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	// 胶囊只负责「走路别穿墙」：物理交给 CharacterMovement 解算，不交给刚体模拟。
	Capsule->SetSimulatePhysics(false);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECC_Pawn);

	// 质量写进 BodyInstance，被小球撞、推小球时才有个靠谱的比值（球是 20kg）
	if (FBodyInstance* Body = Capsule->GetBodyInstance())
	{
		Body->SetMassOverride(FMath::Max(CharacterMass, 1.f), true);
	}

	UE_LOG(LogAlongWithYourself, Log,
		TEXT("[Character] 重力倍率 %.2f（≈%.0f cm/s²）| 质量 %.1f kg | 胶囊半径 %.0f 半高 %.0f"),
		GetCharacterMovement()->GravityScale,
		980.f * GetCharacterMovement()->GravityScale,
		CharacterMass,
		Capsule->GetScaledCapsuleRadius(),
		Capsule->GetScaledCapsuleHalfHeight());

	UE_LOG(LogAlongWithYourself, Log,
		TEXT("[Friction] 地面摩擦 %.1f | 行走刹车 %.0f cm/s² | 可行走坡度 %.0f°"),
		GetCharacterMovement()->GroundFriction,
		GetCharacterMovement()->BrakingDecelerationWalking,
		GetCharacterMovement()->GetWalkableFloorAngle());
}

void AAlongWithYourselfCharacter::RebuildHitboxes()
{
	// 先清掉上一次生成的
	TArray<TObjectPtr<UBoxComponent>> OldHitboxes = Hitboxes;
	Hitboxes.Reset();
	HitboxDamageMultipliers.Reset();

	for (UBoxComponent* OldBox : OldHitboxes)
	{
		if (OldBox)
		{
			OldBox->DestroyComponent();
		}
	}

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!Capsule)
	{
		return;
	}

	for (int32 Index = 0; Index < HitboxDefs.Num(); ++Index)
	{
		const FCharacterHitboxDef& Def = HitboxDefs[Index];
		const FString PartName = Def.PartName.IsNone()
			? FString::Printf(TEXT("Part_%d"), Index)
			: Def.PartName.ToString();

		const FName CompName = MakeUniqueObjectName(
			this, UBoxComponent::StaticClass(), FName(*FString::Printf(TEXT("Hitbox_%s"), *PartName)));

		UBoxComponent* Box = NewObject<UBoxComponent>(this, CompName);
		if (!Box)
		{
			continue;
		}

		// 只做查询：不挡路、不推球、不参与物理，射线能打中就够了
		Box->SetBoxExtent(Def.Extent, false);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_GameTraceChannel1);		// 工程自定义的 Hitbox 通道
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Box->SetGenerateOverlapEvents(false);
		Box->SetHiddenInGame(true);
		Box->SetCanEverAffectNavigation(false);
		Box->SetCastShadow(false);
		Box->ShapeColor = FColor::Green;
		Box->ComponentTags.Add(FName(*PartName));

		Box->RegisterComponent();

		// 骨骼存在就挂骨骼（跟动画走），否则退回胶囊（稳、不依赖骨架）
		USceneComponent* Parent = Capsule;
		FName SocketName = NAME_None;
		if (!Def.BoneName.IsNone() && MeshComp && MeshComp->DoesSocketExist(Def.BoneName))
		{
			Parent = MeshComp;
			SocketName = Def.BoneName;
		}

		Box->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
		Box->SetRelativeLocation(Def.Offset);
		Box->SetRelativeRotation(FRotator::ZeroRotator);

		Hitboxes.Add(Box);
		HitboxDamageMultipliers.Add(Box, Def.DamageMultiplier);

		UE_LOG(LogAlongWithYourself, Log,
			TEXT("[Hitbox] %-8s → %-4s | 世界位置 %s | 半尺寸 %s | 倍率 %.2f"),
			*PartName,
			(Parent == MeshComp) ? TEXT("骨骼") : TEXT("胶囊"),
			*Box->GetComponentLocation().ToCompactString(),
			*Def.Extent.ToCompactString(),
			Def.DamageMultiplier);
	}
}

UBoxComponent* AAlongWithYourselfCharacter::FindHitbox(FName PartName) const
{
	for (const TObjectPtr<UBoxComponent>& Box : Hitboxes)
	{
		if (Box && Box->ComponentHasTag(PartName))
		{
			return Box.Get();
		}
	}

	return nullptr;
}

float AAlongWithYourselfCharacter::GetHitboxDamageMultiplier(const UPrimitiveComponent* Hitbox) const
{
	if (!Hitbox)
	{
		return 1.f;
	}

	for (const TPair<TObjectPtr<UBoxComponent>, float>& Pair : HitboxDamageMultipliers)
	{
		if (Pair.Key == Hitbox)
		{
			return Pair.Value;
		}
	}

	return 1.f;
}

void AAlongWithYourselfCharacter::RoomHitboxDebug(bool bEnable)
{
	bDebugDrawHitboxes = bEnable;
	UE_LOG(LogAlongWithYourself, Log, TEXT("[Hitbox] 调试绘制 %s"), bEnable ? TEXT("开启") : TEXT("关闭"));
}

FName AAlongWithYourselfCharacter::GetHitPartName(const UPrimitiveComponent* Hitbox) const
{
	if (Hitbox)
	{
		for (const FName& Tag : Hitbox->ComponentTags)
		{
			return Tag;
		}
	}

	return NAME_None;
}

bool AAlongWithYourselfCharacter::TraceHitbox(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 注意：这里**不能**忽略自己（this）——hitbox 就是本 Actor 的组件，
	// 一旦把 Owner 加进忽略列表，射线就永远打不到自己的身体。
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RoomHitboxTrace), false);

	// 首选：只查 Hitbox 这个对象通道，墙 / 胶囊 / 小球都不参与
	FCollisionObjectQueryParams ObjectParams(ECC_GameTraceChannel1);
	if (World->LineTraceSingleByObjectType(OutHit, Start, End, ObjectParams, QueryParams))
	{
		return true;
	}

	// 兜底：按 Visibility 多段追踪，再筛出属于本角色 hitbox 的那一段
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, QueryParams);
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* HitComp = Hit.GetComponent();
		if (!HitComp)
		{
			continue;
		}

		for (const TObjectPtr<UBoxComponent>& Box : Hitboxes)
		{
			if (Box.Get() == HitComp)
			{
				OutHit = Hit;
				return true;
			}
		}
	}

	return false;
}

void AAlongWithYourselfCharacter::RoomHitboxTestTrace()
{
	// 从头顶上方竖直往下打，正常应该先命中 Head
	const FVector Center = GetActorLocation();
	FHitResult Hit;
	if (TraceHitbox(Center + FVector(0.f, 0.f, 200.f), Center - FVector(0.f, 0.f, 200.f), Hit))
	{
		UE_LOG(LogAlongWithYourself, Log,
			TEXT("[Hitbox] 测试射线命中部位 %s | 倍率 %.2f | 命中点 %s | 距离 %.1f cm"),
			*GetHitPartName(Hit.GetComponent()).ToString(),
			GetHitboxDamageMultiplier(Hit.GetComponent()),
			*Hit.ImpactPoint.ToCompactString(),
			Hit.Distance);
	}
	else
	{
		UE_LOG(LogAlongWithYourself, Warning, TEXT("[Hitbox] 测试射线没有命中任何 hitbox"));
	}
}

void AAlongWithYourselfCharacter::DrawHitboxDebug() const
{
	if (!bDebugDrawHitboxes || !GetWorld())
	{
		return;
	}

	for (const TObjectPtr<UBoxComponent>& Box : Hitboxes)
	{
		if (!Box)
		{
			continue;
		}

		// 头部用红色，其它部位绿色。
		// 用 SDPG_Foreground 画：hitbox 基本都埋在身体里，走正常深度测试的话会被自身网格挡掉看不见。
		const bool bIsHead = Box->ComponentHasTag(TEXT("Head"));
		const FColor Color = bIsHead ? FColor::Red : FColor::Green;

		DrawDebugBox(GetWorld(), Box->GetComponentLocation(), Box->GetScaledBoxExtent(),
			Box->GetComponentQuat(), Color, false, -1.f, SDPG_Foreground, 1.5f);
	}
}

void AAlongWithYourselfCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAlongWithYourselfCharacter::Move);

		// Looking（手柄右摇杆；鼠标视角映射已关闭，鼠标改用于拖拽旋转立方体）
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAlongWithYourselfCharacter::Look);
	}
	else
	{
		UE_LOG(LogAlongWithYourself, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AAlongWithYourselfCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 被 Possess 之后才能知道控制器朝向，这里补一次俯瞰镜头
	if (!bOverviewCameraApplied)
	{
		ApplyOverviewCamera();
	}

	// 视点被别的东西抢走时（重生 / 自动管理相机）抢回墙面机位
	if (bUseRoomCamera && RoomShell && RoomShell->GetFaceCamera())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (PC->GetViewTarget() != RoomShell)
			{
				ApplyRoomCameraView();
			}
		}
	}

	UpdateRoomDrag(DeltaSeconds);

	// 调试：自动持续走动，用来观察角色移动如何改变盒子重心
	if (bDebugAutoWalk)
	{
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.f;

		// 限时自动走：时间到了就「松手」，用来验证松手后人能不能站住
		if (AutoWalkUntil > 0.f && Now >= AutoWalkUntil)
		{
			bDebugAutoWalk = false;
			AutoWalkUntil = 0.f;
			UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] 自动行走结束（松手），接下来看倾角限位是否兜住"));
		}
		else
		{
			const FVector WalkDirection = (RoomShell && RoomShell->GetFaceCamera())
				? RoomShell->GetFaceCamera()->GetRightVector()
				: GetActorForwardVector();

			AddMovementInput(WalkDirection, 1.f);

			LastMoveInputTime = Now;
		}
	}

	// 后物理阶段：把房间这一帧攒下的「绕形心旋转量」真正应用到角色身上（位置 + 朝向）。
	// 必须放在移动解算之后，否则 CharacterMovement 会在同一帧里把人摆回去 ——
	// 表现就是盒子绕着他转、人悬在盒中间。
	if (RoomShell)
	{
		RoomShell->ApplyPendingCarryToPawn();

		// 再把「站在地板上」的角色钉回地板高度：房间转、移动组件又按世界竖直方向贴地，
		// 两种参照系每帧都会多给一点点向上的位移，不夹住的话十几秒就飘到盒子中间。
		RoomShell->ClampPawnToRoomFloor();
	}

	// 开了平衡日志时顺带每秒打一行角色姿态，用来判断「人是不是被悬在半空」
	if (RoomShell && RoomShell->bLogBalance)
	{
		CharStateTimer += DeltaSeconds;
		if (CharStateTimer >= 1.f)
		{
			CharStateTimer = 0.f;
			RoomCharState();
		}
	}

	DrawHitboxDebug();
}

void AAlongWithYourselfCharacter::UpdateRoomDrag(float DeltaSeconds)
{
	if (!RoomShell)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	// R 键：姿态归零，方便随时把盒子摆正
	if (bEnableResetKey && PC->WasInputKeyJustPressed(EKeys::R))
	{
		RoomShell->ResetRotation();
	}

	if (!bEnableMouseDragRotate || !PC->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		return;
	}

	float DeltaX = 0.f;
	float DeltaY = 0.f;
	PC->GetInputMouseDelta(DeltaX, DeltaY);

	if (FMath::IsNearlyZero(DeltaX) && FMath::IsNearlyZero(DeltaY))
	{
		return;
	}

	// 旋转轴都以「过形心」为前提，所以怎么拖盒子都只是原地翻转、位置不变。
	// 左右拖：绕世界竖直轴自转（和改造前的手感一致，纯水平拖动不会把盒子带歪）；
	// 上下拖：绕屏幕右轴前后翻；Shift + 拖：绕视线打滚。
	const FMatrix ViewMatrix = FRotationMatrix(PC->GetControlRotation());
	const FVector ScreenRight = ViewMatrix.GetUnitAxis(EAxis::Y);
	const FVector ViewDirection = ViewMatrix.GetUnitAxis(EAxis::X);

	const bool bRolling = bShiftDragRollsRoom && PC->IsInputKeyDown(EKeys::LeftShift);
	if (bRolling)
	{
		// Shift + 拖动：绕视线方向打滚，补齐第三个自由度
		RoomShell->AddRotation(ViewDirection, DeltaX * DragRotateSensitivity);
		return;
	}

	// 鼠标右滑 → 盒子顺时针（墙面向右跟手）
	RoomShell->AddRotation(FVector::UpVector, -DeltaX * DragRotateSensitivity);

	if (bDragPitchesCamera)
	{
		// 保留旧行为：纵向拖动改成抬 / 压镜头
		if (!FMath::IsNearlyZero(DeltaY))
		{
			AddControllerPitchInput(DeltaY * DragPitchSensitivity);
		}
	}
	else
	{
		// 鼠标下拖 → 盒子前脸往下翻（顶面朝自己转过来）
		const float PitchSign = bInvertDragVertical ? 1.f : -1.f;
		RoomShell->AddRotation(ScreenRight, PitchSign * DeltaY * DragRotateSensitivity);
	}
}

void AAlongWithYourselfCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AAlongWithYourselfCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AAlongWithYourselfCharacter::DoMove(float Right, float Forward)
{
	if (GetController() == nullptr)
	{
		return;
	}

	// 有真实输入就记一下时刻：静摩擦保持靠它判断「玩家现在在走」还是「松手站着了」
	if (!FMath::IsNearlyZero(Right) || !FMath::IsNearlyZero(Forward))
	{
		LastMoveInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	}

	FVector ForwardDirection;
	FVector RightDirection;

	if (bUseRoomCamera && RoomShell && RoomShell->GetFaceCamera())
	{
		// 视角钉在墙上、不再跟控制器朝向走，所以移动方向也不能再用控制旋转解算——
		// 否则「按 W」跟画面完全对不上。这里直接用墙面机位的世界朝向：
		// 它的 forward / right 都躺在盒子内部的地面平面里，正好是「远离镜头 / 向右」。
		const UCameraComponent* WallCamera = RoomShell->GetFaceCamera();
		ForwardDirection = WallCamera->GetForwardVector();
		RightDirection = WallCamera->GetRightVector();
	}
	else
	{
		// 旧路径：按控制器朝向（水平面内）解算
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	}

	AddMovementInput(ForwardDirection, Forward);
	AddMovementInput(RightDirection, Right);
}

void AAlongWithYourselfCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AAlongWithYourselfCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AAlongWithYourselfCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AAlongWithYourselfCharacter::RoomRotate(float Pitch, float Yaw, float Roll)
{
	if (!RoomShell)
	{
		return;
	}

	// 三个轴都过形心，顺序无所谓，只是给调试用的一条捷径
	RoomShell->AddRotation(FVector::YAxisVector, Pitch);
	RoomShell->AddRotation(FVector::ZAxisVector, Yaw);
	RoomShell->AddRotation(FVector::XAxisVector, Roll);

	UE_LOG(LogAlongWithYourself, Log,
		TEXT("[Balance] 手动摆到 Pitch %.0f / Yaw %.0f / Roll %.0f，当前倾角 %.1f°（松手后会被重力拽回平衡）"),
		Pitch, Yaw, Roll, RoomShell->GetTiltDegrees());
}

void AAlongWithYourselfCharacter::RoomBalanceDebug(bool bEnable)
{
	if (!RoomShell)
	{
		return;
	}

	RoomShell->SetBalanceLogging(bEnable);
	UE_LOG(LogAlongWithYourself, Log,
		TEXT("[Balance] 日志 %s | 求解器 %s | 内容质量 角色 %.0fkg + 球 %.0fkg | 空盒惯性质量 %.0fkg"),
		bEnable ? TEXT("开启") : TEXT("关闭"),
		RoomShell->bEnableInertialRotation ? TEXT("开启（重心驱动）") : TEXT("关闭（鼠标拖拽）"),
		RoomShell->CharacterMass,
		RoomShell->BallMass,
		RoomShell->ShellMass);
}

void AAlongWithYourselfCharacter::RoomWalkDebug(bool bEnable)
{
	bDebugAutoWalk = bEnable;
	AutoWalkUntil = 0.f;
	UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] 自动行走 %s"), bEnable ? TEXT("开启") : TEXT("关闭"));
}

void AAlongWithYourselfCharacter::RoomWalkFor(float Seconds)
{
	if (Seconds <= 0.f)
	{
		RoomWalkDebug(false);
		return;
	}

	bDebugAutoWalk = true;
	AutoWalkUntil = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) + Seconds;

	UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] 自动行走 %.1f 秒后松手"), Seconds);
}

void AAlongWithYourselfCharacter::RoomSolver(bool bEnable)
{
	if (!RoomShell)
	{
		return;
	}

	RoomShell->bEnableInertialRotation = bEnable;
	UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] 重心求解器 %s"), bEnable ? TEXT("开启") : TEXT("关闭"));
}

void AAlongWithYourselfCharacter::RoomGhost(bool bEnable)
{
	// 诊断用：把角色变成「幽灵」，完全不参与任何碰撞与救援，
	// 这样如果盒子还能正常转，就说明问题来自角色与旋转地面之间的交互。
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bEnable ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}

	if (RoomShell)
	{
		RoomShell->bRescuePawn = !bEnable;
	}

	UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] 幽灵模式 %s"), bEnable ? TEXT("开启") : TEXT("关闭"));
}

void AAlongWithYourselfCharacter::RoomCarry(bool bEnable)
{
	if (!RoomShell)
	{
		return;
	}

	RoomShell->bManualCarryPlayer = bEnable;
	UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] 手动搬运角色 %s"), bEnable ? TEXT("开启") : TEXT("关闭"));
}

void AAlongWithYourselfCharacter::RoomCharState()
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Capsule || !Move)
	{
		return;
	}

	FString LocalPos = TEXT("n/a");
	FString LocalUp = TEXT("n/a");
	float FloorGap = 0.f;

	if (RoomShell)
	{
		// 参照系必须是**枢轴的实时姿态**（Actor 本身不带旋转），否则量出来的是「未旋转的房间」，
		// 会把盒子倾斜造成的世界位移误判成角色在爬墙 / 悬空。
		const FTransform PivotXf(RoomShell->GetCurrentRotation().Quaternion(), RoomShell->GetActorLocation());

		LocalPos = PivotXf.InverseTransformPosition(GetActorLocation()).ToCompactString();
		LocalUp = PivotXf.InverseTransformVectorNoScale(GetActorUpVector()).ToCompactString();

		const float Half = RoomShell->GetRoomSize() * 0.5f;
		const float Thickness = RoomShell->WallThickness;
		const float FloorTop = -Half + Thickness;
		const float CapsuleBottom = PivotXf.InverseTransformPosition(
			Capsule->GetComponentLocation() - Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight()).Z;

		FloorGap = CapsuleBottom - FloorTop;
	}

	UE_LOG(LogAlongWithYourself, Log,
		TEXT("[CharState] 模式 %d | 世界 %s | 世界朝上 V(Z=%.2f) | 局部位置 %s | 局部朝上 %s | 脚底离地 %.1fcm | 基座 %s | 倾角 %.1f°"),
		static_cast<int32>(Move->MovementMode),
		*GetActorLocation().ToCompactString(),
		GetActorUpVector().Z,
		*LocalPos,
		*LocalUp,
		FloorGap,
		Move->GetMovementBase() ? *Move->GetMovementBase()->GetName() : TEXT("无"),
		RoomShell ? RoomShell->GetTiltDegrees() : 0.f);
}

void AAlongWithYourselfCharacter::RoomShot(float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// `-ExecCmds` 里的 HighResShot 几乎总在关卡刚加载时触发（r.HighResScreenshotDelay 不可靠），
	// 想看「稳定之后的画面」就用这个：延时 N 秒再自己发一次控制台命令。
	FTimerHandle Handle;
	FTimerDelegate Del = FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		// 走 PlayerController 的控制台（和玩家自己敲命令同一条路径），
		// GEngine->Exec 在这个上下文里不一定能触发 HighResShot。
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->ConsoleCommand(TEXT("HighResShot 1600x900"));
		}
	});

	World->GetTimerManager().SetTimer(Handle, Del, FMath::Max(DelaySeconds, 0.1f), false);

	UE_LOG(LogAlongWithYourself, Log, TEXT("[Balance] %.1f 秒后截图"), DelaySeconds);
}
