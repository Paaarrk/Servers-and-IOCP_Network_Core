#ifndef __LOCKFREE_STACK_H__
#define __LOCKFREE_STACK_H__

#include "TlsObjectPool_IntrusiveList.hpp"

// 카운터로 사용할 비트 수 
#define LOCKFREE_STACK_COUNTER_BIT	17
// 시프트 할 횟수
#define LOCKFREE_STACK_SHIFT_BIT	47
// 마스크
#define LOCKFREE_STACK_BIT_MASK		0x00007FFF'FFFFFFFF
// 고유 풀 번호
#define LOCKFREE_STACK_POOL_NUM		0xE000'0000


namespace Core
{
template <class T>
class CLockFreeStack
{
public:
	//----------------------------------------------------------
	// 노드 구조체
	//----------------------------------------------------------
	struct stNode
	{
		stNode* next;
		T data;
	};

	//----------------------------------------------------------
	// Info: 데이터 삽입
	// Param: const T& data
	// Return: -
	//----------------------------------------------------------
	void push(const T& data)
	{
		//-----------------------------------------------------
		// 락프리를 위한 변수
		//-----------------------------------------------------
		stNode* t;
		stNode* newTop;
		uint64_t counter;

		//-----------------------------------------------------
		// 사전 작업
		//-----------------------------------------------------
		stNode* pNewNode = s_nodePools.Alloc();
		pNewNode->data = data;

		do
		{
			t = _pTop;
			//-----------------------------------------------------
			// 카운터 올린 후 newTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> LOCKFREE_STACK_SHIFT_BIT) + 1;
			newTop = (stNode*)((uint64_t)pNewNode | (counter << LOCKFREE_STACK_SHIFT_BIT));
			pNewNode->next = (stNode*)((uint64_t)t & LOCKFREE_STACK_BIT_MASK);

		} while (_InterlockedCompareExchangePointer((volatile PVOID*)&_pTop, (void*)newTop, (void*)t) != t);
		_InterlockedIncrement(&_size);
	}
	//----------------------------------------------------------
	// Info: 데이터 꺼내기
	// Param: T& data
	// Return: 성공시 true, 실패시 false (비어있음)
	//----------------------------------------------------------
	bool pop(T& data)
	{
		//-----------------------------------------------------
		// 락프리를 위한 변수
		//-----------------------------------------------------
		stNode* t;
		stNode* nextTop;
		uint64_t counter;
		//-----------------------------------------------------
		// 반환할 주소
		//-----------------------------------------------------
		stNode* ret;

		do
		{
			t = _pTop;
			ret = (stNode*)((uint64_t)t & LOCKFREE_STACK_BIT_MASK);
			if (ret == nullptr)
				return false;

			counter = ((uint64_t)t >> LOCKFREE_STACK_SHIFT_BIT) + 1;
			nextTop = (stNode*)((uint64_t)(ret->next) | (counter << LOCKFREE_STACK_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((volatile PVOID*)&_pTop, (void*)nextTop, (void*)t) != t);
		data = (ret->data);
		_InterlockedDecrement(&_size);
		if (s_nodePools.Free(ret))
			__debugbreak();	//추후에 문제 없으면 지우기
		return true;
	}
	//----------------------------------------------------------
	// Info: 사이즈 획득
	// Param: -
	// Return: int (지금 본 사이즈)
	//----------------------------------------------------------
	int GetSize()
	{
		return (int)_size;
	}
	//----------------------------------------------------------
	// Info: 청소
	// Param: -
	// Return: -
	//----------------------------------------------------------
	void Clear()
	{
		T data;
		while (_size > 0)
		{
			pop(data);
		}
	}
	//----------------------------------------------------------------
	// bool isEmpty()
	// 비었으면 true, 뭔가 있으면 false
	//----------------------------------------------------------------
	bool isEmpty()
	{
		return ((stNode*)((uint64_t)_pTop & LOCKFREE_STACK_BIT_MASK) == nullptr);
	}

	//----------------------------------------------------------------
	// 생성한 청크 수
	//----------------------------------------------------------------
	static int GetCreateChunkNum()
	{
		return s_nodePools.GetAllocChunkPoolCreateNum();
	}

	//----------------------------------------------------------------
	// 남은 청크 수
	//----------------------------------------------------------------
	static int GetLeftChunkNum()
	{
		return s_nodePools.GetAllocChunkPoolSize();
	}

	CLockFreeStack() :_pTop(nullptr), _size(0)
	{
		//----------------------------------------------
		// 64비트가 아니면 생성을 막자
		//----------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			__debugbreak();
		}
	}
	~CLockFreeStack()
	{
		T data;
		while (_size > 0)
		{
			pop(data);
		}
	}
private:
	stNode* _pTop;
	long _size;
	inline static CTlsObjectPool<stNode, LOCKFREE_STACK_POOL_NUM, TLS_OBJECTPOOL_USE_RAW> s_nodePools;
};
}



#endif