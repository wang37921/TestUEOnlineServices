// 版权所有 Epic Games, Inc. 保留所有权利。

#include "MySessionWidget.h"
#include "LanSessionSubsystem.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Kismet/GameplayStatics.h"

UMySessionButton::UMySessionButton()
	: SessionIndex(-1)
	, ParentWidget(nullptr)
{
	// 绑定按钮点击事件到内部的处理函数
	OnClicked.AddDynamic(this, &UMySessionButton::OnButtonClicked);
}

void UMySessionButton::OnButtonClicked()
{
	if (ParentWidget)
	{
		ParentWidget->OnSessionItemSelected(SessionIndex);
	}
}

UMySessionWidget::UMySessionWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SelectedSessionIndex(-1)
{
}

void UMySessionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 绑定按钮点击事件到 C++ 响应函数
	if (CreateSessionButton)
	{
		CreateSessionButton->OnClicked.AddDynamic(this, &UMySessionWidget::OnCreateSessionButtonClicked);
	}

	if (FindSessionsButton)
	{
		FindSessionsButton->OnClicked.AddDynamic(this, &UMySessionWidget::OnFindSessionsButtonClicked);
	}



	if (JoinSessionButton)
	{
		JoinSessionButton->OnClicked.AddDynamic(this, &UMySessionWidget::OnJoinSessionButtonClicked);
		// 初始状态下禁用“加入会话”按钮，直到玩家选中了具体会话项
		JoinSessionButton->SetIsEnabled(false);
	}

	// 绑定会话子系统的网络多播委托回调
	if (ULanSessionSubsystem* SessionSubsystem = GetSessionSubsystem())
	{
		SessionSubsystem->OnCreateSessionCompleteDelegate.AddDynamic(this, &UMySessionWidget::OnCreateSessionComplete);
		SessionSubsystem->OnFindSessionsCompleteDelegate.AddDynamic(this, &UMySessionWidget::OnFindSessionsComplete);
		SessionSubsystem->OnJoinSessionCompleteDelegate.AddDynamic(this, &UMySessionWidget::OnJoinSessionComplete);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MySessionWidget] 未能获取到有效的 LanSessionSubsystem。"));
	}
}

void UMySessionWidget::NativeDestruct()
{
	// 界面销毁时解绑代理，防止野指针造成崩溃
	if (ULanSessionSubsystem* SessionSubsystem = GetSessionSubsystem())
	{
		SessionSubsystem->OnCreateSessionCompleteDelegate.RemoveDynamic(this, &UMySessionWidget::OnCreateSessionComplete);
		SessionSubsystem->OnFindSessionsCompleteDelegate.RemoveDynamic(this, &UMySessionWidget::OnFindSessionsComplete);
		SessionSubsystem->OnJoinSessionCompleteDelegate.RemoveDynamic(this, &UMySessionWidget::OnJoinSessionComplete);
	}

	Super::NativeDestruct();
}

void UMySessionWidget::OnCreateSessionButtonClicked()
{
	if (ULanSessionSubsystem* SessionSubsystem = GetSessionSubsystem())
	{
		FString HostName = TEXT("DefaultHost");
		if (HostNameInput)
		{
			FString InputText = HostNameInput->GetText().ToString().TrimStartAndEnd();
			if (!InputText.IsEmpty())
			{
				HostName = InputText;
			}
		}

		// 根据输入框动态获取 HostName 创建局域网会话
		SessionSubsystem->CreateLANSession(HostName);
	}
}

void UMySessionWidget::OnFindSessionsButtonClicked()
{
	if (ULanSessionSubsystem* SessionSubsystem = GetSessionSubsystem())
	{
		FString HostName = TEXT("DefaultHost");
		if (HostNameInput)
		{
			FString InputText = HostNameInput->GetText().ToString().TrimStartAndEnd();
			if (!InputText.IsEmpty())
			{
				HostName = InputText;
			}
		}

		// 根据输入框动态获取 HostName 搜索局域网内对应的会话
		SessionSubsystem->FindLANSessions(HostName);
	}
}



