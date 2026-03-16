#ifndef __TLS_OBJECT_POOL_INTRUSIVE_LIST_H__
#define __TLS_OBJECT_POOL_INTRUSIVE_LIST_H__

/////////////////////////////////////////////////////////////////
// TLS_OBJECT_POOL 친습성 리스트 V2
// < V2: 스왑 기능>
// . 나의 할당 청크가 비었을 때 바로 청크를 새로 가져오는 것이 아닌,
// . 나의 반환 청크의 사이즈를 확인 후 0보다 크면 스왑
//   => 이것을 통해 청크를 가져오는 횟수를 줄이고
//   => 해제될 때 내 캐시에 남아있을 확률이 매우 높으므로 재사용성이 좋아짐
// 
// . 청크는 노드 리스트
// . 풀은 청크를 관리
// . 청크는 다쓰면 새 청크를 받음
// . 청크의 해제는 마지막으로 노드를 반환한 녀석이 진행
//   (청크를 풀에 반환)
// 
// 
// RAW : 생성자 호출 / 소멸자 호출 안함
// CALLONCE : 생성자 청크 생성 단계에서 호출 / 소멸자 청크 지우는 단계에서 호출
// NORMAL : 생성자 노드 단계에서 호출 / 소멸자 노드 반환 단계에서 호출
/////////////////////////////////////////////////////////////////

// #define _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H

#ifdef _DEBUG
//#define _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <intrin.h>
#include <stdlib.h>
#include <new>
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
#include <unordered_set>
#endif


// 청크 기본 노드수
#define TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK	512
// 락프리 청크 공용 풀 청크 최대 갯수
#define TLS_OBJECT_POOL_MAX_ALLOC_CHUNKNUM			3000
#define TLS_OBJECT_POOL_MAX_RELEASE_CHUNKNUM		3000

// 카운터로 사용할 비트 수 
#define TLS_OBJECT_POOL_COUNTER_BIT	17
// 시프트 할 횟수
#define TLS_OBJECT_POOL_SHIFT_BIT	47
// 마스크
#define TLS_OBJECT_POOL_BIT_MASK	0x00007FFF'FFFFFFFF

// 0 = Raw (오버로딩에 사용)
#define TLS_OBJECTPOOL_USE_RAW		0
// 1 = 생성자, 소멸자를 새로운 노드가 생길 때만 호출
#define TLS_OBJECTPOOL_USE_CALLONCE	1
// 2 = 생성자, 소멸자를 항상 호출
#define TLS_OBJECTPOOL_USE_NORMAL	2
// 오버플로 체킹
#define TLS_OBJECTPOOL_OVER_CODE	0xFDFDFDFD'FDFDFDFD
// 언더플로 체킹
#define TLS_OBJECTPOOL_NODE_NOTUSE	0x9B9B9B9B'9B9B9B9B




template <typename T, int __keyValue, int __whatFreeToUse>
class CTlsObjectPool;

//---------------------------------------------------------------
// Intrusive
// 디버그모드 : #define _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
// RAW 버전 (생성자를 호출하지 않음)
//---------------------------------------------------------------
template <typename T, int __keyValue>
class CTlsObjectPool<typename T, __keyValue, TLS_OBJECTPOOL_USE_RAW>
{
public:
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
	struct stChunk;
	struct stNode;
	struct stDebugInfo
	{
		// 청크 만들 때 등록, 청크 해제 때 해제
		SRWLOCK chunksSetLock;
		std::unordered_set<stChunk*> chunksSet;
		// 노드 만들 때 등록, 노드 반환 시 해제
		SRWLOCK nodesSetLock;
		std::unordered_set<stNode*> nodesSet;

		void RegisterChunk(stChunk* pChunk)
		{
			AcquireSRWLockExclusive(&chunksSetLock);
			auto isTrue = chunksSet.insert(pChunk);
			if (isTrue.second == false)
				__debugbreak();
			ReleaseSRWLockExclusive(&chunksSetLock);
		}
		void CancelChunk(stChunk* pChunk)
		{
			AcquireSRWLockExclusive(&chunksSetLock);
			int success = (int)chunksSet.erase(pChunk);
			if (success != 1)
				__debugbreak();
			ReleaseSRWLockExclusive(&chunksSetLock);
		}
		void RegisterNode(stNode* pNode)
		{
			AcquireSRWLockExclusive(&nodesSetLock);
			auto isTrue = nodesSet.insert(pNode);
			if (isTrue.second == false)
				__debugbreak();
			ReleaseSRWLockExclusive(&nodesSetLock);
		}
		void CancelNode(stNode* pNode)
		{
			AcquireSRWLockExclusive(&nodesSetLock);
			int success = (int)nodesSet.erase(pNode);
			if (success != 1)
				__debugbreak();
			ReleaseSRWLockExclusive(&nodesSetLock);
		}
		stDebugInfo():chunksSetLock(SRWLOCK_INIT), nodesSetLock(SRWLOCK_INIT)
		{
			allocCnt = 0;
			releaseCnt = 0;
			aquireChunkCnt = 0;
			releaseChunkCnt = 0;
			nodeMakeCnt = 0;
			nodeRemoveCnt = 0;
			chunkMakeCnt = 0;
			chunkRemoveCnt = 0;
			acquireMaxRaceLoopCnt = 0;
			releaseMaxRaceLoopCnt = 0;
			totalAcquireRaceLoopCnt = 0;
			totalReleaseRaceLoopCnt = 0;
		}
		uint64_t allocCnt;
		uint64_t releaseCnt;
		uint64_t aquireChunkCnt;
		uint64_t releaseChunkCnt;
		uint64_t nodeMakeCnt;
		uint64_t nodeRemoveCnt;
		uint64_t chunkMakeCnt;
		uint64_t chunkRemoveCnt;
		uint64_t acquireMaxRaceLoopCnt;
		uint64_t releaseMaxRaceLoopCnt;
		uint64_t totalAcquireRaceLoopCnt;
		uint64_t totalReleaseRaceLoopCnt;
	};
#endif
	//-----------------------------------------------------------
	// 오류 목록
	//-----------------------------------------------------------
	enum enError
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
	CTlsObjectPool():_allocTop(nullptr), _allocChunkNum(0), _releaseTop(nullptr), _releaseChunkNum(0),
		_createAllocChunkNum(0), _createReleaseChunkNum(0)
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

