// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "AlongWithYourselfCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UBoxComponent;
class ARoomShell;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  一段 hitbox 的定义。
 *
 *  BoneName 留空 → 挂在胶囊上，偏移量按「胶囊局部坐标」解释（胶囊中心为原点，Z 向上），
 *  这种模式下盒子永远和角色朝向对齐，最稳、最好调。
 *  BoneName 有值且骨架里存在该骨骼 → 挂到骨骼上，跟着动画动（偏移量变成骨骼局部坐标）。
 */
USTRUCT(BlueprintType)
struct FCharacterHitboxDef
{
	GENERATED_BODY()

	/** 部位名（同时用于组件命名 / 调试打印），例如 Head、Chest、Thigh_L。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	FName PartName;

	/** 挂到的骨骼；留空 = 挂胶囊（推荐，不依赖具体骨架）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	FName BoneName;

	/** 相对父级的偏移（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	FVector Offset = FVector::ZeroVector;

	/** 盒子半尺寸（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	FVector Extent = FVector(10.f, 10.f, 10.f);

	/** 伤害倍率：头部 2.0、躯干 1.0、四肢 0.7 这类约定，方便以后接战斗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox", meta = (ClampMin = "0"))
	float DamageMultiplier = 1.f;
};

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class AAlongWithYourselfCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	AAlongWithYourselfCharacter();	

protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** Cache the room shell + resize it to this character */
	virtual void BeginPlay() override;

	/** Handle mouse drag -> rotate room */
	virtual void Tick(float DeltaSeconds) override;

	/** 被控制器接管时立刻摆好俯瞰镜头（BeginPlay 时 Pawn 往往还没被 Possess） */
	virtual void PossessedBy(AController* NewController) override;

protected:

	/** 按住鼠标左键拖动时触发的房间旋转处理（现在默认关闭，旋转交给房间的物理求解器） */
	void UpdateRoomDrag(float DeltaSeconds);

	/** 把镜头拉到能看全整个立方体的位置 */
	void ApplyOverviewCamera();

	/** 把视角切到「钉在墙上的那台摄像机」（房间 Actor 当视点） */
	void ApplyRoomCameraView();

	/** 把 GravityScale / 质量 / 碰撞开关落到组件上 */
	void ApplyPhysicsSettings();

	/** 调试绘制 hitbox */
	void DrawHitboxDebug() const;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/**
	 *  控制台调试用：一次性把立方体转到指定姿态（单位：度）。
	 *  例如 `RoomRotate 45 0 0` = 绕世界 Y 轴翻 45°，`RoomRotate 0 90 0` = 水平转 90°。
	 */
	UFUNCTION(Exec, Category="Room")
	void RoomRotate(float Pitch, float Yaw, float Roll);

	/** 控制台：`RoomBalanceDebug 1` 每 0.5 秒打印重心 / 倾角 / 角速度。 */
	UFUNCTION(Exec, Category="Room")
	void RoomBalanceDebug(bool bEnable);

	/** 控制台：`RoomWalkDebug 1` 让角色自动持续走（验证移动如何把盒子压歪）。 */
	UFUNCTION(Exec, Category="Room")
	void RoomWalkDebug(bool bEnable);

	/**
	 *  控制台：`RoomWalkFor 12` = 自动走 12 秒后松手。
	 *  用来验证「松手之后角色靠静摩擦站得住、盒子会不会回摆」。
	 */
	UFUNCTION(Exec, Category="Room")
	void RoomWalkFor(float Seconds);

	/** 控制台：`RoomSolver 0/1` 运行时开关重心求解器。 */
	UFUNCTION(Exec, Category="Room")
	void RoomSolver(bool bEnable);

	/** 控制台：`RoomGhost 1` 关掉角色碰撞与救援，用来隔离「角色 vs 旋转地面」的问题。 */
	UFUNCTION(Exec, Category="Room")
	void RoomGhost(bool bEnable);

	/** 控制台：`RoomCarry 0` 关掉手动把角色绕形心搬运（对照实验：看引擎的基座搬运是否已经够用）。 */
	UFUNCTION(Exec, Category="Room")
	void RoomCarry(bool bEnable);

