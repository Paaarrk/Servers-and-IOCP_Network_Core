////////////////////////////////////////////////////////
// LockFree FreeList V2
// . 락프리 스택 구조의 메모리 풀
// . 64비트 전용
// . 카운터를 alooc, free 둘 다 올림
// 
// V2: 카운터의 멤버 및 인터락 제거 (탑에서 직접 올림)
////////////////////////////////////////////////////////
#ifndef __LOCKFREE_FREE_LIST__
#define __LOCKFREE_FREE_LIST__

#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include <intrin.h>
#include <wchar.h>
#include <new>
//#define _TEST

// 카운터로 사용할 비트 수 
#define FREELIST_COUNTER_BIT	17
// 시프트 할 횟수
#define FREELIST_SHIFT_BIT		47
// 마스크
#define FREELIST_BIT_MASK		0x00007FFF'FFFFFFFF

// 0 = Raw (오버로딩에 사용)
#define FREELIST_USE_RAW		0
// 1 = 생성자, 소멸자를 새로운 노드가 생길 때만 호출
#define FREELIST_USE_CALLONCE	1
// 2 = 생성자, 소멸자를 항상 호출
#define FREELIST_USE_NORMAL		2

// 그냥 OVER, UNDER 둘 다씀
#define FREELIST_OVER_CODE	0xFCFCFCFC'FCFCFCFC
#define FREELIST_RELEASE	0x9A9A9A9A'9A9A9A9A


//-----------------------------------------------------------
// Info: V1
// . T: 데이터 타입
// . __keyValue: 무조건 독립적인 값 삽입을 하자 
//	(잘못된 Free를 막기 위해서)
// . whatFreeListUse: 
//   0 = Raw (오버로딩에 사용)
//   1 = 생성자, 소멸자를 새로운 노드가 생기거나
//       기존 노드를 완전히 해제 할 때만 호출
//   2 = 생성자, 소멸자를 항상 호출
// . Free() 는 실패시 0이아닌 값
//	enFine = 0			// 정상
//	enUnder = 1			// 언더플로우
//	enOver = 3			// 오버플로우
//	enNullptr = 4		// 들어온 ptr이 null
//	enDoubleFree = 5	// 프리를 두번함
//-----------------------------------------------------------
template <typename T, int __keyValue, int __whatFreeListUse>
class CLockFreeFreeList;

