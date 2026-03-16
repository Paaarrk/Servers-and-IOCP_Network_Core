#ifndef __ZONE_MANAGER_H__
#define __ZONE_MANAGER_H__
#include "Type.h"
#include "Zone.h"
#include "LockFreeQueue.hpp"
#include "TimerManager.h"
#include <vector>

namespace Core
{
	template <class T>
	class CLockFreeStack;
}

namespace Net
{
	constexpr const wchar_t* TAG_ZONE = L"Zone";
	class CZone;
	class CZoneServer;
	struct stZoneType;

	class CPinnedZoneThreads
	{
	public:
		enum enPinnedZoneThreads
		{
			EXIT = 0,
		};
		CPinnedZoneThreads(CZoneManager* pManager);
		~CPinnedZoneThreads();
		CPinnedZoneThreads(CPinnedZoneThreads&& moveT) noexcept;
		void PinnedThreadFunc();

		int32 GetWight() const { return _weight; }
		void IncreaseWeight(int32 weight) { _weight += weight; }
		void DecreaseWeight(int32 weight) { _weight -= weight; }
		void  ExitRequest() 
		{ 
			_zoneQueue.Enqueue_NotFail(EXIT); 
			SetEvent(_wakeEvent);
		}
		void  UpdateRequest(uint64 _zoneId)
		{ 
			_zoneQueue.Enqueue_NotFail(_zoneId); 
			SetEvent(_wakeEvent);
		}
	private:
		Core::CLockFreeQueue<uint64> _zoneQueue;
		CZoneManager*   _myManager;
		int32			_weight;
		std::thread		_thread;
		HANDLE			_wakeEvent;
	};

	class CZoneManager
	{
		friend class CZoneTimerJob;
		friend class CZoneServer;
		friend class CReferenceZone;
	public:
		CZoneManager(CZoneServer* pZoneServer);
		~CZoneManager();

		// Init 호출 전에만 가능, new MakeZoneType<T>(conId);
		void RegisterZoneType(stZoneType* pType);

		bool Init(int maxZoneCnt, int maxPinnedZoneThreadsCnt);

		void Clear();

		uint32 GetZoneCnt() const { return _zoneCnt; }

		// 실패시 0 반환, 성공시 zoneId
		uint64 CreateZone(int32 contentsId);

		void DestroyZone(uint64 _zoneId);

		bool EnterZone(uint64 zoneId, uint64 sessionId, const wchar_t* ipw = nullptr);
		bool LeaveZone(uint64 zoneId, uint64 sessionId);
		bool MoveZone(uint64 targetZoneId, uint64 sessionId);

		// 실패시 false(존 없음), 성공시 true
		template <typename F>
		bool PushJob(uint64 zoneId, F&& lambda)
		{
			CReferenceZone refzone(zoneId, this);
			if (refzone.isAlive())
			{
				Core::IJob* pJob = new Net::CZoneJob(std::forward<F>(lambda));
				refzone.GetZone()->_jobQueue->Enqueue_NotFail(pJob);
				return true;
			}
			return false;
		}

		Core::CTimerManager& GetZoneTimer() { return _zoneTimer; }

		CZone* GetZonePtr(uint64 _zoneId);

		bool UpdateZone(int index, uint64 zoneId)
		{
			_zoneThreads[index].UpdateRequest(zoneId);
			return true;
		}

	private:
		CZone* GetZonePtr(int32 index);

		long IncreaseRefcount(CZone* pZone);

		void DecreaseRefcount(CZone* pZone);

		void ReleaseZone(CZone* pZone);

		stZoneType* FindZoneType(int32 contentsId);

		int32 GetZoneIndex(CZone* pZone);
		int32 GetZoneIndex(uint64 zoneId);
	private:
		void* _zones;
		Core::CLockFreeStack<int32>* _indexes;

		bool   _isCallInit;

		std::vector<stZoneType*> _zonetypes;
		int _maxZoneCnt;
		int _size_max;
		int _align_max;
		uint64 _zoneId;
		uint32 _zoneCnt;
		CZoneServer* _pZoneServer;
		Core::CTimerManager _zoneTimer;

		std::mutex _pinnedLock;
		int _maxPinnedZoneThreadsCnt;
		std::vector<CPinnedZoneThreads> _zoneThreads;
	};

	class CReferenceZone
	{
	public:
		CReferenceZone(uint64 zoneId, CZoneManager* pManager);
		~CReferenceZone();
		bool isAlive() const { return (_pZone != nullptr); }
		CZone* GetZone() const { return _pZone; }
	private:
		CZone* _pZone;
		CZoneManager* _pManager;
	};
};

#endif