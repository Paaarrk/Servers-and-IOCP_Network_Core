#ifndef __MUTEX_QUEUE_H__
#define __MUTEX_QUEUE_H__
#include "TlsObjectPool_IntrusiveList.hpp"
#include <mutex>

#ifdef _PROFILE_QUEUE
#include <vector>
#include <algorithm>
#include "logclassV1.h"
#endif


/////////////////////////////////////////////////////////////////////
// Mutex Queue 
/////////////////////////////////////////////////////////////////////


// 고유 풀 번호
#define MUTEX_QUEUE_POOL_NUM		0xFD00'0000


namespace Core
{



#ifdef _PROFILE_QUEUE
	class CProfileMutexQueue
	{
	public:
		struct stTlsInfo
		{
			uint64_t lockCnt = 0;
			uint64_t lock_failCnt = 0;
			LARGE_INTEGER minsuccessTime;
			LARGE_INTEGER maxsuccessTime[5];
			LARGE_INTEGER minfailTime;
			LARGE_INTEGER maxfailTime[5];
			LARGE_INTEGER totalsuccessTime = {};
			LARGE_INTEGER totalfailTime = {};
			stTlsInfo()
			{
				for (int i = 0; i < 5; i++)
				{
					maxsuccessTime[i].QuadPart = LLONG_MIN;
					maxfailTime[i].QuadPart = LLONG_MIN;
				}
				minsuccessTime.QuadPart = LLONG_MAX;
				minfailTime.QuadPart = LLONG_MAX;
			}
			void SetSuccessTime(int time)
			{
				if (time < minsuccessTime.QuadPart)
					minsuccessTime.QuadPart = time;
				
				int index = -1;
				for (int i = 4; i >= 0; i--)
				{
					if (maxsuccessTime[i].QuadPart < time)
					{
						index = i;
						break;
					}
				}
				if (index == -1)
					return;

				for (int i = 0; i < index; i++)
				{
					maxsuccessTime[i].QuadPart = maxsuccessTime[i + 1].QuadPart;
				}

				maxsuccessTime[index].QuadPart = time;
			}
			void SetFailTime(int time)
			{
				if (time < minfailTime.QuadPart)
					minfailTime.QuadPart = time;

				int index = -1;
				for (int i = 4; i >= 0; i--)
				{
					if (maxfailTime[i].QuadPart < time)
					{
						index = i;
						break;
					}
				}
				if (index == -1)
					return;

				for (int i = 0; i < index; i++)
				{
					maxfailTime[i].QuadPart = maxfailTime[i + 1].QuadPart;
				}

				maxfailTime[index].QuadPart = time;
			}
		};

