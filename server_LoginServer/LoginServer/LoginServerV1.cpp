#include "LoginServerV1.h"
#include "NetProcess.h"

#include <timeapi.h>
#pragma comment(lib, "winmm")

#include "21_TextParser.h"
#include "ProfilerV2.hpp"
#include "logclassV1.h"

CUserManager g_userManager;

using Log = Core::c_syslog;
using namespace Core;

#pragma region CChatServerMonitoringJob

CLoginServerMonitoringJob::CLoginServerMonitoringJob()
{
	_startTime = timeGetTime();
}

void CLoginServerMonitoringJob::Excute()
{
	_loginRecvTps = _loginSuccessCnt.exchange(0, std::memory_order_seq_cst);

	DWORD curTime = timeGetTime();
	int32 deltaTime = SERVER_MONITORING_TICK - (int32)(curTime - _startTime);
	if (deltaTime < 0)
	{
		_startTime = curTime + SERVER_MONITORING_TICK;
		Log::logging().Log(TAG_TIMER, Log::en_ERROR, L"CChatServer Monitoring Excute(): [delta ms: %d]  문제있음 (빡빡)", -deltaTime);
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, SERVER_MONITORING_TICK, curTime);
	}
	else
	{
		_startTime += SERVER_MONITORING_TICK;
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, deltaTime, curTime);
	}
}

#pragma endregion

#pragma region CChatServer::stChatServerOpt

