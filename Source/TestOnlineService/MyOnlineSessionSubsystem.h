// 版权所有 Epic Games, Inc. 保留所有权利。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/OnlineServices.h"
#include "Online/Auth.h"
#include "Online/Sessions.h"
#include "MyOnlineSessionSubsystem.generated.h"

// 声明动态多播委托，供蓝图绑定以接收异步网络操作结果
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMyLoginComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMyCreateSessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMyFindSessionsComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMyJoinSessionComplete, bool, bWasSuccessful);

/**
 * 局域网联机会话管理子系统，使用 UE5.7 全新的 Online Services (OSS v2) 实现
 */
UCLASS()
class TESTONLINESERVICE_API UMyOnlineSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UMyOnlineSessionSubsystem();

	// 子系统初始化和清理生命周期函数
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * 本地玩家登录（局域网模式下为自动模拟登录，生成虚拟的 LocalAccountId）
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Auth")
	void Login();

	/**
	 * 创建仅局域网广播的会话 (LAN Session)
	 * @param SessionName 本地会话命名标识
	 * @param MaxPlayers 最大允许加入的玩家数
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Sessions")
	void CreateLANSession(FName SessionName, int32 MaxPlayers);

	/**
	 * 搜索当前局域网内的广播会话
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Sessions")
	void FindLANSessions();

	/**
	 * 加入已搜索到的局域网会话
	 * @param SessionIndex 会话在搜索结果列表中的索引
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Sessions")
	void JoinLANSession(int32 SessionIndex);

	/**
	 * 获取搜索到的会话自定义服务器名称列表，用于 UI 显示
	 */
	UFUNCTION(BlueprintCallable, Category = "Online|Sessions")
	TArray<FString> GetSearchResultsServerNames() const;

	// 蓝图回调事件代理
	UPROPERTY(BlueprintAssignable, Category = "Online|Delegates")
	FOnMyLoginComplete OnLoginCompleteDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Online|Delegates")
	FOnMyCreateSessionComplete OnCreateSessionCompleteDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Online|Delegates")
	FOnMyFindSessionsComplete OnFindSessionsCompleteDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Online|Delegates")
	FOnMyJoinSessionComplete OnJoinSessionCompleteDelegate;

private:
	// 保存 OSS 平台实例与接口指针
	UE::Online::IOnlineServicesPtr OnlineServices;
	UE::Online::IAuthPtr AuthInterface;
	UE::Online::ISessionsPtr SessionsInterface;

	// 登录状态与缓存的本地账号 ID
	UE::Online::FAccountId LocalAccountId;
	bool bIsLoggedIn;

	// 缓存搜索到的会话 ID 列表
	TArray<UE::Online::FOnlineSessionId> SearchResults;
	// 缓存搜索到的会话详情，用以提取自定义属性（例如服务器名字）
	TArray<TSharedRef<const UE::Online::ISession>> SearchResultSessions;

	// Online Services 异步操作完成回调
	void OnLoginComplete(const UE::Online::TOnlineResult<UE::Online::FAuthLogin>& Result);
	void OnCreateSessionComplete(const UE::Online::TOnlineResult<UE::Online::FCreateSession>& Result);
	void OnFindSessionsComplete(const UE::Online::TOnlineResult<UE::Online::FFindSessions>& Result);
	void OnJoinSessionComplete(const UE::Online::TOnlineResult<UE::Online::FJoinSession>& Result);
};
