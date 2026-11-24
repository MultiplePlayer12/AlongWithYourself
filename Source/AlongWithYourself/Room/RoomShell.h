// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomShell.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UStaticMesh;
class UMaterialInterface;
class UCameraComponent;
class ARoomBall;
struct FMinimalViewInfo;

/**
 *  空心立方体房间。
 *
 *  - 六个面（地面 / 天花板 / 四面墙）用 Cube 静态网格拼成一个空心立方体；
 *  - 尺寸 = 角色身高 * SizeMultiplier（默认 3 倍），可在 BeginPlay 时按角色胶囊体自动计算；
 *  - 整个立方体（可见外壳 + 内部碰撞体）绕「过形心（Actor 原点）的任意轴」旋转，
 *    形心位置恒定；
 *  - 旋转不再靠鼠标拖，而是真的算重力力矩：内容物（角色 + 小球）的重心一旦偏离形心，
 *    τ = r × Mg 就把盒子拽向「重心垂在形心正下方」的姿态（见 bEnableInertialRotation /
 *    UpdateInertialRotation）。角色走到哪边、小球滚到哪边，盒子就朝哪边倒，见 MaxTiltAngle；
 *  - 房间里的角色是真的「站在这块会转的地板上」：房间每帧把绕形心的旋转量攒起来
 *    （PendingCarryQuat），角色在自己的 TG_PostPhysics Tick 里消费（ApplyPendingCarryToPawn），
 *    位置和朝向一起转；再用 ClampPawnToRoomFloor 把站在地板上的角色钉回地板高度，
 *    吃掉两套参照系（世界竖直 vs 房间局部）每帧多给的那一点位移 ——
 *    不夹住的话十几秒他就顺着内壁蹭到盒子中间，重心被顶到枢轴以上，又变成滚筒；
 *  - 不让盒子无限翻滚靠的是倾角限位 MaxTiltAngle（默认 20°，到限位清角速度 → 稳定锁死）；
 *  - 玩家视角是钉在侧墙内侧、角色胸口高度、绝对水平的一台固定机位（FaceCamera），
 *    它挂在枢轴上，跟着盒子一起翻；
 *  - 房间本身不生成任何灯光，照明完全交给关卡里摆放的灯光 Actor（模板原始光照）；
 *  - BeginPlay 时会在立方体内部生成一个小球（带物理 + 重力），见 SpawnBallIfMissing()；
 *    小球被甩到盒外时 Tick 里会自动捞回来。
 */
UCLASS()
class ALONGWITHYOURSELF_API ARoomShell : public AActor
{
	GENERATED_BODY()

public:
	ARoomShell();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 把这个 Actor 当作视点时，直接用墙上那台摄像机（见 FaceCamera）。 */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	//~ End AActor interface

	/**
	 *  绕「过形心的任意世界轴」追加旋转。
	 *  @param WorldAxis     旋转轴（世界空间，不必归一化），只要过形心即可，任意方向都行。
	 *  @param AngleDegrees  有符号角度（度），右手螺旋为正。
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void AddRotation(const FVector& WorldAxis, float AngleDegrees);

	/** 追加一段水平旋转（度），等价于绕世界 Z 轴转。正值为从上方看逆时针。 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void AddYaw(float DeltaDegrees);

	/** 直接改房间边长（厘米）。 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SetRoomSize(float NewSize);

	/** 按角色身高 + SizeMultiplier 重算房间尺寸。 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SetCharacterHeight(float Height);

	/** 当前房间边长（厘米）。 */
	UFUNCTION(BlueprintPure, Category = "Room")
	float GetRoomSize() const { return RoomSize; }

	/** 当前姿态（相对初始朝向的旋转量）。 */
	UFUNCTION(BlueprintPure, Category = "Room")
	FRotator GetCurrentRotation() const { return CurrentRotation.Rotator(); }

	/** 当前外壳的偏航角（度），仅用于监视，翻转后没有唯一含义。 */
	UFUNCTION(BlueprintPure, Category = "Room")
	float GetCurrentYaw() const { return CurrentRotation.Rotator().Yaw; }

	/** 房间内部地面的世界坐标（角色站立高度参考点）。 */
	UFUNCTION(BlueprintPure, Category = "Room")
	FVector GetInnerFloorLocation() const;

