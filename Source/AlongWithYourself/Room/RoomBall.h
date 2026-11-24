// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomBall.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UPhysicalMaterial;

/**
 *  房间里的一个小球：启用物理模拟 + 重力，会自由落体、在地板上弹跳。
 *
 *  - 根组件就是球体网格，碰撞类型设为 PhysicsBody；
 *  - 由 ARoomShell 在 BeginPlay 时生成在立方体内（也可以自己在关卡里摆一个）。
 */
UCLASS()
class ALONGWITHYOURSELF_API ARoomBall : public AActor
{
	GENERATED_BODY()

public:
	ARoomBall();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	//~ End AActor interface

	/** 球体网格（根组件）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	/** 球半径（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball", meta = (ClampMin = "2", ClampMax = "200"))
	float BallRadius = 30.f;

	/** 质量（千克）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball", meta = (ClampMin = "0.1"))
	float BallMass = 20.f;

	/** 弹性（0 = 不弹，1 = 完全弹回）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball", meta = (ClampMin = "0", ClampMax = "1"))
	float Bounciness = 0.6f;

	/** 摩擦系数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball", meta = (ClampMin = "0", ClampMax = "2"))
	float Friction = 0.4f;

	/** 是否受重力影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
	bool bUseGravity = true;

	/** 球体材质，留空则尝试加载 /Game/Room/M_Ball，再退回引擎基础材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
	TObjectPtr<UMaterialInterface> BallMaterial;

	/** 运行时改半径（会同步缩放网格、质量与碰撞）。 */
	UFUNCTION(BlueprintCallable, Category = "Ball")
	void SetBallRadius(float NewRadius);

	/** 重新施加一次初始线速度（方便手动把球踢一下）。 */
	UFUNCTION(BlueprintCallable, Category = "Ball")
	void LaunchBall(FVector Velocity);

protected:
	/** 把半径 / 质量 / 弹性 / 材质刷到组件上。 */
	void ApplyBallSettings();

	/** 承载弹性 / 摩擦的物理材质（运行时创建并复用）。 */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> BallPhysMat;

	/** 是否已经由 BeginPlay 打开物理模拟。 */
	bool bPhysicsStarted = false;
};
