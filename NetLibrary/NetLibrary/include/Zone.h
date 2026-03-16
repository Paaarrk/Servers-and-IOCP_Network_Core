#ifndef __ZONE_H__
#define __ZONE_H__
#include "LockFreeQueue.hpp"
#include "TimerJob.h"
#include "Job.h"
#include <memory>
#include <unordered_set>
#include <string>

#define ZONE_RELEASE_FLAG	0x8000'0000
#define ZONE_INDEX_MASK		0x0000'0000'0000'FFFF
#define ZONE_ID_SHIFT		16

namespace Core
{
	template <class T>
	class CLockFreeQueue;
	class RingBuffer;
	
	class IJob;
}

namespace Net
{
	struct stEvent;
	class CZone;
	class CZoneServer;
	class CZoneTimerJob :public Core::CTimerJob
	{
		friend class CZone;
	public:
		void Set(CZoneServer* pZoneServer, CZone* pZone, uint64_t zoneId);
		virtual void Excute();
	private:
		CZoneServer* _pZoneServer = nullptr;
		CZone* _pZone = nullptr;
		uint64_t _zoneId = 0;
	};


	template <typename F>
	class CZoneJob : public Core::IJob
	{
	public:
		CZoneJob(F&& f)
			: func(std::forward<F>(f))
		{

		}
		void Excute() { func(); }
		~CZoneJob() {}

		void* operator new(size_t size)
		{
			return Core::stJobBlock::Alloc();
		}

		void operator delete(void* ptr)
		{
			Core::stJobBlock::Free((Core::stJobBlock*)ptr);
		}
	private:
		std::decay_t<F> func;
	};

	class CPacket;
	class CZone
	{
		friend class CZoneServer;
		friend class CZoneManager;
		friend class CZoneTimerJob;
		friend class CReferenceZone;
		friend class CPinnedZoneThreads;
	public:
		int32_t GetDeltaTime() const { return _updateDeltaTick; }
		unsigned long GetTickStartTime() const { return _startTime; }
		int32_t GetMaxUsers() const { return _maxUsers; }
		uint64_t GetZoneId() const { return _zoneId; }
		CZoneManager* GetZoneManager() const;
		CZoneServer* GetZoneServer() const { return _myServer; }
		template <typename F>
		void PushJob(F&& lambda)
		{
			Core::IJob* pJob = new Net::CZoneJob(std::forward<F>(lambda));
			_jobQueue->Enqueue_NotFail(pJob);
		}

		void EnterFunc(uint64 sessionId, void* playerPointer, std::wstring* ip = nullptr);
		void LeaveFunc(uint64 sessionId, bool bNeedPlayerDelete = false);
		bool Disconnect(uint64 sessionId);
		bool DisconnectZoneZero(uint64 sessionId);
		bool SendPacket(uint64 sessionId, Net::CPacket* packet);

		// OnMessage전용
		bool SendPacketFast(uint64 sessionId, Net::CPacket* packet);

		virtual void OnUpdate() = 0;
		virtual void OnEnter(uint64 sessionId, void* playerPtr, std::wstring* ip) = 0;
		virtual void OnLeave(uint64 sessionId, bool bNeedPlayerDelete) = 0;
		virtual void OnMessage(uint64 sessionId, const char* readPtr, int payloadlen) = 0;

		
		int32_t GetSessionCnt() const { return (int32)_sessions->size(); }
	protected:
#pragma warning(push)
#pragma warning(disable: 26495) // 생성자대신 Acquire, Release로 멤버 컨트롤
		CZone() {}
#pragma warning(pop)
		virtual ~CZone() {}
	private:
		unsigned long IncreaseRefcount();
		unsigned long DecreaseRefcount();
		bool isReleasing();
		void Init(CZoneServer* pZoneServer, int32 maxUser);
		void Acquire(int32 contentsId, uint64 zoneId, int32 minimumTick, int32 maxUsers);
		void Release();
		void Clear();
		void TickUpdate();

		void SetPinned(int index, int weight);
		int32_t GetWeight() const { return _weight; }
	private:
		CZoneServer*						_myServer;
		unsigned long						_startTime;
		int32_t								_updateDeltaTick;
		unsigned long						_befUpdate;
		int32_t								_minimumTick;
		
		unsigned long						_refCount;
		int32_t								_contentsId;
		uint64_t								_zoneId;
		int32_t								_maxUsers;
		Core::CLockFreeQueue <Core::IJob*>* _jobQueue;
		std::shared_ptr<CZoneTimerJob>*		_timerRequest;
		std::unordered_set<uint64_t>*			_sessions;

		int32_t								_pinnedThreadIndex;
		int32_t								_weight;

	};

}

#endif