	/** 把盒子姿态立刻归零（回到正立）。 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void ResetRotation();

	/** 依据当前参数重建外壳与碰撞体（改了尺寸 / 材质后调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void RebuildRoom();

	/**
	 *  把本帧累积的「绕形心搬运」应用到玩家角色上。
	 *
	 *  为什么不在房间的 Tick 里直接搬：CharacterMovement 在自己的预处理 Tick 里
	 *  会把角色重新摆回「世界里的原位」，房间 Tick 里做的位移当帧就被覆盖掉了，
	 *  结果就是盒子绕着他转、他悬在半空。角色把自己的 Tick 组改成 TG_PostPhysics
	 *  之后再调这个函数，搬完这一帧就结束了，没有人能再覆盖。
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void ApplyPendingCarryToPawn();

	/**
	 *  把站在地板上的角色钉回「内底板 + 胶囊半高」的精确高度（房间局部坐标）。
	 *
	 *  盒子一边转、CharacterMovement 一边按世界竖直方向做「贴地修正」，
	 *  两个参照系不一样，每帧会多给角色一丁点向上的位移 —— 累积几秒人就飘到盒子中间，
	 *  看着像悬空。地板上的角色本来就不可能离地，所以这里每帧直接夹回去，
	 *  漂移量一次就被吃掉了。离地超过 ClampTolerance 时不介入（站在小球上、起跳、下落都算）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void ClampPawnToRoomFloor();

	/** 编辑器里点一下这个按钮就能重建（改完参数不用等 BeginPlay）。 */
	UFUNCTION(CallInEditor, Category = "Room")
	void RebuildInEditor();

protected:
	/** 依据当前尺寸重建外壳与碰撞体。 */
	void Rebuild();

	/** 把跑到盒子外面的小球捞回盒内（快速翻转时可能被甩出去）。 */
	void RescueBallIfOutside();

	/** 角色万一被甩出盒子，送回盒内底部，避免无限坠落。 */
	void RescuePawnIfOutside();

	/**
	 *  一帧的角动量求解：算内容物重心 → 重力力矩 → 角加速度 → 积分成姿态。
	 *  形心（Actor 原点 / Root 位置）始终不变，只有枢轴的姿态在变。
	 */
	void UpdateInertialRotation(float DeltaSeconds);

	/**
	 *  带着房间里的角色一起转。
	 *  CharacterMovement 只在「地板被登记为 MovementBase」时才帮忙搬人，
	 *  而我们这面地板是每帧被强行旋转的，实测并没有登记成基座 ——
	 *  于是角色会留在原地、房间绕着它转（像轮子绕着轴心里的乘客转）。
	 *  所以这里手动把角色的世界位置绕形心旋转同样的角度，让它真的「站在这块地板上」。
	 */
	void CarryPlayerWithRotation(const FQuat& WorldDeltaQuat);

	/** 按 CameraWall / CameraEyeHeight 把墙面摄像机摆到正确位置与朝向。 */
	void UpdateFaceCamera();

	/**
	 *  统计房间里全部内容物的质量加权重心（世界坐标）。
	 *  @param OutTotalMass  角色 + 小球的总质量（kg）；空盒子重心在形心上，不计入。
	 */
	FVector ComputeContentCenterOfMass(float& OutTotalMass) const;

	/** 根组件：Actor 原点即立方体形心，旋转时不动。 */
	UPROPERTY(VisibleAnywhere, Category = "Room|Components")
	TObjectPtr<USceneComponent> Root;

	/** 旋转枢轴：可见外壳与内部碰撞体都挂在它下面，绕形心做任意轴旋转。 */
	UPROPERTY(VisibleAnywhere, Category = "Room|Components")
	TObjectPtr<USceneComponent> ShellPivot;

	/**
	 *  钉在侧墙内侧的常驻摄像机（玩家视角）。
	 *  它是 ShellPivot 的子组件，所以「完全刚性」地跟盒子一起翻 ——
	 *  盒子转到哪，它就在那面墙上，画面里角色永远是同一个方位。
	 *  位置 = 选定的那面墙内侧、角色眼高；朝向 = 水平看向房间中心。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room|Camera")
	TObjectPtr<UCameraComponent> FaceCamera;

	/** 六个可见面：0=地面 1=天花板 2=+X 3=-X 4=+Y 5=-Y。 */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ShellFaces;

	/** 不可见碰撞体：0=地板 1=天花板 2..=围墙分段。和外壳一起转。 */
	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> BoundaryColliders;

public:
	/** 立方体边长是否在 BeginPlay 时按角色身高自动计算。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	bool bAutoSizeToCharacter = true;

	/** 角色身高（厘米），仅用于尺寸计算。默认 182 = 模板角色胶囊实际高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	float CharacterHeight = 182.f;

	/** 房间边长 = 角色身高 * 该倍数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	float SizeMultiplier = 3.f;

	/** 墙面厚度（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	float WallThickness = 10.f;

	/** 可见外壳使用的金属材质，留空则尝试加载 /Game/Room/M_RoomMetal。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	TObjectPtr<UMaterialInterface> WallMaterial;

	/** 可见外壳用的网格，默认引擎自带的 Cube。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	TObjectPtr<UStaticMesh> WallMesh;

	/** 不可见围墙的分段数（越多越接近圆柱，角色贴墙越自然）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary", meta = (ClampMin = "4", ClampMax = "16"))
	int32 BoundarySegments = 12;

	/** 围墙比外壳内壁再往里缩多少厘米，保证任何角度下角色都不会穿出外壳。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary")
	float BoundaryInset = 10.f;

	/** 拖拽时的角度插值速度，越大越跟手。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Rotation")
	float RotationInterpSpeed = 20.f;

	/** 当前房间边长（运行时）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Size")
	float RoomSize = 546.f;

	/** 目标姿态 / 当前姿态（相对 Root 的四元数，保证只转姿态、形心不动）。 */
	UPROPERTY(VisibleAnywhere, Category = "Room|Rotation")
	FQuat TargetRotation = FQuat::Identity;

