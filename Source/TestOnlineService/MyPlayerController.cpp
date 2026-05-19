// 版权所有 Epic Games, Inc. 保留所有权利。

#include "MyPlayerController.h"
#include "Blueprint/UserWidget.h"

AMyPlayerController::AMyPlayerController()
	: SessionWidgetInstance(nullptr)
{
}

void AMyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 仅在本地玩家的控制器中创建 UI，避免在服务端生成界面
	if (IsLocalController() && SessionWidgetClass)
	{
		SessionWidgetInstance = CreateWidget<UUserWidget>(this, SessionWidgetClass);
		if (SessionWidgetInstance)
		{
			// 将会话 Widget 添加到屏幕视口中
			SessionWidgetInstance->AddToViewport();

			// 显示鼠标光标，允许与 UI 按钮进行交互
			bShowMouseCursor = true;

			// 配置输入模式为 GameAndUI，使键盘/鼠标可同时交互 UI 且保留部分按键监听
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(SessionWidgetInstance->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(InputMode);
		}
	}
}
