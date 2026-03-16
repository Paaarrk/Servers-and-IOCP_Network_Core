#include "EchoServer.h"
#include "ZoneType.h"
#include "Zone_Lobby.h"
#include "Zone_Echo.h"
#include "Contents.h"

#include "21_TextParser.h"
#include <stdexcept>

#include "logclassV1.h"
using Log = Core::c_syslog;


#pragma region CChatServer::stChatServerOpt

bool CEchoServer::stEchoServerOpt::LoadOption(const char* path)
{
	CParser config(path);
	try
	{
		config.LoadFile();

		int bUseEncode;
		config.GetValue("GAME_SERVER", "bUseEncode", &bUseEncode);
		this->bUseEncode = (bool)bUseEncode;

		int bUseSO_SNDBUF;
		config.GetValue("GAME_SERVER", "bUseSO_SNDBUF", &bUseSO_SNDBUF);
		this->bUseSO_SNDBUF = (bool)bUseSO_SNDBUF;

		int bUseTCP_NODELAY;
		config.GetValue("GAME_SERVER", "bUseTCP_NODELAY", &bUseTCP_NODELAY);
		this->bUseTCP_NODELAY = (bool)bUseTCP_NODELAY;

		int iMaxConcurrentUsers;
		config.GetValue("GAME_SERVER", "iMaxConcurrentUsers", &iMaxConcurrentUsers);
		this->iMaxConcurrentUsers = iMaxConcurrentUsers;

		int iWorkerThreadCreateCnt;
		config.GetValue("GAME_SERVER", "iWorkerThreadCreateCnt", &iWorkerThreadCreateCnt);
		this->iWorkerThreadCreateCnt = iWorkerThreadCreateCnt;

		int iWorkerThreadRunCnt;
		config.GetValue("GAME_SERVER", "iWorkerThreadRunCnt", &iWorkerThreadRunCnt);
		this->iWorkerThreadRunCnt = iWorkerThreadRunCnt;

		char openIP[16];
		config.GetValue("GAME_SERVER", "openIP", openIP);
		MultiByteToWideChar(CP_ACP, 0, openIP, 16, this->openIP, 16);

		int port;
		config.GetValue("GAME_SERVER", "port", &port);
		this->port = (unsigned short)port;

		int serverCode;
		config.GetValue("GAME_SERVER", "serverCode", &serverCode);
		server_code = (uint8)serverCode;

		int staticKey;
		config.GetValue("GAME_SERVER", "staticKey", &staticKey);
		this->static_key = (uint8)staticKey;

		// Game

		int lobbyMaxUsers;
		config.GetValue("GAME_SERVER", "lobbyMaxUsers", &lobbyMaxUsers);
		this->lobbyMaxUsers = lobbyMaxUsers;

		int gameMaxUsers;
		config.GetValue("GAME_SERVER", "gameMaxUsers", &gameMaxUsers);
		this->gameMaxUsers = gameMaxUsers;

		int lobbyMinimumTick;
		config.GetValue("GAME_SERVER", "lobbyMinimumTick", &lobbyMinimumTick);
		this->lobbyMinimumTick = lobbyMinimumTick;

		int gameMinimumTick;
		config.GetValue("GAME_SERVER", "gameMinimumTick", &gameMinimumTick);
		this->gameMinimumTick = gameMinimumTick;

		int maxZoneCnt;
		config.GetValue("GAME_SERVER", "maxZoneCnt", &maxZoneCnt);
		this->maxZoneCnt = maxZoneCnt;
	}
	catch (std::invalid_argument& e)
	{
		wchar_t buffer[256];
		MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, 256);
		Log::logging().Log(TAG_CONTENTS, Core::c_syslog::en_ERROR,
			L"%s", buffer);
		return false;
	}
	return true;
}

#pragma endregion