		//-------------------------------------------------------
		// 동적 TLS 사용을 위해 인덱스 획득
		//-------------------------------------------------------
		_tlsIndex_pStChunks = TlsAlloc();
		if (_tlsIndex_pStChunks == TLS_OUT_OF_INDEXES)
		{
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 소멸자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CTlsObjectPool()
	{
		//-------------------------------------------------------
		// 락프리 공용 청크풀 해제
		//-------------------------------------------------------
		stChunk* pChunk;
		while (((uint64_t)_allocTop & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = AAcquireChunk();
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			DebugInfo.CancelChunk(pChunk);
#endif
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();
			delete pChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		}
		while (((uint64_t)_releaseTop & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = RAcquireChunk();
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			DebugInfo.CancelChunk(pChunk);
#endif
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();
			delete pChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		}
		//-------------------------------------------------------
		// 동적 TLS 반환
		//-------------------------------------------------------
		TlsFree(_tlsIndex_pStChunks);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (DebugInfo.allocCnt != DebugInfo.releaseCnt)
			__debugbreak();
		if (DebugInfo.aquireChunkCnt != DebugInfo.releaseChunkCnt)
			__debugbreak();

		if (DebugInfo.chunksSet.size() != 0)
			__debugbreak();
		if (DebugInfo.nodesSet.size() != 0)
			__debugbreak();
		if (DebugInfo.chunkMakeCnt != DebugInfo.chunkRemoveCnt)
			__debugbreak();
		if (DebugInfo.nodeMakeCnt != DebugInfo.nodeRemoveCnt)
			__debugbreak();
#endif
	}
	
	//-----------------------------------------------------------
	// Info: 풀에서 하나를 획득, 싱글스레드로 일어남
	//       ** 다쓰면 새로 청크 얻어옴.
	// Parameter: -
	// Return: T* (획득 실패시 nullptr)
	//-----------------------------------------------------------
	T* Alloc()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*) &DebugInfo.allocCnt);
#endif
		//-------------------------------------------------------
		// tls에서 내 청크 가져옴
		//-------------------------------------------------------
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		stChunk* pMyAllocChunk;
		stChunk* pMyReleaseChunk;
		BOOL retTls;
		if (pMyChunks == nullptr)
		{
			pMyChunks = (stChunks*)malloc(sizeof(stChunks));
			//---------------------------------------------------
			// 최초 1회 시행
			//---------------------------------------------------
			pMyAllocChunk = AAcquireChunk();	// 새거 생성
			pMyReleaseChunk = RAcquireChunk();	// 청크 껍데기 생성
#pragma warning(push)
#pragma warning(disable: 6011)
			//pMyChunks는 null이면 꺼지게
			pMyChunks->pMyAlloc = pMyAllocChunk;
			pMyChunks->pMyRelease = pMyReleaseChunk;
			retTls = TlsSetValue(_tlsIndex_pStChunks, (void*)pMyChunks);
			if (retTls == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
#pragma warning(pop)
		}
		else
		{
			pMyAllocChunk = pMyChunks->pMyAlloc;
		}

		//-------------------------------------------------------
		// 노드 획득
		//-------------------------------------------------------
		stNode* pRetNode = pMyAllocChunk->pop();
		pRetNode->next = (stNode*)UNIQUE;
		pRetNode->overcheck = UNIQUE;

		//-------------------------------------------------------
		// leftnode가 0이면 청크 새로 받음
		//-------------------------------------------------------
		if (pMyAllocChunk->size == 0)
		{
			pMyReleaseChunk = pMyChunks->pMyRelease;
			if(pMyReleaseChunk->size == 0)
			{
				//-----------------------------------------------
				// 내 반환청크풀 사이즈가 0이면 반환 후 새 청크 받아옴
				//-----------------------------------------------
				RReleaseChunk(pMyAllocChunk);
				pMyAllocChunk = AAcquireChunk();
				pMyChunks->pMyAlloc = pMyAllocChunk;
			}
			else
			{
				//-----------------------------------------------
				// 내 반환 청크풀에 조금이라도 있으면 이거 사용
				//-----------------------------------------------
				stChunk* temp = pMyChunks->pMyAlloc;
				pMyChunks->pMyAlloc = pMyChunks->pMyRelease;
				pMyChunks->pMyRelease = temp;
			}
		}

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 노드 사용
		DebugInfo.RegisterNode(pRetNode);
#endif
		return &pRetNode->data;
	}

	//-----------------------------------------------------------
	// Info: 정확히는 청크에 사용이 끝났다고 알림,
	//       락프리풀에 반환때문에 풀에서 접근이 필요함
	// Parameter: T*
	// Return: int
	//-----------------------------------------------------------
	int Free(T* pObject)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseCnt);
#endif
		//-----------------------------------------------------
		// 널 체크
		//-----------------------------------------------------
		if (pObject == nullptr)
			return enNullptr;
		//-----------------------------------------------------
		// node* 타입으로 변경하기
		//-----------------------------------------------------
		stNode* pRet = (stNode*)((char*)pObject - sizeof(uint64_t));
		//------------------------------------------------------
		// 노드의 유효성 검사 (여기 들어오는 것이 맞는지)
		// . 유효하지 않으면 free한다.
		// . 해제해도 풀 외부에서 온 값이므로 풀 내부에서 참조하는
		//   값이 아니므로 괜찮다.
		//------------------------------------------------------
		
		// 언더플로 체크
		if ((uint64_t)pRet->next != UNIQUE)
		{
			return enUnder;
		}
		// 오버플로 체크
		if (pRet->overcheck != UNIQUE)
		{
			// 이중해제 확인
			if (pRet->overcheck == TLS_OBJECTPOOL_NODE_NOTUSE)
				return enDoubleFree;	// 해제하면 안됨.
			return enOver;
		}

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 노드 사용 끝 (해제)
		DebugInfo.CancelNode(pRet);
#endif
		//------------------------------------------------------
		// 해제 : RAW버전
		//------------------------------------------------------
		// 소멸자 호출X
		pRet->overcheck = TLS_OBJECTPOOL_NODE_NOTUSE;
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		stChunk* pMyAllocChunk;
		stChunk* pMyReleaseChunk;
		if (pMyChunks == nullptr)
		{
			pMyChunks = (stChunks*)malloc(sizeof(stChunks));
			//---------------------------------------------------
			// 최초 1회 시행
			//---------------------------------------------------
			pMyAllocChunk = AAcquireChunk();	// 새거 생성
			pMyReleaseChunk = RAcquireChunk();	// 청크 껍데기 생성

#pragma warning(push)
#pragma warning(disable: 6011)	// pMyChunks의 nullptr 경고
			pMyChunks->pMyAlloc = pMyAllocChunk;
			pMyChunks->pMyRelease = pMyReleaseChunk;
			DWORD retTls = TlsSetValue(_tlsIndex_pStChunks, (void*)pMyChunks);
			if (retTls == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
#pragma warning(pop)
		}
		else
		{
			pMyReleaseChunk = pMyChunks->pMyRelease;
		}
		pMyReleaseChunk->push(pRet);

		//------------------------------------------------------
		// 해제된 개수 확인
		//------------------------------------------------------
		if (pMyReleaseChunk->size >= TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK)
		{
			pMyAllocChunk = pMyChunks->pMyAlloc;
			if(pMyAllocChunk->size == TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK)
			{
				AReleaseChunk(pMyReleaseChunk);
				pMyReleaseChunk = RAcquireChunk();
				pMyChunks->pMyRelease = pMyReleaseChunk;
			}
			else
			{
				//-----------------------------------------------
				// 내 할당 청크풀에 꽉찬 내거로 교체해줌
				//-----------------------------------------------
				stChunk* temp = pMyChunks->pMyAlloc;
				pMyChunks->pMyAlloc = pMyChunks->pMyRelease;
				pMyChunks->pMyRelease = temp;
			}
		}

		return enFine;
	}


