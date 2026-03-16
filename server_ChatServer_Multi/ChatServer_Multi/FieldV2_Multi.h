#ifndef __FIELD_H__
#define __FIELD_H__
#include <list>
#include <shared_mutex>

#include "ProfilerV2.hpp"
#include "TLSObjectPool_IntrusiveList.hpp"


struct stSectorPos
{
	int iX;
	int iY;
};

struct stSectorAround
{
	int iCount;
	stSectorPos around[9];
};

struct stSector
{
	std::shared_mutex lock;
	std::list<uint64_t> sessionIdList;
};

//-------------------------------------------------------------
// 주변 id목록배열을 담는 버퍼
// . 필요하면 버퍼 리사이징
// . 시작 값을 평균의 두배 권장
//-------------------------------------------------------------
class CAroundSessionId
{
public:
	enum enAroundSessionId
	{
		DEFAULT_CAP = 300
	};

	static int GetResizeCnt()
	{
		return s_resizeCnt;
	}
	//-------------------------------------------------------
	// 사이즈 체크, 공간 부족시 알아서 리사이징
	// 몇개 넣을 지 넘기면 됨.
	//-------------------------------------------------------
	void CheckAndSetCapacity(int sessionIdCnt)
	{
		// 너무 비정상 수치면 무시 (뻑 유도, needCap오염)
		if (sessionIdCnt < 0 || sessionIdCnt >= 1000000)
			return;
		if (sessionIdCnt > _capacity)
		{
			// 리사이징
			free(_buffer);
			_buffer = (uint64_t*)malloc(sizeof(uint64_t) * sessionIdCnt);
			_capacity = sessionIdCnt;

			_InterlockedIncrement(&s_resizeCnt);
		}
		_count = sessionIdCnt;
	}
	//-------------------------------------------------------
	// 버퍼 포인터
	//-------------------------------------------------------
	uint64_t* GetBufferPtr() { return _buffer; }
	void SetCount(int cnt) { _count = cnt; }
	int GetCount() const { return _count; }

	CAroundSessionId() :_buffer(nullptr), _count(0), _capacity(DEFAULT_CAP)
	{
		_buffer = (uint64_t*)malloc(sizeof(uint64_t) * DEFAULT_CAP);
	}
	~CAroundSessionId()
	{
		if (_buffer != nullptr)
			free(_buffer);
	}
private:

	uint64_t* _buffer;
	int _count;
	int _capacity;

	static long s_resizeCnt;
};

class CUser;
//-------------------------------------------------------------
// Field
// 
// < V2 >
// . 최종적으로 섹터 낱개락으로 결정 
// 
// . stl사용해보기 
//   => shared_mutex.cpp, mutex.cpp확인 결과 shared_mutex가 정보가 없어서 가장 가벼움
//      (정말 그냥 SRWLOCK의 래핑이였다. 
//      mutex는 스레드정보 기입 후 타고들어감, SRWLOCK사용은 동일함)
//   => 현재 읽기락이 필요해서 shared_mutex사용
// 
// . 정렬을 직접 Y, X 비교 => 일차원으로 펼치고 (Y * WIDTH + X) 비교
//   => 비교 속도 증가 (분기예측이 2회 => 1회)
// 
// 채팅 서버를 위한 필드입니다. 섹터를 관리합니다.
// ** 락 규칙 
// y가 작은 것 우선 -> x가 작은 것 우선
//-------------------------------------------------------------
class CField
{
	friend class CChatServer;

public:
	enum enField
	{
		FIELD_WIDTH = 50,
		FIELD_HEIGHT = 50
	};
	CField()
	{

	}
	~CField()
	{

	}

