// 版权所有 Epic Games, Inc. 保留所有权利。

#include "MyOnlineSessionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Online/OnlineAsyncOpHandle.h"

UMyOnlineSessionSubsystem::UMyOnlineSessionSubsystem()
	: bIsLoggedIn(false)
{
}

void UMyOnlineSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
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
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 未能加载默认的 Online Services。请检查 DefaultEngine.ini 配置。"));
	}
}

void UMyOnlineSessionSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UMyOnlineSessionSubsystem::Login()
{
	if (!AuthInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] Auth 接口无效，无法登录。"));
		OnLoginCompleteDelegate.Broadcast(false);
		return;
	}

	if (bIsLoggedIn)
	{
		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 用户已经处于登录状态。"));
		OnLoginCompleteDelegate.Broadcast(true);
		return;
	}

	// 局域网 Null 平台在初始化时已经自动将本地用户登录（LoggedIn 状态）。
	// 我们首先尝试直接查询本地已登录的用户，如果成功就直接使用，这可以避免调用未实现的 Login 方法。
	UE::Online::FAuthGetLocalOnlineUserByPlatformUserId::Params GetUserParams;
	GetUserParams.PlatformUserId = FPlatformUserId::CreateFromInternalId(0);
	UE::Online::TOnlineResult<UE::Online::FAuthGetLocalOnlineUserByPlatformUserId> GetUserResult = AuthInterface->GetLocalOnlineUserByPlatformUserId(MoveTemp(GetUserParams));

	if (GetUserResult.IsOk())
	{
		const UE::Online::FAuthGetLocalOnlineUserByPlatformUserId::Result& UserResult = GetUserResult.GetOkValue();
		LocalAccountId = UserResult.AccountInfo->AccountId;
		bIsLoggedIn = true;

		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功获取到本地已登录账号！本地账号 ID：%s"), *UE::Online::ToLogString(LocalAccountId));
		OnLoginCompleteDelegate.Broadcast(true);
		return;
	}

	// 如果没有获取到已登录用户（例如其他支持异步登录的平台），则尝试执行真实的异步登录流程
	UE::Online::FAuthLogin::Params Params;
	// 为本地第一个玩家位置生成凭证
	Params.PlatformUserId = FPlatformUserId::CreateFromInternalId(0);
	Params.CredentialsType = UE::Online::LoginCredentialsType::Auto;

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 本地未检测到已登录账户，正在启动自动登录接口..."));
	AuthInterface->Login(MoveTemp(Params))
		.OnComplete(this, &UMyOnlineSessionSubsystem::OnLoginComplete);
}

void UMyOnlineSessionSubsystem::OnLoginComplete(const UE::Online::TOnlineResult<UE::Online::FAuthLogin>& Result)
{
	if (Result.IsOk())
	{
		const UE::Online::FAuthLogin::Result& LoginResult = Result.GetOkValue();
		LocalAccountId = LoginResult.AccountInfo->AccountId;
		bIsLoggedIn = true;

		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 登录成功！分配的本地账号 ID：%s"), *UE::Online::ToLogString(LocalAccountId));
		OnLoginCompleteDelegate.Broadcast(true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] 登录失败！错误信息：%s"), *Result.GetErrorValue().GetLogString());
		bIsLoggedIn = false;
		OnLoginCompleteDelegate.Broadcast(false);
	}
}

void UMyOnlineSessionSubsystem::CreateLANSession(FName SessionName, int32 MaxPlayers)
{
	if (!SessionsInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] Sessions 接口无效，无法创建会话。"));
		OnCreateSessionCompleteDelegate.Broadcast(false);
		return;
	}

	if (!bIsLoggedIn || !LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MyOSS] 未登录本地账号，正在尝试自动登录后再创建会话。"));
		// 自动帮玩家先登录，随后让其重新调用创建
		Login();
		OnCreateSessionCompleteDelegate.Broadcast(false);
		return;
	}

	// 准备创建会话的参数
	UE::Online::FCreateSession::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.SessionName = SessionName;
	Params.bIsLANSession = true;        // 关键步骤：开启局域网广播广播模式，使其他玩家可通过 UDP 广播发现
	Params.bPresenceEnabled = false;    // 局域网环境不需要 Presence 云端同步

	// 配置会话设置
	Params.SessionSettings.SchemaName = FName(TEXT("DefaultSchema"));          // 必须指定Schema，否则会触发 invalid_params 校验失败
	Params.SessionSettings.NumMaxConnections = MaxPlayers;
	Params.SessionSettings.JoinPolicy = UE::Online::ESessionJoinPolicy::Public; // 公开加入
	Params.SessionSettings.bAllowNewMembers = true;                            // 允许新成员加入

	// 添加局域网自定义的属性（比如服务器显示名称）
	FString ComputerName = FPlatformProcess::ComputerName();
	UE::Online::FSchemaVariant ServerNameVariant(FString::Printf(TEXT("%s 的局域网房间"), *ComputerName));
	UE::Online::FCustomSessionSetting ServerNameSetting;
	ServerNameSetting.Data = ServerNameVariant;
	ServerNameSetting.Visibility = UE::Online::ESchemaAttributeVisibility::Public;

	Params.SessionSettings.CustomSettings.Add(FName(TEXT("ServerName")), ServerNameSetting);

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 开始创建局域网 Session: %s, 最大人数: %d"), *SessionName.ToString(), MaxPlayers);
	SessionsInterface->CreateSession(MoveTemp(Params))
		.OnComplete(this, &UMyOnlineSessionSubsystem::OnCreateSessionComplete);
}