void UMySessionWidget::OnJoinSessionButtonClicked()
{
	if (SelectedSessionIndex != -1)
	{
		if (ULanSessionSubsystem* SessionSubsystem = GetSessionSubsystem())
		{
			// 加入已选中的会话索引项
			SessionSubsystem->JoinLANSession(SelectedSessionIndex);
		}
	}
}

void UMySessionWidget::OnCreateSessionComplete(bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogTemp, Log, TEXT("[MySessionWidget] 创建会话成功！"));
		// 提示：主机此时需自行通过 OpenLevel 开启监听服务器，例如 OpenLevel(GetWorld(), TEXT("YourMap"), true, TEXT("listen"))。
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MySessionWidget] 创建会话失败。"));
	}
}

void UMySessionWidget::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (!SessionList)
	{
		return;
	}

	// 清空滚动列表中的旧数据显示新数据
	SessionList->ClearChildren();
	SessionButtons.Empty();
	SelectedSessionIndex = -1;

	if (JoinSessionButton)
	{
		// 查询结束后重置并禁用“加入会话”按钮，直到玩家重新选择
		JoinSessionButton->SetIsEnabled(false);
	}

	if (bWasSuccessful)
	{
		if (ULanSessionSubsystem* SessionSubsystem = GetSessionSubsystem())
		{
			// 获取查询到的所有会话自定义显示名字
			TArray<FString> ServerNames = SessionSubsystem->GetSearchResultsServerNames();
			UE_LOG(LogTemp, Log, TEXT("[MySessionWidget] 查询会话成功，共找到 %d 个会话。"), ServerNames.Num());

			for (int32 Index = 0; Index < ServerNames.Num(); ++Index)
			{
				const FString& ServerName = ServerNames[Index];

				// 动态创建自定义的 UMySessionButton 实例
				UMySessionButton* SessionBtn = NewObject<UMySessionButton>(this);
				if (SessionBtn)
				{
					SessionBtn->SessionIndex = Index;
					SessionBtn->ParentWidget = this;

					// 创建文本子组件来显示会话名称
					UTextBlock* SessionText = NewObject<UTextBlock>(this);
					if (SessionText)
					{
						SessionText->SetText(FText::FromString(ServerName));

						// 设置字体样式
						FSlateFontInfo FontInfo = SessionText->GetFont();
						FontInfo.Size = 16;
						SessionText->SetFont(FontInfo);

						// 增加间距以提高列表的可读性
						SessionText->SetMargin(FMargin(10.f, 5.f, 10.f, 5.f));

						// 将文本添加到按钮中
						SessionBtn->AddChild(SessionText);
					}

					// 将按钮添加到 ScrollBox 滚动列表中
					SessionList->AddChild(SessionBtn);

					// 缓存按钮指针用于之后的选中高亮逻辑
					SessionButtons.Add(SessionBtn);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MySessionWidget] 查询会话失败或无结果。"));
	}
}



void UMySessionWidget::OnJoinSessionComplete(bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogTemp, Log, TEXT("[MySessionWidget] 成功加入远程会话，正在尝试连接..."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MySessionWidget] 加入会话失败。"));
	}
}

void UMySessionWidget::OnSessionItemSelected(int32 SessionIndex)
{
	SelectedSessionIndex = SessionIndex;

	if (JoinSessionButton)
	{
		// 选中会话后，启用“加入会话”按钮
		JoinSessionButton->SetIsEnabled(true);
	}

	// 遍历已缓存的所有按钮，对选中的项进行高亮显示
	for (UMySessionButton* Btn : SessionButtons)
	{
		if (Btn)
		{
			if (Btn->SessionIndex == SelectedSessionIndex)
			{
				// 高亮选中按钮（例如使用半透明淡绿色）
				Btn->SetBackgroundColor(FLinearColor(0.1f, 0.8f, 0.1f, 1.f));
			}
			else
			{
				// 恢复未选中按钮的默认背景色（白色）
				Btn->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 1.f));
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MySessionWidget] 玩家选中了会话索引: %d"), SelectedSessionIndex);
}

ULanSessionSubsystem* UMySessionWidget::GetSessionSubsystem() const
{
	if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(GetWorld()))
	{
		// 获取我们重命名后的局域网会话子系统实例
		return GameInstance->GetSubsystem<ULanSessionSubsystem>();
	}
	return nullptr;
}