bool CLoginServer::stLoginServerOpt::LoadOption(const char* path)
{
	CParser config(path);
	try
	{
		config.LoadFile();

		int bUseEncode;
		config.GetValue("LOGIN_SERVER", "bUseEncode", &bUseEncode);
		this->bUseEncode = (bool)bUseEncode;

		int bUseSO_SNDBUF;
		config.GetValue("LOGIN_SERVER", "bUseSO_SNDBUF", &bUseSO_SNDBUF);
		this->bUseSO_SNDBUF = (bool)bUseSO_SNDBUF;

		int bUseTCP_NODELAY;
		config.GetValue("LOGIN_SERVER", "bUseTCP_NODELAY", &bUseTCP_NODELAY);
		this->bUseTCP_NODELAY = (bool)bUseTCP_NODELAY;

		int iMaxConcurrentUsers;
		config.GetValue("LOGIN_SERVER", "iMaxConcurrentUsers", &iMaxConcurrentUsers);
		this->iMaxConcurrentUsers = iMaxConcurrentUsers;

		int iMaxUsers;
		config.GetValue("LOGIN_SERVER", "iMaxUsers", &iMaxUsers);
		this->iMaxUsers = iMaxUsers;

		int iWorkerThreadCreateCnt;
		config.GetValue("LOGIN_SERVER", "iWorkerThreadCreateCnt", &iWorkerThreadCreateCnt);
		this->iWorkerThreadCreateCnt = iWorkerThreadCreateCnt;

		int iWorkerThreadRunCnt;
		config.GetValue("LOGIN_SERVER", "iWorkerThreadRunCnt", &iWorkerThreadRunCnt);
		this->iWorkerThreadRunCnt = iWorkerThreadRunCnt;

		char openIP[16];
		config.GetValue("LOGIN_SERVER", "openIP", openIP);
		MultiByteToWideChar(CP_ACP, 0, openIP, 16, this->openIP, 16);

		int port;
		config.GetValue("LOGIN_SERVER", "port", &port);
		this->port = (unsigned short)port;

		char gameIP[16];
		config.GetValue("LOGIN_SERVER", "gameIP", gameIP);
		MultiByteToWideChar(CP_ACP, 0, gameIP, 16, this->gameIP, 16);

		int gamePort;
		config.GetValue("LOGIN_SERVER", "gamePort", &gamePort);
		this->gamePort = (unsigned short)gamePort;

		char chatIP[16];
		config.GetValue("LOGIN_SERVER", "chatIP", chatIP);
		MultiByteToWideChar(CP_ACP, 0, chatIP, 16, this->chatIP, 16);

		char chatIPforDummy1[16];
		config.GetValue("LOGIN_SERVER", "dummyOne", chatIPforDummy1);
		MultiByteToWideChar(CP_ACP, 0, chatIPforDummy1, 16, this->chatIPforDummy1, 16);

		char chatIPforDummy2[16];
		config.GetValue("LOGIN_SERVER", "dummyTwo", chatIPforDummy2);
		MultiByteToWideChar(CP_ACP, 0, chatIPforDummy2, 16, this->chatIPforDummy2, 16);

		char chatIPforDummy3[16];
		config.GetValue("LOGIN_SERVER", "dummyThree", chatIPforDummy3);
		MultiByteToWideChar(CP_ACP, 0, chatIPforDummy3, 16, this->chatIPforDummy3, 16);

		int chatPort;
		config.GetValue("LOGIN_SERVER", "chatPort", &chatPort);
		this->chatPort = (unsigned short)chatPort;


		int serverCode;
		config.GetValue("LOGIN_SERVER", "serverCode", &serverCode);
		server_code = (uint8)serverCode;

		int staticKey;
		config.GetValue("LOGIN_SERVER", "staticKey", &staticKey);
		this->static_key = (uint8)staticKey;

		// Redis
		char redisIP[16];
		config.GetValue("REDIS_SESSIONKEY", "redisIP", redisIP);
		this->redisIP = redisIP;
		int redisPort;
		config.GetValue("REDIS_SESSIONKEY", "redisPort", &redisPort);
		this->redisPort = (unsigned short)redisPort;

		// MySql
		config.GetValue("DB_MYSQL_ACCOUNTDB", "mysqlIP", this->mysqlIP);
		int mysqlPort;
		config.GetValue("DB_MYSQL_ACCOUNTDB", "mysqlPort", &mysqlPort);
		this->mysqlPort = (unsigned short)mysqlPort;
		char mysqlId[20];
		config.GetValue("DB_MYSQL_ACCOUNTDB", "mysqlId", mysqlId);
		this->mysqlId = mysqlId;
		char mysqlPw[40];
		config.GetValue("DB_MYSQL_ACCOUNTDB", "mysqlPw", mysqlPw);
		this->mysqlPw = mysqlPw;
		char mysqlSchema[40];
		config.GetValue("DB_MYSQL_ACCOUNTDB", "mysqlSchema", mysqlSchema);
		this->mysqlSchema = mysqlSchema;

		// Whitelist
		int whitenum;
		config.GetValue("WHITE_IP_LIST", "num", &whitenum);
		this->whiteNum = (unsigned short)whitenum;

		for (int i = 0; i < whitenum; i++)
		{
			config.GetValue("WHITE_IP_LIST", "ipList", i, "ip", this->ip[i]);
		}

		inet_pton(AF_INET, "127.0.0.1", &dummy1);
		inet_pton(AF_INET, "10.0.1.2", &dummy2);
		inet_pton(AF_INET, "10.0.2.2", &dummy3);
	}
	catch (std::invalid_argument& e)
	{
		wchar_t buffer[256];
		MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, 256);
		Log::logging().Log(TAG_CONTENTS, Core::c_syslog::en_ERROR,
			L"%s", buffer);
		return false;
	}
	return true;
}

#pragma endregion

