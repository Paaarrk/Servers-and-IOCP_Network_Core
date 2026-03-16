#ifndef __USER_H__
#define __USER_H__

#include <stdint.h>
#include <windows.h>
#include <unordered_map>
#include <shared_mutex>

#include "LockFreeStack.hpp"

//-----------------------------------------------------
// 유저
// 
// < V2 : 유저의 세션 의존관계 삭제 >
// . 기존 생성(세션->유저) / 삭제(유저->세션) 규칙이 깨짐
// . 삭제(세션 -> 유저) 순서가 될 수 있게되어 개별적 관리 필요
// . OnRelease()가 비동기 이벤트가 되면서 어쩔 수 없음.
// 
// < V1_Multi >
// . 멀티 스레드에서 사용할 유저클래스
// . 세션의 확장 구조 느낌이다.
//-----------------------------------------------------

class CUser
{
	friend class CUserManager;
	friend class CLoginServer;
	
public:
	enum enUserState
	{
		STATE_NONE = 0,
		STATE_WAIT_LOGIN,
		STATE_WAIT_IDENTIFY,
		STATE_LOGIN,
		STATE_FIN_LOGIN,
		USER_POOL_KEY = 0x0000EEEE
	};
	
	void Lock()
	{
		_lock.lock();
	}
	void Unlock()
	{
		_lock.unlock();
	}

	//-----------------------------------------
	// 유저 멤버들을 초기화 합니다.
	//-----------------------------------------
	void Reset()
	{
		_sessionId = 0;
		_state = STATE_NONE;
		_lastRecvTime = 0;
		_strangeCnt = 0;
		_accountNo = -1;
		memset(_id, 0, 40);
		memset(_nickName, 0, 40);
		memset(_sessionKey, 0, 64);
	}

private:
	CUser():_sessionId(0), _state(STATE_NONE), _lastRecvTime(0), _lock(),
		_strangeCnt(0), _accountNo(-1), _id{}, _nickName{}, _sessionKey{},
		_isDummyWhat(0), _ip{}
	{
		
	}
	~CUser()
	{

	}

	uint64_t _sessionId;
	int _state;
	DWORD _lastRecvTime;
	std::shared_mutex _lock;

	int _strangeCnt;

	int64_t _accountNo;
	wchar_t _id[20];
	wchar_t _nickName[20];
	char _sessionKey[64];
	int _isDummyWhat;
	in_addr _ip;
};
//-----------------------------------------------------
// 어카운트 넘버 -> 세션아이디 (로그인 계정 확인용)
//-----------------------------------------------------
class CLoginMap
{
	friend class CUserManager;
public:
	CLoginMap()
	{

	}
	~CLoginMap()
	{

	}
	//-------------------------------------------------
	// 계정 찾기 (sessionId to SessionId)
	// . 찾으면 sessionId
	// . 못찾으면 0
	//-------------------------------------------------
	uint64_t FindSessionId(int64_t accountNo)
	{
		_lock.lock();
		auto it = _loginMap.find(accountNo);
		_lock.unlock();
		if (it == _loginMap.end())
			return 0;
		return it->second;
	}
	//-------------------------------------------------
	// 계정 삭제
	//-------------------------------------------------
	void RemoveAccountNo(int64_t accountNo)
	{
		_lock.lock();
		_loginMap.erase(accountNo);
		_lock.unlock();
	}
	//-------------------------------------------------
	// 계정 추가 (검색과 찾기를 한번에)
	// . 기존 계정이 없다면 반환은 0 및 정상적으로 현재 요청 추가
	// . 기존 계정이 있으면 반환은 해당 계정의 sessionId, 현재 요청은 실패
	//-------------------------------------------------
	uint64_t FindAndAddAcountNo(int64_t accountNo, uint64_t sessionId)
	{
		_lock.lock();
		std::unordered_map<int64_t, uint64_t>::iterator it = _loginMap.find(accountNo);
		if (it == _loginMap.end())
		{
			_loginMap.insert({ accountNo, sessionId });
			_lock.unlock();
			return 0;
		}
		_lock.unlock();
		return it->second;
	}

	//-------------------------------------------------
	// 계정 추가 (검색과 찾기를 한번에)
	// . 기존 계정이 없다면 반환은 0 및 정상적으로 현재 요청 추가
	// . 기존 계정이 있으면 반환은 해당 계정의 sessionId, 현재 요청 *성공*
	//-------------------------------------------------
	uint64_t FindAndAddAcountNoEx(int64_t accountNo, uint64_t sessionId)
	{
		uint64_t ret;
		_lock.lock();
		std::unordered_map<int64_t, uint64_t>::iterator it = _loginMap.find(accountNo);
		if (it == _loginMap.end())
		{
			_loginMap.insert({ accountNo, sessionId });
			_lock.unlock();
			return 0;
		}
		else
		{
			ret = it->second;
			it->second = sessionId; // 새로 들어온 유저꺼로 바꿈
		}
		_lock.unlock();
		return ret;
	}

