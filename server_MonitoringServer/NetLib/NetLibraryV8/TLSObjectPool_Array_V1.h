#ifndef __TLS_OBJECT_POOL_ARRAY_H__
#define __TLS_OBJECT_POOL_ARRAY_H__

/////////////////////////////////////////////////////////////////
// TLS_OBJECT_POOL 노드관리 배열버전 V1
// . 청크는 노드 배열
// . 풀은 청크를 관리
// . 청크는 다쓰면 새 청크를 받음
// . 청크의 해제는 마지막으로 노드를 반환한 녀석이 진행
//   (청크를 풀에 반환)
// . 최대한 쓸 수 있는게 있다면, 열심히 씀
// 
// 
// RAW : 생성자 호출 / 소멸자 호출 안함
// CALLONCE : 생성자 청크 생성 단계에서 호출 / 소멸자 청크 지우는 단계에서 호출
// NORMAL : 생성자 노드 단계에서 호출 / 소멸자 노드 반환 단계에서 호출
/////////////////////////////////////////////////////////////////

// #define _DEBUG_TLS_OBJECT_POOL_ARRAY

#ifdef _DEBUG
#define _DEBUG_TLS_OBJECT_POOL_ARRAY
#endif

#include <stdint.h>
#include <windows.h>
#include <wchar.h>
#include <intrin.h>
#include <stdlib.h>
#include <new>

constexpr uint64_t TLS_OBJECT_POOL_BITFLAG[64] = {
	0x0000'0000'0000'0001,
	0x0000'0000'0000'0002,
	0x0000'0000'0000'0004,
	0x0000'0000'0000'0008,
	0x0000'0000'0000'0010,
	0x0000'0000'0000'0020,
	0x0000'0000'0000'0040,
	0x0000'0000'0000'0080,
	0x0000'0000'0000'0100,
	0x0000'0000'0000'0200,
	0x0000'0000'0000'0400,
	0x0000'0000'0000'0800,
	0x0000'0000'0000'1000,
	0x0000'0000'0000'2000,
	0x0000'0000'0000'4000,
	0x0000'0000'0000'8000,

	0x0000'0000'0001'0000,
	0x0000'0000'0002'0000,
	0x0000'0000'0004'0000,
	0x0000'0000'0008'0000,
	0x0000'0000'0010'0000,
	0x0000'0000'0020'0000,
	0x0000'0000'0040'0000,
	0x0000'0000'0080'0000,
	0x0000'0000'0100'0000,
	0x0000'0000'0200'0000,
	0x0000'0000'0400'0000,
	0x0000'0000'0800'0000,
	0x0000'0000'1000'0000,
	0x0000'0000'2000'0000,
	0x0000'0000'4000'0000,
	0x0000'0000'8000'0000,

	0x0000'0001'0000'0000,
	0x0000'0002'0000'0000,
	0x0000'0004'0000'0000,
	0x0000'0008'0000'0000,
	0x0000'0010'0000'0000,
	0x0000'0020'0000'0000,
	0x0000'0040'0000'0000,
	0x0000'0080'0000'0000,
	0x0000'0100'0000'0000,
	0x0000'0200'0000'0000,
	0x0000'0400'0000'0000,
	0x0000'0800'0000'0000,
	0x0000'1000'0000'0000,
	0x0000'2000'0000'0000,
	0x0000'4000'0000'0000,
	0x0000'8000'0000'0000,

	0x0001'0000'0000'0000,
	0x0002'0000'0000'0000,
	0x0004'0000'0000'0000,
	0x0008'0000'0000'0000,
	0x0010'0000'0000'0000,
	0x0020'0000'0000'0000,
	0x0040'0000'0000'0000,
	0x0080'0000'0000'0000,
	0x0100'0000'0000'0000,
	0x0200'0000'0000'0000,
	0x0400'0000'0000'0000,
	0x0800'0000'0000'0000,
	0x1000'0000'0000'0000,
	0x2000'0000'0000'0000,
	0x4000'0000'0000'0000,
	0x8000'0000'0000'0000,
};

// 청크 기본 노드수: 반드시 64의 배수(BitScanForward써야함), 최대 노드 1024개 넘고싶으면 아래 수정해야
#define TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK	128
// 청크 내 비트 검사 갯수
#define TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK	TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK / 64
// 위에 청크 내 비트 검사 갯수만큼 반드시 채워야함.
constexpr uint64_t TLS_OBJECT_POOL_FOR_CMP_ALL_1[16] = {
	0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF,
	0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF,
	0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF,
	0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF, 0xFFFF'FFFF'FFFF'FFFF
};

constexpr uint64_t TLS_OBJECT_POOL_FOR_CMP_ALL_0[16] = {
	0,
};

// 락프리 청크 공용 풀 청크 최대 갯수
#define TLS_OBJECT_POOL_ARRAY_MAX_CHUNKNUM			100

// 카운터로 사용할 비트 수 
#define TLS_OBJECT_POOL_COUNTER_BIT	17
// 시프트 할 횟수
#define TLS_OBJECT_POOL_SHIFT_BIT	47
// 마스크
#define TLS_OBJECT_POOL_BIT_MASK	0x00007FFF'FFFFFFFF

// 0 = Raw (오버로딩에 사용)
#define TLS_OBJECTPOOL_ARRAY_USE_RAW		0
// 1 = 생성자, 소멸자를 새로운 노드가 생길 때만 호출
#define TLS_OBJECTPOOL_ARRAY_USE_CALLONCE	1
// 2 = 생성자, 소멸자를 항상 호출
#define TLS_OBJECTPOOL_ARRAY_USE_NORMAL	2
// 오버플로 체킹
#define TLS_OBJECTPOOL_ARRAY_OVER_CODE	0xFCFCFCFC'FCFCFCFC
// 언더플로 체킹
#define TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE	0x9C9C9C9C'9C9C9C9C

template <typename T, int __keyValue, int __whatFreeToUse>
class CTlsObjectPool_Array;

