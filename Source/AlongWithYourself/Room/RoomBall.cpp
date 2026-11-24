// Copyright Epic Games, Inc. All Rights Reserved.

#include "Room/RoomBall.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 引擎自带球体网格的半径（cm）：Sphere 资源直径 100。 */
	constexpr float EngineSphereRadius = 50.f;
}

ARoomBall::ARoomBall()
{
	PrimaryActorTick.bCanEverTick = false;

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	SetRootComponent(BallMesh);

	BallMesh->SetMobility(EComponentMobility::Movable);
	BallMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BallMesh->SetCollisionObjectType(ECC_PhysicsBody);
	BallMesh->SetCollisionResponseToAllChannels(ECR_Block);
	BallMesh->SetGenerateOverlapEvents(false);
	BallMesh->SetCanEverAffectNavigation(false);
	BallMesh->SetCastShadow(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
	}
}

void ARoomBall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyBallSettings();
}

void ARoomBall::BeginPlay()
{
	Super::BeginPlay();

	ApplyBallSettings();

	// 物理模拟放到 BeginPlay 再打开：位置由生成方（ARoomShell）先摆好，避免一开始就往下掉
	if (!bPhysicsStarted)
	{
		bPhysicsStarted = true;
		BallMesh->SetSimulatePhysics(true);
		BallMesh->SetEnableGravity(bUseGravity);
	}
}

void ARoomBall::SetBallRadius(float NewRadius)
{
	BallRadius = FMath::Clamp(NewRadius, 2.f, 200.f);
	ApplyBallSettings();
}

void ARoomBall::LaunchBall(FVector Velocity)
{
	if (BallMesh)
	{
		BallMesh->SetPhysicsLinearVelocity(Velocity);
	}
}

void ARoomBall::ApplyBallSettings()
{
	if (!BallMesh)
	{
		return;
	}

	// 球体网格原始半径 50，缩放到目标半径
	const float UniformScale = FMath::Max(BallRadius, 2.f) / EngineSphereRadius;
	BallMesh->SetRelativeScale3D(FVector(UniformScale));

	// 材质：优先细节面板指定，其次工程里的球材质，最后引擎基础材质
	UMaterialInterface* Material = BallMaterial;
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Room/M_Ball.M_Ball"));
	}
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (Material)
	{
		BallMesh->SetMaterial(0, Material);
	}

	// 物理参数：质量 + 弹性 / 摩擦（用一个复用的物理材质承载）
	FBodyInstance* Body = BallMesh->GetBodyInstance();
	if (Body)
	{
		Body->SetMassOverride(FMath::Max(BallMass, 0.1f), true);
	}

	if (!BallPhysMat)
	{
		BallPhysMat = NewObject<UPhysicalMaterial>(this, TEXT("BallPhysMat"));
	}
	if (BallPhysMat)
	{
		BallPhysMat->Restitution = FMath::Clamp(Bounciness, 0.f, 1.f);
		BallPhysMat->RestitutionCombineMode = EFrictionCombineMode::Max;
		BallPhysMat->bOverrideRestitutionCombineMode = true;
		BallPhysMat->Friction = FMath::Clamp(Friction, 0.f, 2.f);
		BallPhysMat->FrictionCombineMode = EFrictionCombineMode::Average;
		BallPhysMat->bOverrideFrictionCombineMode = true;
		BallMesh->SetPhysMaterialOverride(BallPhysMat);
	}

	BallMesh->SetEnableGravity(bUseGravity);
}
