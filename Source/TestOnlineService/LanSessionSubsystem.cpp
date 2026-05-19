// 版权所有 Epic Games, Inc. 保留所有权利。

#include "LanSessionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/SessionsLAN.h"
#include "IPAddress.h"


ULanSessionSubsystem::ULanSessionSubsystem()
	: bIsLoggedIn(false)
	, ActiveSessionName(NAME_None)
{
}

void ULanSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 获取默认的 Online Services 实例（根据 DefaultEngine.ini 的配置，即为 Null 提供程序）
	OnlineServices = UE::Online::GetServices(UE::Online::EOnlineServices::Default);
	if (OnlineServices.IsValid())
	{
		// 获取身份认证与会话管理接口
		AuthInterface = OnlineServices->GetAuthInterface();
		SessionsInterface = OnlineServices->GetSessionsInterface();

		UE_LOG(LogTemp, Warning, TEXT("[MyOSS] 成功加载 Online Services Null 提供程序。"));

		// 局域网 Null 平台在初始化时已经自动将本地用户登录（LoggedIn 状态）。
		// 我们在此处直接尝试获取本地已登录的账号，并缓存起来，避免后续操作需要手动调用 Login。
		if (AuthInterface.IsValid())
		{
			UE::Online::FAuthGetLocalOnlineUserByPlatformUserId::Params GetUserParams;
			GetUserParams.PlatformUserId = FPlatformUserId::CreateFromInternalId(0);
			UE::Online::TOnlineResult<UE::Online::FAuthGetLocalOnlineUserByPlatformUserId> GetUserResult = AuthInterface->GetLocalOnlineUserByPlatformUserId(MoveTemp(GetUserParams));

			if (GetUserResult.IsOk())
			{
				const UE::Online::FAuthGetLocalOnlineUserByPlatformUserId::Result& UserResult = GetUserResult.GetOkValue();
				LocalAccountId = UserResult.AccountInfo->AccountId;
				bIsLoggedIn = true;

				UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功在初始化时获取本地已登录账号！本地账号 ID：%s"), *UE::Online::ToLogString(LocalAccountId));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[MyOSS] 初始化时未获取到本地已登录账号，将等待后续操作重新获取。"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 未能加载默认的 Online Services。请检查 DefaultEngine.ini 配置。"));
	}
}

void ULanSessionSubsystem::Deinitialize()
{
	// 子系统注销时自动清理活动会话，防止残留端口/广播通道占用
	if (SessionsInterface.IsValid() && !ActiveSessionName.IsNone() && LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[MyOSS] Subsystem 正在注销，自动清理活动会话: %s"), *ActiveSessionName.ToString());

		UE::Online::FLeaveSession::Params Params;
		Params.LocalAccountId = LocalAccountId;
		Params.SessionName = ActiveSessionName;
		Params.bDestroySession = true;

		// 异步清理底层网络会话，由于子系统即将销毁，在此处不应当绑定 OnComplete 成员函数回调，避免野指针崩溃
		SessionsInterface->LeaveSession(MoveTemp(Params));

		ActiveSessionName = NAME_None;
	}

	Super::Deinitialize();
}


void ULanSessionSubsystem::CreateLANSession()
{
	if (!SessionsInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] Sessions 接口无效，无法创建会话。"));
		OnCreateSessionCompleteDelegate.Broadcast(false);
		return;
	}

	if (!bIsLoggedIn || !LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 局域网未获取到有效的本地已登录账号，无法创建会话。"));
		OnCreateSessionCompleteDelegate.Broadcast(false);
		return;
	}

	// 准备创建会话的参数，直接使用默认的会话名称和连接上限
	UE::Online::FCreateSession::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.SessionName = NAME_GameSession;
	Params.bIsLANSession = true;        // 关键步骤：开启局域网广播广播模式，使其他玩家可通过 UDP 广播发现
	Params.bPresenceEnabled = false;    // 局域网环境不需要 Presence 云端同步

	// 配置会话设置
	Params.SessionSettings.SchemaName = FName(TEXT("DefaultSchema"));          // 必须指定Schema，否则会触发 invalid_params 校验失败
	Params.SessionSettings.NumMaxConnections = 4;
	Params.SessionSettings.JoinPolicy = UE::Online::ESessionJoinPolicy::Public; // 公开加入
	Params.SessionSettings.bAllowNewMembers = true;                            // 允许新成员加入

	// 添加局域网自定义的属性（比如服务器显示名称）
	FString ComputerName = FPlatformProcess::ComputerName();
	UE::Online::FSchemaVariant ServerNameVariant(FString::Printf(TEXT("%s 的局域网房间"), *ComputerName));
	UE::Online::FCustomSessionSetting ServerNameSetting;
	ServerNameSetting.Data = ServerNameVariant;
	ServerNameSetting.Visibility = UE::Online::ESchemaAttributeVisibility::Public;

	Params.SessionSettings.CustomSettings.Add(FName(TEXT("ServerName")), ServerNameSetting);

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 开始创建默认局域网 Session: GameSession, 最大人数: 4"));
	ActiveSessionName = Params.SessionName; // 在开始创建时预先缓存会话名称
	SessionsInterface->CreateSession(MoveTemp(Params))
		.OnComplete(this, &ULanSessionSubsystem::OnCreateSessionComplete);
}