		static void Lock_Invoke(bool isFail)
		{
			if (tls_info == nullptr)
			{
				long index = _InterlockedIncrement(&s_threadIndex);
				tls_info = new stTlsInfo;
				SetPointer(tls_info, index);
			}
			if (isFail)
				tls_info->lock_failCnt++;
			tls_info->lockCnt++;
		}
		static void SetPointer(stTlsInfo* p, int index)
		{
			GetInstance()._pTlsInfos[index] = p;
		}
		static void Set_FailTime(int deltaTime)
		{
			tls_info->totalfailTime.QuadPart += deltaTime;
			tls_info->SetFailTime(deltaTime);
		}
		static void Set_SuccessTime(int deltaTime)
		{
			tls_info->totalsuccessTime.QuadPart += deltaTime;
			tls_info->SetSuccessTime(deltaTime);
		}
		static CProfileMutexQueue& GetInstance()
		{
			static CProfileMutexQueue pf;
			return pf;
		}
	private:
		CProfileMutexQueue()
		{
			_pTlsInfos = new stTlsInfo * [30];
			memset(_pTlsInfos, 0, sizeof(stTlsInfo*) * 30);
		}
		~CProfileMutexQueue()
		{
			uint64_t totalLockCnt = 0;
			uint64_t totalLock_failCnt = 0;
			LARGE_INTEGER totalfailTime = {};
			LARGE_INTEGER totalsuccessTime = {};
			std::vector<LONGLONG> v_minsuccess;
			std::vector<LONGLONG> v_maxsuccess;
			std::vector<LONGLONG> v_minfail;
			std::vector<LONGLONG> v_maxfail;
			for (int i = 0; i <= s_threadIndex; i++)
			{
				if (_pTlsInfos[i] != nullptr)
				{
					totalLockCnt += _pTlsInfos[i]->lockCnt;
					totalLock_failCnt += _pTlsInfos[i]->lock_failCnt;
					totalfailTime.QuadPart += _pTlsInfos[i]->totalfailTime.QuadPart;
					totalsuccessTime.QuadPart += _pTlsInfos[i]->totalsuccessTime.QuadPart;

					for(int j = 0; j < 5; j++)
					{
						v_maxsuccess.push_back(_pTlsInfos[i]->maxsuccessTime[j].QuadPart);
						v_maxfail.push_back(_pTlsInfos[i]->maxfailTime[j].QuadPart);
					}
					v_minsuccess.push_back(_pTlsInfos[i]->minsuccessTime.QuadPart);
					v_minfail.push_back(_pTlsInfos[i]->minfailTime.QuadPart);
				}
			}
			std::sort(v_minsuccess.begin(), v_minsuccess.end());
			std::sort(v_maxsuccess.begin(), v_maxsuccess.end());
			std::sort(v_minfail.begin(), v_minfail.end());
			std::sort(v_maxfail.begin(), v_maxfail.end());


			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			double failavg;
			double successavg;
			double avg;
			if (totalLock_failCnt != 0)
				failavg = ((double)totalfailTime.QuadPart * 1000 * 1000 / freq.QuadPart) / totalLock_failCnt;
			else
				failavg = 0.0;
			uint64_t totalLock_successCnt = totalLockCnt - totalLock_failCnt;
			if (totalLock_successCnt != 0)
				successavg = ((double)totalsuccessTime.QuadPart * 1000 * 1000 / freq.QuadPart) / totalLock_successCnt;
			else
				successavg = 0.0;
			if (totalLockCnt != 0)
				avg = (((double)totalsuccessTime.QuadPart + totalfailTime.QuadPart) * 1000 * 1000 / freq.QuadPart / totalLockCnt);
			else
				avg = 0.0;


			double maxsuccess = (double)v_maxsuccess[v_maxsuccess.size() - 5] * 1000 * 1000 / freq.QuadPart;
			double minsuccess = (double)v_minsuccess[0] * 1000 * 1000 / freq.QuadPart;
			double maxfail = (double)v_maxfail[v_maxfail.size() - 5] * 1000 * 1000 / freq.QuadPart;
			double minfail = (double)v_minfail[0] * 1000 * 1000 / freq.QuadPart;

			Core::c_syslog::logging().Log(L"Profile MutexQueue", Core::c_syslog::en_SYSTEM, L"[EnqThreadCount: %d] lockCnt: %llu, lock_failCnt: %llu, lock_EnqAvgTime: %lf lock_success_EnqAvgTime: %lf, lock_fail_EnqAvgTime: %lf us", s_threadIndex + 1, totalLockCnt, totalLock_failCnt, avg, successavg, failavg);
			Core::c_syslog::logging().Log(L"Profile MutexQueue", Core::c_syslog::en_SYSTEM, L"lock_success_min: %lf us, lock_success_max: %lf us, lock_fail_min: %lf, lock_fail_max: %lf us", minsuccess, maxsuccess, minfail, maxfail);
		}
		stTlsInfo** _pTlsInfos;
		
		inline static thread_local stTlsInfo* tls_info;
		inline static long s_threadIndex = -1;
	};
#endif

template <typename T>
class CMutexQueue
{
public:
	enum QUEUE
	{
		MAX_QUEUE_SIZE = 1000
	};
	//----------------------------------------------------------------
	// 노드 구조체
	//----------------------------------------------------------------
	struct stNode
	{
		stNode* next;
		T data;
	};

