#include "ZoneManager.h"
#include "ZoneType.h"
#include "ZoneServer.h"
#include "LockFreeStack.hpp"
#include <malloc.h>
#include <string>

#include "Job.h"
#include "logclassV1.h"

using Log = Core::c_syslog;

/////////////////////////////////////////////////////////////
// PinnedZoneThreads
/////////////////////////////////////////////////////////////

Net::CPinnedZoneThreads::CPinnedZoneThreads(CZoneManager* pManager)
: _weight(0), _wakeEvent(NULL), _zoneQueue(), _myManager(pManager)
{
	_wakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	_thread = std::thread(&Net::CPinnedZoneThreads::PinnedThreadFunc, this);
}

Net::CPinnedZoneThreads::~CPinnedZoneThreads()
{
	_zoneQueue.Enqueue(EXIT);
	SetEvent(_wakeEvent);
	if (_thread.joinable())
		_thread.join();
	_weight = 0;
	CloseHandle(_wakeEvent);
}

Net::CPinnedZoneThreads::CPinnedZoneThreads(CPinnedZoneThreads&& move) noexcept
: _zoneQueue() //이동 할 게 없음
{
	_weight = move._weight;
	move._weight = 0;
	_wakeEvent = move._wakeEvent;
	move._wakeEvent = NULL;
	_myManager = move._myManager;
	_thread = std::move(move._thread);
}

void Net::CPinnedZoneThreads::PinnedThreadFunc()
{
	Log::logging().Log(TAG_ZONE, Log::en_SYSTEM, L"Pinned Thread Start!!");

	for(;;)
	{
		WaitForSingleObject(_wakeEvent, INFINITE);
		uint64 zoneId;
		bool deqret = _zoneQueue.Dequeue(zoneId);
		if(deqret)
		{
			if (zoneId == EXIT)
				break;

			CReferenceZone refZone(zoneId, _myManager);
			if (refZone.isAlive())
			{
				refZone.GetZone()->TickUpdate();
			}
		}
	}

	_zoneQueue.Clear();
	Log::logging().Log(TAG_ZONE, Log::en_SYSTEM, L"Pinned Thread End!!");
}


/////////////////////////////////////////////////////////////
// Zone Manager
/////////////////////////////////////////////////////////////

Net::CZoneManager::CZoneManager(CZoneServer* pZoneServer) :
	_zones(nullptr), _indexes(nullptr), _isCallInit(0), _zonetypes(),
	_maxZoneCnt(0), _size_max(0), _align_max(0), _zoneCnt(0), _zoneId(0),
	_pZoneServer(pZoneServer), _maxPinnedZoneThreadsCnt(0)
{

}

Net::CZoneManager::~CZoneManager()
{
	Clear();
}


void Net::CZoneManager::RegisterZoneType(stZoneType* pType)
{
	if(_isCallInit == false)
		_zonetypes.push_back(pType);
}

bool Net::CZoneManager::Init(int maxZoneCnt, int maxPinnedZoneThreadsCnt)
{
	if (_isCallInit == true)
		return true;

	if (_zonetypes.size() == 0)
	{
		// 존 사용 안한다는 뜻
		_isCallInit = true;
		return true;
	}

	_maxZoneCnt = maxZoneCnt;

	size_t size_max = 0;
	size_t align_max = 8;
	int32 userMax = 0;
	do
	{
		if(_zonetypes.size() != 0)
		{
			for (stZoneType* pType : _zonetypes)
			{
				if (pType->zoneSize > size_max)
					size_max = (size_t)pType->zoneSize;
				if (pType->zoneAlign > align_max)
					align_max = (size_t)pType->zoneAlign;
				if (pType->maxUsers > userMax)
					userMax = pType->maxUsers;
			}
		}

		// 배열 초기화
		_size_max = (int)size_max;
		_align_max = (int)align_max;
		_zones = (void*)_aligned_malloc(size_max * maxZoneCnt, align_max);
		if (_zones == nullptr)
		{
			Log::logging().LogEx(TAG_ZONE, (unsigned long)errno, Log::en_ERROR, L"ZoneManager Zone배열 메모리 할당 실패");
			break;
		}

		_indexes = new Core::CLockFreeStack<int32>();
		if (_indexes == nullptr)
		{
			Log::logging().LogEx(TAG_ZONE, (unsigned long)errno, Log::en_ERROR, L"ZoneManager 락프리 스택 메모리 할당 실패");
			break;
		}
		// 인덱스 초기화
		for (int i = 0; i < _maxZoneCnt; i++)
		{
			GetZonePtr(i)->Init(_pZoneServer, userMax);
			_indexes->push(_maxZoneCnt - i - 1);
		}

		_maxPinnedZoneThreadsCnt = maxPinnedZoneThreadsCnt;
		_zoneThreads.reserve(maxPinnedZoneThreadsCnt);

		for (int i = 0; i < maxPinnedZoneThreadsCnt; i++)
			_zoneThreads.emplace_back(this);

		_isCallInit = true;
		return true;
	} while (0);

	Clear();
	return false;
}