	UPROPERTY(VisibleAnywhere, Category = "Room|Rotation")
	FQuat CurrentRotation = FQuat::Identity;

	/**
	 *  外壳是否遮挡阴影。
	 *  关掉（默认）时，关卡里的平行光 / 天光可以照进这个封闭盒子，室内才看得见东西；
	 *  打开则是「真·封闭金属盒」，室内完全没有外部光，只有你自己在盒子里放的灯才亮。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	bool bShellCastsShadow = false;

	// ------------------------------------------------------------------ 墙面摄像机
	// 玩家视角不再是「跟着角色跑的弹簧臂」，而是钉在盒子某个侧墙内侧、
	// 角色眼睛高度、水平看向房间中心的一台固定机位。
	// 因为它是 ShellPivot 的子组件，盒子翻转时视角跟着一起翻（完全刚性）。

	/** 摄像机钉在哪面墙：0=-Y 墙（默认） 1=+Y 墙 2=-X 墙 3=+X 墙。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Camera", meta = (ClampMin = "0", ClampMax = "3"))
	int32 CameraWall = 0;

	/**
	 *  摄像机高度：距房间内部地面多少厘米。
	 *  105 是「胸口高度、镜头轴完全水平」——严格平视，同时 182cm 的角色
	 *  在 250cm 外正好从头到脚完整进画面（放在眼高 150 会把脚切掉）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Camera", meta = (ClampMin = "10"))
	float CameraEyeHeight = 105.f;

	/** 摄像机离开内壁多少厘米（避免近裁剪面切进墙面）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Camera", meta = (ClampMin = "1"))
	float CameraWallInset = 12.f;

	/** 摄像机视场角（水平，度）。房间只有 546cm，广一点才看得到整个内部。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Camera", meta = (ClampMin = "30", ClampMax = "150"))
	float CameraFOV = 90.f;

	/** 墙面摄像机（玩家视角）。 */
	UCameraComponent* GetFaceCamera() const { return FaceCamera; }

	// ------------------------------------------------------------------ 惯性（重心）旋转
	// 不再是「鼠标拖 → 转」，而是真的算力矩：
	//
	//   τ = r × (M·g)      r = 形心 → 全部内容物（角色 + 小球）重心的矢量
	//
	// 然后 α = τ / I，对角速度积分、乘阻尼，再积分成姿态四元数。
	// 于是「角色走到哪边、小球滚到哪边」都会改变 r，盒子就朝那边倒，
	// 直到 r 转到竖直向下（重心垂在形心正下方）——纯自由悬挂的钟摆平衡。
	// 形心（Actor 原点）始终不动，所有转动都是绕它的。

