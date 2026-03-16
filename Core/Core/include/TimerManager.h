#ifndef __TIMER_H__
#define __TIMER_H__

#include "TimerJob.h"
#include <memory>
#include <queue>
#include <mutex>
#include <vector>

constexpr const wchar_t* TAG_TIMER = L"Timer";
namespace Core
{
	struct CompareTimerJob
	{
		bool operator()(const std::shared_ptr<CTimerJob>& a, const std::shared_ptr<CTimerJob>& b) const
		{
			return *a > *b;
		}
	};


	class CTimerManager
	{
	public:
		CTimerManager();
		~CTimerManager();

		//----------------------------------------------------------
		// 타이머 종료
		// (소멸자가 해주지만, 명시적인게 좋아서 넣음)
		//----------------------------------------------------------
		void ExitTimer();

		//----------------------------------------------------------
		// 타이머에 예약
		//----------------------------------------------------------
		void RequestTimerJob(const std::shared_ptr<CTimerJob>& timer, int32 sleepTime, unsigned long curTime = 0);

		//----------------------------------------------------------
		// 타이머 자가 예약 (반복 루프용)
		// 반드시 자기자신 (this)를 넣으세요
		//----------------------------------------------------------
		void RequestTimerItSelf(CTimerJob* pThis, int32 sleepTime, unsigned long curTime = 0);

		//----------------------------------------------------------
		// 우선순위 큐 사이즈
		//----------------------------------------------------------
		int GetQueueSize() { return (int)_pq.size(); }

		int32 isRun() { return _timerRun; }
	private:
		//----------------------------------------------------------
		// 타이머 스레드 루프
		//----------------------------------------------------------
		void TimerProc();
	private:
		void* _hExitEvent = 0;
		void* _hEnqueueEvent = 0;
		std::atomic_int32_t _timerRun = 1;
		std::mutex _lock;
		std::priority_queue < std::shared_ptr<Core::CTimerJob>, std::vector<std::shared_ptr<Core::CTimerJob>>, Core::CompareTimerJob > _pq;
		std::thread _thTimerThread;
	};
}

#endif