void ULanSessionSubsystem::OnCreateSessionComplete(const UE::Online::TOnlineResult<UE::Online::FCreateSession>& Result)
{
	if (Result.IsOk())
	{
		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 局域网 Session 创建成功！正在局域网中进行心跳广播。"));
		
		/**
		 * 【重要提示 (常规网络流程规范)】
		 * 仅仅创建会话只完成了局域网的信息广播登记。
		 * 要让其他玩家连入，Host 端在会话创建完毕后必须开启 Listen Server：
		 * 必须调用 OpenLevel(GetWorld(), TEXT("MapName"), true, TEXT("listen"))。
		 * 请在蓝图回调 "OnCreateSessionCompleteDelegate" 触发后执行 OpenLevel 命令开启监听。
		 */

		OnCreateSessionCompleteDelegate.Broadcast(true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 局域网 Session 创建失败！错误信息：%s"), *Result.GetErrorValue().GetLogString());
		ActiveSessionName = NAME_None; // 创建失败，清除缓存
		OnCreateSessionCompleteDelegate.Broadcast(false);
	}
}

void ULanSessionSubsystem::FindLANSessions()
{
	if (!SessionsInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] Sessions 接口无效，无法搜索会话。"));
		OnFindSessionsCompleteDelegate.Broadcast(false);
		return;
	}

	if (!bIsLoggedIn || !LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 局域网未获取到有效的本地已登录账号，无法搜索会话。"));
		OnFindSessionsCompleteDelegate.Broadcast(false);
		return;
	}

	// 搜索参数配置
	UE::Online::FFindSessions::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.bFindLANSessions = true; // 关键：指定搜索局域网 Session
	Params.MaxResults = 10;        // 限制返回最多 10 个结果

	// 清空历史搜索数据
	SearchResults.Empty();
	SearchResultSessions.Empty();

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 开始搜索局域网会话..."));
	SessionsInterface->FindSessions(MoveTemp(Params))
		.OnComplete(this, &ULanSessionSubsystem::OnFindSessionsComplete);
}

