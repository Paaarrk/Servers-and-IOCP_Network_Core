#ifndef __LOCKFREE_QUEUE_H__
#define __LOCKFREE_QUEUE_H__
#include "TlsObjectPool_IntrusiveList_V2.h"

/////////////////////////////////////////////////////////////////////
// LockFreeQueue V4 (with TlsPool)
// < V5: 약간의 수정 >
// . tail이동 코드 추가
// . 데이터 접근시 한번 접근 불가인지 확인
// 
// < V4: 노드 풀을 Tls풀로 교체 >
// 
// < V3: Dequeue에서 Tail이동 개선 >
// . tail이동은 디큐의 끝에서 하자. 
// 
// < V2: size의 부정확 문제 >
// ** isEmpty()의 추가
// . 인큐에 성공해 tail이동까지 성공했는데 아직 size를 올리지 않았다면
// . 디큐가 실행되고 순간적으로 size < 0일 수 있다.
// . 따라서 GetUseSize() > 0 대신 isEmpty()의 필요성을 느꼈고,
// . size는 참고용 변수로만 사용해야 한 다는 것을 느꼈다.
// 
// 
// . 최대한 '락프리' 자료구조의 특성을 살려보자
//   => 경쟁에 이긴놈이 성공한다는 특성 살리기
//   => 선판단을 아얘 없애기!
/////////////////////////////////////////////////////////////////////

// 카운터로 사용할 비트 수 
#define LOCKFREE_QUEUE_COUNTER_BIT	17
// 시프트 할 횟수
#define LOCKFREE_QUEUE_SHIFT_BIT	47
// 마스크
#define LOCKFREE_QUEUE_BIT_MASK		0x00007FFF'FFFFFFFF
// 고유 풀 번호
#define LOCKFREE_QUEUE_POOL_NUM		0xFE00'0000

/////////////////////////////////////////////////////////////////////
// 미친듯이 생성삭제하면 _pMyNull(nullptr대용)이 겹칠 수 있으니 지양
/////////////////////////////////////////////////////////////////////
template <typename T>
class CLockFreeQueue
{
#ifdef _DEBUG
	friend int main();
#endif

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
		newNode->next = _pMyNull;
		newNode->data = data;
		