bool CLoginServer::OnInit(const stServerOpt* pSopt)
{
	stLoginServerOpt* pOpt = (stLoginServerOpt*)pSopt;

	//----------------------------------------------------------
	// 해시 구조 공간 확보를 위해서 세션수, 맥스 유저수 넘김
	//----------------------------------------------------------
	_maxUser = pOpt->iMaxUsers;
	g_userManager.Init(_option.iMaxConcurrentUsers, _maxUser);

	_redisConn.Init(_option.redisIP,
		_option.redisPort, 0);

	_mysqlConn.SetConnector(_option.mysqlIP, _option.mysqlId.c_str(),
		_option.mysqlPw.c_str(), _option.mysqlSchema.c_str(),
		_option.mysqlPort);

	in_addr addrIp;
	for(int i = 0; i < pOpt->whiteNum; i++)
	{
		inet_pton(AF_INET, pOpt->ip[i], &addrIp);
		_whiteList.AddWhiteList(addrIp);
	}

	_hEventTimeoutCheckThread = CreateEventW(NULL, FALSE, FALSE, L"LoginTimeOutCheck");
	if (_hEventTimeoutCheckThread == NULL)
		return false;

	_hEventForExit = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (_hEventForExit == NULL)
	{
		Log::logging().LogEx(TAG_CONTENTS, GetLastError(), Log::en_ERROR, NULL);
		return false;
	}

	_timeoutCheckThread = std::thread(TimeoutCheckThread, this);

	Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM,
		L"On Init!!() : MaxUser:%d / WhiteList (num: %d)", _maxUser, pOpt->whiteNum);


	_monitoringJob = std::make_shared<CLoginServerMonitoringJob>();
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_monitoringJob, 0);

	return true;
}

void CLoginServer::OnStop()
{
	
}

void CLoginServer::OnExit()
{
	SetEvent(_hEventForExit);
	if (_timeoutCheckThread.joinable())
		_timeoutCheckThread.join();

	CloseHandle(_hEventForExit);
	CloseHandle(_hEventTimeoutCheckThread);
	_monitoringJob->CancelJob();
}

void CLoginServer::OnWorkerStart()
{
	
}

void CLoginServer::OnWorkerEnd()
{
	_redisConn.Disconnect();
	_mysqlConn.ReleaseConn();
}