//-----------------------------------------------------------
// LockFree Raw버전 (생성자, 소멸자 호출 X)
//-----------------------------------------------------------
template <typename T, int __keyValue>
class CLockFreeFreeList<T, __keyValue, FREELIST_USE_RAW>
{
//#ifdef _TEST
//	friend void AllocFreeTest();
//	friend void AllocFreeTest2();
//	friend void AllocFreeTest3();
//	friend struct stInfo;
//#endif
	friend class CPacket;
public:
	//-----------------------------------------------------------
	// 에러 종류
	//-----------------------------------------------------------
	enum enFreeListError
	{
		enFine = 0,
		enUnder,
		enOver,
		enNullptr,
		enDoubleFree,
	};
	//-----------------------------------------------------------
	// Info: 생성자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	CLockFreeFreeList() :_top(nullptr), _size(0)
	{
		//----------------------------------------------
		// 64비트가 아니면 생성을 막자
		//----------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not x86-64!!\n");
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 파괴자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CLockFreeFreeList()
	{
		node* n;
		while (((uint64_t)_top & FREELIST_BIT_MASK))
		{
			n = ((node*)((uint64_t)_top & FREELIST_BIT_MASK))->next;
			free((node*)((uint64_t)_top & FREELIST_BIT_MASK));
			_top = n;
		}
	}
	//-----------------------------------------------------------
	// Info: 비우기, 내부 노드들 비우고 초기화
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	void Clear()
	{
		node* n;
		while (((uint64_t)_top & FREELIST_BIT_MASK))
		{
			n = ((node*)((uint64_t)_top & FREELIST_BIT_MASK))->next;
			free((node*)((uint64_t)_top & FREELIST_BIT_MASK));
			_top = n;
		}
		_top = nullptr;
		_size = 0;
	}
#pragma warning(push)
#pragma warning(disable: 6011)
	//-----------------------------------------------------------
	// Info: Alloc (new, delete 오버로딩용, 생성자 소멸자 호출 X)
	// Parameter: -
	// Return: 데이터를 쓸 수 있는 포인터 (T*)
	//-----------------------------------------------------------
	T* Alloc()
	{
		//-----------------------------------------------------------
		// CAS를 활용하여 안전하게 만들기
		// 카운터를 올리고 탑만 변경하기
		//-----------------------------------------------------------
		node* t;
		node* nextTop;
		node* ret;
		uint64_t counter;
		do
		{
			t = _top;
			ret = (node*)((uint64_t)t & FREELIST_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{	// 새로 만들면 탈출
				ret = (node*)malloc(sizeof(node));
				ret->next = (node*)UNIQUE;			//언더플로 세팅
				ret->overcheck = UNIQUE;	//오버플로 세팅
				return &ret->data;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> FREELIST_SHIFT_BIT) + 1;
			nextTop = (node*)((uint64_t)(ret->next) | (counter << FREELIST_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*) &_top, nextTop, t) != t);
		//-----------------------------------------------------
		// 풀에서 획득 성공 시 사이즈 내림
		// 이제 노드는 내것
		//-----------------------------------------------------
		_InterlockedDecrement(&_size);
		ret->next = (node*)UNIQUE;			//언더플로 세팅
		ret->overcheck = UNIQUE;	//오버플로 세팅
		return &ret->data;
	}
	//-----------------------------------------------------------
	// Info: Free (메모리 반환)
	// Parameter: void* pObject
	// Return: 성공시 0, 실패시 에러코드 반환
	// enUnder = 1
	// enOver = 2
	// enNullptr = 3
	// enDoubleFree = 4
	//-----------------------------------------------------------
	int Free(T* pObject)
	{
		//-----------------------------------------------------
		// 널 체크
		//-----------------------------------------------------
		if (pObject == nullptr)
			return enNullptr;
		//-----------------------------------------------------
		// node* 타입으로 변경하기
		//-----------------------------------------------------
		node* pRet = (node*)((char*)pObject - sizeof(uint64_t));
		//------------------------------------------------------
		// 노드의 유효성 검사 (여기 들어오는 것이 맞는지)
		// . 유효하지 않으면 free한다.
		// . 해제해도 풀 외부에서 온 값이므로 풀 내부에서 참조하는
		//   값이 아니므로 괜찮다.
		//------------------------------------------------------
		/* 1. 언더플로 체크 */
		if ((uint64_t)(pRet->next) != UNIQUE)
		{	
			/* 1 - 1.이중 해제인지 한번 확인*/
			if (pRet->overcheck == FREELIST_RELEASE)
				return enDoubleFree;	// 해제하면 안됨. 
			// 해제해도 됨. 외부에서 온거라서 내부에서 참조하지 않음
			// 해제하다 잘못쓴거라 뻑나면 더 좋음
			free(pRet);
			return enUnder;
		}
		/* 2. 오버플로 체크 */
		if (pRet->overcheck != UNIQUE)
		{	
			free(pRet);
			return enOver;
		}
		//-----------------------------------------------------
		// 무조건 넣을거니까 (위에 못넣는 경우는 배제 했으므로)
		// 미리 오버플로 릴리즈로 세팅하자
		// 
		// 생각해 보니 인터락은 1회로 충분함.
		// 오히려 매번 올리는게 불필요
		//-----------------------------------------------------
		/* 3. 락프리 삽입 */
		node* t;
		node* newTop;
		uint64_t counter;

		//-----------------------------------------------------
		// 노드 해제표시 및 세팅 (지금은 내꺼)
		//-----------------------------------------------------
		pRet->overcheck = FREELIST_RELEASE;
		do
		{
			t = _top;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> FREELIST_SHIFT_BIT) + 1;
			newTop = (node*)((uint64_t)pRet | (counter << FREELIST_SHIFT_BIT));
			pRet->next = (node*)((uint64_t)t & FREELIST_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*) &_top, newTop, t) != t);
		_InterlockedIncrement(&_size);
		//-----------------------------------------------------
		// _top 이 pRet + counter값으로 바뀌었다. (삽입 성공)
		// 이제 노드는 모두의 것
		//-----------------------------------------------------
		return 0;
	}
#pragma warning(pop)
	//-----------------------------------------------------------
	// Info: 사이즈를 얻음, 멀티스레드 환경에서 참고용 값이다.
	// Parameter: -
	// Return: 현재 측정된 사이즈
	//-----------------------------------------------------------
	int size()
	{
		return _size;
	}
private:
	//-----------------------------------------------------------
	// 복사 생성자 사용 불가
	//-----------------------------------------------------------
	CLockFreeFreeList(const CLockFreeFreeList& ref) = delete;
	//-----------------------------------------------------------
	// 대입 연산자 사용 불가
	//-----------------------------------------------------------
	CLockFreeFreeList& operator=(const CLockFreeFreeList& ref) = delete;
	//-----------------------------------------------------------
	// 오버플로우 체크 시 비교할 값
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = FREELIST_OVER_CODE ^ __keyValue;
	//-----------------------------------------------------------
	// 프리리스트 내부에서 관리하는 구조체입니다.
	// node: [underflowcheck(next)|	 T	| overcheck]
	//							  ^
	//							  +----  상대에게 반환하는 지점
	//-----------------------------------------------------------
	struct node
	{
		node* next;
		T data;
		uint64_t overcheck;
	};