	//----------------------------------------------------------------
	// Info: 인큐
	// Parameter: const T& data (넣을 데이터)
	// Return: 성공 true, 실패 false,  실패사유: size 오버
	//----------------------------------------------------------------
	bool Enqueue(const T& data)
	{
		if (_size > _maxSize)
			return false;
		
		stNode* newNode = s_nodePool.Alloc();
		newNode->next = nullptr;
		newNode->data = data;

#ifdef _PROFILE_QUEUE
		LARGE_INTEGER startTime;
		QueryPerformanceCounter(&startTime);
		//bool ret = _enqueuelock.try_lock();
		bool ret = TryAcquireSRWLockExclusive(&_enqueuelock);
		if (ret == false)
		{
			CProfileMutexQueue::Lock_Invoke(true);
			//PRO_BEGIN(L"FAILED_LOCK");
			//_enqueuelock.lock();
			AcquireSRWLockExclusive(&_enqueuelock);
		}
		else
		{
			CProfileMutexQueue::Lock_Invoke(false);
		}
#else
		_enqueuelock.lock();
#endif

		_tail->next = newNode;
		_tail = newNode;
		_InterlockedIncrement(&_size);

		//_enqueuelock.unlock();
		ReleaseSRWLockExclusive(&_enqueuelock);
#ifdef _PROFILE_QUEUE
		LARGE_INTEGER endTime;
		QueryPerformanceCounter(&endTime);
		if (ret == false)
		{
			//PRO_END(L"FAILED_LOCK");
			CProfileMutexQueue::Set_FailTime((int)(endTime.QuadPart - startTime.QuadPart));
		}
		else
		{
			CProfileMutexQueue::Set_SuccessTime((int)(endTime.QuadPart - startTime.QuadPart));
		}
#endif

		return true;
	}

	//----------------------------------------------------------------
	// Info: 인큐
	// Parameter: const T& data (넣을 데이터)
	// ** 무조건 성공함 **
	//----------------------------------------------------------------
	void Enqueue_NotFail(const T& data)
	{
		stNode* newNode = s_nodePool.Alloc();
		newNode->next = nullptr;
		newNode->data = data;


#ifdef _PROFILE_QUEUE
		LARGE_INTEGER startTime;
		QueryPerformanceCounter(&startTime);
		//bool ret = _enqueuelock.try_lock();
		bool ret = TryAcquireSRWLockExclusive(&_enqueuelock);
		if (ret == false)
		{
			CProfileMutexQueue::Lock_Invoke(true);
			//PRO_BEGIN(L"FAILED_LOCK");
			//_enqueuelock.lock();
			AcquireSRWLockExclusive(&_enqueuelock);
		}
		else
		{
			CProfileMutexQueue::Lock_Invoke(false);
		}
#else
		_enqueuelock.lock();
#endif

		_tail->next = newNode;
		_tail = newNode;
		_InterlockedIncrement(&_size);

		//_enqueuelock.unlock();
		ReleaseSRWLockExclusive(&_enqueuelock);
#ifdef _PROFILE_QUEUE
		LARGE_INTEGER endTime;
		QueryPerformanceCounter(&endTime);
		if(ret == false)
		{
			//PRO_END(L"FAILED_LOCK");
			CProfileMutexQueue::Set_FailTime((int)(endTime.QuadPart - startTime.QuadPart));
		}
		else
		{
			CProfileMutexQueue::Set_SuccessTime((int)(endTime.QuadPart - startTime.QuadPart));
		}
#endif
	}

