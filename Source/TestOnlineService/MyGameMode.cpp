// 版权所有 Epic Games, Inc. 保留所有权利。

#include "MyGameMode.h"
#include "MyPlayerController.h"

AMyGameMode::AMyGameMode()
{
	// 关联本 GameMode 默认使用的 PlayerController
	PlayerControllerClass = AMyPlayerController::StaticClass();
}
