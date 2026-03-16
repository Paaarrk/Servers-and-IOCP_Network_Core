#include "Zone_Echo.h"
#include "EchoServer.h"
#include "NetProcess.h"

#include "ProfilerV2.hpp"

#include "ZoneManager.h"
#include "Contents.h"
#include "User.h"
#include "Zone_Lobby.h"

#include "RingBufferV4.h"


#include <string>
#include <timeapi.h>
#pragma comment(lib, "winmm")

#include "logclassV1.h"
using Log = Core::c_syslog;

#include "CommonProtocol.h"

CEchoTimerJob::CEchoTimerJob():_startTime(0), _excuteCount(0), _echoFps(0),
_recvEchoCount(0), _recvEchoTps(0), _recvHeartbeatCount(0), _recvHeartbeatTps(0)
{
	_startTime = timeGetTime();
}

void CEchoTimerJob::Excute()
{
	_echoFps = _InterlockedExchange(&_excuteCount, 0);
	_recvEchoTps = _InterlockedExchange(&_recvEchoCount, 0);
	_recvHeartbeatTps = _InterlockedExchange(&_recvHeartbeatCount, 0);

	DWORD curTime = timeGetTime();
	int32 deltaTime = MONITORING_TICK - (int32)(curTime - _startTime);
	//Core::c_syslog::logging().Log(L"Debug EchoTimer", Core::c_syslog::en_SYSTEM, L"[LobbyTimer excute: %d | deltaTime: %d]", curTime % 10000, deltaTime);
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

void CEchoTimerJob::IncreaseExcuteCount()
{
	_InterlockedIncrement(&_excuteCount);
}
void CEchoTimerJob::IncreaseRecvEchoCount()
{
	_InterlockedIncrement(&_recvEchoCount);
}
void CEchoTimerJob::IncreaseRecvHeartbeatCount()
{
	_InterlockedIncrement(&_recvHeartbeatCount);
}




CEcho::CEcho():_bUseTimeout(0), _lobbyId(0)
{
	_userMap.reserve(GetMaxUsers());	
	_monitorJob = std::make_shared<CEchoTimerJob>();
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_monitorJob, 0);
}

CEcho::~CEcho()
{
	for (std::pair<const uint64, CUser*>& userNode : _userMap)
	{
		DisconnectZoneZero(userNode.first);
		CUser::Free(userNode.second);
	}
	_userMap.clear();

	_monitorJob->CancelJob();
}

void CEcho::ToggleTimeout()
{
	if (_bUseTimeout.load())
		_bUseTimeout.exchange(false);
	else
		_bUseTimeout.exchange(true);
}

CUser* CEcho::FindUser(uint64 sessionId)
{
	auto it = _userMap.find(sessionId);
	if (it == _userMap.end())
		return nullptr;
	return it->second;
}

void CEcho::SetDead(uint64 sessionId)
{
	CUser* pUser = FindUser(sessionId);
	if (pUser != nullptr)
		pUser->SetDead();
}

bool CEcho::ReleaseAccountNo(int64 accountNo, uint64 sessionId)
{
	// lobby로 쏘기
	return GetZoneManager()->PushJob(_lobbyId, [z = GetZoneManager()->GetZonePtr(_lobbyId), accountNo, sessionId]() {
		((CLobby*)z)->ReleaseAccountNo(accountNo, sessionId);
	});
}

void CEcho::OnUpdate()
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
			if (deltaTime > TIME_OUT_MS_ECHO)
			{
				if(node.second->IsAlive())
				{
					Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"[AccountNo: %lld] Time - out", node.second->GetAccountNo());
					SetDead(node.first);
					Disconnect(node.first);
				}
			}
		}
	}
}

