#include "Zone_Lobby.h"
#include "EchoServer.h"
#include "NetProcess.h"

#include "ZoneManager.h"
#include "Contents.h"
#include "User.h"


#include <string>
#include <timeapi.h>
#pragma comment(lib, "winmm")

#include "logclassV1.h"
using Log = Core::c_syslog;

#include "CommonProtocol.h"

CLobbyTimerJob::CLobbyTimerJob() : _startTime(0), _excuteCount(0), _lobbyFps(0),
_recvLoginCount(0), _recvLoginTps(0), _totalDuplicateLoginCount(0)
{
	_startTime = timeGetTime();
}

void CLobbyTimerJob::Excute()
{
	_lobbyFps = _InterlockedExchange(&_excuteCount, 0);
	_recvLoginTps = _InterlockedExchange(&_recvLoginCount, 0);

	DWORD curTime = timeGetTime();
	int32 deltaTime = MONITORING_TICK - (int32)(curTime - _startTime);
	//Core::c_syslog::logging().Log(L"Debug LobbyTimer", Core::c_syslog::en_SYSTEM, L"[LobbyTimer excute: %d | deltaTime: %d]", curTime % 10000, deltaTime);
	if (deltaTime < 0)
	{
		_startTime = curTime + MONITORING_TICK;
		Core::c_syslog::logging().Log(TAG_TIMER, Core::c_syslog::en_ERROR, L"CZoneServer Monitoring TickUpdate(): [delta ms: %d]  문제있음 (빡빡)", -deltaTime);
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, MONITORING_TICK, curTime);
	}
	else
	{
		_startTime += MONITORING_TICK;
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, deltaTime, curTime);
	}
}

void CLobbyTimerJob::IncreaseExcuteCount()
{
	_InterlockedIncrement(&_excuteCount);
}
void CLobbyTimerJob::IncreaseRecvLoginCount()
{
	_InterlockedIncrement(&_recvLoginCount);
}
void CLobbyTimerJob::IncreaseDuplicateLoginCount()
{
	_InterlockedIncrement(&_totalDuplicateLoginCount);
}




CLobby::CLobby():_bUseTimeout(0)
{
	_userMap.reserve(GetMaxUsers());
	_accountMap.reserve(GetMaxUsers());

	_monitorJob = std::make_shared<CLobbyTimerJob>();
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_monitorJob, 0);
}

CLobby::~CLobby()
{
	for (std::pair<const uint64, CUser*>& userNode : _userMap)
	{
		DisconnectZoneZero(userNode.first);
		CUser::Free(userNode.second);
	}
	_userMap.clear();
	_accountMap.clear();

	_monitorJob->CancelJob();
}

void CLobby::ToggleTimeout()
{
	if (_bUseTimeout.load())
		_bUseTimeout.exchange(false);
	else
		_bUseTimeout.exchange(true);
}

void CLobby::DeleteLogin(uint64 sessionId)
{
	size_t num = _accountMap.erase(sessionId);
	if (num == 0)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"이미 없는 세션 아이디");
	}
}

CUser* CLobby::FindUser(uint64 sessionId)
{
	auto it = _userMap.find(sessionId);
	if (it == _userMap.end())
		return nullptr;
	return it->second;
}

void CLobby::SetDead(uint64 sessionId)
{
	CUser* pUser = FindUser(sessionId);
	if (pUser != nullptr)
		pUser->SetDead();
}

void CLobby::ReleaseAccountNo(int64 accountNo, uint64 sessionId)
{
	auto it = _accountMap.find(accountNo);
	if (it != _accountMap.end() && it->second == sessionId)
	{
		_accountMap.erase(it);
	}
}

void CLobby::OnUpdate()
{
	_monitorJob->IncreaseExcuteCount();

	if (_bUseTimeout.load())
	{
		DWORD curTime = timeGetTime();
		int32 deltaTime;
		std::unordered_map<uint64, CUser*>::iterator it_end = _userMap.end();
		for (std::pair<const uint64, CUser*>& node : _userMap)
		{
			deltaTime = (int32)(curTime - node.second->GetLastRecvTime());
			if (deltaTime > TIME_OUT_MS_LOBBY)
			{
				Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"[AccountNo: %lld] Not logined Time - out", node.second->GetAccountNo());
				SetDead(node.first);
				Disconnect(node.first);
			}
		}
	}
}