	//-----------------------------------------------------------
	// Info: 스레드 종료시 호출
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	void ThreadRelease()
	{
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		if (pMyChunks == nullptr)
			return;
		stChunk* pMyAllocChunk = pMyChunks->pMyAlloc;
		stChunk* pMyReleaseChunk = pMyChunks->pMyRelease;

		//-------------------------------------------------------
		// 그냥 클리어해서 지우자
		//-------------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		DebugInfo.CancelChunk(pMyAllocChunk);
		DebugInfo.CancelChunk(pMyReleaseChunk);
		int size = pMyAllocChunk->size;
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size); 
		size = pMyReleaseChunk->size;
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
		_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		pMyAllocChunk->Clear();
		pMyReleaseChunk->Clear();
		delete pMyAllocChunk;
		delete pMyReleaseChunk;
		free(pMyChunks);

		TlsSetValue(_tlsIndex_pStChunks, nullptr);
	}
	//-----------------------------------------------------------
	// 공용 할당 풀 청크 수 획득
	//-----------------------------------------------------------
	int GetAllocChunkPoolSize()
	{
		return _allocChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 릴리즈 풀 껍데기 청크 수 획득
	//-----------------------------------------------------------
	int GetReleaseChunkPoolSize()
	{
		return _releaseChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 할당 풀 청크 생성수 획득
	//-----------------------------------------------------------
	int GetAllocChunkPoolCreateNum()
	{
		return _createAllocChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 릴리즈 풀 청크 생성수 획득
	//-----------------------------------------------------------
	int GetReleaseChunkPoolCreateNum()
	{
		return _createReleaseChunkNum;
	}
private:
	struct stChunk;
	//-----------------------------------------------------------
	// 노드 구조체
	//-----------------------------------------------------------
	struct stNode
	{
		stNode* next;
		T data;
		uint64_t overcheck;
	};
	//-----------------------------------------------------------
	// 청크 구조체
	// . 청크는 없으면 할당하고, 바로 버린다
	//-----------------------------------------------------------
	struct stChunk
	{
		stNode* top;
		long size;
		stChunk* next;
		stChunk() :next(0), top(0), size(0) {}
		//---------------------------------------------------
		// 노드 초기화해서 청크 기본개수 만큼 넣음
		//---------------------------------------------------
		void Set()
		{
			stNode* newNode;
			int num = TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK;
			while (num != 0)
			{
#pragma warning(push)
#pragma warning(disable: 6011)
				// newNOde null이면 뻑나게
				newNode = (stNode*)malloc(sizeof(stNode));
				newNode->next = top;
				newNode->overcheck = TLS_OBJECTPOOL_NODE_NOTUSE;
				top = newNode;
				num--;
#pragma warning(pop)
			}
			size = TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK;
		}
		//---------------------------------------------------
		// 노드를 비움
		//---------------------------------------------------
		void Clear()
		{
			stNode* pDelete;
			while (top != nullptr)
			{
				pDelete = top;
				top = top->next;
				free((void*)pDelete);
			}
			size = 0;
		}
		//---------------------------------------------------
		// 노드를 뽑음
		//---------------------------------------------------
		stNode* pop()
		{
			stNode* pRet;
			if (top != nullptr)
			{
				pRet = top;
				top = top->next;
				size--;
				return pRet;
			}
			return nullptr;
		}
		//---------------------------------------------------
		// 노드를 넣음
		//---------------------------------------------------
		void push(stNode* releaseNode)
		{
			releaseNode->next = top;
			top = releaseNode;
			size++;
		}
	};
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------
	stChunk* AAcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*) & DebugInfo.aquireChunkCnt);
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _allocTop;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{	
				_InterlockedIncrement(&_createAllocChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				if (loop > DebugInfo.acquireMaxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = new stChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				// 사용 청크표시
				DebugInfo.RegisterChunk(ret);
#endif
				ret->Set();

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeMakeCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_allocTop, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.acquireMaxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 사용 청크표시
		DebugInfo.RegisterChunk(ret);
#endif
		_InterlockedDecrement(&_allocChunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 반환, 모든 청크가 해제됬을 때만
	//-----------------------------------------------------------
	void AReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		// 해제 청크표시
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_allocChunkNum > TLS_OBJECT_POOL_MAX_ALLOC_CHUNKNUM)
		{
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------
			_InterlockedDecrement(&_createReleaseChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _allocTop;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
#pragma warning(push)
#pragma warning(disable: 6001)
			// pChunk는 내함수가 멀쩡하면 null이아님
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_allocTop, (void*)newTop, (void*)t) != t);
		

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.acquireMaxRaceLoopCnt)
		{
			_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
		}
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif

		_InterlockedIncrement(&_allocChunkNum);
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크껍데기를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------
	stChunk* RAcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _releaseTop;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createReleaseChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				if (loop > DebugInfo.releaseMaxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = new stChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				// 사용 청크 표시
				DebugInfo.RegisterChunk(ret);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_releaseTop, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.releaseMaxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 사용 청크 표시
		DebugInfo.RegisterChunk(ret);
#endif

		_InterlockedDecrement(&_releaseChunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 껍데기를 반환
	//-----------------------------------------------------------
	void RReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_releaseChunkNum > TLS_OBJECT_POOL_MAX_RELEASE_CHUNKNUM)
		{
			_InterlockedDecrement(&_createAllocChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _releaseTop;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
#pragma warning(push)
#pragma warning(disable: 6001)
			// pChunk는 내함수가 멀쩡하면 null이아님
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_releaseTop, (void*)newTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.releaseMaxRaceLoopCnt)
		{
			_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
		}
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif
		_InterlockedIncrement(&_releaseChunkNum);
	}
	//-----------------------------------------------------------
	// 풀의 고유한 값 (Free시 잘 왔는지 참조용)
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = TLS_OBJECTPOOL_OVER_CODE ^ __keyValue;

	//-----------------------------------------------------------
	// 스레드별 할당 청크
	//-----------------------------------------------------------
	struct stChunks
	{
		stChunk* pMyAlloc;
		stChunk* pMyRelease;
	};
	DWORD _tlsIndex_pStChunks;

	//-----------------------------------------------------------
	// 공용 청크 (꽉찬거) 담는 곳
	//-----------------------------------------------------------
	stChunk* _allocTop;
	long _allocChunkNum;
	//-----------------------------------------------------------
	// 공용 청크 (껍데기) 담는 곳
	//-----------------------------------------------------------
	stChunk* _releaseTop;
	long _releaseChunkNum;
	//-----------------------------------------------------------
	// 현재 생성된 청크 개수
	//-----------------------------------------------------------
	long _createAllocChunkNum;
	long _createReleaseChunkNum;

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
	stDebugInfo DebugInfo;
#endif
};

//---------------------------------------------------------------
// Intrusive
// 디버그모드 : #define _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
// CALL_ONCE버전, 생성자 1회 호출
//---------------------------------------------------------------
template <typename T, int __keyValue>
class CTlsObjectPool<typename T, __keyValue, TLS_OBJECTPOOL_USE_CALLONCE>
{
public:
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
	struct stChunk;
	struct stNode;
	struct stDebugInfo
	{
		// 청크 만들 때 등록, 청크 해제 때 해제
		SRWLOCK chunksSetLock;
		std::unordered_set<stChunk*> chunksSet;
		// 노드 만들 때 등록, 노드 반환 시 해제
		SRWLOCK nodesSetLock;
		std::unordered_set<stNode*> nodesSet;

		void RegisterChunk(stChunk* pChunk)
		{
			AcquireSRWLockExclusive(&chunksSetLock);
			auto isTrue = chunksSet.insert(pChunk);
			if (isTrue.second == false)
				__debugbreak();
			ReleaseSRWLockExclusive(&chunksSetLock);
		}
		void CancelChunk(stChunk* pChunk)
		{
			AcquireSRWLockExclusive(&chunksSetLock);
			int success = (int)chunksSet.erase(pChunk);
			if (success != 1)
				__debugbreak();
			ReleaseSRWLockExclusive(&chunksSetLock);
		}
		void RegisterNode(stNode* pNode)
		{
			AcquireSRWLockExclusive(&nodesSetLock);
			auto isTrue = nodesSet.insert(pNode);
			if (isTrue.second == false)
				__debugbreak();
			ReleaseSRWLockExclusive(&nodesSetLock);
		}
		void CancelNode(stNode* pNode)
		{
			AcquireSRWLockExclusive(&nodesSetLock);
			int success = (int)nodesSet.erase(pNode);
			if (success != 1)
				__debugbreak();
			ReleaseSRWLockExclusive(&nodesSetLock);
		}
		stDebugInfo() :chunksSetLock(SRWLOCK_INIT), nodesSetLock(SRWLOCK_INIT)
		{
			allocCnt = 0;
			releaseCnt = 0;
			aquireChunkCnt = 0;
			releaseChunkCnt = 0;
			nodeMakeCnt = 0;
			nodeRemoveCnt = 0;
			chunkMakeCnt = 0;
			chunkRemoveCnt = 0;
			acquireMaxRaceLoopCnt = 0;
			releaseMaxRaceLoopCnt = 0;
			totalAcquireRaceLoopCnt = 0;
			totalReleaseRaceLoopCnt = 0;
		}
		uint64_t allocCnt;
		uint64_t releaseCnt;
		uint64_t aquireChunkCnt;
		uint64_t releaseChunkCnt;
		uint64_t nodeMakeCnt;
		uint64_t nodeRemoveCnt;
		uint64_t chunkMakeCnt;
		uint64_t chunkRemoveCnt;
		uint64_t acquireMaxRaceLoopCnt;
		uint64_t releaseMaxRaceLoopCnt;
		uint64_t totalAcquireRaceLoopCnt;
		uint64_t totalReleaseRaceLoopCnt;
	};
#endif
	//-----------------------------------------------------------
	// 오류 목록
	//-----------------------------------------------------------
	enum enError
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
	CTlsObjectPool() :_allocTop(nullptr), _allocChunkNum(0), _releaseTop(nullptr), _releaseChunkNum(0),
		_createAllocChunkNum(0), _createReleaseChunkNum(0)
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

		//-------------------------------------------------------
		// 동적 TLS 사용을 위해 인덱스 획득
		//-------------------------------------------------------
		_tlsIndex_pStChunks = TlsAlloc();
		if (_tlsIndex_pStChunks == TLS_OUT_OF_INDEXES)
		{
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 소멸자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CTlsObjectPool()
	{
		//-------------------------------------------------------
		// 락프리 공용 청크풀 해제
		//-------------------------------------------------------
		stChunk* pChunk;
		while (((uint64_t)_allocTop & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = AAcquireChunk();
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			DebugInfo.CancelChunk(pChunk);
#endif
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();
			delete pChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		}
		while (((uint64_t)_releaseTop & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = RAcquireChunk();
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			DebugInfo.CancelChunk(pChunk);
#endif
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();
			delete pChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		}
		//-------------------------------------------------------
		// 동적 TLS 반환
		//-------------------------------------------------------
		TlsFree(_tlsIndex_pStChunks);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (DebugInfo.allocCnt != DebugInfo.releaseCnt)
			__debugbreak();
		if (DebugInfo.aquireChunkCnt != DebugInfo.releaseChunkCnt)
			__debugbreak();

		if (DebugInfo.chunksSet.size() != 0)
			__debugbreak();
		if (DebugInfo.nodesSet.size() != 0)
			__debugbreak();
		if (DebugInfo.chunkMakeCnt != DebugInfo.chunkRemoveCnt)
			__debugbreak();
		if (DebugInfo.nodeMakeCnt != DebugInfo.nodeRemoveCnt)
			__debugbreak();
#endif
	}

	//-----------------------------------------------------------
	// Info: 풀에서 하나를 획득, 싱글스레드로 일어남
	//       ** 다쓰면 새로 청크 얻어옴.
	// Parameter: -
	// Return: T* (획득 실패시 nullptr)
	//-----------------------------------------------------------
	T* Alloc()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.allocCnt);
#endif
		//-------------------------------------------------------
		// tls에서 내 청크 가져옴
		//-------------------------------------------------------
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		stChunk* pMyAllocChunk;
		stChunk* pMyReleaseChunk;
		BOOL retTls;
		if (pMyChunks == nullptr)
		{
			pMyChunks = (stChunks*)malloc(sizeof(stChunks));
			//---------------------------------------------------
			// 최초 1회 시행
			//---------------------------------------------------
			pMyAllocChunk = AAcquireChunk();	// 새거 생성
			pMyReleaseChunk = RAcquireChunk();	// 청크 껍데기 생성
#pragma warning(push)
#pragma warning(disable: 6011)
			// nullptr이면 뻑남
			pMyChunks->pMyAlloc = pMyAllocChunk;
			pMyChunks->pMyRelease = pMyReleaseChunk;
#pragma warning(pop)
			retTls = TlsSetValue(_tlsIndex_pStChunks, (void*)pMyChunks);
			if (retTls == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
		}
		else
		{
			pMyAllocChunk = pMyChunks->pMyAlloc;
		}

		//-------------------------------------------------------
		// 노드 획득
		//-------------------------------------------------------
		stNode* pRetNode = pMyAllocChunk->pop();
		pRetNode->next = (stNode*)UNIQUE;
		pRetNode->overcheck = UNIQUE;

		//-------------------------------------------------------
		// leftnode가 0이면 청크 새로 받음
		//-------------------------------------------------------
		if (pMyAllocChunk->size == 0)
		{
			pMyReleaseChunk = pMyChunks->pMyRelease;
			if (pMyReleaseChunk->size == 0)
			{
				//-----------------------------------------------
				// 내 반환청크풀 사이즈가 0이면 반환 후 새 청크 받아옴
				//-----------------------------------------------
				RReleaseChunk(pMyAllocChunk);
				pMyAllocChunk = AAcquireChunk();
				pMyChunks->pMyAlloc = pMyAllocChunk;
			}
			else
			{
				//-----------------------------------------------
				// 내 반환 청크풀에 조금이라도 있으면 이거 사용
				//-----------------------------------------------
				stChunk* temp = pMyChunks->pMyAlloc;
				pMyChunks->pMyAlloc = pMyChunks->pMyRelease;
				pMyChunks->pMyRelease = temp;
			}
		}

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 노드 사용
		DebugInfo.RegisterNode(pRetNode);
#endif
		return &pRetNode->data;
	}

	//-----------------------------------------------------------
	// Info: 정확히는 청크에 사용이 끝났다고 알림,
	//       락프리풀에 반환때문에 풀에서 접근이 필요함
	// Parameter: T*
	// Return: bool
	//-----------------------------------------------------------
	int Free(T* pObject)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseCnt);
#endif
		//-----------------------------------------------------
		// 널 체크
		//-----------------------------------------------------
		if (pObject == nullptr)
			return enNullptr;
		//-----------------------------------------------------
		// node* 타입으로 변경하기
		//-----------------------------------------------------
		stNode* pRet = (stNode*)((char*)pObject - sizeof(uint64_t));
		//------------------------------------------------------
		// 노드의 유효성 검사 (여기 들어오는 것이 맞는지)
		// . 유효하지 않으면 free한다.
		// . 해제해도 풀 외부에서 온 값이므로 풀 내부에서 참조하는
		//   값이 아니므로 괜찮다.
		//------------------------------------------------------

		// 언더플로 체크
		if ((uint64_t)pRet->next != UNIQUE)
		{
			return enUnder;
		}
		// 오버플로 체크
		if (pRet->overcheck != UNIQUE)
		{
			// 이중해제 확인
			if (pRet->overcheck == TLS_OBJECTPOOL_NODE_NOTUSE)
				return enDoubleFree;	// 해제하면 안됨.
			return enOver;
		}

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 노드 사용 끝 (해제)
		DebugInfo.CancelNode(pRet);
#endif
		//------------------------------------------------------
		// 해제 : RAW버전
		//------------------------------------------------------
		// 소멸자 호출X
		pRet->overcheck = TLS_OBJECTPOOL_NODE_NOTUSE;
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		stChunk* pMyAllocChunk;
		stChunk* pMyReleaseChunk;
		if (pMyChunks == nullptr)
		{
			pMyChunks = (stChunks*)malloc(sizeof(stChunks));
			//---------------------------------------------------
			// 최초 1회 시행
			//---------------------------------------------------
			pMyAllocChunk = AAcquireChunk();	// 새거 생성
			pMyReleaseChunk = RAcquireChunk();	// 청크 껍데기 생성

			pMyChunks->pMyAlloc = pMyAllocChunk;
			pMyChunks->pMyRelease = pMyReleaseChunk;
			DWORD retTls = TlsSetValue(_tlsIndex_pStChunks, (void*)pMyChunks);
			if (retTls == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
		}
		else
		{
			pMyReleaseChunk = pMyChunks->pMyRelease;
		}
		pMyReleaseChunk->push(pRet);

		//------------------------------------------------------
		// 해제된 개수 확인
		//------------------------------------------------------
		if (pMyReleaseChunk->size >= TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK)
		{
			pMyAllocChunk = pMyChunks->pMyAlloc;
			if (pMyAllocChunk->size == TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK)
			{
				AReleaseChunk(pMyReleaseChunk);
				pMyReleaseChunk = RAcquireChunk();
				pMyChunks->pMyRelease = pMyReleaseChunk;
			}
			else
			{
				//-----------------------------------------------
				// 내 할당 청크풀에 꽉찬 내거로 교체해줌
				//-----------------------------------------------
				stChunk* temp = pMyChunks->pMyAlloc;
				pMyChunks->pMyAlloc = pMyChunks->pMyRelease;
				pMyChunks->pMyRelease = temp;
			}
		}

		return enFine;
	}


	//-----------------------------------------------------------
	// Info: 스레드 종료시 호출
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	void ThreadRelease()
	{
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		if (pMyChunks == nullptr)
			return;
		stChunk* pMyAllocChunk = pMyChunks->pMyAlloc;
		stChunk* pMyReleaseChunk = pMyChunks->pMyRelease;

		//-------------------------------------------------------
		// 그냥 클리어해서 지우자
		//-------------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		DebugInfo.CancelChunk(pMyAllocChunk);
		DebugInfo.CancelChunk(pMyReleaseChunk);
		int size = pMyAllocChunk->size;
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
		size = pMyReleaseChunk->size;
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
		_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		pMyAllocChunk->Clear();
		pMyReleaseChunk->Clear();
		delete pMyAllocChunk;
		delete pMyReleaseChunk;
		free(pMyChunks);

		TlsSetValue(_tlsIndex_pStChunks, nullptr);
	}
	//-----------------------------------------------------------
	// 공용 할당 풀 청크 수 획득
	//-----------------------------------------------------------
	int GetAllocChunkPoolSize()
	{
		return _allocChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 릴리즈 풀 껍데기 청크 수 획득
	//-----------------------------------------------------------
	int GetReleaseChunkPoolSize()
	{
		return _releaseChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 할당 풀 청크 생성수 획득
	//-----------------------------------------------------------
	int GetAllocChunkPoolCreateNum()
	{
		return _createAllocChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 릴리즈 풀 청크 생성수 획득
	//-----------------------------------------------------------
	int GetReleaseChunkPoolCreateNum()
	{
		return _createReleaseChunkNum;
	}
private:
	struct stChunk;
	//-----------------------------------------------------------
	// 노드 구조체
	//-----------------------------------------------------------
	struct stNode
	{
		stNode* next;
		T data;
		uint64_t overcheck;
		stNode() :next(nullptr), data(), overcheck(0)
		{

		}
		~stNode()
		{

		}
	};
	//-----------------------------------------------------------
	// 청크 구조체
	// . 청크는 없으면 할당하고, 바로 버린다
	//-----------------------------------------------------------
	struct stChunk
	{
		stNode* top;
		long size;
		stChunk* next;
		stChunk() :next(0), top(0), size(0) {}
		//---------------------------------------------------
		// 노드 초기화해서 청크 기본개수 만큼 넣음
		//---------------------------------------------------
		void Set()
		{
			stNode* newNode;
			int num = TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK;
			while (num != 0)
			{
#pragma warning(push)
#pragma warning(disable: 6011)
				// newNOde null이면 뻑나게
				//-------------------------------------------
				// CallOnce버전이라 생성자 호출
				//-------------------------------------------
				newNode = new stNode();
				newNode->next = top;
				newNode->overcheck = TLS_OBJECTPOOL_NODE_NOTUSE;
				top = newNode;
				num--;
#pragma warning(pop)
			}
			size = TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK;
		}
		//---------------------------------------------------
		// 노드를 비움
		//---------------------------------------------------
		void Clear()
		{
			stNode* pDelete;
			while (top != nullptr)
			{
				pDelete = top;
				top = top->next;
				//-------------------------------------------
				// CALL_ONCE버전이라 소멸자 호출
				//-------------------------------------------
				delete pDelete;
			}
			size = 0;
		}
		//---------------------------------------------------
		// 노드를 뽑음
		//---------------------------------------------------
		stNode* pop()
		{
			stNode* pRet;
			if (top != nullptr)
			{
				pRet = top;
				top = top->next;
				size--;
				return pRet;
			}
			return nullptr;
		}
		//---------------------------------------------------
		// 노드를 넣음
		//---------------------------------------------------
		void push(stNode* releaseNode)
		{
			releaseNode->next = top;
			top = releaseNode;
			size++;
		}
	};
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------
	stChunk* AAcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _allocTop;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createAllocChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				if (loop > DebugInfo.acquireMaxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = new stChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				// 사용 청크표시
				DebugInfo.RegisterChunk(ret);
#endif
				ret->Set();

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeMakeCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_allocTop, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.acquireMaxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 사용 청크표시
		DebugInfo.RegisterChunk(ret);
#endif
		_InterlockedDecrement(&_allocChunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 반환, 모든 청크가 해제됬을 때만
	//-----------------------------------------------------------
	void AReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		// 해제 청크표시
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_allocChunkNum > TLS_OBJECT_POOL_MAX_ALLOC_CHUNKNUM)
		{
			//---------------------------------------------------
			// CALLONCE버전이라 바로 Delete
			//---------------------------------------------------
			_InterlockedDecrement(&_createReleaseChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _allocTop;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
#pragma warning(push)
#pragma warning(disable: 6001)
			// pChunk는 내함수가 멀쩡하면 null이아님
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_allocTop, (void*)newTop, (void*)t) != t);


#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.acquireMaxRaceLoopCnt)
		{
			_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
		}
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif

		_InterlockedIncrement(&_allocChunkNum);
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크껍데기를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------
	stChunk* RAcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _releaseTop;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createReleaseChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				if (loop > DebugInfo.releaseMaxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = new stChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				// 사용 청크 표시
				DebugInfo.RegisterChunk(ret);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_releaseTop, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.releaseMaxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 사용 청크 표시
		DebugInfo.RegisterChunk(ret);
#endif

		_InterlockedDecrement(&_releaseChunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 껍데기를 반환
	//-----------------------------------------------------------
	void RReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_releaseChunkNum > TLS_OBJECT_POOL_MAX_RELEASE_CHUNKNUM)
		{
			//---------------------------------------------------
			// CALL_ONCE 버전이라 바로 Delete
			//---------------------------------------------------
			_InterlockedDecrement(&_createAllocChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _releaseTop;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
#pragma warning(push)
#pragma warning(disable: 6001)
			// pChunk는 내함수가 멀쩡하면 null이아님
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_releaseTop, (void*)newTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.releaseMaxRaceLoopCnt)
		{
			_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
		}
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif
		_InterlockedIncrement(&_releaseChunkNum);
	}
	//-----------------------------------------------------------
	// 풀의 고유한 값 (Free시 잘 왔는지 참조용)
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = TLS_OBJECTPOOL_OVER_CODE ^ __keyValue;

	//-----------------------------------------------------------
	// 스레드별 할당 청크
	//-----------------------------------------------------------
	struct stChunks
	{
		stChunk* pMyAlloc;
		stChunk* pMyRelease;
	};
	DWORD _tlsIndex_pStChunks;

	//-----------------------------------------------------------
	// 공용 청크 (꽉찬거) 담는 곳
	//-----------------------------------------------------------
	stChunk* _allocTop;
	long _allocChunkNum;
	//-----------------------------------------------------------
	// 공용 청크 (껍데기) 담는 곳
	//-----------------------------------------------------------
	stChunk* _releaseTop;
	long _releaseChunkNum;
	//-----------------------------------------------------------
	// 현재 생성된 청크 개수
	//-----------------------------------------------------------
	long _createAllocChunkNum;
	long _createReleaseChunkNum;

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
	stDebugInfo DebugInfo;
#endif
};

//---------------------------------------------------------------
// Intrusive
// 디버그모드 : #define _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
// NORMAL 버전 (생성자를 매번 호출)
//---------------------------------------------------------------
template <typename T, int __keyValue>
class CTlsObjectPool<typename T, __keyValue, TLS_OBJECTPOOL_USE_NORMAL>
{
public:
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
	struct stChunk;
	struct stNode;
	struct stDebugInfo
	{
		// 청크 만들 때 등록, 청크 해제 때 해제
		SRWLOCK chunksSetLock;
		std::unordered_set<stChunk*> chunksSet;
		// 노드 만들 때 등록, 노드 반환 시 해제
		SRWLOCK nodesSetLock;
		std::unordered_set<stNode*> nodesSet;

		void RegisterChunk(stChunk* pChunk)
		{
			AcquireSRWLockExclusive(&chunksSetLock);
			auto isTrue = chunksSet.insert(pChunk);
			if (isTrue.second == false)
				__debugbreak();
			ReleaseSRWLockExclusive(&chunksSetLock);
		}
		void CancelChunk(stChunk* pChunk)
		{
			AcquireSRWLockExclusive(&chunksSetLock);
			int success = (int)chunksSet.erase(pChunk);
			if (success != 1)
				__debugbreak();
			ReleaseSRWLockExclusive(&chunksSetLock);
		}
		void RegisterNode(stNode* pNode)
		{
			AcquireSRWLockExclusive(&nodesSetLock);
			auto isTrue = nodesSet.insert(pNode);
			if (isTrue.second == false)
				__debugbreak();
			ReleaseSRWLockExclusive(&nodesSetLock);
		}
		void CancelNode(stNode* pNode)
		{
			AcquireSRWLockExclusive(&nodesSetLock);
			int success = (int)nodesSet.erase(pNode);
			if (success != 1)
				__debugbreak();
			ReleaseSRWLockExclusive(&nodesSetLock);
		}
		stDebugInfo() :chunksSetLock(SRWLOCK_INIT), nodesSetLock(SRWLOCK_INIT)
		{
			allocCnt = 0;
			releaseCnt = 0;
			aquireChunkCnt = 0;
			releaseChunkCnt = 0;
			nodeMakeCnt = 0;
			nodeRemoveCnt = 0;
			chunkMakeCnt = 0;
			chunkRemoveCnt = 0;
			acquireMaxRaceLoopCnt = 0;
			releaseMaxRaceLoopCnt = 0;
			totalAcquireRaceLoopCnt = 0;
			totalReleaseRaceLoopCnt = 0;
		}
		uint64_t allocCnt;
		uint64_t releaseCnt;
		uint64_t aquireChunkCnt;
		uint64_t releaseChunkCnt;
		uint64_t nodeMakeCnt;
		uint64_t nodeRemoveCnt;
		uint64_t chunkMakeCnt;
		uint64_t chunkRemoveCnt;
		uint64_t acquireMaxRaceLoopCnt;
		uint64_t releaseMaxRaceLoopCnt;
		uint64_t totalAcquireRaceLoopCnt;
		uint64_t totalReleaseRaceLoopCnt;
	};
#endif
	//-----------------------------------------------------------
	// 오류 목록
	//-----------------------------------------------------------
	enum enError
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
	CTlsObjectPool() :_allocTop(nullptr), _allocChunkNum(0), _releaseTop(nullptr), _releaseChunkNum(0),
		_createAllocChunkNum(0), _createReleaseChunkNum(0)
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

		//-------------------------------------------------------
		// 동적 TLS 사용을 위해 인덱스 획득
		//-------------------------------------------------------
		_tlsIndex_pStChunks = TlsAlloc();
		if (_tlsIndex_pStChunks == TLS_OUT_OF_INDEXES)
		{
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 소멸자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CTlsObjectPool()
	{
		//-------------------------------------------------------
		// 락프리 공용 청크풀 해제
		//-------------------------------------------------------
		stChunk* pChunk;
		while (((uint64_t)_allocTop & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = AAcquireChunk();
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			DebugInfo.CancelChunk(pChunk);
#endif
			//---------------------------------------------------
			// NORMAL버전은 FREE 할 때 이미 소멸자가 호출됨
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();
			delete pChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		}
		while (((uint64_t)_releaseTop & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = RAcquireChunk();
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			DebugInfo.CancelChunk(pChunk);
#endif
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();
			delete pChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		}
		//-------------------------------------------------------
		// 동적 TLS 반환
		//-------------------------------------------------------
		TlsFree(_tlsIndex_pStChunks);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (DebugInfo.allocCnt != DebugInfo.releaseCnt)
			__debugbreak();
		if (DebugInfo.aquireChunkCnt != DebugInfo.releaseChunkCnt)
			__debugbreak();

		if (DebugInfo.chunksSet.size() != 0)
			__debugbreak();
		if (DebugInfo.nodesSet.size() != 0)
			__debugbreak();
		if (DebugInfo.chunkMakeCnt != DebugInfo.chunkRemoveCnt)
			__debugbreak();
		if (DebugInfo.nodeMakeCnt != DebugInfo.nodeRemoveCnt)
			__debugbreak();
#endif
	}

	//-----------------------------------------------------------
	// Info: 풀에서 하나를 획득, 싱글스레드로 일어남
	//       ** 다쓰면 새로 청크 얻어옴.
	// Parameter: -
	// Return: T* (획득 실패시 nullptr)
	//-----------------------------------------------------------
	template <typename... Args>
	T* Alloc(Args... args)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.allocCnt);
#endif
		//-------------------------------------------------------
		// tls에서 내 청크 가져옴
		//-------------------------------------------------------
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		stChunk* pMyAllocChunk;
		stChunk* pMyReleaseChunk;
		BOOL retTls;
		if (pMyChunks == nullptr)
		{
			pMyChunks = (stChunks*)malloc(sizeof(stChunks));
			//---------------------------------------------------
			// 최초 1회 시행
			//---------------------------------------------------
			pMyAllocChunk = AAcquireChunk();	// 새거 생성
			pMyReleaseChunk = RAcquireChunk();	// 청크 껍데기 생성
#pragma warning(push)
#pragma warning(disable: 6011)
			// nullptr이면 뻑남
			pMyChunks->pMyAlloc = pMyAllocChunk;
			pMyChunks->pMyRelease = pMyReleaseChunk;
			retTls = TlsSetValue(_tlsIndex_pStChunks, (void*)pMyChunks);
			if (retTls == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
#pragma warning(pop)
		}
		else
		{
			pMyAllocChunk = pMyChunks->pMyAlloc;
		}

		//-------------------------------------------------------
		// 노드 획득
		// NORMAL 버전이라 생성자 호출 (노드에다가)
		//-------------------------------------------------------
		stNode* pRetNode = pMyAllocChunk->pop();
		pRetNode->next = (stNode*)UNIQUE;
		pRetNode->overcheck = UNIQUE;
		new (&pRetNode->data) T(args...);

		//-------------------------------------------------------
		// leftnode가 0이면 청크 새로 받음
		//-------------------------------------------------------
		if (pMyAllocChunk->size == 0)
		{
			pMyReleaseChunk = pMyChunks->pMyRelease;
			if (pMyReleaseChunk->size == 0)
			{
				//-----------------------------------------------
				// 내 반환청크풀 사이즈가 0이면 반환 후 새 청크 받아옴
				//-----------------------------------------------
				RReleaseChunk(pMyAllocChunk);
				pMyAllocChunk = AAcquireChunk();
				pMyChunks->pMyAlloc = pMyAllocChunk;
			}
			else
			{
				//-----------------------------------------------
				// 내 반환 청크풀에 조금이라도 있으면 이거 사용
				//-----------------------------------------------
				stChunk* temp = pMyChunks->pMyAlloc;
				pMyChunks->pMyAlloc = pMyChunks->pMyRelease;
				pMyChunks->pMyRelease = temp;
			}
		}

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 노드 사용
		DebugInfo.RegisterNode(pRetNode);
#endif
		return &pRetNode->data;
	}

	//-----------------------------------------------------------
	// Info: 정확히는 청크에 사용이 끝났다고 알림,
	//       락프리풀에 반환때문에 풀에서 접근이 필요함
	// Parameter: T*
	// Return: bool
	//-----------------------------------------------------------
	int Free(T* pObject)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseCnt);
#endif
		//-----------------------------------------------------
		// 널 체크
		//-----------------------------------------------------
		if (pObject == nullptr)
			return enNullptr;
		//-----------------------------------------------------
		// node* 타입으로 변경하기
		//-----------------------------------------------------
		stNode* pRet = (stNode*)((char*)pObject - sizeof(uint64_t));
		//------------------------------------------------------
		// 노드의 유효성 검사 (여기 들어오는 것이 맞는지)
		// . 유효하지 않으면 free한다.
		// . 해제해도 풀 외부에서 온 값이므로 풀 내부에서 참조하는
		//   값이 아니므로 괜찮다.
		//------------------------------------------------------

		// 언더플로 체크
		if ((uint64_t)pRet->next != UNIQUE)
		{
			return enUnder;
		}
		// 오버플로 체크
		if (pRet->overcheck != UNIQUE)
		{
			// 이중해제 확인
			if (pRet->overcheck == TLS_OBJECTPOOL_NODE_NOTUSE)
				return enDoubleFree;	// 해제하면 안됨.
			return enOver;
		}

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 노드 사용 끝 (해제)
		DebugInfo.CancelNode(pRet);
#endif
		//------------------------------------------------------
		// 해제 : NORMAL 버전, 소멸자 호출 (노드마다)
		//------------------------------------------------------
		pRet->overcheck = TLS_OBJECTPOOL_NODE_NOTUSE;
		pRet->data.~T();

		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		stChunk* pMyAllocChunk;
		stChunk* pMyReleaseChunk;
		if (pMyChunks == nullptr)
		{
			pMyChunks = (stChunks*)malloc(sizeof(stChunks));
			//---------------------------------------------------
			// 최초 1회 시행
			//---------------------------------------------------
			pMyAllocChunk = AAcquireChunk();	// 새거 생성
			pMyReleaseChunk = RAcquireChunk();	// 청크 껍데기 생성

			pMyChunks->pMyAlloc = pMyAllocChunk;
			pMyChunks->pMyRelease = pMyReleaseChunk;
			DWORD retTls = TlsSetValue(_tlsIndex_pStChunks, (void*)pMyChunks);
			if (retTls == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
		}
		else
		{
			pMyReleaseChunk = pMyChunks->pMyRelease;
		}
		pMyReleaseChunk->push(pRet);

		//------------------------------------------------------
		// 해제된 개수 확인
		//------------------------------------------------------
		if (pMyReleaseChunk->size >= TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK)
		{
			pMyAllocChunk = pMyChunks->pMyAlloc;
			if (pMyAllocChunk->size == TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK)
			{
				AReleaseChunk(pMyReleaseChunk);
				pMyReleaseChunk = RAcquireChunk();
				pMyChunks->pMyRelease = pMyReleaseChunk;
			}
			else
			{
				//-----------------------------------------------
				// 내 할당 청크풀에 꽉찬 내거로 교체해줌
				//-----------------------------------------------
				stChunk* temp = pMyChunks->pMyAlloc;
				pMyChunks->pMyAlloc = pMyChunks->pMyRelease;
				pMyChunks->pMyRelease = temp;
			}
		}

		return enFine;
	}


	//-----------------------------------------------------------
	// Info: 스레드 종료시 호출
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	void ThreadRelease()
	{
		stChunks* pMyChunks = (stChunks*)TlsGetValue(_tlsIndex_pStChunks);
		if (pMyChunks == nullptr)
			return;
		stChunk* pMyAllocChunk = pMyChunks->pMyAlloc;
		stChunk* pMyReleaseChunk = pMyChunks->pMyRelease;

		//-------------------------------------------------------
		// 그냥 클리어해서 지우자
		//-------------------------------------------------------
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		DebugInfo.CancelChunk(pMyAllocChunk);
		DebugInfo.CancelChunk(pMyReleaseChunk);
		int size = pMyAllocChunk->size;
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
		size = pMyReleaseChunk->size;
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
		_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
		pMyAllocChunk->Clear();
		pMyReleaseChunk->Clear();
		delete pMyAllocChunk;
		delete pMyReleaseChunk;
		free(pMyChunks);

		TlsSetValue(_tlsIndex_pStChunks, nullptr);
	}
	//-----------------------------------------------------------
	// 공용 할당 풀 청크 수 획득
	//-----------------------------------------------------------
	int GetAllocChunkPoolSize()
	{
		return _allocChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 릴리즈 풀 껍데기 청크 수 획득
	//-----------------------------------------------------------
	int GetReleaseChunkPoolSize()
	{
		return _releaseChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 할당 풀 청크 생성수 획득 (현재 만들어진 값)
	//-----------------------------------------------------------
	int GetAllocChunkPoolCreateNum()
	{
		return _createAllocChunkNum;
	}
	//-----------------------------------------------------------
	// 공용 릴리즈 풀 청크 생성수 획득 (현재 만들어진 값)
	//-----------------------------------------------------------
	int GetReleaseChunkPoolCreateNum()
	{
		return _createReleaseChunkNum;
	}
private:
	struct stChunk;
	//-----------------------------------------------------------
	// 노드 구조체
	//-----------------------------------------------------------
	struct stNode
	{
		stNode* next;
		T data;
		uint64_t overcheck;
	};
	//-----------------------------------------------------------
	// 청크 구조체
	// . 청크는 없으면 할당하고, 바로 버린다
	//-----------------------------------------------------------
	struct stChunk
	{
		stNode* top;
		long size;
		stChunk* next;
		stChunk() :next(0), top(0), size(0) {}
		//---------------------------------------------------
		// 노드 초기화해서 청크 기본개수 만큼 넣음
		//---------------------------------------------------
		void Set()
		{
			stNode* newNode;
			int num = TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK;
			while (num != 0)
			{
#pragma warning(push)
#pragma warning(disable: 6011)
				// newNOde null이면 뻑나게
				newNode = (stNode*)malloc(sizeof(stNode));
				newNode->next = top;
				newNode->overcheck = TLS_OBJECTPOOL_NODE_NOTUSE;
				top = newNode;
				num--;
#pragma warning(pop)
			}
			size = TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK;
		}
		//---------------------------------------------------
		// 노드를 비움
		//---------------------------------------------------
		void Clear()
		{
			stNode* pDelete;
			while (top != nullptr)
			{
				pDelete = top;
				top = top->next;
				free((void*)pDelete);
			}
			size = 0;
		}
		//---------------------------------------------------
		// 노드를 뽑음
		//---------------------------------------------------
		stNode* pop()
		{
			stNode* pRet;
			if (top != nullptr)
			{
				pRet = top;
				top = top->next;
				size--;
				return pRet;
			}
			return nullptr;
		}
		//---------------------------------------------------
		// 노드를 넣음
		//---------------------------------------------------
		void push(stNode* releaseNode)
		{
			releaseNode->next = top;
			top = releaseNode;
			size++;
		}
	};
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------
	stChunk* AAcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _allocTop;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createAllocChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				if (loop > DebugInfo.acquireMaxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = new stChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				// 사용 청크표시
				DebugInfo.RegisterChunk(ret);
#endif
				ret->Set();

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeMakeCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_allocTop, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.acquireMaxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 사용 청크표시
		DebugInfo.RegisterChunk(ret);
#endif
		_InterlockedDecrement(&_allocChunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 반환, 모든 청크가 해제됬을 때만
	//-----------------------------------------------------------
	void AReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		// 해제 청크표시
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_allocChunkNum > TLS_OBJECT_POOL_MAX_ALLOC_CHUNKNUM)
		{
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------
			_InterlockedDecrement(&_createReleaseChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			int size = pChunk->size;
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, size);
#endif
			pChunk->Clear();

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _allocTop;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
#pragma warning(push)
#pragma warning(disable: 6001)
			// pChunk는 내함수가 멀쩡하면 null이아님
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_allocTop, (void*)newTop, (void*)t) != t);


#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.acquireMaxRaceLoopCnt)
		{
			_InterlockedExchange((uint64_t*)&DebugInfo.acquireMaxRaceLoopCnt, loop);
		}
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalAcquireRaceLoopCnt, loop);
#endif

		_InterlockedIncrement(&_allocChunkNum);
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크껍데기를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------
	stChunk* RAcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _releaseTop;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createReleaseChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				if (loop > DebugInfo.releaseMaxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = new stChunk;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
				// 사용 청크 표시
				DebugInfo.RegisterChunk(ret);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_releaseTop, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.releaseMaxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		// 사용 청크 표시
		DebugInfo.RegisterChunk(ret);
#endif

		_InterlockedDecrement(&_releaseChunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 껍데기를 반환
	//-----------------------------------------------------------
	void RReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_releaseChunkNum > TLS_OBJECT_POOL_MAX_RELEASE_CHUNKNUM)
		{
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------
			_InterlockedDecrement(&_createAllocChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		int loop = -1;
#endif
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
			loop++;
#endif
			t = _releaseTop;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
#pragma warning(push)
#pragma warning(disable: 6001)
			// pChunk는 내함수가 멀쩡하면 null이아님
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_releaseTop, (void*)newTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
		if (loop > DebugInfo.releaseMaxRaceLoopCnt)
		{
			_InterlockedExchange((uint64_t*)&DebugInfo.releaseMaxRaceLoopCnt, loop);
		}
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalReleaseRaceLoopCnt, loop);
#endif
		_InterlockedIncrement(&_releaseChunkNum);
	}
	//-----------------------------------------------------------
	// 풀의 고유한 값 (Free시 잘 왔는지 참조용)
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = TLS_OBJECTPOOL_OVER_CODE ^ __keyValue;

	//-----------------------------------------------------------
	// 스레드별 할당 청크
	//-----------------------------------------------------------
	struct stChunks
	{
		stChunk* pMyAlloc;
		stChunk* pMyRelease;
	};
	DWORD _tlsIndex_pStChunks;

	//-----------------------------------------------------------
	// 공용 청크 (꽉찬거) 담는 곳
	//-----------------------------------------------------------
	stChunk* _allocTop;
	long _allocChunkNum;
	//-----------------------------------------------------------
	// 공용 청크 (껍데기) 담는 곳
	//-----------------------------------------------------------
	stChunk* _releaseTop;
	long _releaseChunkNum;
	//-----------------------------------------------------------
	// 현재 생성된 청크 개수
	//-----------------------------------------------------------
	long _createAllocChunkNum;
	long _createReleaseChunkNum;

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
	stDebugInfo DebugInfo;
#endif
};

#endif