	//----------------------------------------------------------------
	// Info: 디큐
	// Parameter: T& data (데이터 받을 메모리)
	// Return: 성공시 true, 실패시 false, 실패는 큐가 비어서 -> 경쟁에서 져서 누가 채감
	//----------------------------------------------------------------
	bool Dequeue(T& data)
	{
		stNode* ret;

		//_dequeuelock.lock();
		AcquireSRWLockExclusive(&_dequeuelock);

		if (_head->next == nullptr)
		{
			//_dequeuelock.unlock();
			ReleaseSRWLockExclusive(&_dequeuelock);
			return false;
		}

		ret = _head;
		_head = _head->next;
		data = _head->data;
		_InterlockedDecrement(&_size);

		//_dequeuelock.unlock();
		ReleaseSRWLockExclusive(&_dequeuelock);

		s_nodePool.Free(ret);

		return true;
	}

	//----------------------------------------------------------------
	// 사이즈 얻기, 참고용이다
	//----------------------------------------------------------------
	int GetSize()
	{
		return (int)_size;
	}
	//----------------------------------------------------------------
	// bool isEmpty()
	// 비었으면 true, 뭔가 있으면 false
	// 
	// . 이게 가능한 이유는 빈큐문제에서는 확실히 isEmpty,
	// . 만약 빈큐문제 해결 시 size는 1이되고, 
	// . 디큐시 tail이동도 성공하면서 size는 1이 더 증가함
	//----------------------------------------------------------------
	bool isEmpty()
	{
		return (((uint64_t)_head & LOCKFREE_QUEUE_BIT_MASK) == ((uint64_t)_tail & LOCKFREE_QUEUE_BIT_MASK));
	}

	//----------------------------------------------------------------
	// 청소 -> 문제있네... 리스트 같은거 줘야하나..?
	//----------------------------------------------------------------
	void Clear()
	{
		T data;
		while (((uint64_t)_head & LOCKFREE_QUEUE_BIT_MASK) != ((uint64_t)_tail & LOCKFREE_QUEUE_BIT_MASK))
		{
			Dequeue(data);
		}
	}
	//----------------------------------------------------------------
	// 생성자
	//----------------------------------------------------------------
	CMutexQueue(int maxSize = MAX_QUEUE_SIZE) :_maxSize(maxSize),_enqueuelock(SRWLOCK_INIT), _dequeuelock(SRWLOCK_INIT)
	{
		//------------------------------------------------------------
		// 64비트가 아니면 생성을 막자
		//------------------------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			__debugbreak();
		}
		//------------------------------------------------------------
		// 시작 준비는 head, tail이 더미 노드를 가리키도록
		//------------------------------------------------------------
		stNode* dummy = s_nodePool.Alloc();
		dummy->next = nullptr;
		_head = dummy;
		_tail = dummy;
		_size = 0;
	}
	//----------------------------------------------------------------
	// 소멸자
	//----------------------------------------------------------------
	~CMutexQueue()
	{
		T data;
		while (_head != _tail)
		{
			Dequeue(data);
		}
	}
	//----------------------------------------------------------------
	// 생성한 청크 수
	//----------------------------------------------------------------
	static int GetCreateChunkNum()
	{
		return s_nodePool.GetAllocChunkPoolCreateNum();
	}
	static int GetInPoolChunkNum()
	{
		return s_nodePool.GetAllocChunkPoolSize();
	}
private:
	//----------------------------------------------------------------
	// 헤드 (디큐)
	//----------------------------------------------------------------
	stNode* _head;
	//----------------------------------------------------------------
	// 사이즈
	//----------------------------------------------------------------
	long _size;
	//----------------------------------------------------------------
	// 큐 최대 사이즈
	//----------------------------------------------------------------
	int _maxSize;

	SRWLOCK _enqueuelock;
	SRWLOCK _dequeuelock;
	//----------------------------------------------------------------
	// 테일 (인큐)
	//----------------------------------------------------------------
	stNode* _tail;

	inline static CTlsObjectPool<stNode, MUTEX_QUEUE_POOL_NUM, TLS_OBJECTPOOL_USE_RAW> s_nodePool;
};

}



#endif 