void CLoginServer::OnUserEvent(Net::CPacket* pPacket)
{
	int type;
	*pPacket >> type;
	CPACKET_UPDATE_TRACE(pPacket);
	Net::CPacketPtr autoFree(pPacket);

	switch (type)
	{
	case en_LOGINSERVER_EVENT::RequestMySql:
	{
		CUser* pUser;
		uint64_t sessionId;
		*pPacket >> sessionId;
		*pPacket >> (uint64_t&)pUser;
		pUser->Lock();
		if (pUser->_sessionId != sessionId)
		{
			//-------------------------------------
			// 이미 끊어진 연결
			//-------------------------------------
			pUser->Unlock();
			return;
		}

		//-----------------------------------------
		// MySql에 조회
		//-----------------------------------------
		int ret = _mysqlConn.RequestQuery(L"SELECT userid, usernick, sessionkey FROM v_account WHERE accountno = %lld;",
			(long long)pUser->_accountNo);
		if (ret)
		{
			Log::logging().Log(TAG_DB, Log::en_ERROR, L"DB연결에 문제 생김 [%d](1: 재연결 실패, 2: 쿼리 오류)", ret);
			Disconnect(sessionId);
			pUser->Unlock();
			return;
		}

		MYSQL_RES* res = _mysqlConn.GetResult();
		if (res == nullptr)
		{
			Log::logging().Log(TAG_DB, Log::en_ERROR, L"Mysql _ res 가 null");
			Disconnect(sessionId);
			pUser->Unlock();
			return;
		}
		uint64_t num = mysql_num_rows(res);
		if (num == 0)
		{
			Log::logging().Log(TAG_DB, Log::en_ERROR, L"[accountNo: %lld] 검색된 row가 없어 끊음", pUser->_accountNo);
			Disconnect(sessionId);
			pUser->Unlock();
			_mysqlConn.FreeResult();
			return;
		}

		MYSQL_ROW sqlrow = mysql_fetch_row(res);
		MultiByteToWideChar(CP_UTF8, 0, sqlrow[0], -1, pUser->_id, 20);
		MultiByteToWideChar(CP_UTF8, 0, sqlrow[1], -1, pUser->_nickName, 20);
		if(sqlrow[2] != nullptr)	
			int ret = memcmp(pUser->_sessionKey, sqlrow[2], 64);
		_mysqlConn.FreeResult();

		pUser->Unlock();

		//-----------------------------------------
		// PQCS 로 레디스 등록하기
		//-----------------------------------------
		CPACKET_CREATE(postRedisRequest);
		postRedisRequest->SetRecvBuffer();
		*postRedisRequest << (int)en_LOGINSERVER_EVENT::RequestRedis;
		*postRedisRequest << sessionId;
		*postRedisRequest << (uint64_t)pUser;
		PostUserEvent(postRedisRequest.GetCPacketPtr());
	}
		break;
	case en_LOGINSERVER_EVENT::RequestRedis:
	{
		CUser* pUser;
		uint64_t sessionId;
		*pPacket >> sessionId;
		*pPacket >> (uint64_t&)pUser;
		pUser->Lock();
		//-----------------------------------------
		// 레디스 등록
		//-----------------------------------------
		if (pUser->_sessionId != sessionId)
		{
			//-------------------------------------
			// 이미 끊어진 연결
			//-------------------------------------
			pUser->Unlock();
			return;
		}
		
		if (_redisConn.SendKeyValueEx(std::to_string(pUser->_accountNo), std::string(pUser->_sessionKey, 64), SESSIONKEY_LIMIT_MS)==false)
		{
			pUser->Unlock();
			// 재연결 시도
			Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
				L"[sessionId:%016llx, accountNo:%lld] Redis 끊김, 재연결... 현재 세션은 끊음", sessionId, pUser->_accountNo);
			Disconnect(sessionId);
			return;
		}

		//-----------------------------------------
		// 유저로 격상
		//-----------------------------------------
		pUser->_lastRecvTime = timeGetTime();
		pUser->_state = CUser::STATE_LOGIN;

		//-----------------------------------------
		// 패킷 보내기
		//-----------------------------------------
		CPACKET_CREATE(resLoginPacket);
		*resLoginPacket << (int16_t)en_PACKET_TYPE::en_PACKET_CS_LOGIN_RES_LOGIN;
		*resLoginPacket << pUser->_accountNo;
		*resLoginPacket << (BYTE)1;
		resLoginPacket->PushData((char*)pUser->_id, 40);
		resLoginPacket->PushData((char*)pUser->_nickName, 40);
		resLoginPacket->PushData((char*)_option.gameIP, 32);
		*resLoginPacket << (unsigned short)_option.gamePort;
		if (pUser->_isDummyWhat == 1)
		{
			resLoginPacket->PushData((char*)_option.chatIPforDummy1, 32);
		}
		else if (pUser->_isDummyWhat == 2)
		{
			resLoginPacket->PushData((char*)_option.chatIPforDummy2, 32);
		}
		else if (pUser->_isDummyWhat == 3)
		{
			resLoginPacket->PushData((char*)_option.chatIPforDummy3, 32);
		}
		else
		{
			resLoginPacket->PushData((char*)_option.chatIP, 32);
		}
		*resLoginPacket << _option.chatPort;
		

		SendPacket(pUser->_sessionId, resLoginPacket.GetCPacketPtr());

		pUser->_lastRecvTime = timeGetTime();
		pUser->_state = CUser::STATE_FIN_LOGIN;
		pUser->Unlock();

		_monitoringJob->IncreaseLoginCount();
		SetEvent(_hEventTimeoutCheckThread);

	}
		break;
	default:
		__debugbreak();
		break;
	}

}

bool CLoginServer::OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip)
{
	CUser* pUser = g_userManager.CreateWaitLogin(sessionId, timeGetTime(), ip);
	if (pUser == nullptr)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"유저가 꽉 찼습니다.");
		Disconnect(sessionId);
	}
	SetEvent(_hEventTimeoutCheckThread);
	return true;
}