	//-------------------------------------------------
	// 계정 추가
	//-------------------------------------------------
	uint64_t AddAcountNo(int64_t accountNo, uint64_t sessionId)
	{
		_lock.lock();
		_loginMap.insert({ accountNo, sessionId });
		_lock.unlock();
	}

private:
	std::unordered_map<int64_t, uint64_t> _loginMap;
	std::shared_mutex _lock;
};

//-----------------------------------------------------
// 세션 아이디 -> 유저 포인터
//-----------------------------------------------------
class CSessionIdToUser
{
	friend class CUserManager;
public:
	CSessionIdToUser()
	{

	}
	~CSessionIdToUser()
	{

	}
	//-------------------------------------------------
	// 계정 찾기 (sessionId to CUser*)
	// . 찾으면 CUser*
	// . 못찾으면 nullptr
	//-------------------------------------------------
	CUser* FindUser(uint64_t sessionId)
	{
		_lock.lock();
		auto it = _sidToUserMap.find(sessionId);
		_lock.unlock();
		if (it == _sidToUserMap.end())
			return nullptr;
		return it->second;
	}
	//-------------------------------------------------
	// 세션 매핑 삭제
	//-------------------------------------------------
	void RemoveSessionId(uint64_t sessionId)
	{
		_lock.lock();
		_sidToUserMap.erase(sessionId);
		_lock.unlock();
	}

	//-------------------------------------------------
	// 계정 추가
	//-------------------------------------------------
	void AddSessionId(uint64_t sessionId, CUser* pUser)
	{
		_lock.lock();
		_sidToUserMap.insert({ sessionId, pUser });
		_lock.unlock();
	}

private:
	std::unordered_map<uint64_t, CUser*> _sidToUserMap;
	std::shared_mutex _lock;
};

//-----------------------------------------------------
// 유저를 다루는 매니저
//-----------------------------------------------------
class CUserManager
{
	friend int main();
	friend class CLoginServer;
public:
	enum enUserManager
	{
		USER_MAP_MASK = 0x3F,
		USER_MAP_CNT = 64
	};
	//-------------------------------------------------------
	// 유저 배열 관리
	//-------------------------------------------------------
	class UserStructure
	{
		friend class CLoginServer;
	public:
		UserStructure() :_usersArray(nullptr), _usersCnt(0), _maxUserCnt(0) {}
		~UserStructure()
		{
			if (_usersArray != nullptr)	//생성이 되어있음
				delete[] _usersArray;
		}
		//-------------------------------------------------------
		// 세션 구조 초기화, maxCnt: 배열 확보
		//-------------------------------------------------------
		bool Init(int maxCnt);
		//-------------------------------------------------------
		// 세션 획득, 자리가 없으면 nullptr반환, 자리가 없으면 이상한 것
		// index, CUser* 둘다 얻음, 
		// 실패 시 index = -1, 반환은 nullptr
		//-------------------------------------------------------
		CUser* AcquireUser(int* index);
		//-------------------------------------------------------
		// 세션 반환 (클리어 하고 반환하세요)
		// index가 이상하면 false반환
		//-------------------------------------------------------
		bool ReleaseUser(CUser* pSession);
	private:
		CUser* _usersArray;
		volatile long _usersCnt;
		int _maxUserCnt;

		Core::CLockFreeStack<int> _indexStack;
	};


	CUserManager() :_maxUserCnt(0), _maxSessionCnt(0), _currentUserCnt(0), 
		_userLoginMap(nullptr), _userSidToUserMap(nullptr)
	{

	}

	~CUserManager()
	{
		if (_userLoginMap != nullptr)
			delete[] _userLoginMap;
		if (_userSidToUserMap != nullptr)
			delete[] _userSidToUserMap;
 	}

	//------------------------------------------------
	// 시작 Init 맵 크기라던지 등등 세팅함
	//------------------------------------------------
	void Init(int maxSession, int maxUser)
	{
		_maxSessionCnt = maxSession;
		_maxUserCnt = maxUser;

		_userStructure.Init(maxSession);

		_userLoginMap = new CLoginMap[USER_MAP_CNT];
		for (int i = 0; i < USER_MAP_CNT; i++)
		{
			_userLoginMap[i]._loginMap.reserve(maxUser);
		}

		_userSidToUserMap = new CSessionIdToUser[USER_MAP_CNT];
		for (int i = 0; i < USER_MAP_CNT; i++)
		{
			_userSidToUserMap[i]._sidToUserMap.reserve(maxUser);
		}
	}

