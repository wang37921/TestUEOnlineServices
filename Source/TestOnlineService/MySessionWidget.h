// 版权所有 Epic Games, Inc. 保留所有权利。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MySessionWidget.generated.h"

class UMySessionWidget;

/**
 * 动态生成的会话按钮，用于在 ScrollBox 列表中表示单个会话项
 */
UCLASS()
class TESTONLINESERVICE_API UMySessionButton : public UButton
{
	GENERATED_BODY()

public:
	UMySessionButton();

	// 该按钮对应的会话在子系统结果列表中的索引
	int32 SessionIndex;

	// 指向父 Widget 实例的指针
	UPROPERTY()
	UMySessionWidget* ParentWidget;

private:
	UFUNCTION()
	void OnButtonClicked();
};

/**
 * 联机会话交互界面，包含创建会话、查询会话和显示会话列表的功能
 */
UCLASS()
class TESTONLINESERVICE_API UMySessionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMySessionWidget(const FObjectInitializer& ObjectInitializer);

	// 被列表项按钮点击时调用的回调，用于记录选中项
	void OnSessionItemSelected(int32 SessionIndex);

protected:
	// 重写生命周期函数，用于绑定事件与初始化界面
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 绑定蓝图中的“创建会话”按钮，名称必须与蓝图控件一致
	UPROPERTY(meta = (BindWidget))
	class UButton* CreateSessionButton;

	// 绑定蓝图中的“查询会话”按钮，名称必须与蓝图控件一致
	UPROPERTY(meta = (BindWidget))
	class UButton* FindSessionsButton;



	// 绑定蓝图中的“加入会话”按钮，名称必须与蓝图控件一致
	UPROPERTY(meta = (BindWidget))
	class UButton* JoinSessionButton;

	// 绑定蓝图中的“会话列表滚动框”，名称必须与蓝图控件一致
	UPROPERTY(meta = (BindWidget))
	class UScrollBox* SessionList;

private:
	// 按钮点击回调函数
	UFUNCTION()
	void OnCreateSessionButtonClicked();

	UFUNCTION()
	void OnFindSessionsButtonClicked();



	UFUNCTION()
	void OnJoinSessionButtonClicked();

	// 会话子系统异步操作完成的回调函数
	UFUNCTION()
	void OnCreateSessionComplete(bool bWasSuccessful);

	UFUNCTION()
	void OnFindSessionsComplete(bool bWasSuccessful);



	UFUNCTION()
	void OnJoinSessionComplete(bool bWasSuccessful);

	// 辅助函数：获取局域网会话子系统指针
	class ULanSessionSubsystem* GetSessionSubsystem() const;

	// 当前选中的会话索引，-1 表示未选中
	int32 SelectedSessionIndex;

	// 缓存当前动态创建出来的列表按钮，用于在选中时高亮或重置颜色
	UPROPERTY()
	TArray<UMySessionButton*> SessionButtons;
};

