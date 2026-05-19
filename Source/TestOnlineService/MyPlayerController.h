// 版权所有 Epic Games, Inc. 保留所有权利。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

/**
 * 自定义玩家控制器，在开始游戏时自动为本地玩家生成并显示联机会话 UI 界面
 */
UCLASS()
class TESTONLINESERVICE_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMyPlayerController();

protected:
	// 重写 BeginPlay，在控制器初始化时加载 UI
	virtual void BeginPlay() override;

	// UI 控件类，在蓝图派生类中指定具体的 Widget 蓝图
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<class UUserWidget> SessionWidgetClass;

	// 保存运行时创建出来的 UI 实例引用
	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	class UUserWidget* SessionWidgetInstance;
};
