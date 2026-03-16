#ifndef __TIMER_JOB_H__
#define __TIMER_JOB_H__
#include "Type.h"
#include <intrin.h>
#include <memory>

namespace Core
{
	class CTimerJob : public std::enable_shared_from_this<CTimerJob>
	{
	public:
		virtual void Excute() = 0;
		virtual ~CTimerJob() {}
		void CancelJob()
		{
			_InterlockedExchange(&_isValid, 0);
		}
		void ActivateJob()
		{
			_InterlockedExchange(&_isValid, 1);
		}
		bool operator>(const CTimerJob& tj) const
		{
			return this->_dwExcuteTime > tj._dwExcuteTime;
		}
		unsigned long GetExcuteTime() const { return _dwExcuteTime; }
		long isValid() const { return _isValid; }
		void SetExcuteTime(unsigned long excuteTime) { _dwExcuteTime = excuteTime; }
	protected:
		// Shared Ptr로만 사용
		CTimerJob() = default;
	private:
		unsigned long _dwExcuteTime = 0;
		long _isValid = 1;
	};
}


#endif