void ULanSessionSubsystem::OnFindSessionsComplete(const UE::Online::TOnlineResult<UE::Online::FFindSessions>& Result)
{
	if (Result.IsOk())
	{
		const UE::Online::FFindSessions::Result& FindResult = Result.GetOkValue();
		SearchResults = FindResult.FoundSessionIds;

		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 局域网会话搜索完毕。共发现 %d 个活动会话。"), SearchResults.Num());

		// 缓存查询到的具体 ISession 实例，以便获取自定义属性
		for (const UE::Online::FOnlineSessionId& SessionId : SearchResults)
		{
			UE::Online::FGetSessionById::Params IdParams;
			IdParams.SessionId = SessionId;
			
			// 同步获取缓存 of Session 详情
			UE::Online::TOnlineResult<UE::Online::FGetSessionById> IdResult = SessionsInterface->GetSessionById(MoveTemp(IdParams));
			if (IdResult.IsOk())
			{
				TSharedRef<const UE::Online::ISession> SessionRef = IdResult.GetOkValue().Session;
				SearchResultSessions.Add(SessionRef);
				
				// 打印 Session 的所有内部状态进行调试
				SessionRef->DumpState();

				// 由于 UE5.7 OSSv2 Null 平台在局域网下通过 GetResolvedConnectString 获取连接地址存在 Bug（常返回空或报错）
				// 因此此处直接强转为 FSessionLAN 获取 OwnerInternetAddr，这是当前引擎版本下局域网联机稳定工作的做法。
				const UE::Online::FSessionLAN& LanSession = UE::Online::FSessionLAN::Cast(*SessionRef);
				FString ConnectURL = TEXT("未知IP");
				if (LanSession.OwnerInternetAddr.IsValid())
				{
					ConnectURL = LanSession.OwnerInternetAddr->ToString(true);
				}

				// 尝试直接从 CustomSettings 中读取 ServerName 属性
				const UE::Online::FSessionSettings& Settings = SessionRef->GetSessionSettings();
				const UE::Online::FCustomSessionSetting* FoundServerNameSetting = Settings.CustomSettings.Find(FName(TEXT("ServerName")));
				if (FoundServerNameSetting && FoundServerNameSetting->Data.GetType() == UE::Online::ESchemaAttributeType::String)
				{
					UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功发现会话 ID [%s]，服务器房间名称：%s，连接 IP：%s"), 
						*UE::Online::ToLogString(SessionId), 
						*FoundServerNameSetting->Data.GetString(),
						*ConnectURL);
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功发现会话 ID [%s]，连接 IP：%s"), 
						*UE::Online::ToLogString(SessionId), 
						*ConnectURL);
				}
			}
		}

		OnFindSessionsCompleteDelegate.Broadcast(true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 局域网会话搜索失败！错误信息：%s"), *Result.GetErrorValue().GetLogString());
		OnFindSessionsCompleteDelegate.Broadcast(false);
	}
}

void ULanSessionSubsystem::JoinLANSession(int32 SessionIndex)
{
	if (!SessionsInterface.IsValid() || !OnlineServices.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 接口无效，无法加入会话。"));
		OnJoinSessionCompleteDelegate.Broadcast(false);
		return;
	}

	if (!SearchResults.IsValidIndex(SessionIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 索引 %d 无效，已越界（搜索到的总数为 %d）。"), SessionIndex, SearchResults.Num());
		OnJoinSessionCompleteDelegate.Broadcast(false);
		return;
	}

	if (!bIsLoggedIn || !LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 本地账户未登录，无法加入会话。"));
		OnJoinSessionCompleteDelegate.Broadcast(false);
		return;
	}

	UE::Online::FJoinSession::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.SessionName = NAME_GameSession; // 客户端本地同样映射为标准的 NAME_GameSession
	Params.SessionId = SearchResults[SessionIndex];

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 正在尝试加入 Session ID: %s"), *UE::Online::ToLogString(Params.SessionId));
	ActiveSessionName = Params.SessionName; // 缓存本地加入会话所映射的本地会话标识名
	SessionsInterface->JoinSession(MoveTemp(Params))
		.OnComplete(this, &ULanSessionSubsystem::OnJoinSessionComplete);
}

void ULanSessionSubsystem::OnJoinSessionComplete(const UE::Online::TOnlineResult<UE::Online::FJoinSession>& Result)
{
	if (Result.IsOk())
	{
		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功加入局域网会话。正在解析连接 URL 并执行 Travel..."));

		// 我们先在本地通过刚刚加入的会话查找对应的 Session 实例
		UE::Online::FGetSessionByName::Params GetByNameParams;
		GetByNameParams.LocalName = NAME_GameSession;
		UE::Online::TOnlineResult<UE::Online::FGetSessionByName> GetByNameResult = SessionsInterface->GetSessionByName(MoveTemp(GetByNameParams));

		if (GetByNameResult.IsOk())
		{
			TSharedRef<const UE::Online::ISession> SessionRef = GetByNameResult.GetOkValue().Session;
			
			// 由于 UE5.7 OSSv2 Null 平台在局域网下通过 GetResolvedConnectString 获取连接地址存在 Bug（常返回空或报错）
			// 因此此处直接强转为 FSessionLAN 获取 OwnerInternetAddr，这是当前引擎版本下局域网联机唯一能正常工作的做法。
			const UE::Online::FSessionLAN& LanSession = UE::Online::FSessionLAN::Cast(*SessionRef);
			if (LanSession.OwnerInternetAddr.IsValid())
			{
				FString ConnectURL = LanSession.OwnerInternetAddr->ToString(true);
				UE_LOG(LogTemp, Log, TEXT("[MyOSS] 强转解析成功。局域网连接地址：%s"), *ConnectURL);

				// 执行 Client Travel，让客机自动联入 Host 端的主地图
				if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
				{
					PC->ClientTravel(ConnectURL, ETravelType::TRAVEL_Absolute);
				}
				OnJoinSessionCompleteDelegate.Broadcast(true);
				return;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[MyOSS] 强转解析失败，局域网连接 IP 地址无效。"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[MyOSS] 本地未能找到刚才加入的缓存会话。"));
		}

		OnJoinSessionCompleteDelegate.Broadcast(false);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 加入会话失败！错误信息：%s"), *Result.GetErrorValue().GetLogString());
		ActiveSessionName = NAME_None; // 加入会话失败，清除缓存
		OnJoinSessionCompleteDelegate.Broadcast(false);
	}
}

