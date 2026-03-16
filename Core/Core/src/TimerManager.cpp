#include "TimerManager.h"
#include "logclassV1.h"


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <synchapi.h>
#include <time.h>
#pragma comment(lib, "winmm")

Core::CTimerManager::CTimerManager()
{
	_hExitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (_hExitEvent == NULL)
		__debugbreak();
	_hEnqueueEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (_hEnqueueEvent == NULL)
		__debugbreak();

	std::thread THtimer(&Core::CTimerManager::TimerProc, this);
	_thTimerThread = std::move(THtimer);
}

Core::CTimerManager::~CTimerManager()
{
	if (_hExitEvent != 0)
	{
		SetEvent(_hExitEvent);
	}
	if (_thTimerThread.joinable())
		_thTimerThread.join();
	if (_hExitEvent != 0)
	{
		CloseHandle(_hExitEvent);
		_hExitEvent = 0;
	}
	if (_hEnqueueEvent != 0)
	{
		CloseHandle(_hEnqueueEvent);
		_hEnqueueEvent = 0;
	}
}

void Core::CTimerManager::ExitTimer()
{
	if (_hExitEvent != 0)
	{
		SetEvent(_hExitEvent);
	}
	if (_thTimerThread.joinable())
		_thTimerThread.join();
	if (_hExitEvent != 0)
	{
		CloseHandle(_hExitEvent);
		_hExitEvent = 0;
	}
	if(_hEnqueueEvent != 0)
	{
		CloseHandle(_hEnqueueEvent);
		_hEnqueueEvent = 0;
	}
}

void Core::CTimerManager::RequestTimerJob(const std::shared_ptr<CTimerJob>& timer, int32 sleepTime, unsigned long curTime)
{
	if (sleepTime < 0)
	{
		Core::c_syslog::logging().Log(TAG_TIMER, Core::c_syslog::en_ERROR, L"RequestTimerJob(): sleepTime < 0 [%d]", sleepTime);
		return;
	}
	if (_timerRun.load(std::memory_order_relaxed) == 0)
		return;
	
	if (curTime == 0)
		curTime = timeGetTime();
	curTime += (DWORD)sleepTime;
	timer->SetExcuteTime(curTime);
	
	{
		std::lock_guard<std::mutex> guard(_lock);
		if (_pq.empty() == false)
		{
			const std::shared_ptr<Core::CTimerJob>& cur = _pq.top();
			DWORD peekTime = cur->GetExcuteTime();
			_pq.push(timer);
			int32 delta = (int32)(peekTime - curTime);
			if (delta >= 0)
				SetEvent(_hEnqueueEvent);
		}
		else
		{
			_pq.push(timer);
			SetEvent(_hEnqueueEvent);
		}
	}
}


void Core::CTimerManager::RequestTimerItSelf(CTimerJob* pThis, int32 sleepTime, unsigned long curTime)
{
	std::shared_ptr<CTimerJob> selfShared = pThis->shared_from_this();
	RequestTimerJob(selfShared, sleepTime, curTime);
}

void Core::CTimerManager::TimerProc()
{
	_timerRun.store(1, std::memory_order_seq_cst);

	timeBeginPeriod(1);
	DWORD tid = GetCurrentThreadId();
	Core::c_syslog::logging().Log(TAG_TIMER, c_syslog::en_SYSTEM, L"Timer Thread Start!!");
	HANDLE hEvents[2] = { _hExitEvent, _hEnqueueEvent };

	DWORD dwSleepTime = INFINITE;
	int32 i32sleepTime;
	std::vector < std::shared_ptr<Core::CTimerJob>> v;
	for (;;)
	{
		DWORD retEvent = WaitForMultipleObjects(2, hEvents, FALSE, dwSleepTime);
		if (retEvent == WAIT_OBJECT_0)
		{
			break;
		}

	WORK:
		DWORD dwCurtime = timeGetTime();
		i32sleepTime = 0x7FFF'FFFF;
		{
			std::lock_guard<std::mutex> guard(_lock);
			while (_pq.empty() == false)
			{
				dwCurtime = timeGetTime();
				const std::shared_ptr<Core::CTimerJob>& cur = _pq.top();
				int32 delta = (int32)(cur->GetExcuteTime() - dwCurtime);
				if (delta <= 2)
				{
					std::shared_ptr<Core::CTimerJob> curJob = _pq.top();
					_pq.pop();
					v.push_back(std::move(curJob));
					if (delta < -50)
					{
						Core::c_syslog::logging().Log(TAG_TIMER, c_syslog::en_ERROR, L"[delta: %d]Timer System: 타이머가 제때 동작 안함, 확장 필요", -delta);
					}
				}
				else
				{
					if (i32sleepTime > delta)
					{
						i32sleepTime = delta;
						break;
					}
				}
			}
		}
		for (std::shared_ptr<Core::CTimerJob>& r : v)
		{
			if (r->isValid())
				r->Excute();
		}
		v.clear();

		if (i32sleepTime != 0x7FFF'FFFF)
		{
			i32sleepTime -= (int32)(timeGetTime() - dwCurtime);
		}
		dwSleepTime = i32sleepTime;
		if (i32sleepTime <= 1)
			goto WORK;
	}

	_timerRun.store(0, std::memory_order_seq_cst);
	{
		std::lock_guard<std::mutex> guard(_lock);
		while (_pq.empty() == false)
			_pq.pop();
	}

	Core::c_syslog::logging().Log(TAG_TIMER, c_syslog::en_SYSTEM, L"Timer Thread End!!");
}