void Net::CZoneManager::Clear()
{
	for (stZoneType* pType : _zonetypes)
	{
		delete pType;
	}
	_zonetypes.clear();

	if (_zones != nullptr)
	{
		// Zone의 소멸자 필요시 호출
		

		_aligned_free(_zones);
		_zones = nullptr;
	}

	if (_indexes != nullptr)
		_indexes->Clear();

	_zoneThreads.clear();

	_isCallInit = false;
}

void Net::CZoneManager::DestroyZone(uint64 zoneId)
{
	int32 index = GetZoneIndex(zoneId);
	CZone* pZone = GetZonePtr(index);
	if (pZone->_zoneId == zoneId)
	{
		int32 thIndex = pZone->_pinnedThreadIndex;
		if (thIndex != -1)
		{
			_pinnedLock.lock();
			_zoneThreads[thIndex].DecreaseWeight(pZone->GetWeight());
			_pinnedLock.unlock();
		}
		DecreaseRefcount(pZone);
	}
}

uint64 Net::CZoneManager::CreateZone(int32 contentsId)
{
	stZoneType* pType = FindZoneType(contentsId);
	if (pType == nullptr)
		return 0;
	int32 index;
	bool ret = _indexes->pop(index);
	if (ret == false)
		return 0;

	uint64 zoneId = _InterlockedIncrement(&_zoneId);
	if (zoneId == 0)
		zoneId = _InterlockedIncrement(&_zoneId);

	zoneId = (zoneId << ZONE_ID_SHIFT) | ((uint64)index);
	
	CZone* pNewZone = GetZonePtr(index);
	pNewZone->Acquire(contentsId, zoneId, pType->minimumTick, pType->maxUsers);
	pType->constructer(pNewZone);

	// 초기화 끝나서
	pNewZone->IncreaseRefcount();
	_InterlockedAnd((long*) &pNewZone->_refCount, (~ZONE_RELEASE_FLAG));

	if (pType->usePinned)
	{
		_pinnedLock.lock();
		int32 minWeight = 999999;
		int32 index = -1;
		for (int i = 0; i < _zoneThreads.size(); ++i)
		{
			if (_zoneThreads[i].GetWight() < minWeight)
			{
				minWeight = _zoneThreads[i].GetWight();
				index = i;
			}
		}

		_zoneThreads[index].IncreaseWeight(pType->zoneWeight);
		pNewZone->SetPinned(index, pType->zoneWeight);
		_pinnedLock.unlock();

		_zoneThreads[index].UpdateRequest(zoneId);
	}
	else
		_pZoneServer->ZonePQCS(zoneId);
	
	return zoneId;
}


bool Net::CZoneManager::EnterZone(uint64 zoneId, uint64 sessionId, const wchar_t* ipw)
{
	CReferenceZone zoneRef(zoneId, this);
	CReferenceZoneSession sessionRef(sessionId, _pZoneServer);
	// 둘다 참조 획득
	if (zoneRef.isAlive() && sessionRef.isAlive())
	{
		void* playerPointer = sessionRef.GetZoneSession()->userPtr;

		if (_InterlockedCompareExchange(&sessionRef.GetZoneSession()->zoneId, zoneId, 0) != 0)
		{
			Log::logging().Log(TAG_ZONE, Log::en_ERROR, L"EnterZone하는데 zoneId != 0");
			return false;
		}

		// // FOR ZONE: zone을 위한 참조 올리기
		// sessionRef.GetZoneSession()->IncreaseRefcount();

		if (ipw == nullptr)
		{
			zoneRef.GetZone()->PushJob([z = zoneRef.GetZone(), sessionId, playerPointer]()
				{
					z->EnterFunc(sessionId, playerPointer, nullptr);
				});
		}
		else
		{
			zoneRef.GetZone()->PushJob([z = zoneRef.GetZone(), sessionId, playerPointer, ipW = std::wstring(ipw)]()
				mutable {
					z->EnterFunc(sessionId, playerPointer, &ipW);
				});
		}

		// Enter은 먼저 들어갔고, 함수를 나가면서 SessionRef소멸자와 함께 세션이 해제되고 릴리즈타면
		// Leave는 후에 들어가니 Leve->enter순으로 꼬일일은 없다.
		return true;
	}

	return false;
}

bool Net::CZoneManager::LeaveZone(uint64 zoneId, uint64 sessionId)
{
	// 세션아이디 생존 여부 무관하게 들어가야함 (OnLeave호출때문에)
	CReferenceZone zoneRef(zoneId, this);
	if (zoneRef.isAlive())
	{
		zoneRef.GetZone()->PushJob([z = zoneRef.GetZone(), sessionId]()
			{
				z->LeaveFunc(sessionId, true);
			});
		return true;
	}

	return false;
}