void UMyOnlineSessionSubsystem::OnCreateSessionComplete(const UE::Online::TOnlineResult<UE::Online::FCreateSession>& Result)
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
		OnCreateSessionCompleteDelegate.Broadcast(false);
	}
}

void UMyOnlineSessionSubsystem::FindLANSessions()
{
	if (!SessionsInterface.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MyOSS] Sessions 接口无效，无法搜索会话。"));
		OnFindSessionsCompleteDelegate.Broadcast(false);
		return;
	}

	if (!bIsLoggedIn || !LocalAccountId.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MyOSS] 搜索会话前需要先完成登录，正在执行自动登录..."));
		Login();
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
		.OnComplete(this, &UMyOnlineSessionSubsystem::OnFindSessionsComplete);
}

void UMyOnlineSessionSubsystem::OnFindSessionsComplete(const UE::Online::TOnlineResult<UE::Online::FFindSessions>& Result)
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
			
			// 同步获取缓存的 Session 详情
			UE::Online::TOnlineResult<UE::Online::FGetSessionById> IdResult = SessionsInterface->GetSessionById(MoveTemp(IdParams));
			if (IdResult.IsOk())
			{
				SearchResultSessions.Add(IdResult.GetOkValue().Session);
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

void UMyOnlineSessionSubsystem::JoinLANSession(int32 SessionIndex)
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
	Params.SessionName = FName(TEXT("LAN_Joined_Session")); // 在本地为此会话取的唯一缓存名
	Params.SessionId = SearchResults[SessionIndex];

	UE_LOG(LogTemp, Log, TEXT("[MyOSS] 正在尝试加入 Session ID: %s"), *UE::Online::ToLogString(Params.SessionId));
	SessionsInterface->JoinSession(MoveTemp(Params))
		.OnComplete(this, &UMyOnlineSessionSubsystem::OnJoinSessionComplete);
}

void UMyOnlineSessionSubsystem::OnJoinSessionComplete(const UE::Online::TOnlineResult<UE::Online::FJoinSession>& Result)
{
	if (Result.IsOk())
	{
		UE_LOG(LogTemp, Log, TEXT("[MyOSS] 成功加入局域网会话。正在解析连接 URL 并执行 Travel..."));

		// 通过 OnlineServices 接口解析加入后会话的连接字符串，以便客户端能通过 IP 连入主机
		UE::Online::FGetResolvedConnectString::Params ConnectParams;
		ConnectParams.LocalAccountId = LocalAccountId;
		// 局域网 Null 下的 Session 统一通过本地已注册的会话名字查找
		
		// 我们先在本地通过刚刚加入的会话查找对应的 Session 实例
		UE::Online::FGetSessionByName::Params GetByNameParams;
		GetByNameParams.LocalName = FName(TEXT("LAN_Joined_Session"));
		UE::Online::TOnlineResult<UE::Online::FGetSessionByName> GetByNameResult = SessionsInterface->GetSessionByName(MoveTemp(GetByNameParams));

		if (GetByNameResult.IsOk())
		{
			ConnectParams.SessionId = GetByNameResult.GetOkValue().Session->GetSessionId();
			
			// 同步解析连接字
			UE::Online::TOnlineResult<UE::Online::FGetResolvedConnectString> ResolveResult = OnlineServices->GetResolvedConnectString(MoveTemp(ConnectParams));
			if (ResolveResult.IsOk())
			{
				FString ConnectURL = ResolveResult.GetOkValue().ResolvedConnectString;
				UE_LOG(LogTemp, Log, TEXT("[MyOSS] 解析成功。局域网连接地址：%s"), *ConnectURL);

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
				UE_LOG(LogTemp, Error, TEXT("[MyOSS] 无法解析局域网连接地址。"));
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
		OnJoinSessionCompleteDelegate.Broadcast(false);
	}
}

TArray<FString> UMyOnlineSessionSubsystem::GetSearchResultsServerNames() const
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