//---------------------------------------------------------------
// ** 지역 선언 금지 **
// RAW 버전 (생성자를 호출하지 않음)
//---------------------------------------------------------------
template <typename T, int __keyValue>
class CTlsObjectPool_Array<typename T, __keyValue, TLS_OBJECTPOOL_ARRAY_USE_RAW>
{
public:
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
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
			  freeCnt = 0;
			  aquireChunkCnt = 0;
			  releaseChunkCnt = 0;
			  nodeMakeCnt = 0;
			  nodeRemoveCnt = 0;
			  chunkMakeCnt = 0;
			  chunkRemoveCnt = 0;
			  maxRaceLoopCnt = 0;
			  totalRaceLoopCnt = 0;
		  }
		  uint64_t allocCnt;
		  uint64_t freeCnt;
		  uint64_t aquireChunkCnt;
		  uint64_t releaseChunkCnt;
		  uint64_t nodeMakeCnt;
		  uint64_t nodeRemoveCnt;
		  uint64_t chunkMakeCnt;
		  uint64_t chunkRemoveCnt;
		  uint64_t maxRaceLoopCnt;;
		  uint64_t totalRaceLoopCnt;
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
	CTlsObjectPool_Array():_top(nullptr), _chunkNum(0), _createChunkNum(0)
	{
		//-------------------------------------------------------
		// 64비트가 아니면 생성을 막자
		//-------------------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not 64bit!!\n");
			__debugbreak();
		}

		//-------------------------------------------------------
		// 동적 TLS 사용을 위해 인덱스 획득
		//-------------------------------------------------------
		_tlsIndex_pTlsAllocChunk = TlsAlloc();
		if (_tlsIndex_pTlsAllocChunk == TLS_OUT_OF_INDEXES)
		{
			wprintf_s(L"DTls have no indexes");
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 소멸자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CTlsObjectPool_Array()
	{
		//-------------------------------------------------------
		// 락프리 공용 청크풀 해제
		//-------------------------------------------------------
		stChunk* pChunk;
		while (((uint64_t)_top & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = AcquireChunk();
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
			_InterlockedDecrement(&_createChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			DebugInfo.CancelChunk(pChunk);
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
			free((void*)pChunk);
		}
		//-------------------------------------------------------
		// 동적 TLS 반환
		//-------------------------------------------------------
		TlsFree(_tlsIndex_pTlsAllocChunk);


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (DebugInfo.allocCnt != DebugInfo.freeCnt)
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
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*) &DebugInfo.allocCnt);
#endif
		//-------------------------------------------------------
		// tls에서 내 청크 가져옴
		//-------------------------------------------------------
		stChunk* pMyChunk = (stChunk*)TlsGetValue(_tlsIndex_pTlsAllocChunk);
		if (pMyChunk == nullptr)
		{
			pMyChunk = AcquireChunk();
			BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, (void*)pMyChunk);
			if (ret == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
		}

		//-------------------------------------------------------
		// 건네줄 인덱스를 얻고,
		// 세팅을 하고,
		// 그 주소의 노드 건네줌
		//-------------------------------------------------------
		stNode* pRetNode;
		int index;
		if(pMyChunk->leftNodeCnt > 0)
		{
			index = --pMyChunk->leftNodeCnt;
			pRetNode = &pMyChunk->nodeArray[index];
			pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
			pRetNode->overcheck = UNIQUE;
		}
		else
		{
FIND_NODE:
			//---------------------------------------------------
			// 비트 플래그 뒤져서 줄거 있나 찾기
			//---------------------------------------------------
			int index = -1;
			bool bFind = false;
			int flagIndex = TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK - 1;
			uint64_t* pBitflags = pMyChunk->bitflags;
			for (flagIndex; flagIndex >= 0; flagIndex--)
			{
				if (_BitScanReverse64((DWORD*) & index, (uint64_t)pBitflags[flagIndex]))
				{
					//-------------------------------------------
					// 찾음
					//-------------------------------------------
					bFind = true;
					break;
				}
			}

			//---------------------------------------------------
			// 결과 확인
			//---------------------------------------------------
			if (bFind)
			{
				//-----------------------------------------------
				// 찾음, 실제 인덱스 계산
				// 플래그 0으로 바꾸고 줄 준비
				// 
				// . 누가 현재 영역을 1로 바꿀 수 있으니..해제하면서
				//   인터락 필요
				//-----------------------------------------------
				_InterlockedAnd((volatile uint64_t*)&pMyChunk->bitflags[flagIndex], ~(TLS_OBJECT_POOL_BITFLAG[index]));

				index = (flagIndex << 6) + index;
				pRetNode = &pMyChunk->nodeArray[index];
				pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
				pRetNode->overcheck = UNIQUE;
			}
			else
			{
				//-----------------------------------------------
				// 못찾음, 플래그 올려서 누군가 해제하게 유도
				// . 처음으로 1이 되는 순간
				// . 소유권 포기
				//-----------------------------------------------
				_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);

				//-----------------------------------------------
				// 그래도~ 내가 1로 올리는 사이에 
				// * 모든 플래그가 1이 됬을 수도 있음(희박하게)
				// . 한번만 더 검사... 재사용은 안함. 해제하기로 결정했으니
				// 
				// ** 플래그를 변경하지 못했다 -> 그냥 무조건 남이 쓰러갔다.
				// ** 그냥 청크 새로 받으면 된다.
				//-----------------------------------------------
				if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_0, sizeof(pMyChunk->bitflags)) == 0)
				{
					//-------------------------------------------
					// 전부 0 = 전부 사용중, 새로 청크 받아야됨.
					// 해제 시도는 하면 안됨.
					//-------------------------------------------
				}
				else
				{
					//-------------------------------------------
					// 1이 있으니까 일단 재사용 각 노림
					//-------------------------------------------
					if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
					{
						//---------------------------------------
						// 내가 획득함.
						// 1이 최소 1개 ~ n개
						//---------------------------------------
						goto FIND_NODE;
					}
					else
					{
						//---------------------------------------
						// 재사용에 실패, 이미 누가 0으로 바꿈
						// 그냥 나가면 된다.
						//---------------------------------------
					}
				}

				//-----------------------------------------------
				// 일단 없으니까 청크를 새로 받아오자
				//-----------------------------------------------
				pMyChunk = AcquireChunk();
				BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, (void*)pMyChunk);
				if (ret == 0)
				{
					DWORD tlsError = GetLastError();
					__debugbreak();
				}
				//-----------------------------------------------
				// 새로 받아온 청크에서 하나 주자
				//-----------------------------------------------
				index = --pMyChunk->leftNodeCnt;
				pRetNode = &pMyChunk->nodeArray[index];
				pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
				pRetNode->overcheck = UNIQUE;
			}
		}
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
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
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.freeCnt);
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
		
		// 언더플로는 접근해서 뻑나는거로 (부모 청크노드 포인터)
		// 이중해제 확인
		if (pRet->overcheck == TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE)
			return enDoubleFree;	// 해제하면 안됨.
		// 오버플로 체크
		if (pRet->overcheck != UNIQUE)
		{
			__debugbreak();
			return enOver;
		}


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		DebugInfo.CancelNode(pRet);
#endif
		//------------------------------------------------------
		// 해제 : RAW버전 / 노드 초기화 진행 후 반환 플래그 올림
		//------------------------------------------------------
		// 소멸자 호출X
		pRet->overcheck = TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE;
		stChunk* pMyChunk = pRet->pParentChunk;
		int myindex = (int)(pRet - pMyChunk->nodeArray);
		// 플래그 올리기
		_InterlockedOr((volatile uint64_t*)&pMyChunk->bitflags[myindex >> 6], TLS_OBJECT_POOL_BITFLAG[myindex & 0x00003F]);
		
		//------------------------------------------------------
		// 필요하면 반환
		// 얘는 전부 비트가 1이면 무조건 반환이므로 xor로 0과비교 
		//------------------------------------------------------
		if (pMyChunk->releaseFlag)
		{
			uint64_t* pBitflags = pMyChunk->bitflags;
			if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_1, sizeof(pMyChunk->bitflags)) == 0)
			{
				//-----------------------------------------------
				// 실패시 다른애가 한거임
				// 1. 다른애가 릴리즈
				// 2. 해당 청크의 소유자가 재획득
				//-----------------------------------------------
				if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
				{
					// 내가 바꿨으니 해제하자
					ReleaseChunk(pMyChunk);
				}
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
		stChunk* pMyChunk = (stChunk*)TlsGetValue(_tlsIndex_pTlsAllocChunk);
		//---------------------------------------------------
		// 스레드 생성하자마자 풀 안받고 종료한 경우
		//---------------------------------------------------
		if (pMyChunk == nullptr)
		{
			return;
		}

		//-------------------------------------------------------
		// leftNodeCnt > 0 이면 남은 횟수만큼 Alloc후 바로 Free
		//-------------------------------------------------------
		if (pMyChunk->leftNodeCnt > 0)
		{
			int left = pMyChunk->leftNodeCnt;
			T* pNode;
			_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);
			for (int i = 0; i < left; i++)
			{
				if (i == left - 1)
					int a = 0;
				pNode = Alloc();
				Free(pNode);	// 여기서 100% 해제루트 탐.
			}
			BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, nullptr);
			if (ret == FALSE)
			{
				DWORD gle = GetLastError();
				__debugbreak();
			}
			return;
		}

		//------------------------------------------------------
		// ** 여기부터는 0이 있으면 무조건 사용중 **
		// 남은 횟수가 0이면, 비트플래그를 확인하면서 해야됨
		// 내 스레드에서 Alloc과 같은 효과를 내는거니까
		// RAW 버전은 소멸자 호출할게 없음. 
		// 청크에서 소멸자호출은 only CALL ONCE 
		// 
		// 1. releaseFlag를 올림. 
		//    => 이제 내것이 아닐 수 있음.
		// 
		// 2. 플래그 전부 검사해서 FF..FF면 해제 시도,
		//    => 실패하면 떠넘김
		//------------------------------------------------------
		_InterlockedExchange((volatile long*) &pMyChunk->releaseFlag, 1);
		if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_1, sizeof(pMyChunk->bitflags)) == 0)
		{
			//-----------------------------------------------
			// 실패시 다른애가 한거임
			// 1. 다른애가 릴리즈
			// 2. 해당 청크의 소유자가 재획득
			//-----------------------------------------------
			if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
			{
				// 내가 바꿨으니 해제하자
				ReleaseChunk(pMyChunk);
			}
		}
		BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, nullptr);
		if (ret == FALSE)
		{
			DWORD gle = GetLastError();
			__debugbreak();
		}
	}

	//-----------------------------------------------------------
	// 공용 풀 청크 수 획득
	//-----------------------------------------------------------
	int GetSize()
	{
		return _chunkNum;
	}
	//-----------------------------------------------------------
	// 프로그램에서 사용중인 청크 수 획득
	//-----------------------------------------------------------
	int GetChunkUsingNum()
	{
		return _createChunkNum;
	}