	/** 控制台：`RoomCharState` 打印角色当前姿态 / 移动模式 / 离地板高度（查「人浮在空中」用）。 */
	UFUNCTION(Exec, Category="Room")
	void RoomCharState();

	/** 控制台：`RoomShot 12` = 12 秒后截一张高分辨率图（`-ExecCmds` 里的 HighResShot 总在开局触发，用这个补）。 */
	UFUNCTION(Exec, Category="Room")
	void RoomShot(float DelaySeconds);

public:

	/** 场景中的空心立方体房间（BeginPlay 时自动查找） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Room")
	TObjectPtr<ARoomShell> RoomShell;

	/** 是否启用「按住鼠标左键拖动 → 旋转立方体」 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	bool bEnableMouseDragRotate = false;

	/**
	 *  视角是否用「钉在盒子侧墙上的固定机位」（ARoomShell::FaceCamera）。
	 *  打开后玩家看到的就是那个墙上的视角，鼠标不再控制视角，盒子翻转时画面跟着一起翻。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Camera")
	bool bUseRoomCamera = true;

	/**
	 *  角色能站住的最大坡度（度）。默认 50（引擎 44.77 的常用值），
	 *  太陡会让角色顺着房间内壁「爬墙」，把盒子压成仓鼠轮。
	 *
	 *  注意 CMC 判断「能不能站住」用的是**世界空间**的 ImpactNormal：
	 *  盒子倾到 20° 时内壁在世界里就是 20° 的坡，还在这个范围里；
	 *  再往上（倾角限位放开时）就会被判成「脚下不可站立」直接掉下去。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character|Physics", meta = (ClampMin = "44", ClampMax = "89"))
	float MaxWalkableSlopeAngle = 50.f;

	// ------------------------------------------------------------------ 摩擦
	// 行走摩擦用的是 CharacterMovement 的 GroundFriction（**不是**物理材质的 Friction），
	// 这里保持引擎默认量级；「不让盒子无限翻滚」靠的是 ARoomShell::MaxTiltAngle 的倾角限位。

	/** 地面摩擦系数（GroundFriction，引擎默认 8）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character|Physics", meta = (ClampMin = "0", ClampMax = "64"))
	float CharacterGroundFriction = 8.f;

	/** 行走刹车减速度（cm/s²，引擎默认 2000）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character|Physics", meta = (ClampMin = "0"))
	float WalkBrakingDeceleration = 2000.f;

	/** 调试：自动朝墙面机位的右边一直走，用来验证「角色移动 → 重心偏移 → 盒子倾斜」。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	bool bDebugAutoWalk = false;

	/** 旋转灵敏度（度 / 鼠标位移单位） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	float DragRotateSensitivity = 0.85f;

	/** 纵向拖动的方向反过来（如果上下拖动的手感反了，勾上这个） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	bool bInvertDragVertical = false;

	/** 按住 Shift 拖动：绕视线方向打滚（第三个旋转自由度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	bool bShiftDragRollsRoom = true;

	/** 按 R 键把立方体姿态一键归零 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	bool bEnableResetKey = true;

	/** 拖动时是否改成用纵向拖动控制摄像机俯仰（默认关闭，纵向拖动用来翻转立方体） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	bool bDragPitchesCamera = false;

	/** 摄像机俯仰灵敏度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Input")
	float DragPitchSensitivity = 0.05f;

	// ------------------------------------------------------------------ 重力 / 质量
	// 角色本身由 CharacterMovementComponent 驱动，重力是 CMC 算的：
	// 每帧 v.z -= 980 * GravityScale * dt，再由胶囊与地面做扫描碰撞 —— 这里把这些参数显式化。

	/** 重力倍率（1 = 标准 980 cm/s²，2 = 双倍重力，0 = 失重漂浮）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character|Physics", meta = (ClampMin = "0", ClampMax = "10"))
	float GravityScale = 1.f;

	/** 角色质量（kg）。影响被小球撞 / 推小球时的物理表现。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character|Physics", meta = (ClampMin = "1"))
	float CharacterMass = 80.f;

	// ------------------------------------------------------------------ Hitbox
	// 胶囊负责「走路时别穿墙」，这组盒子负责「被打中时算哪个部位」：
	// QueryOnly + 只对 Visibility 响应 Block，所以绝不会阻挡移动、也不会推动小球。

	/** 各部位 hitbox 定义。默认按胶囊人体比例给了头 / 胸 / 腹 / 大腿 / 上臂共 7 段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hitbox")
	TArray<FCharacterHitboxDef> HitboxDefs;

	/** 运行时生成出来的 hitbox 组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hitbox")
	TArray<TObjectPtr<UBoxComponent>> Hitboxes;

	/** hitbox → 伤害倍率。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hitbox")
	TMap<TObjectPtr<UBoxComponent>, float> HitboxDamageMultipliers;

	/** 在游戏里把 hitbox 画出来（绿框；头是红框）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hitbox")
	bool bDebugDrawHitboxes = false;

	/** 按 HitboxDefs 重建全部 hitbox（改了定义后调用）。 */
	UFUNCTION(BlueprintCallable, Category="Hitbox")
	void RebuildHitboxes();