bool CLoginServer::OnConnectionRequest(in_addr ip)
{
	if (_useWhite)
	{
		return _whiteList.IsWhiteIp(ip);
	}

	return true;
}

void CLoginServer::OnRelease(uint64_t sessionId)
{
	CUser* pUser = g_userManager.FindUser(sessionId);
	if (pUser == nullptr)
		return;

	pUser->Lock();

	g_userManager.ReleaseUser(pUser);

	pUser->Unlock();
}

void CLoginServer::OnMessage(uint64_t sessionId, Net::CPacket* pPacket, int len)
{
	Net::CPacketViewer packetView;
	packetView.SetView(pPacket, len);
	RequestProc(sessionId, packetView);
}

//-----------------------------------------------------------------
// 미로그인/ 타임아웃(패킷 보내는 시간 대기) 체크 스레드
//-----------------------------------------------------------------
void CLoginServer::TimeoutCheckThread(CLoginServer* nowServer)
{
	PRO_START();
	HANDLE hEventUserJoin = nowServer->_hEventTimeoutCheckThread;
	HANDLE hEventExit = nowServer->_hEventForExit;
	HANDLE hEvents[2] = { hEventUserJoin, hEventExit };
	DWORD tid = GetCurrentThreadId();

	std::atomic_bool& timeOutOff = nowServer->_timeoutOff;

	CUser* pUserCheckCurrent = &g_userManager._userStructure._usersArray[0];
	CUser* pUserCheckEnd = &(g_userManager._userStructure._usersArray[g_userManager._maxSessionCnt -1]);
	pUserCheckEnd++;

	
	Log::logging().Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"WaitLogin Thread Start!");

	DWORD curTime;

	DWORD dwSleepTime = INFINITE;
	int sleepTime;
	for(;;)
	{
		DWORD retSignal = WaitForMultipleObjects(2, hEvents, FALSE, dwSleepTime);
		if (retSignal == WAIT_OBJECT_0 + 1)
		{
			break;
		}

		sleepTime = 9999999;
		curTime = timeGetTime();
		pUserCheckCurrent = &g_userManager._userStructure._usersArray[0];
		while (pUserCheckCurrent != pUserCheckEnd)
		{
			if (pUserCheckCurrent->_state != CUser::STATE_NONE)
			{
				if (pUserCheckCurrent->_state == CUser::STATE_WAIT_LOGIN)
				{
					//--------------------------------------------
					// 로그인 대기 (_lastRecvTime == 연결한 시간)
					//--------------------------------------------
					int deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
					if (deltaTime > WAIT_LOGIN_TIME)
					{
						pUserCheckCurrent->Lock();
						//----------------------------------------
						// 만약 로그인 패킷을 받았다면
						// WAIT_LOGIN 상태가 아니고, _lastRecvTime도 갱신됨
						//----------------------------------------
						deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
						if (deltaTime > WAIT_LOGIN_TIME && pUserCheckCurrent->_state == CUser::STATE_WAIT_LOGIN)
						{
							if (timeOutOff.load() == true)
							{
								pUserCheckCurrent->_lastRecvTime = curTime;
								pUserCheckCurrent->Unlock();
								continue;
							}
							Log::logging().Log(TAG_MESSAGE, c_syslog::en_ERROR,
								L"[sessionId: %016llx] 로그인 대기 길어져 끊음(%d)",
								pUserCheckCurrent->_sessionId, deltaTime);

							uint64_t sessionId = pUserCheckCurrent->_sessionId;
							g_userManager.ReleaseUser(pUserCheckCurrent);
							pUserCheckCurrent->Unlock();

							nowServer->Disconnect(sessionId);
						}
						else
						{
							pUserCheckCurrent->Unlock();
						}
					}
					else
					{
						//-----------------------------------------
						// 그냥 다음 검사할 시간만 갱신
						//-----------------------------------------
						if (deltaTime < 0)
						{
							deltaTime = WAIT_LOGIN_TIME - deltaTime;
						}
						if (sleepTime > deltaTime)
							sleepTime = deltaTime;
					}
				}
				else if (pUserCheckCurrent->_state == CUser::STATE_FIN_LOGIN)
				{
					//--------------------------------------------
					// 로그인 대기 (_lastRecvTime == 연결한 시간)
					//--------------------------------------------
					int deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
					if (deltaTime > WAIT_SEND_TIME)
					{
						pUserCheckCurrent->Lock();
						//----------------------------------------
						// ** 더블 체킹 **
						// 만약 로그인 패킷을 받았다면
						// WAIT_LOGIN 상태가 아니고, _lastRecvTime도 갱신됨
						//----------------------------------------
						deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
						if (deltaTime > WAIT_SEND_TIME && pUserCheckCurrent->_state == CUser::STATE_FIN_LOGIN)
						{
							if (timeOutOff.load() == true)
							{
								pUserCheckCurrent->_lastRecvTime = curTime;
								pUserCheckCurrent->Unlock();
								continue;
							}
							uint64_t sessionId = pUserCheckCurrent->_sessionId;
							g_userManager.ReleaseUser(pUserCheckCurrent);
							pUserCheckCurrent->Unlock();

							nowServer->Disconnect(sessionId);
						}
						else
						{
							pUserCheckCurrent->Unlock();
						}
					}
					else
					{
						//-----------------------------------------
						// 그냥 다음 검사할 시간만 갱신
						//-----------------------------------------
						if (deltaTime < 0)
						{
							deltaTime = WAIT_SEND_TIME - deltaTime;
						}
						if (sleepTime > deltaTime)
							sleepTime = deltaTime;
					}
				}
			}

			pUserCheckCurrent++;
		}


		dwSleepTime = (DWORD)sleepTime;
	}

	Log::logging().Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"WaitLogin Thread Exit!");
	
	return;
}