private:
	struct stChunk;
	//-----------------------------------------------------------
	// 노드 구조체
	//-----------------------------------------------------------
	struct stNode
	{
		stChunk* pParentChunk;
		T data;
		uint64_t overcheck;
	};
	//-----------------------------------------------------------
	// 청크 구조체
	// . 청크는 없으면 할당하고, 바로 버린다
	//-----------------------------------------------------------
	struct stChunk
	{
		int leftNodeCnt;	// 이게 0이되고부터 비트플래그를 찾기
		stChunk* next;
		uint64_t bitflags[TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK];
		stNode nodeArray[TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK];
		long releaseFlag;
	};
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------

	stChunk* AcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
		int loop = -1;
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			loop++;
#endif
			t = _top;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{	
				_InterlockedIncrement(&_createChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeMakeCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
				if (loop > DebugInfo.maxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.maxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = (stChunk*)malloc(sizeof(stChunk));
#pragma warning(push)
#pragma warning(disable: 6011)
				// nullptr 역참조 경고. 허용함
				ret->next = nullptr;
#pragma warning(pop)
				ret->leftNodeCnt = TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK;
				memset((void*)ret->bitflags, 0, sizeof(ret->bitflags));

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
				DebugInfo.RegisterChunk(ret);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (loop > DebugInfo.maxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.maxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
		DebugInfo.RegisterChunk(ret);
#endif
		_InterlockedDecrement(&_chunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 반환, 모든 청크가 해제됬을 때만
	//-----------------------------------------------------------
	void ReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_chunkNum > TLS_OBJECT_POOL_ARRAY_MAX_CHUNKNUM)
		{
			_InterlockedDecrement(&_createChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			free((void*)pChunk);
			return;
		}
		//-------------------------------------------------------
		// 청크 초기화
		//-------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 6001)
		// pChunk는 내함수가 멀쩡하면 null이아님
		pChunk->leftNodeCnt = TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK;
		memset((void*)pChunk->bitflags, 0, sizeof(pChunk->bitflags));

		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		int loop = -1;
#endif
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;
		do
		{

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			loop++;
#endif
			t = _top;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, (void*)newTop, (void*)t) != t);
		

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (loop > DebugInfo.maxRaceLoopCnt)
			_InterlockedExchange(&DebugInfo.maxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
#endif

		_InterlockedIncrement(&_chunkNum);
	}


	//-----------------------------------------------------------
	// 풀의 고유한 값 (Free시 잘 왔는지 참조용)
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = TLS_OBJECTPOOL_ARRAY_OVER_CODE ^ __keyValue;

	//-----------------------------------------------------------
	// 스레드별 청크
	//-----------------------------------------------------------
	DWORD _tlsIndex_pTlsAllocChunk;

	//-----------------------------------------------------------
	// 공용 청크 담는 곳
	//-----------------------------------------------------------
	stChunk* _top;
	long _chunkNum;
	long _createChunkNum;

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
public:
	stDebugInfo DebugInfo;
#endif
};

//---------------------------------------------------------------
// ** 지역 선언 금지 **
// CALLONCE 버전 (최초 1회 생성자 호출, 소멸시 1회 소멸자호출)
//---------------------------------------------------------------
template <typename T, int __keyValue>
class CTlsObjectPool_Array<typename T, __keyValue, TLS_OBJECTPOOL_ARRAY_USE_CALLONCE>
{
public:
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
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
			freeCnt = 0;
			aquireChunkCnt = 0;
			releaseChunkCnt = 0;
			nodeMakeCnt = 0;
			nodeRemoveCnt = 0;
			chunkMakeCnt = 0;
			chunkRemoveCnt = 0;
			maxRaceLoopCnt = 0;
			totalRaceLoopCnt = 0;
		}
		uint64_t allocCnt;
		uint64_t freeCnt;
		uint64_t aquireChunkCnt;
		uint64_t releaseChunkCnt;
		uint64_t nodeMakeCnt;
		uint64_t nodeRemoveCnt;
		uint64_t chunkMakeCnt;
		uint64_t chunkRemoveCnt;
		uint64_t maxRaceLoopCnt;;
		uint64_t totalRaceLoopCnt;
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
	CTlsObjectPool_Array() :_top(nullptr), _chunkNum(0), _createChunkNum(0)
	{
		//-------------------------------------------------------
		// 64비트가 아니면 생성을 막자
		//-------------------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not 64bit!!\n");
			__debugbreak();
		}

		//-------------------------------------------------------
		// 동적 TLS 사용을 위해 인덱스 획득
		//-------------------------------------------------------
		_tlsIndex_pTlsAllocChunk = TlsAlloc();
		if (_tlsIndex_pTlsAllocChunk == TLS_OUT_OF_INDEXES)
		{
			wprintf_s(L"DTls have no indexes");
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 소멸자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CTlsObjectPool_Array()
	{
		//-------------------------------------------------------
		// 락프리 공용 청크풀 해제
		//-------------------------------------------------------
		stChunk* pChunk;
		while (((uint64_t)_top & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = AcquireChunk();
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
			_InterlockedDecrement(&_createChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			DebugInfo.CancelChunk(pChunk);
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
			delete pChunk;
		}
		//-------------------------------------------------------
		// 동적 TLS 반환
		//-------------------------------------------------------
		TlsFree(_tlsIndex_pTlsAllocChunk);


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (DebugInfo.allocCnt != DebugInfo.freeCnt)
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
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.allocCnt);
#endif
		//-------------------------------------------------------
		// tls에서 내 청크 가져옴
		//-------------------------------------------------------
		stChunk* pMyChunk = (stChunk*)TlsGetValue(_tlsIndex_pTlsAllocChunk);
		if (pMyChunk == nullptr)
		{
			pMyChunk = AcquireChunk();
			BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, (void*)pMyChunk);
			if (ret == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
		}

		//-------------------------------------------------------
		// 건네줄 인덱스를 얻고,
		// 세팅을 하고,
		// 그 주소의 노드 건네줌
		//-------------------------------------------------------
		stNode* pRetNode;
		int index;
		if (pMyChunk->leftNodeCnt > 0)
		{
			index = --pMyChunk->leftNodeCnt;
			pRetNode = &pMyChunk->nodeArray[index];
			pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
			pRetNode->overcheck = UNIQUE;
		}
		else
		{
		FIND_NODE:
			//---------------------------------------------------
			// 비트 플래그 뒤져서 줄거 있나 찾기
			//---------------------------------------------------
			int index = -1;
			bool bFind = false;
			int flagIndex = TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK - 1;
			uint64_t* pBitflags = pMyChunk->bitflags;
			for (flagIndex; flagIndex >= 0; flagIndex--)
			{
				if (_BitScanReverse64((DWORD*)&index, (uint64_t)pBitflags[flagIndex]))
				{
					//-------------------------------------------
					// 찾음
					//-------------------------------------------
					bFind = true;
					break;
				}
			}

			//---------------------------------------------------
			// 결과 확인
			//---------------------------------------------------
			if (bFind)
			{
				//-----------------------------------------------
				// 찾음, 실제 인덱스 계산
				// 플래그 0으로 바꾸고 줄 준비
				// 
				// . 누가 현재 영역을 1로 바꿀 수 있으니..해제하면서
				//   인터락 필요
				//-----------------------------------------------
				_InterlockedAnd((volatile uint64_t*)&pMyChunk->bitflags[flagIndex], ~(TLS_OBJECT_POOL_BITFLAG[index]));

				index = (flagIndex << 6) + index;
				pRetNode = &pMyChunk->nodeArray[index];
				pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
				pRetNode->overcheck = UNIQUE;
			}
			else
			{
				//-----------------------------------------------
				// 못찾음, 플래그 올려서 누군가 해제하게 유도
				// . 처음으로 1이 되는 순간
				// . 소유권 포기
				//-----------------------------------------------
				_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);

				//-----------------------------------------------
				// 그래도~ 내가 1로 올리는 사이에 
				// * 모든 플래그가 1이 됬을 수도 있음(희박하게)
				// . 한번만 더 검사... 재사용은 안함. 해제하기로 결정했으니
				// 
				// ** 플래그를 변경하지 못했다 -> 그냥 무조건 남이 쓰러갔다.
				// ** 그냥 청크 새로 받으면 된다.
				//-----------------------------------------------
				if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_0, sizeof(pMyChunk->bitflags)) == 0)
				{
					//-------------------------------------------
					// 전부 0 = 전부 사용중, 새로 청크 받아야됨.
					// 해제 시도는 하면 안됨.
					//-------------------------------------------
				}
				else
				{
					//-------------------------------------------
					// 1이 있으니까 일단 재사용 각 노림
					//-------------------------------------------
					if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
					{
						//---------------------------------------
						// 내가 획득함.
						// 1이 최소 1개 ~ n개
						//---------------------------------------
						goto FIND_NODE;
					}
					else
					{
						//---------------------------------------
						// 재사용에 실패, 이미 누가 0으로 바꿈
						// 그냥 나가면 된다.
						//---------------------------------------
					}
				}

				//-----------------------------------------------
				// 일단 없으니까 청크를 새로 받아오자
				//-----------------------------------------------
				pMyChunk = AcquireChunk();
				BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, (void*)pMyChunk);
				if (ret == 0)
				{
					DWORD tlsError = GetLastError();
					__debugbreak();
				}
				//-----------------------------------------------
				// 새로 받아온 청크에서 하나 주자
				//-----------------------------------------------
				index = --pMyChunk->leftNodeCnt;
				pRetNode = &pMyChunk->nodeArray[index];
				pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
				pRetNode->overcheck = UNIQUE;
			}
		}
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
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
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.freeCnt);
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

		// 언더플로는 접근해서 뻑나는거로 (부모 청크노드 포인터)
		// 이중해제 확인
		if (pRet->overcheck == TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE)
			return enDoubleFree;	// 해제하면 안됨.
		// 오버플로 체크
		if (pRet->overcheck != UNIQUE)
		{
			__debugbreak();
			return enOver;
		}


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		DebugInfo.CancelNode(pRet);
#endif
		//------------------------------------------------------
		// 해제 : RAW버전 / 노드 초기화 진행 후 반환 플래그 올림
		//------------------------------------------------------
		// 소멸자 호출X
		pRet->overcheck = TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE;
		stChunk* pMyChunk = pRet->pParentChunk;
		int myindex = (int)(pRet - pMyChunk->nodeArray);
		// 플래그 올리기
		_InterlockedOr((volatile uint64_t*)&pMyChunk->bitflags[myindex >> 6], TLS_OBJECT_POOL_BITFLAG[myindex & 0x00003F]);

		//------------------------------------------------------
		// 필요하면 반환
		// 얘는 전부 비트가 1이면 무조건 반환이므로 xor로 0과비교 
		//------------------------------------------------------
		if (pMyChunk->releaseFlag)
		{
			uint64_t* pBitflags = pMyChunk->bitflags;
			if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_1, sizeof(pMyChunk->bitflags)) == 0)
			{
				//-----------------------------------------------
				// 실패시 다른애가 한거임
				// 1. 다른애가 릴리즈
				// 2. 해당 청크의 소유자가 재획득
				//-----------------------------------------------
				if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
				{
					// 내가 바꿨으니 해제하자
					ReleaseChunk(pMyChunk);
				}
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
		stChunk* pMyChunk = (stChunk*)TlsGetValue(_tlsIndex_pTlsAllocChunk);
		//---------------------------------------------------
		// 스레드 생성하자마자 풀 안받고 종료한 경우
		//---------------------------------------------------
		if (pMyChunk == nullptr)
		{
			return;
		}

		//-------------------------------------------------------
		// leftNodeCnt > 0 이면 남은 횟수만큼 Alloc후 바로 Free
		//-------------------------------------------------------
		if (pMyChunk->leftNodeCnt > 0)
		{
			int left = pMyChunk->leftNodeCnt;
			T* pNode;
			_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);
			for (int i = 0; i < left; i++)
			{
				if (i == left - 1)
					int a = 0;
				pNode = Alloc();
				Free(pNode);	// 여기서 100% 해제루트 탐.
			}
			BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, nullptr);
			if (ret == FALSE)
			{
				DWORD gle = GetLastError();
				__debugbreak();
			}
			return;
		}

		//------------------------------------------------------
		// ** 여기부터는 0이 있으면 무조건 사용중 **
		// 남은 횟수가 0이면, 비트플래그를 확인하면서 해야됨
		// 내 스레드에서 Alloc과 같은 효과를 내는거니까
		// RAW 버전은 소멸자 호출할게 없음. 
		// 청크에서 소멸자호출은 only CALL ONCE 
		// 
		// 1. releaseFlag를 올림. 
		//    => 이제 내것이 아닐 수 있음.
		// 
		// 2. 플래그 전부 검사해서 FF..FF면 해제 시도,
		//    => 실패하면 떠넘김
		//------------------------------------------------------
		_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);
		if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_1, sizeof(pMyChunk->bitflags)) == 0)
		{
			//-----------------------------------------------
			// 실패시 다른애가 한거임
			// 1. 다른애가 릴리즈
			// 2. 해당 청크의 소유자가 재획득
			//-----------------------------------------------
			if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
			{
				// 내가 바꿨으니 해제하자
				ReleaseChunk(pMyChunk);
			}
		}
		BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, nullptr);
		if (ret == FALSE)
		{
			DWORD gle = GetLastError();
			__debugbreak();
		}
	}

	//-----------------------------------------------------------
	// 공용 풀 청크 수 획득
	//-----------------------------------------------------------
	int GetSize()
	{
		return _chunkNum;
	}
	//-----------------------------------------------------------
	// 프로그램에서 사용중인 청크 수 획득
	//-----------------------------------------------------------
	int GetChunkUsingNum()
	{
		return _createChunkNum;
	}