	/** 是否用上面的物理求解器驱动旋转。关掉就退回鼠标拖拽那套。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia")
	bool bEnableInertialRotation = true;

	/** 角色的质量（kg），用于算 r。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "1"))
	float CharacterMass = 80.f;

	/** 小球的质量（kg），用于算 r。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "0"))
	float BallMass = 20.f;

	/**
	 *  空盒子的质量（kg）。它本身的重心就在形心上，不产生力矩，
	 *  只决定「转动惯量」——也就是盒子响应得多迟钝。越大越沉稳、越晃得慢。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "1"))
	float ShellMass = 800.f;

	/** 力矩倍率（1 = 真实重力；调小更缓，调大更敏感）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "0", ClampMax = "10"))
	float InertialResponse = 1.f;

	/** 角速度阻尼（1/秒）。0 = 永远摆动不停，越大越快稳住。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "0", ClampMax = "20"))
	float AngularDamping = 1.2f;

	/** 角速度上限（度/秒），防止极端情况下转飞。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "1", ClampMax = "720"))
	float MaxAngularSpeed = 40.f;

	/**
	 *  倾角限位（度）。0 或 ≥179 = 不限位。
	 *
	 *  纯自由悬挂 + 会走路的角色 = 永不停的滚筒：房间一倾，最低点就换位置，
	 *  角色和小球跟着滑过去，盒子继续转（实测能一路转到 156°）。
	 *  所以用这道闸兜住 —— 到限位就把角速度清零，盒子稳定锁死。
	 *
	 *  20° 时：站着不动 → 倾 6.9°（小球滚到一侧，重心偏 24cm）；
	 *  走到墙边 → 锁在 20°。
	 *
	 *  想让盒子彻底自由（会一直滚到最低点）：调成 0。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "0", ClampMax = "180"))
	float MaxTiltAngle = 20.f;

	/**
	 *  「落地回正」刚度（相对重力力矩的倍数）。
	 *  0（默认）= 重心主导，盒子会一直倒到重心垂在形心正下方（或撞到倾角限位）；
	 *  调大则像盒子被地板托住一样，只朝重心方向倾一个有限角度就停住。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia", meta = (ClampMin = "0", ClampMax = "20"))
	float UprightStiffness = 0.f;

	/** 每 0.5 秒往日志里打一行重心 / 倾角 / 角速度（验证用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Inertia")
	bool bLogBalance = false;

	/** 当前角速度（度/秒，世界空间），只读监视用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room|Inertia")
	FVector AngularVelocityDeg = FVector::ZeroVector;

	/** 当前全部内容物的重心（外壳局部坐标）。x/y 偏向哪边，盒子就往哪边倒。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room|Inertia")
	FVector CenterOfMassLocal = FVector::ZeroVector;

	/** 当前倾角：外壳局部 +Z 与世界竖直方向之间的夹角（度）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room|Inertia")
	float TiltDegrees = 0.f;

	/** 当前倾角（度）。 */
	UFUNCTION(BlueprintPure, Category = "Room|Inertia")
	float GetTiltDegrees() const { return TiltDegrees; }

	/** 全部内容物重心（外壳局部坐标）。 */
	UFUNCTION(BlueprintPure, Category = "Room|Inertia")
	FVector GetCenterOfMassLocal() const { return CenterOfMassLocal; }

	/** 开关重心日志。 */
	UFUNCTION(BlueprintCallable, Category = "Room|Inertia")
	void SetBalanceLogging(bool bEnable) { bLogBalance = bEnable; }

	// ------------------------------------------------------------------ 小球
	// 房间中央会掉下来一个带重力的小球，用来验证「立方体内部是可活动的物理空间」。

	/** 是否在 BeginPlay 时往房间里放一个小球。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Ball")
	bool bSpawnBall = true;

	/** 小球的类，默认 ARoomBall。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Ball")
	TSubclassOf<ARoomBall> BallClass;

	/** 小球半径（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Ball", meta = (ClampMin = "2"))
	float BallRadius = 30.f;

	/** 小球出生高度 = 房间边长 × 该比例（相对房间内地面）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Ball", meta = (ClampMin = "0.05", ClampMax = "0.9"))
	float BallSpawnHeightRatio = 0.55f;

	/** 小球被转出盒外时是否自动捞回（关掉的话它会一直往下掉）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Ball")
	bool bRescueBall = true;

	/** 角色被转出盒外时是否自动送回形心（诊断时可以关掉）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room|Ball")
	bool bRescuePawn = true;

	/**
	 *  是否手动把角色绕着形心搬一圈（CarryPlayerWithRotation）。
	 *
	 *  CharacterMovement 其实会按「基座（脚下那块地板）的变换」自动搬人，
	 *  连旋转也一并处理；我们再做一次就是「一帧搬两倍」——
	 *  表现就是盒子倾斜时角色顺着内壁往上爬。
	 *  留这个开关是为了对照实验：关掉之后如果角色仍被带着转，就说明引擎在管，手动那段可以彻底去掉。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	bool bManualCarryPlayer = true;

	/** 是否把站在地板上的角色钉在内底板高度（吃掉参照系不一致带来的缓慢上飘）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	bool bClampPawnToFloor = true;

	/** 当前房间里的小球（运行时）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room|Ball")
	TObjectPtr<ARoomBall> Ball;

	/** 房间里没有小球时生成一个，返回已有 / 新建的小球。 */
	UFUNCTION(BlueprintCallable, Category = "Room|Ball")
	ARoomBall* SpawnBallIfMissing();

private:
	/** 是否已经把房间缩放到真实角色身高（BeginPlay 时角色可能还没生成，需要延后补一次）。 */
	bool bSizedToCharacter = false;

	/** 重心日志的累计计时。 */
	float BalanceLogTimer = 0.f;

	/** 心跳日志的累计计时。 */
	float BalanceHeartbeat = 0.f;

	/** 本帧待应用的搬运量（房间 Tick 累积，角色在 PostPhysics 消费）。 */
	FQuat PendingCarryQuat = FQuat::Identity;

	/** 固定数量的碰撞体上限（1 地板 + 1 天花板 + 16 段墙）。 */
	static constexpr int32 MaxBoundaryColliders = 18;
};