	//-------------------------------------------------
	// 로그인중인 유저라면 세션아이디를,
	// 로그인 아니면 0을 반환 (세션아이디는 0이 없어서)
	// 0반환이 아니면 요청은 실패.
	//-------------------------------------------------
	uint64_t AddLoginMap(int64_t accountNo, uint64_t sessionId)
	{
		int index = (int)(accountNo & USER_MAP_MASK);
		return _userLoginMap[index].FindAndAddAcountNo(accountNo, sessionId);
	}
	//-------------------------------------------------
	// 로그인중인 유저라면 세션아이디를,
	// 로그인 아니면 0을 반환 (세션아이디는 0이 없어서)
	// 기존 요청은 성공함.
	//-------------------------------------------------
	uint64_t AddLoginMapEx(int64_t accountNo, uint64_t sessionId)
	{
		int index = (int)(accountNo & USER_MAP_MASK);
		return _userLoginMap[index].FindAndAddAcountNoEx(accountNo, sessionId);
	}

	void RemoveLoginMap(int64_t accountNo)
	{
		int index = (int)(accountNo & USER_MAP_MASK);
		_userLoginMap[index].RemoveAccountNo(accountNo);
	}

	//-------------------------------------------------
	// 등록
	//-------------------------------------------------
	void AddSidToUserMap(uint64_t sessionId, CUser* pUser)
	{
		int index = (int)(sessionId & USER_MAP_MASK);
		_userSidToUserMap[index].AddSessionId(sessionId, pUser);
	}
	void RemoveSidToUserMap(uint64_t sessionId)
	{
		int index = (int)(sessionId & USER_MAP_MASK);
		_userSidToUserMap[index].RemoveSessionId(sessionId);
	}
	CUser* FindUser(uint64_t sessionId)
	{
		int index = (int)(sessionId & USER_MAP_MASK);
		return _userSidToUserMap[index].FindUser(sessionId);
	}

	CUser* CreateWaitLogin(uint64_t sessionId, DWORD time, in_addr ip)
	{
		int index;
		CUser* pUser = _userStructure.AcquireUser(&index);
		if (pUser == nullptr)
			return nullptr;

		pUser->Lock();
		
		pUser->_sessionId = sessionId;
		pUser->_state = CUser::STATE_WAIT_LOGIN;
		pUser->_lastRecvTime = time;
		pUser->_ip = ip;

		AddSidToUserMap(sessionId, pUser);

		pUser->Unlock();
		return pUser;
	}

	

	//-----------------------------------------
	// 유저(미로그인 포함)를 제거합니다.
	// ** 외부에서 유저에 락 걸고 해야합니다 **
	//-----------------------------------------
	void ReleaseUser(CUser* pUser)
	{
		//--------------------------------------------
		// STATE_NONE이라면 이미 서버에서 먼저 제거한 것
		//--------------------------------------------
		if (pUser->_state != CUser::STATE_NONE)
		{
			if (pUser->_state != CUser::STATE_WAIT_LOGIN
				&& pUser->_state != CUser::STATE_WAIT_IDENTIFY)
			{
				//------------------------------------
				// (로그인 정리)
				//------------------------------------
				RemoveLoginMap(pUser->_accountNo);
				--_currentUserCnt;
			}

			RemoveSidToUserMap(pUser->_sessionId);
			pUser->Reset();
			_userStructure.ReleaseUser(pUser);
		}
	}

	//------------------------------------------------
	// 총 어카운트 수(참고용)
	//------------------------------------------------
	int GetAccountCnt()
	{
		int Cnt = 0;
		for (int i = 0; i < USER_MAP_CNT; i++)
		{
			Cnt += (int)_userLoginMap[i]._loginMap.size();
		}
		return Cnt;
	}

	//------------------------------------------------
	// 총 유저구조체 수(참고용)
	//------------------------------------------------
	int GetUserStructCnt()
	{
		int Cnt = 0;
		for (int i = 0; i < USER_MAP_CNT; i++)
		{
			Cnt += (int)_userSidToUserMap[i]._sidToUserMap.size();
		}
		return Cnt;
	}

	int GetUserCnt()
	{
		return _currentUserCnt.load();
	}


	void IncreaseUserCnt()
	{
		++_currentUserCnt;
	}

private:
	// 최대 세션 수
	int _maxSessionCnt;
	// 최대 유저 수
	int _maxUserCnt;
	// 현재 로그인 한 유저 수
	std::atomic_int32_t _currentUserCnt;

	UserStructure _userStructure;

	// 계정 검색용 해시 (sessionId -> sessionId)
	CLoginMap* _userLoginMap;
	// 세션아이디->유저포인터 해시
	CSessionIdToUser* _userSidToUserMap;
};


#endif