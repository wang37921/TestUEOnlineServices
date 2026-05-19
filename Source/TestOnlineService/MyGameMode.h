// 版权所有 Epic Games, Inc. 保留所有权利。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MyGameMode.generated.h"

/**
 * 自定义游戏模式类，用于关联自定义的 PlayerController 并配置本关卡的基础联机网络规则
 */
UCLASS()
class TESTONLINESERVICE_API AMyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMyGameMode();
};
