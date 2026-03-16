#include "Zone.h"
#include "Job.h"
#include "ZoneServer.h"
#include "ZoneManager.h"
#include "logclassV1.h"

#include "EventObject.hpp"

#include "ProfilerV2.hpp"

#include <timeapi.h>
#pragma comment(lib, "winmm")
using Log = Core::c_syslog;

void Net::CZoneTimerJob::Set(CZoneServer* pZoneServer, CZone* pZone, uint64 zoneId)
{
	_pZoneServer = pZoneServer;
	_pZone = pZone;
	_zoneId = zoneId;
}

void Net::CZoneTimerJob::Excute()
{
	if (_pZone->isReleasing() == true)
	{
		_pZoneServer->GetZoneManager().DecreaseRefcount(_pZone);
		return;
	}

	if (_pZone->_zoneId != _zoneId)
	{
		_pZoneServer->GetZoneManager().DecreaseRefcount(_pZone);
		return;
	}

	if (_pZone->_pinnedThreadIndex == -1)
		_pZoneServer->ZonePQCS(_zoneId);
	else
		_pZoneServer->GetZoneManager().UpdateZone(_pZone->_pinnedThreadIndex, _zoneId);
	_pZoneServer->GetZoneManager().DecreaseRefcount(_pZone);
}


////////////////////////////////////////////////////////////////
// CZone
////////////////////////////////////////////////////////////////

// PUBLIC ------------------------//

void Net::CZone::EnterFunc(uint64 sessionId, void* playerPointer, std::wstring* ip)
{
	// 일단 넣고 확인
	std::pair<std::unordered_set<uint64>::iterator, bool> ret = _sessions->insert(sessionId);
	if (ret.second == false)
	{
		Log::logging().Log(TAG_ZONE, Log::en_ERROR, L"Enter이 두번왔음. 왜? (이미 소속임)");
		return;
	}

	OnEnter(sessionId, playerPointer, ip);
}

void Net::CZone::LeaveFunc(uint64 sessionId, bool bNeedPlayerDelete)
{
	size_t ret = _sessions->erase(sessionId);

	OnLeave(sessionId, bNeedPlayerDelete);
}

bool Net::CZone::Disconnect(uint64 sessionId)
{
	return _myServer->Disconnect(sessionId);
}

bool Net::CZone::DisconnectZoneZero(uint64 sessionId)
{

	return _myServer->DisconnectZoneZero(sessionId);
}

bool Net::CZone::SendPacket(uint64 sessionId, Net::CPacket* packet)
{
	return _myServer->SendPacket(sessionId, packet);
}

bool Net::CZone::SendPacketFast(uint64 sessionId, Net::CPacket* packet)
{
	return _myServer->SendPacket_Fast(sessionId, packet);
}

Net::CZoneManager* Net::CZone::GetZoneManager() const
{
	return &_myServer->_zoneManager;
}

// PRIVATE -----------------------//

void Net::CZone::Init(CZoneServer* pZoneServer, int32 maxUser)
{
	// 여기부터. std객체 생성자 호출필요

	_myServer = pZoneServer;

	_refCount = 0;
	_contentsId = 0;
	_zoneId = 0;
	_maxUsers = 0;
	_jobQueue = nullptr;
	_jobQueue = new Core::CLockFreeQueue<Core::IJob*>(10000);
	_timerRequest = nullptr;
	_timerRequest = new std::shared_ptr<CZoneTimerJob>();
	*_timerRequest = std::make_shared<CZoneTimerJob>();
	_sessions = nullptr;
	_sessions = new std::unordered_set<uint64>();
	_sessions->reserve(maxUser);

	_pinnedThreadIndex = -1;

}

void Net::CZone::Acquire(int32 contentsId, uint64 zoneId,int32 minimumTick, int32 maxUsers)
{
	_startTime = timeGetTime();
	_updateDeltaTick = 0;
	_befUpdate = _startTime;
	_minimumTick = minimumTick;

	_contentsId = contentsId;
	_zoneId = zoneId;
	_maxUsers = maxUsers;

	(*_timerRequest)->Set(_myServer, this, zoneId);
	(*_timerRequest)->ActivateJob();
	_pinnedThreadIndex = -1;
}

void Net::CZone::Release()
{
	_contentsId = 0;
	_zoneId = 0;
	Core::IJob* pJob;
	while (_jobQueue->isEmpty() == false)
	{
		_jobQueue->Dequeue(pJob);
		delete pJob;
	}
	(*_timerRequest)->CancelJob();

	for (uint64 sessionId : *_sessions)
		DisconnectZoneZero(sessionId);

	_sessions->clear();
}

unsigned long Net::CZone::IncreaseRefcount()
{
	return _InterlockedIncrement(&_refCount);
}

unsigned long Net::CZone::DecreaseRefcount()
{
	return _InterlockedDecrement(&_refCount);
}

bool Net::CZone::isReleasing()
{
	unsigned long ret = IncreaseRefcount();
	if ((ret & ZONE_RELEASE_FLAG) == ZONE_RELEASE_FLAG)
	{
		return true;
	}
	return false;
}

void Net::CZone::Clear()
{
	if (_jobQueue != nullptr)
		delete _jobQueue;
	if (_timerRequest != nullptr)
		delete _timerRequest;
	if (_sessions != nullptr)
		delete _sessions;
}

void Net::CZone::TickUpdate()
{
	// Queue 비우기 (Enter / Leave)
	int size = _jobQueue->GetSize();
	Core::IJob* pJob;
	for (int i = 0; i < size; i++)
	{
		_jobQueue->Dequeue(pJob);
		pJob->Excute();
		delete pJob;
	}

	char buffer[Net::NET_RECVQ_SIZE];
	// Recv
	auto it_end = _sessions->end();
	for (auto it = _sessions->begin(); it != it_end; ++it)
	{
		uint64 sessionId = *it;
		CReferenceZoneSession refSession(sessionId, _myServer);
		if (refSession.isAlive())
		{
			Net::stZoneSession* pSession = refSession.GetZoneSession();
			Core::RingBuffer& recvQ = pSession->recvedPackets;
			while (recvQ.GetUseSize() >= sizeof(uint16_t))
			{
				uint16_t payloadlen;
				recvQ.Peek((char*) &payloadlen, sizeof(uint16_t));
				if (recvQ.GetUseSize() >= (int)payloadlen + (int)sizeof(uint16_t))
				{
					recvQ.MoveRead(sizeof(uint16_t));
					int ret = recvQ.Dequeue(buffer, (int)payloadlen);
					OnMessage(sessionId, buffer, (int)payloadlen);
				}
				else
					break;
			}
		}
	}

	// Update
	DWORD curUpdate = timeGetTime();
	_updateDeltaTick = (int32)(curUpdate - _befUpdate);
	_befUpdate = curUpdate;
	OnUpdate();

	DWORD endTime = timeGetTime();
	int32 deltaTime = _minimumTick - (int32)(endTime - _startTime);

	if (deltaTime >= 0)
	{
		_startTime += (DWORD)_minimumTick;
		GetZoneManager()->GetZoneTimer().RequestTimerJob(*_timerRequest, deltaTime, endTime);
	}
	else
	{
		_startTime = endTime;

		if (_pinnedThreadIndex == -1)
			_myServer->ZonePQCS(_zoneId);
		else
			GetZoneManager()->UpdateZone(_pinnedThreadIndex, _zoneId);
	}
	
}

void Net::CZone::SetPinned(int index, int weight)
{
	_pinnedThreadIndex = index;
	_weight = weight;
}