private:
	struct stChunk;
	//-----------------------------------------------------------
	// 노드 구조체
	//-----------------------------------------------------------
	struct stNode
	{
		stChunk* pParentChunk;
		T data;
		uint64_t overcheck;
		stNode(): pParentChunk(nullptr), overcheck(0), data()
		{

		}
	};
	//-----------------------------------------------------------
	// 청크 구조체
	// . 청크는 없으면 할당하고, 바로 버린다
	//-----------------------------------------------------------
	
	struct stChunk
	{
		int leftNodeCnt;	// 이게 0이되고부터 비트플래그를 찾기
		stChunk* next;
		uint64_t bitflags[TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK];
		stNode nodeArray[TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK];
		long releaseFlag;
		stChunk() :next(nullptr), leftNodeCnt(TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK), 
			bitflags {0,}, releaseFlag(0)
		{
			
		}
	};
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------

	stChunk* AcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
		int loop = -1;
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			loop++;
#endif
			t = _top;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeMakeCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
				if (loop > DebugInfo.maxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.maxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, CALLONCE버전, 생성자 호출 new를 이용
				//-------------------------------------------------
				ret = new stChunk();

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
				DebugInfo.RegisterChunk(ret);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (loop > DebugInfo.maxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.maxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
		DebugInfo.RegisterChunk(ret);
#endif
		_InterlockedDecrement(&_chunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 반환, 모든 청크가 해제됬을 때만
	//-----------------------------------------------------------
	void ReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_chunkNum > TLS_OBJECT_POOL_ARRAY_MAX_CHUNKNUM)
		{
			_InterlockedDecrement(&_createChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			delete pChunk;
			return;
		}
		//-------------------------------------------------------
		// 청크 초기화
		//-------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 6001)
		// pChunk는 내함수가 멀쩡하면 null이아님
		pChunk->leftNodeCnt = TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK;
		memset((void*)pChunk->bitflags, 0, sizeof(pChunk->bitflags));

		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		int loop = -1;
#endif
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;
		do
		{

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			loop++;
#endif
			t = _top;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, (void*)newTop, (void*)t) != t);


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (loop > DebugInfo.maxRaceLoopCnt)
			_InterlockedExchange(&DebugInfo.maxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
#endif

		_InterlockedIncrement(&_chunkNum);
	}


	//-----------------------------------------------------------
	// 풀의 고유한 값 (Free시 잘 왔는지 참조용)
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = TLS_OBJECTPOOL_ARRAY_OVER_CODE ^ __keyValue;

	//-----------------------------------------------------------
	// 스레드별 청크
	//-----------------------------------------------------------
	DWORD _tlsIndex_pTlsAllocChunk;

	//-----------------------------------------------------------
	// 공용 청크 담는 곳
	//-----------------------------------------------------------
	stChunk* _top;
	long _chunkNum;
	long _createChunkNum;

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
public:
	stDebugInfo DebugInfo;
#endif
};

//---------------------------------------------------------------
// ** 지역 선언 금지 **
// NORMAL 버전, Alloc, Free마다 생성자. 소멸자 호출
//---------------------------------------------------------------
template <typename T, int __keyValue>
class CTlsObjectPool_Array<typename T, __keyValue, TLS_OBJECTPOOL_ARRAY_USE_NORMAL>
{
public:
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
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
			freeCnt = 0;
			aquireChunkCnt = 0;
			releaseChunkCnt = 0;
			nodeMakeCnt = 0;
			nodeRemoveCnt = 0;
			chunkMakeCnt = 0;
			chunkRemoveCnt = 0;
			maxRaceLoopCnt = 0;
			totalRaceLoopCnt = 0;
		}
		uint64_t allocCnt;
		uint64_t freeCnt;
		uint64_t aquireChunkCnt;
		uint64_t releaseChunkCnt;
		uint64_t nodeMakeCnt;
		uint64_t nodeRemoveCnt;
		uint64_t chunkMakeCnt;
		uint64_t chunkRemoveCnt;
		uint64_t maxRaceLoopCnt;;
		uint64_t totalRaceLoopCnt;
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
	CTlsObjectPool_Array() :_top(nullptr), _chunkNum(0), _createChunkNum(0)
	{
		//-------------------------------------------------------
		// 64비트가 아니면 생성을 막자
		//-------------------------------------------------------
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
		{
			wprintf_s(L"This System is not 64bit!!\n");
			__debugbreak();
		}

		//-------------------------------------------------------
		// 동적 TLS 사용을 위해 인덱스 획득
		//-------------------------------------------------------
		_tlsIndex_pTlsAllocChunk = TlsAlloc();
		if (_tlsIndex_pTlsAllocChunk == TLS_OUT_OF_INDEXES)
		{
			wprintf_s(L"DTls have no indexes");
			__debugbreak();
		}
	}
	//-----------------------------------------------------------
	// Info: 소멸자
	// Parameter: -
	// Return: -
	//-----------------------------------------------------------
	~CTlsObjectPool_Array()
	{
		//-------------------------------------------------------
		// 락프리 공용 청크풀 해제
		//-------------------------------------------------------
		stChunk* pChunk;
		while (((uint64_t)_top & TLS_OBJECT_POOL_BIT_MASK) != 0)
		{
			pChunk = AcquireChunk();
			//---------------------------------------------------
			// RAW버전은 소멸자 무관
			//---------------------------------------------------
			_InterlockedDecrement(&_createChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			DebugInfo.CancelChunk(pChunk);
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
			_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
#endif
			free((void*)pChunk);
		}
		//-------------------------------------------------------
		// 동적 TLS 반환
		//-------------------------------------------------------
		TlsFree(_tlsIndex_pTlsAllocChunk);


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (DebugInfo.allocCnt != DebugInfo.freeCnt)
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
	template<typename... Args>
	T* Alloc(Args... args)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.allocCnt);
#endif
		//-------------------------------------------------------
		// tls에서 내 청크 가져옴
		//-------------------------------------------------------
		stChunk* pMyChunk = (stChunk*)TlsGetValue(_tlsIndex_pTlsAllocChunk);
		if (pMyChunk == nullptr)
		{
			pMyChunk = AcquireChunk();
			BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, (void*)pMyChunk);
			if (ret == 0)
			{
				DWORD tlsError = GetLastError();
				__debugbreak();
			}
		}

		//-------------------------------------------------------
		// 건네줄 인덱스를 얻고,
		// 세팅을 하고,
		// 그 주소의 노드 건네줌
		//-------------------------------------------------------
		stNode* pRetNode;
		int index;
		if (pMyChunk->leftNodeCnt > 0)
		{
			index = --pMyChunk->leftNodeCnt;
			pRetNode = &pMyChunk->nodeArray[index];
			pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
			pRetNode->overcheck = UNIQUE;
		}
		else
		{
		FIND_NODE:
			//---------------------------------------------------
			// 비트 플래그 뒤져서 줄거 있나 찾기
			//---------------------------------------------------
			int index = -1;
			bool bFind = false;
			int flagIndex = TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK - 1;
			uint64_t* pBitflags = pMyChunk->bitflags;
			for (flagIndex; flagIndex >= 0; flagIndex--)
			{
				if (_BitScanReverse64((DWORD*)&index, (uint64_t)pBitflags[flagIndex]))
				{
					//-------------------------------------------
					// 찾음
					//-------------------------------------------
					bFind = true;
					break;
				}
			}

			//---------------------------------------------------
			// 결과 확인
			//---------------------------------------------------
			if (bFind)
			{
				//-----------------------------------------------
				// 찾음, 실제 인덱스 계산
				// 플래그 0으로 바꾸고 줄 준비
				// 
				// . 누가 현재 영역을 1로 바꿀 수 있으니..해제하면서
				//   인터락 필요
				//-----------------------------------------------
				_InterlockedAnd((volatile uint64_t*)&pMyChunk->bitflags[flagIndex], ~(TLS_OBJECT_POOL_BITFLAG[index]));

				index = (flagIndex << 6) + index;
				pRetNode = &pMyChunk->nodeArray[index];
				pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
				pRetNode->overcheck = UNIQUE;
			}
			else
			{
				//-----------------------------------------------
				// 못찾음, 플래그 올려서 누군가 해제하게 유도
				// . 처음으로 1이 되는 순간
				// . 소유권 포기
				//-----------------------------------------------
				_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);

				//-----------------------------------------------
				// 그래도~ 내가 1로 올리는 사이에 
				// * 모든 플래그가 1이 됬을 수도 있음(희박하게)
				// . 한번만 더 검사... 재사용은 안함. 해제하기로 결정했으니
				// 
				// ** 플래그를 변경하지 못했다 -> 그냥 무조건 남이 쓰러갔다.
				// ** 그냥 청크 새로 받으면 된다.
				//-----------------------------------------------
				if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_0, sizeof(pMyChunk->bitflags)) == 0)
				{
					//-------------------------------------------
					// 전부 0 = 전부 사용중, 새로 청크 받아야됨.
					// 해제 시도는 하면 안됨.
					//-------------------------------------------
				}
				else
				{
					//-------------------------------------------
					// 1이 있으니까 일단 재사용 각 노림
					//-------------------------------------------
					if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
					{
						//---------------------------------------
						// 내가 획득함.
						// 1이 최소 1개 ~ n개
						//---------------------------------------
						goto FIND_NODE;
					}
					else
					{
						//---------------------------------------
						// 재사용에 실패, 이미 누가 0으로 바꿈
						// 그냥 나가면 된다.
						//---------------------------------------
					}
				}

				//-----------------------------------------------
				// 일단 없으니까 청크를 새로 받아오자
				//-----------------------------------------------
				pMyChunk = AcquireChunk();
				BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, (void*)pMyChunk);
				if (ret == 0)
				{
					DWORD tlsError = GetLastError();
					__debugbreak();
				}
				//-----------------------------------------------
				// 새로 받아온 청크에서 하나 주자
				//-----------------------------------------------
				index = --pMyChunk->leftNodeCnt;
				pRetNode = &pMyChunk->nodeArray[index];
				pRetNode->pParentChunk = pMyChunk;	//나중에 해제할 때 필요
				pRetNode->overcheck = UNIQUE;
			}
		}
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		DebugInfo.RegisterNode(pRetNode);
#endif
		//-------------------------------------------------------
		// 생성자 호출
		//-------------------------------------------------------
		new (&pRetNode->data) T(args...);
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
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.freeCnt);
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

		// 언더플로는 접근해서 뻑나는거로 (부모 청크노드 포인터)
		// 이중해제 확인
		if (pRet->overcheck == TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE)
			return enDoubleFree;	// 해제하면 안됨.
		// 오버플로 체크
		if (pRet->overcheck != UNIQUE)
		{
			__debugbreak();
			return enOver;
		}


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		DebugInfo.CancelNode(pRet);
#endif
		//------------------------------------------------------
		// 해제 : NORMAL버전 / 노드 초기화 진행 후 반환 플래그 올림
		//------------------------------------------------------
		// 소멸자 호출
		pRet->overcheck = TLS_OBJECTPOOL_ARRAY_NODE_NOTUSE;
		pRet->data.~T();
		stChunk* pMyChunk = pRet->pParentChunk;
		int myindex = (int)(pRet - pMyChunk->nodeArray);
		// 플래그 올리기
		_InterlockedOr((volatile uint64_t*)&pMyChunk->bitflags[myindex >> 6], TLS_OBJECT_POOL_BITFLAG[myindex & 0x00003F]);

		//------------------------------------------------------
		// 필요하면 반환
		// 얘는 전부 비트가 1이면 무조건 반환이므로 xor로 0과비교 
		//------------------------------------------------------
		if (pMyChunk->releaseFlag)
		{
			uint64_t* pBitflags = pMyChunk->bitflags;
			if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_1, sizeof(pMyChunk->bitflags)) == 0)
			{
				//-----------------------------------------------
				// 실패시 다른애가 한거임
				// 1. 다른애가 릴리즈
				// 2. 해당 청크의 소유자가 재획득
				//-----------------------------------------------
				if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
				{
					// 내가 바꿨으니 해제하자
					ReleaseChunk(pMyChunk);
				}
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
		stChunk* pMyChunk = (stChunk*)TlsGetValue(_tlsIndex_pTlsAllocChunk);
		//---------------------------------------------------
		// 스레드 생성하자마자 풀 안받고 종료한 경우
		//---------------------------------------------------
		if (pMyChunk == nullptr)
		{
			return;
		}

		//-------------------------------------------------------
		// leftNodeCnt > 0 이면 남은 횟수만큼 Alloc후 바로 Free
		//-------------------------------------------------------
		if (pMyChunk->leftNodeCnt > 0)
		{
			int left = pMyChunk->leftNodeCnt;
			T* pNode;
			_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);
			for (int i = 0; i < left; i++)
			{
				if (i == left - 1)
					int a = 0;
				pNode = Alloc();
				Free(pNode);	// 여기서 100% 해제루트 탐.
			}
			BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, nullptr);
			if (ret == FALSE)
			{
				DWORD gle = GetLastError();
				__debugbreak();
			}
			return;
		}

		//------------------------------------------------------
		// ** 여기부터는 0이 있으면 무조건 사용중 **
		// 남은 횟수가 0이면, 비트플래그를 확인하면서 해야됨
		// 내 스레드에서 Alloc과 같은 효과를 내는거니까
		// RAW 버전은 소멸자 호출할게 없음. 
		// 청크에서 소멸자호출은 only CALL ONCE 
		// 
		// 1. releaseFlag를 올림. 
		//    => 이제 내것이 아닐 수 있음.
		// 
		// 2. 플래그 전부 검사해서 FF..FF면 해제 시도,
		//    => 실패하면 떠넘김
		//------------------------------------------------------
		_InterlockedExchange((volatile long*)&pMyChunk->releaseFlag, 1);
		if (memcmp((void*)pMyChunk->bitflags, TLS_OBJECT_POOL_FOR_CMP_ALL_1, sizeof(pMyChunk->bitflags)) == 0)
		{
			//-----------------------------------------------
			// 실패시 다른애가 한거임
			// 1. 다른애가 릴리즈
			// 2. 해당 청크의 소유자가 재획득
			//-----------------------------------------------
			if (_InterlockedCompareExchange((volatile long*)&pMyChunk->releaseFlag, 0, 1) == 1)
			{
				// 내가 바꿨으니 해제하자
				ReleaseChunk(pMyChunk);
			}
		}
		BOOL ret = TlsSetValue(_tlsIndex_pTlsAllocChunk, nullptr);
		if (ret == FALSE)
		{
			DWORD gle = GetLastError();
			__debugbreak();
		}
	}

	//-----------------------------------------------------------
	// 공용 풀 청크 수 획득
	//-----------------------------------------------------------
	int GetSize()
	{
		return _chunkNum;
	}
	//-----------------------------------------------------------
	// 프로그램에서 사용중인 청크 수 획득
	//-----------------------------------------------------------
	int GetChunkUsingNum()
	{
		return _createChunkNum;
	}