	node* _top;			// 스택 천장
	long _size;			// 풀 내부의 남은 노드 개수
};


//-----------------------------------------------------------
// LockFree 생성자, 소멸자 노드당 1회 호출버전
//-----------------------------------------------------------
template <typename T, int __keyValue>
class CLockFreeFreeList<T, __keyValue, FREELIST_USE_CALLONCE>
{
//#ifdef _TEST
//	friend void AllocFreeTest();
//	friend void AllocFreeTest2();
//	friend void AllocFreeTest3();
//	friend struct stInfo;
//#endif
	friend class CPacket;
public:
	//-----------------------------------------------------------
	// 에러 종류
	//-----------------------------------------------------------
	enum enFreeListError
	{
		enFine = 0,
		enUnder,
		enOver,
		enNullptr,
		enDoubleFree,
	};
	//-----------------------------------------------------------
	// Info: 생성자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	CLockFreeFreeList() :_top(nullptr), _size(0)
	{
		//----------------------------------------------
		// 64비트가 아니면 생성을 막자
		//----------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not x86-64!!\n");
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 파괴자 (데이터에 대한 소멸자 호출, 노드를 그냥 반환해서)
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CLockFreeFreeList()
	{
		node* n;
		node* deleteNode;
		while (((uint64_t)_top & FREELIST_BIT_MASK))
		{
			n = ((node*)((uint64_t)_top & FREELIST_BIT_MASK))->next;
			deleteNode = (node*)((uint64_t)_top & FREELIST_BIT_MASK);
			deleteNode->data.~T();
			free(deleteNode);
			_top = n;
		}
	}
	//-----------------------------------------------------------
	// Info: 비우기, 내부 노드들 비우고 초기화
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	void Clear()
	{
		node* n;
		node* deleteNode;
		while (((uint64_t)_top & FREELIST_BIT_MASK))
		{
			n = ((node*)((uint64_t)_top & FREELIST_BIT_MASK))->next;
			deleteNode = (node*)((uint64_t)_top & FREELIST_BIT_MASK);
			deleteNode->data.~T();
			free(deleteNode);
			_top = n;
		}
		_top = nullptr;
		_size = 0;
	}
#pragma warning(push)
#pragma warning(disable: 6011)
	//-----------------------------------------------------------
	// Info: Alloc (최초 1회 노드생성 시 데이터에 대한 생성자 호출)
	// Parameter: -
	// Return: 데이터를 쓸 수 있는 포인터 (T*)
	//-----------------------------------------------------------
	template <typename... Args>
	T* Alloc(Args... args)
	{
		//-----------------------------------------------------------
		// CAS를 활용하여 안전하게 만들기
		// 카운터를 올리고 탑만 변경하기
		//-----------------------------------------------------------
		node* t;
		node* nextTop;
		node* ret;
		uint64_t counter;
		do
		{
			t = _top;
			ret = (node*)((uint64_t)t & FREELIST_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{	// 새로 만들면 탈출
				ret = (node*)malloc(sizeof(node));
				ret->next = (node*)UNIQUE;		// 언더플로 세팅
				ret->overcheck = UNIQUE;		// 오버플로 세팅
				new (&ret->data) T(args...);	// 최초 1회 생성 시 생성자 호출
				return &ret->data;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> FREELIST_SHIFT_BIT) + 1;
			nextTop = (node*)((uint64_t)(ret->next) | (counter << FREELIST_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, nextTop, t) != t);
		//-----------------------------------------------------
		// 풀에서 획득 성공 시 사이즈 내림
		//-----------------------------------------------------
		_InterlockedDecrement(&_size);
		ret->next = (node*)UNIQUE;			//언더플로 세팅
		ret->overcheck = UNIQUE;	//오버플로 세팅
		return &ret->data;
	}
	//-----------------------------------------------------------
	// Info: Free (메모리 반환, 실패할 경우만 소멸자 호출, 그 외에는
	//			   이 프리리스트의 소멸자가 호출함)
	// Parameter: void* pObject
	// Return: 성공시 0, 실패시 에러코드 반환
	// enUnder = 1
	// enOver = 2
	// enNullptr = 3
	// enDoubleFree = 4
	//-----------------------------------------------------------
	int Free(T* pObject)
	{
		//-----------------------------------------------------
		// 널 체크
		//-----------------------------------------------------
		if (pObject == nullptr)
			return enNullptr;
		//-----------------------------------------------------
		// node* 타입으로 변경하기
		//-----------------------------------------------------
		node* pRet = (node*)((char*)pObject - sizeof(uint64_t));
		//------------------------------------------------------
		// 노드의 유효성 검사 (여기 들어오는 것이 맞는지)
		// . 유효하지 않으면 free한다.
		// . 해제해도 풀 외부에서 온 값이므로 풀 내부에서 참조하는
		//   값이 아니므로 괜찮다.
		//------------------------------------------------------
		/* 1. 언더플로 체크 */
		if ((uint64_t)(pRet->next) != UNIQUE)
		{
			/* 1 - 1.이중 해제인지 한번 확인*/
			if (pRet->overcheck == FREELIST_RELEASE)
				return enDoubleFree;	// 해제하면 안됨. 
			// 해제해도 됨. 외부에서 온거라서 내부에서 참조하지 않음
			// 해제하다 잘못쓴거라 뻑나면 더 좋음
			pRet->data.~T();
			free(pRet);
			return enUnder;
		}
		/* 2. 오버플로 체크 */
		if (pRet->overcheck != UNIQUE)
		{
			pRet->data.~T();
			free(pRet);
			return enOver;
		}
		//-----------------------------------------------------
		// 무조건 넣을거니까 (위에 못넣는 경우는 배제 했으므로)
		// 미리 오버플로 릴리즈로 세팅하자
		// 
		// 생각해 보니 인터락은 1회로 충분함.
		// 오히려 매번 올리는게 불필요 (pRet을 넣는게 목적)
		//-----------------------------------------------------
		/* 3. 락프리 삽입 */
		node* t;
		node* newTop;
		uint64_t counter;
		pRet->overcheck = FREELIST_RELEASE;

		do
		{
			t = _top;
			//-----------------------------------------------------
			// 카운터 뽑아서 올리기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> FREELIST_SHIFT_BIT) + 1;

			newTop = (node*)((uint64_t)pRet | (counter << FREELIST_SHIFT_BIT));
			pRet->next = (node*)((uint64_t)t & FREELIST_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, newTop, t) != t);
		_InterlockedIncrement(&_size);
		//-----------------------------------------------------
		// _top 이 pRet + counter값으로 바뀌었다. (삽입 성공)
		//-----------------------------------------------------
		return 0;
	}//-----------------------------------------------------------
	// Info: 사이즈를 얻음, 멀티스레드 환경에서 참고용 값이다.
	// Parameter: -
	// Return: 현재 측정된 사이즈
	//-----------------------------------------------------------
	int size()
	{
		return _size;
	}
#pragma warning(pop)
private:
	//-----------------------------------------------------------
	// 복사 생성자 사용 불가
	//-----------------------------------------------------------
	CLockFreeFreeList(const CLockFreeFreeList& ref) = delete;
	//-----------------------------------------------------------
	// 대입 연산자 사용 불가
	//-----------------------------------------------------------
	CLockFreeFreeList& operator=(const CLockFreeFreeList& ref) = delete;
	//-----------------------------------------------------------
	// 오버플로우 체크 시 비교할 값
	//-----------------------------------------------------------
	static constexpr size_t UNIQUE = FREELIST_OVER_CODE ^ __keyValue;
	//-----------------------------------------------------------
	// 프리리스트 내부에서 관리하는 구조체입니다.
	// node: [underflowcheck(next)|	 T	| overcheck]
	//							  ^
	//							  +----  상대에게 반환하는 지점
	//-----------------------------------------------------------
	struct node
	{
		node* next;
		T data;
		size_t overcheck;
	};

	node* _top;			// 스택 천장
	long _size;			// 풀 내부의 남은 노드 개수
};

//-----------------------------------------------------------
// LockFree 생성자, 소멸자 매 회 호출버전
//-----------------------------------------------------------
template <typename T, int __keyValue>
class CLockFreeFreeList<T, __keyValue, FREELIST_USE_NORMAL>
{
//#ifdef _TEST
//	friend void AllocFreeTest();
//	friend void AllocFreeTest2();
//	friend void AllocFreeTest3();
//	friend struct stInfo;
//#endif
public:
	//-----------------------------------------------------------
	// 에러 종류
	//-----------------------------------------------------------
	enum enFreeListError
	{
		enFine = 0,
		enUnder,
		enOver,
		enNullptr,
		enDoubleFree,
	};
	//-----------------------------------------------------------
	// Info: 생성자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	CLockFreeFreeList() :_top(nullptr), _size(0)
	{
		//----------------------------------------------
		// 64비트가 아니면 생성을 막자
		//----------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not x86-64!!\n");
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 파괴자 (여기는 소멸자 호출 필요X, 노드 Free에서 함)
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CLockFreeFreeList()
	{
		node* n;
		while (((uint64_t)_top & FREELIST_BIT_MASK))
		{
			n = ((node*)((uint64_t)_top & FREELIST_BIT_MASK))->next;
			free((node*)((uint64_t)_top & FREELIST_BIT_MASK));
			_top = n;
		}
	}
	//-----------------------------------------------------------
	// Info: 비우기, 내부 노드들 비우고 초기화
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	void Clear()
	{
		node* n;
		while (((uint64_t)_top & FREELIST_BIT_MASK))
		{
			n = ((node*)((uint64_t)_top & FREELIST_BIT_MASK))->next;
			free((node*)((uint64_t)_top & FREELIST_BIT_MASK));
			_top = n;
		}
		_top = nullptr;
		_size = 0;
	}
#pragma warning(push)
#pragma warning(disable: 6011)
	//-----------------------------------------------------------
	// Info: Alloc (매 회 데이터에 대한 생성자 호출)
	// Parameter: -
	// Return: 데이터를 쓸 수 있는 포인터 (T*)
	//-----------------------------------------------------------
	template <typename... Args>
	T* Alloc(Args... args)
	{
		//-----------------------------------------------------------
		// CAS를 활용하여 안전하게 만들기
		// 카운터를 올리고 탑만 변경하기
		//-----------------------------------------------------------
		node* t;
		node* nextTop;
		node* ret;
		uint64_t counter;
		do
		{
			t = _top;
			ret = (node*)((uint64_t)t & FREELIST_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{	// 새로 만들면 탈출
				ret = (node*)malloc(sizeof(node));
				ret->next = (node*)UNIQUE;		// 언더플로 세팅
				ret->overcheck = UNIQUE;		// 오버플로 세팅
				new (&ret->data) T(args...);	// 생성 시 생성자 호출
				return &ret->data;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> FREELIST_SHIFT_BIT) + 1;

			nextTop = (node*)((uint64_t)(ret->next) | (counter << FREELIST_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, nextTop, t) != t);
		//-----------------------------------------------------
		// 풀에서 획득 성공 시 사이즈 내림
		//-----------------------------------------------------
		_InterlockedDecrement(&_size);
		ret->next = (node*)UNIQUE;		// 언더플로 세팅
		ret->overcheck = UNIQUE;		// 오버플로 세팅
		new (&ret->data) T(args...);	// 생성 시 생성자 호출
		return &ret->data;
	}
	//-----------------------------------------------------------
	// Info: Free (메모리 반환, 실패할 경우만 소멸자 호출, 그 외에는
	//			   이 프리리스트의 소멸자가 호출함)
	// Parameter: void* pObject
	// Return: 성공시 0, 실패시 에러코드 반환
	// enUnder = 1
	// enOver = 2
	// enNullptr = 3
	// enDoubleFree = 4
	//-----------------------------------------------------------
	int Free(T* pObject)
	{
		//-----------------------------------------------------
		// 널 체크
		//-----------------------------------------------------
		if (pObject == nullptr)
			return enNullptr;
		//-----------------------------------------------------
		// node* 타입으로 변경하기
		//-----------------------------------------------------
		node* pRet = (node*)((char*)pObject - sizeof(uint64_t));
		//------------------------------------------------------
		// 노드의 유효성 검사 (여기 들어오는 것이 맞는지)
		// . 유효하지 않으면 free한다.
		// . 해제해도 풀 외부에서 온 값이므로 풀 내부에서 참조하는
		//   값이 아니므로 괜찮다.
		//------------------------------------------------------
		/* 1. 언더플로 체크 */
		if ((uint64_t)(pRet->next) != UNIQUE)
		{
			/* 1 - 1.이중 해제인지 한번 확인*/
			if (pRet->overcheck == FREELIST_RELEASE)
				return enDoubleFree;	// 해제하면 안됨. 
			// 해제해도 됨. 외부에서 온거라서 내부에서 참조하지 않음
			// 해제하다 잘못쓴거라 뻑나면 더 좋음
			pRet->data.~T();
			free(pRet);
			return enUnder;
		}
		/* 2. 오버플로 체크 */
		if (pRet->overcheck != UNIQUE)
		{
			pRet->data.~T();
			free(pRet);
			return enOver;
		}
		//-----------------------------------------------------
		// 무조건 넣을거니까 (위에 못넣는 경우는 배제 했으므로)
		// 미리 오버플로 릴리즈로 세팅하자
		// 
		// 생각해 보니 인터락은 1회로 충분함.
		// 오히려 매번 올리는게 불필요 (pRet을 넣는게 목적)
		//-----------------------------------------------------
		/* 3. 락프리 삽입 */
		node* t;
		node* newTop;
		uint64_t counter;
		pRet->overcheck = FREELIST_RELEASE;
		pRet->data.~T();	// 소멸자 호출해주기

		do
		{
			t = _top;
			//-----------------------------------------------------
			// 카운터 뽑아서 올리기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> FREELIST_SHIFT_BIT) + 1;

			newTop = (node*)((uint64_t)pRet | (counter << FREELIST_SHIFT_BIT));
			pRet->next = (node*)((uint64_t)t & FREELIST_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, newTop, t) != t);
		_InterlockedIncrement(&_size);
		//-----------------------------------------------------
		// _top 이 pRet + counter값으로 바뀌었다. (삽입 성공)
		//-----------------------------------------------------
		return 0;
	}//-----------------------------------------------------------
	// Info: 사이즈를 얻음, 멀티스레드 환경에서 참고용 값이다.
	// Parameter: -
	// Return: 현재 측정된 사이즈
	//-----------------------------------------------------------
	int size()
	{
		return _size;
	}
#pragma warning(pop)
private:
	CLockFreeFreeList(const CLockFreeFreeList& ref) = delete;
	CLockFreeFreeList& operator=(const CLockFreeFreeList& ref) = delete;
	static constexpr size_t UNIQUE = FREELIST_OVER_CODE ^ __keyValue;
	//-----------------------------------------------------------
	// 프리리스트 내부에서 관리하는 구조체입니다.
	// node: [underflowcheck|	T	| 관리 정보]
	//						^
	//						+----  상대에게 반환하는 지점
	// 5.29
	// - securitycode_under	: 언더 플로 감지
	// - T data				: 데이터
	// - node* next			: 가리키는 다음 노드
	// - securitycode_over	: 오버플로 감지
	//-----------------------------------------------------------
	struct node
	{
		node* next;
		T data;
		size_t overcheck;
	};

	node* _top;			// 스택 천장
	long _size;			// 풀 내부의 남은 노드 개수
};

#endif