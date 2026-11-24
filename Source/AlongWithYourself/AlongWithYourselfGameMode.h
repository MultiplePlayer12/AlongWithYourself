// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AlongWithYourselfGameMode.generated.h"

class ARoomShell;

/**
 *  Simple GameMode for a third person game
 *
 *  除了指定 Pawn / Controller，它还在 InitGame 阶段做一次「场景兜底」：
 *  - 关卡里没有 ARoomShell 就就地生成一个；
 *  - 把出生点对齐到房间内地面正上方（房间尺寸改小后，旧关卡里手摆的出生点会跑到盒子外面）；
 *  - 关卡里没有 PlayerStart 就生成一个。
 *  这样哪怕用一张完全空白的关卡，也能直接开跑，不需要任何外部生成脚本。
 */
UCLASS(abstract)
class AAlongWithYourselfGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AAlongWithYourselfGameMode();

	//~ Begin AGameModeBase interface
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	//~ End AGameModeBase interface

	/** 关卡里缺什么就补什么（房间 / 出生点）。 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void EnsureRoomScene();

	/** 关卡里已有的房间；没有则为空。 */
	UFUNCTION(BlueprintPure, Category = "Room")
	ARoomShell* FindRoomShell() const;

	/** 关卡里已有的出生点；没有则为空。 */
	AActor* FindPlayerStartActor() const;

protected:
	/** 是否允许在关卡缺少房间 / 出生点时自动生成。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Room")
	bool bAutoCreateMissingSceneActors = true;

	/** 是否每次开局都把出生点搬到房间内地面正上方（房间尺寸变化后必须这么做）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Room")
	bool bAlignPlayerStartToRoom = true;
};