		while (1)
		{
			stNode* tail = _tail;
			uint64_t counter = ((uint64_t)tail >> LOCKFREE_QUEUE_SHIFT_BIT) + 1;
			stNode* nextTail = (stNode*)((uint64_t)newNode | (counter << LOCKFREE_QUEUE_SHIFT_BIT));
			stNode* next = ((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next;

			if (_InterlockedCompareExchangePointer((volatile PVOID*)&(((stNode*)((uint64_t)tail&LOCKFREE_QUEUE_BIT_MASK))->next), (void*)newNode, (void*)_pMyNull) == _pMyNull)
			{
				if (_InterlockedCompareExchangePointer((volatile PVOID*)&_tail, (void*)nextTail, (void*)tail) == tail)
					_InterlockedIncrement(&_size);
				break;
			}
			else
			{	//무한루프 방지, Tail 옮겨주기
				stNode* tail_next = ((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next;
				nextTail = (stNode*)((uint64_t)tail_next | (counter << LOCKFREE_QUEUE_SHIFT_BIT));
				if (tail_next != _pMyNull)
				{
					if(_InterlockedCompareExchangePointer((volatile PVOID*)&_tail, (void*)nextTail, (void*)tail) == tail)
						_InterlockedIncrement(&_size);
				}
			}
		}

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
		newNode->next = _pMyNull;
		newNode->data = data;

		while (1)
		{
			stNode* tail = _tail;
			uint64_t counter = ((uint64_t)tail >> LOCKFREE_QUEUE_SHIFT_BIT) + 1;
			stNode* nextTail = (stNode*)((uint64_t)newNode | (counter << LOCKFREE_QUEUE_SHIFT_BIT));
			stNode* next = ((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next;

			if (_InterlockedCompareExchangePointer((volatile PVOID*)&(((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next), (void*)newNode, (void*)_pMyNull) == _pMyNull)
			{
				if (_InterlockedCompareExchangePointer((volatile PVOID*)&_tail, (void*)nextTail, (void*)tail) == tail)
					_InterlockedIncrement(&_size);
				break;
			}
			else
			{	//무한루프 방지, Tail 옮겨주기
				stNode* tail_next = ((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next;
				nextTail = (stNode*)((uint64_t)tail_next | (counter << LOCKFREE_QUEUE_SHIFT_BIT));
				if (tail_next != _pMyNull)
				{
					if (_InterlockedCompareExchangePointer((volatile PVOID*)&_tail, (void*)nextTail, (void*)tail) == tail)
						_InterlockedIncrement(&_size);
				}
			}
		}
	}

	//----------------------------------------------------------------
	// Info: 디큐
	// Parameter: T& data (데이터 받을 메모리)
	// Return: 성공시 true, 실패시 false, 실패는 큐가 비어서 -> 경쟁에서 져서 누가 채감
	//----------------------------------------------------------------
	bool Dequeue(T& data)
	{
		stNode* ret;
		stNode* head;
		stNode* ret_next;
		stNode* next;
		while (1)
		{
			head = _head;
			uint64_t counter = ((uint64_t)head >> LOCKFREE_QUEUE_SHIFT_BIT) + 1;
			ret = (stNode*)((uint64_t)head & LOCKFREE_QUEUE_BIT_MASK);
			ret_next = ret->next;
			//----------------------------------------------------------
			// [1] head를 읽고 자다깻는데 재사용되서 next가 null
			// [2] 진짜로 큐가 비어서
			//----------------------------------------------------------
			if (ret_next == _pMyNull)
			{
				if (head != _head)
					continue;	// [1]번상황
				return false;	// [2]번상황
			}
			next = (stNode*)((uint64_t)ret_next | (counter << LOCKFREE_QUEUE_SHIFT_BIT));
			
			//----------------------------------------------------------
			// 더미노드만 있는 경우
			//----------------------------------------------------------
			if (ret == (stNode*)((uint64_t)_tail & LOCKFREE_QUEUE_BIT_MASK))
			{
				if (ret->next == _pMyNull)
				{
					return false;	//진짜 빈거
				}
				else
				{
					//--------------------------------------------------
					// pMyNull이 아니니까 한ㅂ번 이동
					//--------------------------------------------------
					stNode* tail = _tail;
					uint64_t tailcounter = ((uint64_t)tail >> LOCKFREE_QUEUE_SHIFT_BIT) + 1;
					stNode* tail_next = ((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next;
					stNode* nextTail = (stNode*)((uint64_t)tail_next | (tailcounter << LOCKFREE_QUEUE_SHIFT_BIT));
					if (tail_next != _pMyNull)
					{
						if (_InterlockedCompareExchangePointer((volatile PVOID*)&_tail, (void*)nextTail, (void*)tail) == tail)
							_InterlockedIncrement(&_size);
					}
					continue;
				}
			}

			//-------------------------------------------------------------------------
			// . 만약 ret_next->data 값이 외부에서 수정되었다면, 이는 이미 디큐된 값임
			//   => head와 _head가 100% 불일치 -> 루프 다시 돌음
			// . head와 _head가 일치하면 ret_next->data역시 100% 수정된 적이 없음
			//   => 교환 후 복사해둔 데이터를 data에 덮는다.
			// . 중요한건데 널검사를 해야됨... 이걸 놓쳤었네 (이미 다른애가 내가 본 헤드를
			// 뽑고, 그걸 반환까지 했다면, 이게 재사용까지 되었다면 읽을수가 없음)
			// 
			// **여기서 다른 큐에서 재사용되면 _pMyNull은 아닌데 접근을 못함. 이거 체크
			//-------------------------------------------------------------------------
			if (((uint64_t)ret_next & (~LOCKFREE_QUEUE_BIT_MASK)) == (~LOCKFREE_QUEUE_BIT_MASK))
				continue;
			T tempData = ret_next->data;

			if (_InterlockedCompareExchangePointer((volatile PVOID*)&_head, (void*)next, (void*)head) == head)
			{
				_InterlockedDecrement(&_size);
				data = tempData;
				break;
			}
		}

		s_nodePool.Free(ret);
		//----------------------------------------------------------
		// 내가 head를 이동했는데 마침 tail과 같음
		// 그런데 tail의 next != null이면 이동해주기
		// (다음 디큐에 문제가 생기지 않게)
		// 
		// 왜 후반에 해야하나?
		// . 큐 밖에서 들어온 노드에 대한 테일이동은 직접 함.
		// . 이게 움직여야 우리는 디큐를 할 수 있음
		// . 따라서 이게 성공한다 = 덜 이동한 tail이 맨 끝으로 간다
		//----------------------------------------------------------
		stNode* tail = _tail;
		if(ret_next == (stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))
		{
			uint64_t tailcounter = ((uint64_t)tail >> LOCKFREE_QUEUE_SHIFT_BIT) + 1;
			stNode* tail_next = ((stNode*)((uint64_t)tail & LOCKFREE_QUEUE_BIT_MASK))->next;
			stNode* nextTail = (stNode*)((uint64_t)tail_next | (tailcounter << LOCKFREE_QUEUE_SHIFT_BIT));
			if (tail_next != _pMyNull)
			{
				if (_InterlockedCompareExchangePointer((volatile PVOID*)&_tail, (void*)nextTail, (void*)tail) == tail)
					_InterlockedIncrement(&_size);
			}
		}

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
		return (((uint64_t)_head&LOCKFREE_QUEUE_BIT_MASK) == ((uint64_t)_tail&LOCKFREE_QUEUE_BIT_MASK));
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
	CLockFreeQueue(int maxSize = MAX_QUEUE_SIZE):_maxSize(maxSize)
	{
		//------------------------------------------------------------
		// 64비트가 아니면 생성을 막자
		//------------------------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not x86-64!!\n");
			__debugbreak();
		}
		//------------------------------------------------------------
		// V4: 나의 nullptr을 만들자
		// . 접근 불가 주소 (커널영역) _pMyNull을 nullptr대신 씀
		//------------------------------------------------------------
		unsigned long myCounter = _InterlockedIncrement(&CLockFreeQueue<T>::s_counter);
		_pMyNull = (stNode*)(0xFFFF'8000'0000'0000 | (uint64_t)myCounter);
		//------------------------------------------------------------
		// 시작 준비는 head, tail이 더미 노드를 가리키도록
		//------------------------------------------------------------
		stNode* dummy = s_nodePool.Alloc();
		dummy->next = _pMyNull;
		_head = dummy;
		_tail = dummy;
		_size = 0;
	}
	//----------------------------------------------------------------
	// 소멸자
	//----------------------------------------------------------------
	~CLockFreeQueue()
	{
		T data;
		while (((uint64_t)_head & LOCKFREE_QUEUE_BIT_MASK) != ((uint64_t)_tail & LOCKFREE_QUEUE_BIT_MASK))
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
	// 테일 (인큐)
	//----------------------------------------------------------------
	stNode* _tail;
	//----------------------------------------------------------------
	// 전용 nullptr, 이게 달라야 큐끼리 문제가 없다.
	// 왜냐하면 기존 방식은 nullptr만 보고 인큐했기 때문
	//----------------------------------------------------------------
	stNode* _pMyNull;
	//----------------------------------------------------------------
	// 사이즈
	//----------------------------------------------------------------
	long _size;
	//----------------------------------------------------------------
	// 큐 최대 사이즈
	//----------------------------------------------------------------
	int _maxSize;

	//----------------------------------------------------------------
	// 전용 노드 풀, 전용 카운터 (nullptr을 다르게 해야 큐끼리 문제 x)
	//----------------------------------------------------------------
	static unsigned long s_counter;
	static CTlsObjectPool<stNode, LOCKFREE_QUEUE_POOL_NUM, TLS_OBJECTPOOL_USE_RAW> s_nodePool;
};



#endif 