bool CLoginServer::RequestProc(uint64_t sessionId, Net::CPacketViewer& refPacket)
{
	WORD type;
	refPacket >> type;
	switch (type)
	{
	case en_PACKET_TYPE::en_PACKET_CS_LOGIN_REQ_LOGIN:
		return RequestLogin(sessionId, refPacket);

	case en_PACKET_TYPE::en_PACKET_CS_LOGIN_RES_LOGIN:
		return RequestDefault(sessionId, refPacket);

	case 103:
		return RequestDefault(sessionId, refPacket);

	case 104:
		return RequestDefault(sessionId, refPacket);

	case 105:
		return RequestDefault(sessionId, refPacket);

	case 106:
		return RequestDefault(sessionId, refPacket);

	default:
		break;
	}
	return RequestDefault(sessionId, refPacket);
}

bool CLoginServer::RequestLogin(uint64_t sessionId, Net::CPacketViewer& refPacket)
{
#ifdef _EVENT_PROFILE
	PRO_BEGIN(L"Find User");
#endif

	DWORD curTime = timeGetTime();
	CUser* pUser = g_userManager.FindUser(sessionId);

#ifdef _EVENT_PROFILE
	PRO_END(L"Find User");
#endif

	if(pUser != nullptr)
	{
#ifdef _EVENT_PROFILE
		Profile pf(L"Request Login");
#endif
		pUser->Lock();
		if (pUser->_sessionId != sessionId)
		{
			// 이미 해제됨
			pUser->Unlock();
			return true;
		}

		if (g_userManager._currentUserCnt >= g_userManager._maxUserCnt)
		{
			Log::logging().Log(TAG_MESSAGE, c_syslog::en_SYSTEM,
				L"[sessionId: %016llx] 유저가 꽉차서 로그인을 받을 수 없어요",
				sessionId, pUser->_accountNo);
			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();
			Disconnect(sessionId);
			return true;
		}
		//-----------------------------------------------------
		// 이미 로그인을 했음
		//-----------------------------------------------------
		if (pUser->_state >= CUser::STATE_LOGIN)
		{
			Log::logging().Log(TAG_MESSAGE, c_syslog::en_SYSTEM,
				L"[sessionId: %016llx / AccountNo: %lld] 로그인 패킷이 두번 이상 왔다",
				sessionId, pUser->_accountNo);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();
			Disconnect(sessionId);
			return true;	// 로그인 패킷이 두번옴, 끊자
		}

		//-----------------------------------------------------
		// 메시지 파싱
		//-----------------------------------------------------
		int64_t accountNo;
		char sessionKey[64];

		//-----------------------------------------------------
		// 길이 먼저 확인 (가능한 메시지라서)
		//-----------------------------------------------------
		int checkLen = refPacket.GetDataSize() - sizeof(int64_t) - 64;
		if (checkLen != 0)
		{
			Log::logging().LogHex(TAG_MESSAGE, c_syslog::en_ERROR,
				(unsigned char*)refPacket.GetReadPtr(), refPacket.GetDataSize(),
				L"[sessionId: %016llx] Login패킷 길이가 이상함. 다 제거했는데 남음: 남은 양: %d", sessionId, checkLen);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		refPacket >> accountNo;
		refPacket.GetData(sessionKey, 64);

		//-----------------------------------------------------
		// 테스트를 위한 더미 전용 아이피, 계정번호 검사
		//-----------------------------------------------------
		if (accountNo < 10000)
		{
			if(pUser->_ip.s_addr != _option.dummy1.s_addr)
			{
				Log::logging().Log(TAG_MESSAGE, c_syslog::en_SYSTEM,
					L"[%d vs %d] 더미전용 계정번호로 접속함",
					pUser->_ip.s_addr, _option.dummy1.s_addr);

					g_userManager.ReleaseUser(pUser);
					pUser->Unlock();
					Disconnect(sessionId);
					return true;
			}
		}
		else if (accountNo < 20000)
		{
			if(pUser->_ip.s_addr != _option.dummy2.s_addr)
			{
				Log::logging().Log(TAG_MESSAGE, c_syslog::en_SYSTEM,
					L"[%d vs %d] 더미전용 계정번호로 접속함",
					pUser->_ip.s_addr, _option.dummy1.s_addr);

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();
				Disconnect(sessionId);
				return true;
			}
		}
		else if (accountNo < 30000)
		{
			if(pUser->_ip.s_addr != _option.dummy3.s_addr)
			{
				Log::logging().Log(TAG_MESSAGE, c_syslog::en_SYSTEM,
					L"[%d vs %d] 더미전용 계정번호로 접속함",
					pUser->_ip.s_addr, _option.dummy1.s_addr);

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();
				Disconnect(sessionId);
				return true;
			}
		}

		//-----------------------------------------------------
		// 중복 로그인 (기존거 끊기)
		// 와 도저히 방법이 생각안나서 새로운 연결 허용으로 해봄
		// AddLoginMapEx는 기존 연결의 accountNo의 연결된 세션 자리에 
		// 요청한 sessionId를 넣어줌
		//-----------------------------------------------------
		uint64_t loginSessionId = g_userManager.AddLoginMap(accountNo, pUser->_sessionId);
		if (loginSessionId != 0)
		{
			//-------------------------------------------------
			// 기존 로그인 유저를 여기서 락 거는데 
			// A a (기존 연결) B a
			// 여기서 B -> A 순 nested락을 걸지만
			// A -> B가 걸릴 일은 없기 때문에 문제는 없다.
			//-------------------------------------------------
			int loginedState;
			CUser* pLoginUser = g_userManager.FindUser(loginSessionId);
			if(pLoginUser != nullptr)
			{
				pLoginUser->Lock();
				if (pLoginUser->_sessionId != loginSessionId)
				{	// 이미 끊김
					pLoginUser->Unlock();
				}
				else
				{
					loginedState = pLoginUser->_state;
					g_userManager.ReleaseUser(pLoginUser);

					pLoginUser->Unlock();

					Disconnect(loginSessionId);

					Log::logging().Log(TAG_MESSAGE, c_syslog::en_ERROR,
						L"[sessionId: %016llx to %016llx / 기존 세션 상태: %d]중복 로그인, 기존 연결 끊음",
						loginSessionId, sessionId, loginedState);
					_monitoringJob->IncreaseDuplicateDisconnectCnt();
				}
			}

			//------------------------------------------------
			// 재시도, 여기도 실패하면 중복 로그인이라 해제
			// ** 실제라면 DB저장 시간도 있어서 이렇게 못하고,
			//    . 어디 대기시키는 스레드를 만들거나
			//    . 그냥 끊어버리거나
			//    . 여기까지 안오고 첨부터 교체하거나 (Ex버전)
			//------------------------------------------------
			loginSessionId = g_userManager.AddLoginMap(accountNo, pUser->_sessionId);
			if (loginSessionId != 0)
			{
				Log::logging().Log(TAG_MESSAGE, c_syslog::en_ERROR,
					L"[sessionId: %016llx to %016llx] 중복 로그인이라 끊음",
					loginSessionId, sessionId);
				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();

				Disconnect(sessionId);
				return true;
			}
		}
		//-----------------------------------------
		// 인증 대기 상태로 변환
		//-----------------------------------------
		pUser->_lastRecvTime = curTime;
		pUser->_state = CUser::STATE_WAIT_IDENTIFY;

		pUser->_accountNo = accountNo;
		memcpy_s(pUser->_sessionKey, 64, sessionKey, 64);

		g_userManager.IncreaseUserCnt();
		
		if (accountNo < 10000)
		{
			pUser->_isDummyWhat = 1;
		}
		else if (accountNo < 20000)
		{
			pUser->_isDummyWhat = 2;
		}
		else if (accountNo < 30000)
		{
			pUser->_isDummyWhat = 3;
		}
		else
		{
			pUser->_isDummyWhat = 0;
		}
		
		pUser->Unlock();

		//-----------------------------------------
		// PQCS 로 DB 검색
		//-----------------------------------------
		CPACKET_CREATE(postRedisRequest);
		postRedisRequest->SetRecvBuffer();
		*postRedisRequest << (int)en_LOGINSERVER_EVENT::RequestMySql;
		*postRedisRequest << sessionId;
		*postRedisRequest << (uint64_t)pUser;
		PostUserEvent(postRedisRequest.GetCPacketPtr());
	}
	return true;
}
bool CLoginServer::RequestDefault(uint64_t sessionId, Net::CPacketViewer& refPacket)
{
	CUser* pUser = g_userManager.FindUser(sessionId);
	if(pUser != nullptr)
	{
		pUser->Lock();
		if (pUser->_sessionId != sessionId)
		{
			// 이미 해제됨
			pUser->Unlock();
			return true;
		}

		if(pUser->_state != CUser::STATE_WAIT_LOGIN)
		{
			Log::logging().LogHex(TAG_MESSAGE, c_syslog::en_SYSTEM,
				(unsigned char*)refPacket.GetReadPtr(), refPacket.GetDataSize(),
				L"[sessionId: %016llx / AccountNo: %lld] 메시지 이상함 이상함", sessionId, pUser->_accountNo);
		}
		else
		{
			Log::logging().LogHex(TAG_MESSAGE, c_syslog::en_SYSTEM,
				(unsigned char*)refPacket.GetReadPtr(), refPacket.GetDataSize(),
				L"[sessionId: %016llx] 메시지 이상함 이상함", sessionId);
		}

		g_userManager.ReleaseUser(pUser);
		pUser->Unlock();

		Disconnect(sessionId);
	}
	return true;
}