	//---------------------------------------------------------
	// Info: 섹터의 리스트를 얻습니다.
	// 성공: 유효 리스트 포인터
	// 실패: nullptr
	//---------------------------------------------------------
	std::list<uint64_t>* GetSectorSessionIdList(int iY, int iX)
	{
		if (iY < 0 || iX < 0 || iY >= FIELD_HEIGHT || iX >= FIELD_WIDTH)
			return nullptr;

		return &_sectors[iY][iX].sessionIdList;
	}
	//---------------------------------------------------------
	// Info: 섹터에 추가
	// false: 좌표 이상
	//---------------------------------------------------------
	bool AddSector(int iY, int iX, uint64_t sessionId)
	{
		if (iY < 0 || iX < 0 || iY >= FIELD_HEIGHT || iX >= FIELD_WIDTH)
			return false;

		stSector* pSector = &_sectors[iY][iX];
		pSector->lock.lock();

		pSector->sessionIdList.push_back(sessionId);

		pSector->lock.unlock();
		
		return true;
	}
	//---------------------------------------------------------
	// Info: 섹터에서 제거
	// return: 제거 개수(1 or 0), -1은 좌표이상
	//---------------------------------------------------------
	int RemoveSector(int iY, int iX, uint64_t sessionId)
	{
		if (iY < 0 || iX < 0 || iY >= FIELD_HEIGHT || iX >= FIELD_WIDTH)
			return -1;

		stSector* pSector = &_sectors[iY][iX];
		pSector->lock.lock();

		auto end = pSector->sessionIdList.end();
		for (auto it = pSector->sessionIdList.begin(); it != end; ++it)
		{
			if(*it == sessionId)
			{
				pSector->sessionIdList.erase(it);
				pSector->lock.unlock();
				return 1;
			}
		}

		pSector->lock.unlock();
		
		return 0;
	}
	//---------------------------------------------------------
	// Info: 기존 섹터에서 제거 후 추가
	// 반환: 제거 개수
	//---------------------------------------------------------
	int RemoveAndAddSector(int removeY, int removeX, int newY, int newX, uint64_t sessionId)
	{
		if (newY < 0 || newX < 0 || newY >= FIELD_HEIGHT || newX >= FIELD_WIDTH)
			return false;

		if (removeY < 0 || removeX < 0 || removeY >= FIELD_HEIGHT || removeX >= FIELD_WIDTH)
		{
			AddSector(newY, newX, sessionId);
			return 0;
		}

		//----------------------------------------------------------
		// 2개의 구역에 락이 필요
		// 순차적 락 후 순차적 해제
		// 
		// ** 순서는 y * width + x가 작을수록 우선, 같으면 같은좌표
		//----------------------------------------------------------
		std::shared_mutex* pFirstLock;
		std::shared_mutex* pSecondLock;
		stSector* pRemoveSector = &_sectors[removeY][removeX];
		stSector* pNewSector = &_sectors[newY][newX];
		int removeOrder = removeY * FIELD_WIDTH + removeX;
		int newOrder = newY * FIELD_WIDTH + newX;
		if (removeOrder > newOrder)
		{
			pFirstLock = &pNewSector->lock;
			pSecondLock = &pRemoveSector->lock;
		}
		else // removeOrder < newOrder (만약 같게 왔으면 strangeCnt올리고, 무시했어야 한다.
		{
			pFirstLock = &pRemoveSector->lock;
			pSecondLock = &pNewSector->lock;
		}

		//PRO_BEGIN(L"SECTOR_LOCKS(2)_EXCLUSIVE");

		pFirstLock->lock();
		pSecondLock->lock();

		//PRO_END(L"SECTOR_LOCKS(2)_EXCLUSIVE");

		std::list<uint64_t>::iterator end = pRemoveSector->sessionIdList.end();
		int ret = 0;
		for (std::list<uint64_t>::iterator it = pRemoveSector->sessionIdList.begin(); it != end; ++it)
		{
			if (*it == sessionId)
			{
				pRemoveSector->sessionIdList.erase(it);
				ret = 1;
				break;
			}
		}

		pNewSector->sessionIdList.push_back(sessionId);

		pFirstLock->unlock();
		pSecondLock->unlock();

		return ret;
	}


	static bool InSectorRange(int iY, int iX)
	{
		return (0 <= iX && iX < CField::FIELD_WIDTH && 0 <= iY && iY < CField::FIELD_HEIGHT);
	}

	//-----------------------------------------------
	// 락의 순서에 맞게 주변 섹터 좌표를 줌
	//-----------------------------------------------
	static void GetAround(stSectorPos* pSector, stSectorAround* pAround);