void CLobby::OnEnter(uint64 sessionId, void* playerPtr, std::wstring* ip)
{
	CUser* pUser = (CUser*)playerPtr;
	if(pUser == nullptr)
	{
		pUser = CUser::Alloc();
		bool ret = GetZoneServer()->SetUserPointer(sessionId, pUser);
		if (ret == false)
		{
			Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"이미 나간 유저");
			SetDead(sessionId);
			Disconnect(sessionId);
			return; //leave가 나중에 pUser을 해제 해준다.
		}
	}
	else
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"세션 초기화의 문제");
	}
	if (ip != nullptr)
	{
		pUser->SetIp(*ip);
	}

	std::pair<std::unordered_map<uint64, CUser*>::iterator, bool> ret = _userMap.insert({ sessionId, pUser });
	if (ret.second == false)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR, L"이미 로비에 있는 캐릭터");
		if (playerPtr != nullptr)
		{
			CUser::Free(pUser);
		}
		SetDead(sessionId);
		Disconnect(sessionId);
	}
}

void CLobby::OnLeave(uint64 sessionId, bool bNeedPlayerDelete)
{
	if (bNeedPlayerDelete)
	{
		auto it = _userMap.find(sessionId);
		if (it != _userMap.end())
		{
			CUser* pUser = it->second;
			int64 accNo = pUser->GetAccountNo();
			CUser::Free(pUser);
			ReleaseAccountNo(accNo, sessionId);
		}
	}

	_userMap.erase(sessionId);
}

void CLobby::OnMessage(uint64 sessionId, const char* readPtr, int payloadlen)
{
	if (payloadlen < 2)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] Zone Lobby 타입이 없는 메시지 (payloadlen: %d)", sessionId, payloadlen);

		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}

	uint16 type = *((uint16*)readPtr);
	readPtr += 2;
	payloadlen -= sizeof(uint16);

	switch (type)
	{
	case en_PACKET_TYPE::en_PACKET_CS_GAME_REQ_LOGIN:
	{
		RequestLogin(sessionId, readPtr, payloadlen);
		break;
	}

	default:
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] Zone Lobby: type이상한 메시지(%d)", sessionId, type);

		SetDead(sessionId);
		Disconnect(sessionId);
		break;
	}

	}
}

void CLobby::RequestLogin(uint64 sessionId, const char* readPtr, int payloadlen)
{
	_monitorJob->IncreaseRecvLoginCount();

	CUser* pUser = FindUser(sessionId);
	if (pUser == nullptr)
	{
		Disconnect(sessionId);
		return;
	}

	if (payloadlen != 76)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx | accountNo: %lld] RequstLogin: payloadlen 이상함 (%d != 76)",
			sessionId, pUser->GetAccountNo(), payloadlen);

		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}

	int64 accountNo;
	int32 version;
	accountNo = *((int64*)readPtr);
	readPtr += sizeof(int64);
	pUser->SetSessionKey(readPtr);
	readPtr += 64;
	version = *((int32*)readPtr);
	readPtr += sizeof(int32);

	// 더미 테스트용 코드
	if (10000 <= accountNo && accountNo < 20000)
	{
		if (pUser->IsIpSame(L"10.0.1.2") == false)
		{
			Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM,
				L"더미가 사용할 계정, 외부접속 불가");
			SetDead(sessionId);
			Disconnect(sessionId);
			return;
		}
	}
	else if (20000 <= accountNo && accountNo < 30000)
	{
		if (pUser->IsIpSame(L"10.0.2.2") == false)
		{
			Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM,
				L"더미가 사용할 계정, 외부접속 불가");
			SetDead(sessionId);
			Disconnect(sessionId);
			return;
		}
	}
	
	// 로그인 처리
	std::pair<std::unordered_map<int64, uint64>::iterator, bool> ret =
		_accountMap.insert({ accountNo, sessionId });
	if (ret.second == false)
	{
		_monitorJob->IncreaseDuplicateLoginCount();
		uint64 loginedSessionId = ret.first->second;
		ret.first->second = sessionId;

		Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"[sessionId: %016llx, accountNo: %lld] 중복로그인으로 퇴출 !!", loginedSessionId, accountNo);
		SetDead(loginedSessionId);
		Disconnect(loginedSessionId);
	}

	pUser->SetAccountNo(accountNo);
	pUser->MessageRecved(GetTickStartTime());
	GetZoneManager()->MoveZone(((CEchoServer*)GetZoneServer())->GetEchoId(), sessionId);
	// Log::logging().Log(L"Deb", Log::en_SYSTEM, L"MOVE !!");
}