private:
	struct stChunk;
	//-----------------------------------------------------------
	// 노드 구조체
	//-----------------------------------------------------------
	struct stNode
	{
		stChunk* pParentChunk;
		T data;
		uint64_t overcheck;
	};
	//-----------------------------------------------------------
	// 청크 구조체
	// . 청크는 없으면 할당하고, 바로 버린다
	//-----------------------------------------------------------
	struct stChunk
	{
		int leftNodeCnt;	// 이게 0이되고부터 비트플래그를 찾기
		stChunk* next;
		uint64_t bitflags[TLS_OBJECT_POOL_BASE_BITFLAGNUM_IN_CHUNK];
		stNode nodeArray[TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK];
		long releaseFlag;
	};
	//-----------------------------------------------------------
	// Info: 공용 풀에서 청크를 얻어옴
	// Parameter: -
	// Return: stChunk* 유효포인터
	//-----------------------------------------------------------

	stChunk* AcquireChunk()
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.aquireChunkCnt);
		int loop = -1;
#endif
		//-----------------------------------------------------------
		// 락프리 청크 풀에서 청크 획득
		//-----------------------------------------------------------
		stChunk* t;
		stChunk* nextTop;
		stChunk* ret;
		uint64_t counter;
		do
		{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			loop++;
#endif
			t = _top;
			ret = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
			//-----------------------------------------------------
			// 널 체크
			//-----------------------------------------------------
			if (ret == nullptr)
			{
				_InterlockedIncrement(&_createChunkNum);
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
				_InterlockedIncrement((uint64_t*)&DebugInfo.chunkMakeCnt);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeMakeCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
				if (loop > DebugInfo.maxRaceLoopCnt)
					_InterlockedExchange((uint64_t*)&DebugInfo.maxRaceLoopCnt, loop);
				_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
#endif
				//-------------------------------------------------
				// 노드 새로 생성, RAW버전 생성자 호출 X
				//-------------------------------------------------
				ret = (stChunk*)malloc(sizeof(stChunk));
#pragma warning(push)
#pragma warning(disable: 6011)
				// nullptr 역참조 경고. 허용함
				ret->next = nullptr;
#pragma warning(pop)
				ret->leftNodeCnt = TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK;
				memset((void*)ret->bitflags, 0, sizeof(ret->bitflags));

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
				DebugInfo.RegisterChunk(ret);
#endif
				return ret;
			}
			//-----------------------------------------------------
			// 카운터 뽑아서 올리고 nextTop 세팅
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			nextTop = (stChunk*)((uint64_t)(ret->next) | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, (void*)nextTop, (void*)t) != t);

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (loop > DebugInfo.maxRaceLoopCnt)
			_InterlockedExchange((uint64_t*)&DebugInfo.maxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
		DebugInfo.RegisterChunk(ret);
#endif
		_InterlockedDecrement(&_chunkNum);
		return ret;
	}
	//-----------------------------------------------------------
	// Info: 공용 풀에 청크를 반환, 모든 청크가 해제됬을 때만
	//-----------------------------------------------------------
	void ReleaseChunk(stChunk* pChunk)
	{
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		_InterlockedIncrement((uint64_t*)&DebugInfo.releaseChunkCnt);
		DebugInfo.CancelChunk(pChunk);
#endif
		//-------------------------------------------------------
		// 청크가 풀에 너무 많으면 버림
		//-------------------------------------------------------
		if (_chunkNum > TLS_OBJECT_POOL_ARRAY_MAX_CHUNKNUM)
		{
			_InterlockedDecrement(&_createChunkNum);
			//---------------------------------------------------
			// RAW버전이라 바로 Delete
			//---------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.nodeRemoveCnt, TLS_OBJECT_POOL_BASE_NODENUM_IN_CHUNK);
			_InterlockedIncrement((uint64_t*)&DebugInfo.chunkRemoveCnt);
#endif
			free((void*)pChunk);
			return;
		}
		//-------------------------------------------------------
		// 청크 초기화
		//-------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 6001)
		// pChunk는 내함수가 멀쩡하면 null이아님
		pChunk->leftNodeCnt = TLS_OBJECT_POOL_ARRAY_BASE_NODENUM_IN_CHUNK;
		memset((void*)pChunk->bitflags, 0, sizeof(pChunk->bitflags));

		//-------------------------------------------------------
		// 락프리 삽입
		//-------------------------------------------------------

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		int loop = -1;
#endif
		stChunk* t;
		stChunk* newTop;
		uint64_t counter;
		do
		{

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
			loop++;
#endif
			t = _top;
			//-----------------------------------------------------
			// top 에서 카운터 뽑아내기
			//-----------------------------------------------------
			counter = ((uint64_t)t >> TLS_OBJECT_POOL_SHIFT_BIT) + 1;
			newTop = (stChunk*)((uint64_t)pChunk | (counter << TLS_OBJECT_POOL_SHIFT_BIT));
#pragma warning(pop)
			pChunk->next = (stChunk*)((uint64_t)t & TLS_OBJECT_POOL_BIT_MASK);
		} while (_InterlockedCompareExchangePointer((void* volatile*)&_top, (void*)newTop, (void*)t) != t);


#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		if (loop > DebugInfo.maxRaceLoopCnt)
			_InterlockedExchange(&DebugInfo.maxRaceLoopCnt, loop);
		_InlineInterlockedAdd64((volatile LONG64*)&DebugInfo.totalRaceLoopCnt, loop);
#endif

		_InterlockedIncrement(&_chunkNum);
	}


	//-----------------------------------------------------------
	// 풀의 고유한 값 (Free시 잘 왔는지 참조용)
	//-----------------------------------------------------------
	static constexpr uint64_t UNIQUE = TLS_OBJECTPOOL_ARRAY_OVER_CODE ^ __keyValue;

	//-----------------------------------------------------------
	// 스레드별 청크
	//-----------------------------------------------------------
	DWORD _tlsIndex_pTlsAllocChunk;

	//-----------------------------------------------------------
	// 공용 청크 담는 곳
	//-----------------------------------------------------------
	stChunk* _top;
	long _chunkNum;
	long _createChunkNum;

#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
public:
	stDebugInfo DebugInfo;
#endif
};

#endif