bool Net::CZoneManager::MoveZone(uint64 targetZoneId, uint64 sessionId)
{
	uint64 curPlayerZone = 0;
	CReferenceZoneSession sessionRef(sessionId, _pZoneServer);
	if (sessionRef.isAlive())
	{
		curPlayerZone = sessionRef.GetZoneSession()->zoneId;
	}
	else
		return false;

	CReferenceZone curZoneRef(curPlayerZone, this);
	CReferenceZone zoneRef(targetZoneId, this);
	if (zoneRef.isAlive() && sessionRef.isAlive())
	{
		void* playerPointer = sessionRef.GetZoneSession()->userPtr;
		if (_InterlockedCompareExchange(&sessionRef.GetZoneSession()->zoneId, targetZoneId, curPlayerZone) != curPlayerZone)
		{
			Log::logging().Log(TAG_ZONE, Log::en_SYSTEM, L"[%16llx -> %16llx] MoveZone하는데 zoneId != curPlayerZone",curPlayerZone, targetZoneId);
			return false;
		}
		
		zoneRef.GetZone()->PushJob([z = zoneRef.GetZone(), sessionId, playerPointer]()
			{
				z->EnterFunc(sessionId, playerPointer);
			});

		curZoneRef.GetZone()->PushJob([z = curZoneRef.GetZone(), sessionId]()
			{
				z->LeaveFunc(sessionId, false);
			});

		return true;
	}

	return false;
}


///// PRIVATE //////

Net::CZone* Net::CZoneManager::GetZonePtr(int32 index)
{
	if (index < 0 || index >= _maxZoneCnt)
		return nullptr;
	return reinterpret_cast<CZone*>(reinterpret_cast<char*>(_zones) + index * _size_max);
}

Net::CZone* Net::CZoneManager::GetZonePtr(uint64 zoneId)
{
	int index = (zoneId & ZONE_INDEX_MASK);
	if (index < 0 || index >= _maxZoneCnt)
		return nullptr;
	return reinterpret_cast<CZone*>(reinterpret_cast<char*>(_zones) + index * _size_max);
}

long Net::CZoneManager::IncreaseRefcount(CZone* pZone)
{
	return pZone->IncreaseRefcount();
}

void Net::CZoneManager::DecreaseRefcount(CZone* pZone)
{
	long ret = pZone->DecreaseRefcount();
	if (ret == 0)
	{
		ReleaseZone(pZone);
	}
	else if (ret == 0xFFFF'FFFF)
	{
		Log::logging().Log(TAG_ZONE, Log::en_ERROR, L"Zone의 참조카운트가 음수, 확인바람");
		__debugbreak();
	}
}

void Net::CZoneManager::ReleaseZone(CZone* pZone)
{
	if (_InterlockedCompareExchange(&(pZone->_refCount), ZONE_RELEASE_FLAG, 0) != 0)
	{
		return;
	}
	
	stZoneType* pType = FindZoneType(pZone->_contentsId);
	pType->destructer(pZone);	//소멸자 호출
	pZone->Release();

	int32 index = GetZoneIndex(pZone);
	if (index == -1)
		__debugbreak();

	_indexes->push(index);
	_InterlockedDecrement(&_zoneCnt);
}

Net::stZoneType* Net::CZoneManager::FindZoneType(int32 contentsId)
{
	stZoneType* ret = nullptr;
	for (stZoneType* type : _zonetypes)
	{
		if (type->contentsId == contentsId)
		{
			ret = type;
			break;
		}
	}
	return ret;
}

int32 Net::CZoneManager::GetZoneIndex(CZone* pZone)
{
	int32 index = (int32)((char*)pZone - (char*)_zones) / _size_max;
	if (index < 0 || index >= _maxZoneCnt)
		return -1;
	return index;
}
int32 Net::CZoneManager::GetZoneIndex(uint64 zoneId)
{
	int32 index = (int32)(zoneId & ZONE_INDEX_MASK);
	return index;
}



Net::CReferenceZone::CReferenceZone(uint64 zoneId, CZoneManager* pManager) :_pZone(nullptr), _pManager(pManager)
{
	Net::CZone* pZone = pManager->GetZonePtr(zoneId);
	if (pZone == nullptr)
		return;
	if (pZone->isReleasing() == true)
	{
		pManager->DecreaseRefcount(pZone);
		return;
	}
	if (pZone->_zoneId != zoneId)
	{
		pManager->DecreaseRefcount(pZone);
		return;
	}
	
	_pZone = pZone;
}

Net::CReferenceZone::~CReferenceZone()
{
	if (_pZone != nullptr)
	{
		_pManager->DecreaseRefcount(_pZone);
	}
}