TArray<FString> ULanSessionSubsystem::GetSearchResultsServerNames() const
{
	TArray<FString> ServerNames;
	for (const TSharedRef<const UE::Online::ISession>& SessionRef : SearchResultSessions)
	{
		const UE::Online::FSessionSettings& Settings = SessionRef->GetSessionSettings();
		
		// 尝试从 CustomSettings 中读取 ServerName 键值对
		const UE::Online::FCustomSessionSetting* FoundSetting = Settings.CustomSettings.Find(FName(TEXT("ServerName")));
		if (FoundSetting && FoundSetting->Data.GetType() == UE::Online::ESchemaAttributeType::String)
		{
			ServerNames.Add(FoundSetting->Data.GetString());
		}
		else
		{
			// 如果没找到，显示主机的系统标识
			ServerNames.Add(FString::Printf(TEXT("未命名局域网房间 (%s)"), *UE::Online::ToLogString(SessionRef->GetSessionId())));
		}
	}
	return ServerNames;
}

void ULanSessionSubsystem::DestroySession()
{
	if (!SessionsInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] Sessions 接口无效，无法关闭会话。"));
		OnDestroySessionCompleteDelegate.Broadcast(false);
		return;
	}

	if (!bIsLoggedIn || !LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 局域网未获取到有效的本地已登录账号，无法关闭会话。"));
		OnDestroySessionCompleteDelegate.Broadcast(false);
		return;
	}

	if (ActiveSessionName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MyOSS] 当前没有处于活动状态的会话需要关闭。"));
		OnDestroySessionCompleteDelegate.Broadcast(true); // 无活动会话，直接视作清理完成
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 开始主动关闭局域网会话: %s"), *ActiveSessionName.ToString());

	UE::Online::FLeaveSession::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.SessionName = ActiveSessionName;
	Params.bDestroySession = true; // 对 Host 是销毁会话，对 Client 是离开并清理

	SessionsInterface->LeaveSession(MoveTemp(Params))
		.OnComplete(this, &ULanSessionSubsystem::OnLeaveSessionComplete);
}

void ULanSessionSubsystem::OnLeaveSessionComplete(const UE::Online::TOnlineResult<UE::Online::FLeaveSession>& Result)
{
	if (Result.IsOk())
	{
		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功关闭/离开局域网会话：%s"), *ActiveSessionName.ToString());
		ActiveSessionName = NAME_None;
		OnDestroySessionCompleteDelegate.Broadcast(true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 关闭/离开局域网会话失败！错误信息：%s"), *Result.GetErrorValue().GetLogString());
		// 即使失败，也需要清空缓存的会话，以便玩家重新发起操作
		ActiveSessionName = NAME_None;
		OnDestroySessionCompleteDelegate.Broadcast(false);
	}
}