	// Exclusive: 반드시 y가 작은 것 우선 -> x가 작은 것 우선 넣어주세요
	void LocksExclusive(stSectorAround* pSectors)
	{
		//PRO_BEGIN(L"SECTOR_LOCKS(1~2)_EXCLUSIVE");

		int iCount = pSectors->iCount;
		stSectorPos* pSectorsArray = pSectors->around;
		for (int i = 0; i < iCount; i++)
		{
			_sectors[pSectorsArray[i].iY][pSectorsArray[i].iX].lock.lock();
		}

		//PRO_END(L"SECTOR_LOCKS(1~2)_EXCLUSIVE");
	}
	// Exclusive: 반드시 y가 작은 것 우선 -> x가 작은 것 우선 넣어주세요
	void UnlocksExclusive(stSectorAround* pSectors)
	{
		int iCount = pSectors->iCount;
		stSectorPos* pSectorsArray = pSectors->around;
		for (int i = 0; i < iCount; i++)
		{
			_sectors[pSectorsArray[i].iY][pSectorsArray[i].iX].lock.unlock();
		}
	}
	// Shared: 반드시 y가 작은 것 우선 -> x가 작은 것 우선 넣어주세요
	void LocksShared(stSectorAround* pSectors)
	{
		//PRO_BEGIN(L"SECTOR_LOCKS(1~9)_SHARED");

		int iCount = pSectors->iCount;
		stSectorPos* pSectorsArray = pSectors->around;
		for (int i = 0; i < iCount; i++)
		{
			_sectors[pSectorsArray[i].iY][pSectorsArray[i].iX].lock.lock_shared();
		}

		//PRO_END(L"SECTOR_LOCKS(1~9)_SHARED");
	}
	// Shared: 반드시 y가 작은 것 우선 -> x가 작은 것 우선 넣어주세요
	void UnlocksShared(stSectorAround* pSectors)
	{
		int iCount = pSectors->iCount;
		stSectorPos* pSectorsArray = pSectors->around;
		for (int i = 0; i < iCount; i++)
		{
			_sectors[pSectorsArray[i].iY][pSectorsArray[i].iX].lock.unlock_shared();
		}
	}
	//----------------------------------------------------------------
	// pBuffer은 tls를 사용합시다.
	//----------------------------------------------------------------
	void GetAroundUserSessionId(stSectorPos* pSector, CAroundSessionId* pBuffer)
	{
		stSectorAround pAround;
		GetAround(pSector, &pAround);
		int Cnt = 0;

		//-----------------------------------------------
		// 락 4 ~ 9개 획득 (4 / 6/ 9 중 한개)
		//-----------------------------------------------
		LocksShared(&pAround);

		for (int i = 0; i < pAround.iCount; i++)
		{
			Cnt += (int)_sectors[pAround.around[i].iY][pAround.around[i].iX].sessionIdList.size();
		}
		pBuffer->SetCount(Cnt);
		pBuffer->CheckAndSetCapacity(Cnt);
		uint64_t* pBuf = pBuffer->GetBufferPtr();

		int index = 0;
		for (int i = 0; i < pAround.iCount; i++)
		{
			stSector* pCurSector = &_sectors[pAround.around[i].iY][pAround.around[i].iX];
			auto end = pCurSector->sessionIdList.end();
			for (auto it = pCurSector->sessionIdList.begin(); it != end; ++it)
			{
				*pBuf = (*it);
				pBuf++;
			}
			//----------------------------------------------
			// 다 쓰면 바로바로 읽기락 해제
			//----------------------------------------------
			pCurSector->lock.unlock_shared();
		}

		// 섹터 다 쓰면 바로 해제하려고 (여기서 한번에 안하고)
		// UnlocksShared(&pAround);
	}
	//----------------------------------------------------------------
	// ids 에 새로운 버퍼를 할당해서 (cnt개 만큼)
	// 주변 유저 세션 아이디 목록을 줍니다.
	// ids는 free로 해제
	//----------------------------------------------------------------
	void GetAroundUserSessionId(stSectorPos* pSector, uint64_t** ids, int* cnt)
	{
		stSectorAround pAround;
		GetAround(pSector, &pAround);
		int Cnt = 0;

		//-----------------------------------------------
		// 락 4 ~ 9개 획득 (4 / 6/ 9 중 한개)
		//-----------------------------------------------
		LocksShared(&pAround);

		for (int i = 0; i < pAround.iCount; i++)
		{
			Cnt += (int)_sectors[pAround.around[i].iY][pAround.around[i].iX].sessionIdList.size();
		}
		*cnt = Cnt;

		*ids = (uint64_t*)malloc(sizeof(uint64_t) * Cnt);

		int index = 0;
		for (int i = 0; i < pAround.iCount; i++)
		{
			stSector* pCurSector = &_sectors[pAround.around[i].iY][pAround.around[i].iX];
			auto end = pCurSector->sessionIdList.end();
			for (auto it = pCurSector->sessionIdList.begin(); it != end; ++it)
			{
#pragma warning(push)
#pragma warning(disable: 6011) // nullptr이면 뻑나게 (메모리 부족)
				*((*ids)+index) = (*it);
				index++;
#pragma warning(pop)
			}
			//----------------------------------------------
			// 다 쓰면 바로바로 읽기락 해제
			//----------------------------------------------
			pCurSector->lock.unlock_shared();
		}

		// 섹터 다 쓰면 바로 해제하려고 (여기서 한번에 안하고)
	}
private:
	stSector _sectors[FIELD_HEIGHT][FIELD_WIDTH];
};
extern CField g_field;




#endif