bool CEchoServer::OnInit(const stServerOpt* pOpt)
{
	// opt에 의한 init
	stEchoServerOpt* pOption = (stEchoServerOpt*)pOpt;

	int32 minimumTick_lobby = pOption->lobbyMinimumTick;
	int32 maxUsers_lobby = pOption->lobbyMaxUsers;
	int32 minimumTick_echo = pOption->gameMinimumTick;
	int32 maxUsers_echo = pOption->gameMaxUsers;
	int32 maxZoneCnt = pOption->maxZoneCnt;

	// zone init
	// register
	GetZoneManager().RegisterZoneType(Net::MakeZoneType<CLobby>(en_CONTENTS::CONTENTS_ID_LOBBY, minimumTick_lobby, maxUsers_lobby, false, 100));
	GetZoneManager().RegisterZoneType(Net::MakeZoneType<CEcho>(en_CONTENTS::CONTENTS_ID_ECHO, minimumTick_echo, maxUsers_echo, false, 100));
	// init
	if (GetZoneManager().Init(maxZoneCnt, 0) == false)
		return false;

	_lobbyId = GetZoneManager().CreateZone(en_CONTENTS::CONTENTS_ID_LOBBY);
	if (_lobbyId == 0)
		return false;
	_echoId = GetZoneManager().CreateZone(en_CONTENTS::CONTENTS_ID_ECHO);
	if (_echoId == 0)
		return false;
	((CEcho*)GetZoneManager().GetZonePtr(_echoId))
		->SetLobby(_lobbyId);

	_pLobby = (CLobby*)GetZoneManager().GetZonePtr(_lobbyId);
	_pEcho = (CEcho*)GetZoneManager().GetZonePtr(_echoId);
	
	return true;
}
bool CEchoServer::OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip)
{
	return GetZoneManager().EnterZone(_lobbyId, sessionId, wip);
}
bool CEchoServer::OnConnectionRequest(in_addr ip)
{
	return true;
}

void CEchoServer::OnRelease(uint64_t sessionId)
{
	// 지금은 할거 없음
}
void CEchoServer::OnMessage(uint64_t sessionId, Net::CPacket* pPacket, int len)
{
	Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
		L"오면 안되는데 ...");
	// 지금은 할거 없음
}

void CEchoServer::OnWorkerStart()
{
	// 할거 없음
}
void CEchoServer::OnWorkerEnd()
{
	// 할거 없음
}
void CEchoServer::OnUserEvent(Net::CPacket* pPacket)
{
	// 할거 없음
}

void CEchoServer::OnStop()
{

}

void CEchoServer::OnExit()
{
	if (_pLobby != nullptr)
	{
		_pLobby = nullptr;
		GetZoneManager().DestroyZone(_lobbyId);
	}
	if (_pEcho != nullptr)
	{
		_pEcho = nullptr;
		GetZoneManager().DestroyZone(_echoId);
	}
}

void CEchoServer::ToggleUseTimeout()
{
	if (_pLobby != nullptr && _pEcho != nullptr)
	{
		_pLobby->ToggleTimeout();
		_pEcho->ToggleTimeout();
	}
}

int32 CEchoServer::GetAccountCnt() const
{
	if (_pLobby != nullptr)
		return _pLobby->GetLoginedAccountNum();
	return 0;
}

int64 CEchoServer::GetDuplicateLoginCnt() const
{
	if (_pLobby != nullptr)
		return _pLobby->GetTotalDuplicatedLoginCount();
	return 0;
}

bool CEchoServer::GetLobbyTImeout() const
{
	if (_pLobby != nullptr)
		return _pLobby->isUseTimeout();
	return false;
}

bool CEchoServer::GetEchoTimeout() const
{
	if (_pEcho != nullptr)
		return _pEcho->isUseTimeout();
	return false;
}

int32 CEchoServer::GetLobbyPlayerCnt() const
{
	if(_pLobby != nullptr)
		return _pLobby->GetUserCnt();
	return 0;
}

int32 CEchoServer::GetLobbySessionCnt() const
{
	if (_pLobby != nullptr)
		return _pLobby->GetSessionCnt();
	return 0;
}

int32 CEchoServer::GetEchoPlayerCnt() const
{
	if(_pEcho != nullptr)
		return _pEcho->GetUserCnt();
	return 0;
}

int32 CEchoServer::GetEchoSessionCnt() const
{
	if (_pEcho != nullptr)
		return _pEcho->GetSessionCnt();
	return 0;
}

int32 CEchoServer::GetLobbyFps() const
{
	if (_pLobby != nullptr)
		return _pLobby->GetFps();
	return 0;
}

int32 CEchoServer::GetEchoFps() const
{
	if (_pEcho != nullptr)
		return _pEcho->GetFps();
	return 0;
}

int32 CEchoServer::GetLobbyDeltaTick() const
{
	if (_pLobby != nullptr)
		return _pLobby->GetDeltaTime();
	return 0;
}
int32 CEchoServer::GetEchoDeltaTick() const
{
	if (_pEcho != nullptr)
		return _pEcho->GetDeltaTime();
	return 0;
}