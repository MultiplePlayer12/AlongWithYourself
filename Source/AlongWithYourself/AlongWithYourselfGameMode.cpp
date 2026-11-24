// Copyright Epic Games, Inc. All Rights Reserved.

#include "AlongWithYourselfGameMode.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "Room/RoomShell.h"

namespace
{
	/** 出生点要抬高多少（避免和地板碰撞体初始穿插）。 */
	constexpr float SpawnZOffset = 4.f;

	/** 找不到角色胶囊时的兜底身高（模板角色 192cm）。 */
	constexpr float FallbackCapsuleHalfHeight = 96.f;
}

AAlongWithYourselfGameMode::AAlongWithYourselfGameMode()
{
	// stub
}

void AAlongWithYourselfGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// 出生点必须在本局 Pawn 生成之前准备好，所以放在 InitGame 而不是 BeginPlay
	if (bAutoCreateMissingSceneActors)
	{
		EnsureRoomScene();
	}
}

ARoomShell* AAlongWithYourselfGameMode::FindRoomShell() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ARoomShell> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

AActor* AAlongWithYourselfGameMode::FindPlayerStartActor() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

void AAlongWithYourselfGameMode::EnsureRoomScene()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// ---- 1. 房间 ----
	ARoomShell* Room = FindRoomShell();
	if (!Room)
	{
		Room = World->SpawnActor<ARoomShell>(ARoomShell::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		UE_LOG(LogTemp, Log, TEXT("[RoomScene] 关卡中没有 RoomShell，已在原点生成一个。"));
	}

	// ---- 2. 房间尺寸 ----
	// 用默认 Pawn 的胶囊高度先把尺寸算好：出生点必须按房间内地面摆放，
	// 而此刻 ARoomShell 的 BeginPlay 还没跑（Actor 的 BeginPlay 晚于 InitGame）。
	float HalfHeight = FallbackCapsuleHalfHeight;
	if (DefaultPawnClass)
	{
		if (const ACharacter* CharacterCDO = Cast<ACharacter>(DefaultPawnClass->GetDefaultObject()))
		{
			HalfHeight = CharacterCDO->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
	}

	if (Room && Room->bAutoSizeToCharacter)
	{
		Room->SetCharacterHeight(HalfHeight * 2.f);
	}

	// ---- 3. 出生点 ----
	AActor* ExistingStart = FindPlayerStartActor();

	// 房间尺寸改小后，旧关卡里手摆的出生点会落在盒子外面（角色会一直往下掉），
	// 所以这里每次都把它搬到「内地面 + 胶囊半高 + 余量」。
	if (ExistingStart && bAlignPlayerStartToRoom && Room)
	{
		const FVector FloorLocation = Room->GetInnerFloorLocation();
		const FVector StartLocation = FloorLocation + FVector(0.f, 0.f, HalfHeight + SpawnZOffset);
		ExistingStart->SetActorLocation(StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Log, TEXT("[RoomScene] 出生点已对齐到房间内地面：%s"), *StartLocation.ToCompactString());
		return;
	}

	if (ExistingStart)
	{
		return;
	}

	// 原本就没有出生点：在房间内地板正上方生成一个
	const FVector FloorLocation = Room ? Room->GetInnerFloorLocation() : FVector::ZeroVector;
	const FVector StartLocation = FloorLocation + FVector(0.f, 0.f, HalfHeight + SpawnZOffset);

	World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), StartLocation, FRotator::ZeroRotator, SpawnParams);
	UE_LOG(LogTemp, Log, TEXT("[RoomScene] 关卡中没有 PlayerStart，已在 %s 生成一个。"), *StartLocation.ToCompactString());
}