	/** 按部位名取 hitbox，例如 FindHitbox("Head")。 */
	UFUNCTION(BlueprintPure, Category="Hitbox")
	UBoxComponent* FindHitbox(FName PartName) const;

	/** 取某个 hitbox 的伤害倍率（射线命中后查部位伤害用）。 */
	UFUNCTION(BlueprintPure, Category="Hitbox")
	float GetHitboxDamageMultiplier(const UPrimitiveComponent* Hitbox) const;

	/** 取部位的名称（Head / Chest / ...），不是 hitbox 则返回 None。 */
	UFUNCTION(BlueprintPure, Category="Hitbox")
	FName GetHitPartName(const UPrimitiveComponent* Hitbox) const;

	/**
	 *  对 hitbox 通道做一次射线检测：只可能打到身体各段，墙、胶囊、小球都不会拦。
	 *  命中后可以用 GetHitPartName / GetHitboxDamageMultiplier 取部位与伤害倍率。
	 */
	UFUNCTION(BlueprintCallable, Category="Hitbox")
	bool TraceHitbox(const FVector& Start, const FVector& End, FHitResult& OutHit) const;

	/** 控制台：`RoomHitboxDebug 1` 打开 hitbox 调试绘制。 */
	UFUNCTION(Exec, Category="Hitbox")
	void RoomHitboxDebug(bool bEnable);

	/** 控制台：从头顶往下打一条 hitbox 射线，把命中的部位打到日志里。 */
	UFUNCTION(Exec, Category="Hitbox")
	void RoomHitboxTestTrace();

	// ------------------------------------------------------------------ 俯瞰镜头
	// 镜头距离跟着房间尺寸走：臂长 = 房间边长 × 系数，
	// 这样把 SizeMultiplier 从 10 改成 3 之后构图比例完全不变。

	/** 是否在开局时把镜头拉到「能看到整个立方体」的位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Camera")
	bool bUseOverviewCamera = true;

	/** 俯瞰镜头的弹簧臂长度 = 房间边长 × 该系数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Camera", meta = (ClampMin = "0.2"))
	float OverviewCameraArmFactor = 1.92f;

	/** 俯瞰镜头的俯仰角（负数表示向下看）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Camera", meta = (ClampMin = "-89", ClampMax = "0"))
	float OverviewCameraPitch = -30.f;

	/**
	 *  镜头瞄准点相对角色抬高多少 = 房间边长 × 该系数。
	 *  角色站在立方体底部，直接瞄角色会让盒子挤在画面上半部分；
	 *  抬高半边长就正好瞄在盒子中心，盒子居中。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room|Camera", meta = (ClampMin = "0"))
	float OverviewCameraTargetOffsetFactor = 0.5f;

protected:

	/** 俯瞰镜头是否已经摆好（Pawn 被 Possess 之前拿不到控制器旋转）。 */
	bool bOverviewCameraApplied = false;

	/** 最近一次收到移动输入的时刻（秒）。调试与手感诊断用。 */
	float LastMoveInputTime = -1000.f;

	/** 自动行走的停止时刻（秒，0 = 一直走）。 */
	float AutoWalkUntil = 0.f;

	/** 姿态诊断日志的累计计时（只在开了平衡日志时用）。 */
	float CharStateTimer = 0.f;

	/** 返回房间 */
	UFUNCTION(BlueprintPure, Category="Room")
	ARoomShell* GetRoomShell() const { return RoomShell; }

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