void CEcho::OnEnter(uint64 sessionId, void* playerPtr, std::wstring* ip)
{
	CUser* pUser = (CUser*)playerPtr;
	if(pUser == nullptr)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR, L"echo enter: player null ... ");
		__debugbreak();
	}

	pUser->MessageRecved(timeGetTime());

	std::pair<std::unordered_map<uint64, CUser*>::iterator, bool> ret = _userMap.insert({ sessionId, pUser });
	if (ret.second == false)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR, L"이미 에코에 있는 캐릭터");
		if (playerPtr != nullptr)
		{
			CUser::Free(pUser);
		}
		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}

	// DB 가 있었다면 .... 저장을 완료하고 진입필요함
	// 다시 Enter메시지를 큐에 넣으면 어떨까?

	CPACKET_CREATE(resLoginPacket);
	*resLoginPacket << (uint16)en_PACKET_CS_GAME_RES_LOGIN
		<< (uint8)1
		<< (int64)pUser->GetAccountNo();
	SendPacket(sessionId, resLoginPacket.GetCPacketPtr());
}

void CEcho::OnLeave(uint64 sessionId, bool bNeedPlayerDelete)
{
	if (bNeedPlayerDelete)
	{
		auto it = _userMap.find(sessionId);
		if (it != _userMap.end())
		{
			CUser* pUser = it->second;
			int64 accNo = pUser->GetAccountNo();
			CUser::Free(pUser);
			bool ret = ReleaseAccountNo(accNo, sessionId);
			if (ret == false)
			{
				Log::logging().Log(TAG_CONTENTS, Log::en_ERROR, L"로비가 사라졌어요");
			}
		}
	}

	_userMap.erase(sessionId);
}

void CEcho::OnMessage(uint64 sessionId, const char* readPtr, int payloadlen)
{
	if (payloadlen < 2)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] Zone Echo 타입이 없는 메시지 (payloadlen: %d)", sessionId, payloadlen);

		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}

	uint16 type = *((uint16*)readPtr);
	readPtr += 2;
	payloadlen -= sizeof(uint16);
	
	switch (type)
	{

	case en_PACKET_TYPE::en_PACKET_CS_GAME_REQ_ECHO:
	{
		RequestEcho(sessionId, readPtr, payloadlen);
		break;
	}
	
	case en_PACKET_TYPE::en_PACKET_CS_GAME_REQ_HEARTBEAT:
	{
		RequestHeartbeat(sessionId, readPtr, payloadlen);
		break;
	}

	default:
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] Zone Echo: type이상한 메시지(%d)", sessionId, type);

		SetDead(sessionId);
		Disconnect(sessionId);
		break;
	}

	}
}


void CEcho::RequestEcho(uint64 sessionId, const char* readPtr, int payloadlen)
{
	_monitorJob->IncreaseRecvEchoCount();

	CUser* pUser = FindUser(sessionId);
	if (pUser == nullptr)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] 등록되지 않은 세션", sessionId);
		Disconnect(sessionId);
		return;
	}
	
	if (pUser->IsAlive() == false)
	{
		return;
	}

	if (payloadlen != 16)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx | accountNo: %lld] RequestEcho: payloadlen 이상함 (%d != 16)",
			sessionId, pUser->GetAccountNo(), payloadlen);

		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}

	int64 accountNo;
	int64 sendTick;

	accountNo = *((int64*)readPtr);
	readPtr += sizeof(int64);
	sendTick = *((int64*)readPtr);
	readPtr += sizeof(int64);

	if (pUser->GetAccountNo() != accountNo)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx | accountNo: %lld] 기존 플레이어 accountNo랑 다름(%lld)",
			sessionId, pUser->GetAccountNo(), accountNo);
		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}

	
	pUser->MessageRecved(GetTickStartTime());
	

	CPACKET_CREATE(resEcho);
	*resEcho << (uint16)en_PACKET_CS_GAME_RES_ECHO
		<< (int64)accountNo
		<< (int64)sendTick;
	SendPacketFast(sessionId, resEcho.GetCPacketPtr());
}

void CEcho::RequestHeartbeat(uint64 sessionId, const char* readPtr, int payloadlen)
{
	_monitorJob->IncreaseRecvHeartbeatCount();

	CUser* pUser = FindUser(sessionId);
	if (pUser == nullptr)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] 등록되지 않은 세션", sessionId);
		Disconnect(sessionId);
		return;
	}
	if (payloadlen != 0)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx | accountNo: %lld] Heartbeat 메시지에 잔여가남음(%d)",
			sessionId, pUser->GetAccountNo(), payloadlen);
		SetDead(sessionId);
		Disconnect(sessionId);
		return;
	}
	pUser->MessageRecved(GetTickStartTime());
}