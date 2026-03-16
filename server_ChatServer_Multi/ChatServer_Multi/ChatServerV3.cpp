#include "ChatServerV3.h"
#include "NetProcess.h"
#include "21_TextParser.h"
#include "logclassV1.h"
#include "TlsPacket.hpp"
#include "ProfilerV2.hpp"

#include <process.h>
#include <string_view>
#include <string>
#include <timeapi.h>

using Log = Core::c_syslog;
using namespace Core;

CUserManager g_userManager;
CAuthContainer g_redisAuth;
thread_local CAroundSessionId* tls_pBuffer;


#pragma region CChatServerMonitoringJob

CChatServerMonitoringJob::CChatServerMonitoringJob()
{
	_startTime = timeGetTime();
}

void CChatServerMonitoringJob::Excute()
{
	_loginRecvTps = _loginRecvCnt.exchange(0, std::memory_order_seq_cst);
	_moveSectorRecvTps = _moveSectorRecvCnt.exchange(0, std::memory_order_seq_cst);
	_chatRecvTps = _chatRecvCnt.exchange(0, std::memory_order_seq_cst);
	_heartBeatRecvTps = _heartBeatRecvCnt.exchange(0, std::memory_order_seq_cst);

	DWORD curTime = timeGetTime();
	int32 deltaTime = SERVER_MONITORING_TICK - (int32)(curTime - _startTime);
	//Log::logging().Log(L"Debug CS", Log::en_SYSTEM, L"[CS excute: %d | deltaTime: %d]", curTime % 10000, deltaTime);
	if (deltaTime < 0)
	{
		_startTime = curTime + SERVER_MONITORING_TICK;
		Core::c_syslog::logging().Log(TAG_TIMER, Core::c_syslog::en_ERROR, L"CChatServer Monitoring Excute(): [delta ms: %d]  문제있음 (빡빡)", -deltaTime);
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

bool CChatServer::stChatServerOpt::LoadOption(const char* path)
{
	CParser config(path);
	try
	{
		config.LoadFile();

		int bUseEncode;
		config.GetValue("CHAT_SERVER", "bUseEncode", &bUseEncode);
		this->bUseEncode = (bool)bUseEncode;

		int bUseSO_SNDBUF;
		config.GetValue("CHAT_SERVER", "bUseSO_SNDBUF", &bUseSO_SNDBUF);
		this->bUseSO_SNDBUF = (bool)bUseSO_SNDBUF;

		int bUseTCP_NODELAY;
		config.GetValue("CHAT_SERVER", "bUseTCP_NODELAY", &bUseTCP_NODELAY);
		this->bUseTCP_NODELAY = (bool)bUseTCP_NODELAY;

		int iMaxConcurrentUsers;
		config.GetValue("CHAT_SERVER", "iMaxConcurrentUsers", &iMaxConcurrentUsers);
		this->iMaxConcurrentUsers = iMaxConcurrentUsers;

		int iMaxUsers;
		config.GetValue("CHAT_SERVER", "iMaxUsers", &iMaxUsers);
		this->iMaxUsers = iMaxUsers;

		int iWorkerThreadCreateCnt;
		config.GetValue("CHAT_SERVER", "iWorkerThreadCreateCnt", &iWorkerThreadCreateCnt);
		this->iWorkerThreadCreateCnt = iWorkerThreadCreateCnt;

		int iWorkerThreadRunCnt;
		config.GetValue("CHAT_SERVER", "iWorkerThreadRunCnt", &iWorkerThreadRunCnt);
		this->iWorkerThreadRunCnt = iWorkerThreadRunCnt;

		char openIP[16];
		config.GetValue("CHAT_SERVER", "openIP", openIP);
		MultiByteToWideChar(CP_ACP, 0, openIP, 16, this->openIP, 16);

		int port;
		config.GetValue("CHAT_SERVER", "port", &port);
		this->port = (unsigned short)port;

		int serverCode;
		config.GetValue("CHAT_SERVER", "serverCode", &serverCode);
		server_code = (uint8)serverCode;

		int staticKey;
		config.GetValue("CHAT_SERVER", "staticKey", &staticKey);
		this->static_key = (uint8)staticKey;

		int whitenum;
		config.GetValue("WHITE_IP_LIST", "num", &whitenum);
		this->whiteNum = (unsigned short)whitenum;

		for (int i = 0; i < whitenum; i++)
		{
			config.GetValue("WHITE_IP_LIST", "ipList", i, "ip", this->ip[i]);
		}

		// Redis
		int redisPort;
		config.GetValue("REDIS_SESSIONKEY", "redisIP", this->redisIP);
		config.GetValue("REDIS_SESSIONKEY", "redisPort", &redisPort);
		this->redisPort = redisPort;
		config.GetValue("CHAT_SERVER", "authThreadCreateNum", &authThreadCreateNum);
		config.GetValue("CHAT_SERVER", "authThreadRunNum", &authThreadRunNum);

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

bool CChatServer::OnInit(const stServerOpt* pSopt)
{
	stChatServerOpt* pOpt = (stChatServerOpt*)pSopt;

	//----------------------------------------------------------
	// 해시 구조 공간 확보를 위해서 세션수, 맥스 유저수 넘김
	//----------------------------------------------------------
	_maxUser = pOpt->iMaxUsers;
	g_userManager.Init(pOpt->iMaxConcurrentUsers, _maxUser);

	in_addr addrIp;
	for(int i = 0; i < pOpt->whiteNum; i++)
	{
		inet_pton(AF_INET, pOpt->ip[i], &addrIp);
		_whiteList.AddWhiteList(addrIp);
	}

	Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, 
		L"On Init!!() : MaxUser:%d / WhiteList (num: %d)", _maxUser, pOpt->whiteNum);

	_hEventTimeoutCheckThread = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (_hEventTimeoutCheckThread == NULL)
		return false;


	_hEventForExit = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (_hEventForExit == NULL)
	{
		Log::logging().LogEx(TAG_CONTENTS, GetLastError(), Log::en_ERROR, NULL);
		return false;
	}

	_timeoutCheckThread = std::thread(TimeoutCheckThread, this);

	_monitoringJob = std::make_shared<CChatServerMonitoringJob>();
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_monitoringJob, 0);

	return true;
}

void CChatServer::OnStop()
{
	
}

void CChatServer::OnExit()
{
	SetEvent(_hEventForExit);
	if (_timeoutCheckThread.joinable())
		_timeoutCheckThread.join();

	CloseHandle(_hEventForExit);
	CloseHandle(_hEventTimeoutCheckThread);
	_monitoringJob->CancelJob();
}

void CChatServer::OnWorkerStart()
{
	tls_pBuffer = new CAroundSessionId;
}

void CChatServer::OnWorkerEnd()
{
	delete tls_pBuffer;
}

void CChatServer::OnUserEvent(Net::CPacket* pPacket)
{
	int type;
	*pPacket >> type;
	CPACKET_UPDATE_TRACE(pPacket);
	Net::CPacketPtr autoFree(pPacket);

	switch (type)
	{
	case enChatServer::PQCS_REQUEST_REDIS:
	{
		//int64_t accountNo;
		//uint64_t sessionId;
		//*pPacket >> sessionId;	// 이게 왜 필요하냐면 검사중에 유저가 나가버리고 재로그인하면 해당 것이 맞는지 판별이 안됨
		//*pPacket >> accountNo;
		//const char* sessionKey = pPacket->GetReadPtr();
		//
		////-----------------------------------------
		//// 레디스 검색
		////-----------------------------------------
		//std::string getSessionKey = std::move(_redisConn.GetValue(std::to_string(accountNo)));
		//std::string_view userKey(sessionKey, 64);
		////bool identified = true;	//로그 남기려고 변수 하나만
		//if (userKey != getSessionKey)
		//{
		//	Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
		//		L"[sessionId: %lld / accountNo:%lld] 세션 키가 다름. (Redis세션키 길이: %d, 0이면 연결 끊겼거나 로그인 서버 안거침))"
		//		, sessionId, accountNo, getSessionKey.size());
		//
		//	Disconnect(sessionId); // OnRelease유도
		//	return;
		//}
		////----------------------------------------
		//// 결과를 알림
		////----------------------------------------
		//CPACKET_CREATE(postIdentifyResultPacket);
		//postIdentifyResultPacket->SetRecvBuffer();
		//*postIdentifyResultPacket << PQCS_IDENTIFYING;
		//*postIdentifyResultPacket << sessionId;
		//*postIdentifyResultPacket << accountNo;
		//PostUserEvent(postIdentifyResultPacket.GetCPacketPtr());
		__debugbreak();
		break;
	}

	case enChatServer::PQCS_IDENTIFYING:
	{
		int64_t accountNo;
		uint64_t sessionId;
		*pPacket >> sessionId;	// 이게 왜 필요하냐면 검사중에 유저가 나가버리고 재로그인하면 해당 것이 맞는지 판별이 안됨
		*pPacket >> accountNo;

		CUser* pUser = g_userManager.FindUser(sessionId);
		if (pUser != nullptr)
		{
			pUser->Lock();
			if (pUser->_sessionId != sessionId)
			{
				// 이미 해제됨
				pUser->Unlock();
				return;
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
				if (pLoginUser != nullptr)
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

						_monitoringJob->IncreaseDuplicateDisconnectCnt();
						Disconnect(loginSessionId);

						Log::logging().Log(TAG_MESSAGE, Log::en_ERROR,
							L"[sessionId: %016llx to %016llx / 기존 세션 상태: %d]중복 로그인, 기존 연결 끊음",
							loginSessionId, sessionId, loginedState);
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
					Log::logging().Log(TAG_MESSAGE, Log::en_ERROR,
						L"[sessionId: %016llx to %016llx] 중복 로그인이라 끊음",
						loginSessionId, sessionId);
					g_userManager.ReleaseUser(pUser);
					pUser->Unlock();

					Disconnect(sessionId);
					return;
				}
			}
			//-----------------------------------------
			// 유저 상태로 업그렝드
			//-----------------------------------------
			DWORD curTime = timeGetTime();
			pUser->_state = CUser::STATE_LOGIN;
			pUser->_lastRecvTime = curTime;
			pUser->_lastLoginTime = curTime;
			pUser->_lastHeartBeatTime = curTime - 30;
			pUser->_lastSectorMoveTime = curTime - 10;
			pUser->_lastChatTime = curTime - 10;

			pUser->_accountNo = accountNo;
			pUser->_curPos.iX = -1;
			pUser->_curPos.iY = -1;

			g_userManager.IncreaseUserCnt();
			pUser->Unlock();

			_monitoringJob->IncreaseLoginCount();


			CPACKET_CREATE(resLoginPacket);
			*resLoginPacket << (int16_t)en_PACKET_TYPE::en_PACKET_CS_CHAT_RES_LOGIN;
			*resLoginPacket << (BYTE)1;
			*resLoginPacket << accountNo;

			SendPacket(sessionId, resLoginPacket.GetCPacketPtr());
		}
		break;
	}

	case DEFAULT1:
		__debugbreak();
		break;

	case DEFAULT2:
		__debugbreak();
		break;

	case DEFAULT3:
		__debugbreak();
		break;

	case DEFAULT4:
		__debugbreak();
		break;

	default:
		__debugbreak();
		break;
	}
}

bool CChatServer::OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip)
{
	CUser* pUser = g_userManager.CreateWaitLogin(sessionId, ip, wip, timeGetTime());
	if (pUser == nullptr)
	{
		Log::logging().Log(TAG_CONTENTS, Log::en_ERROR,
			L"[sessionId: %016llx] 유저가 꽉 찼습니다. 끊을게요", sessionId);
		Disconnect(sessionId);
	}
	SetEvent(_hEventTimeoutCheckThread);
	return true;
}

bool CChatServer::OnConnectionRequest(in_addr ip)
{
	if (_useWhite)
	{
		return _whiteList.IsWhiteIp(ip);
	}

	return true;
}

void CChatServer::OnRelease(uint64_t sessionId)
{
	CUser* pUser = g_userManager.FindUser(sessionId);
	if (pUser == nullptr)
		return;

	pUser->Lock();

	g_userManager.ReleaseUser(pUser);

	pUser->Unlock();
}

void CChatServer::OnMessage(uint64_t sessionId, Net::CPacket* pPacket, int len)
{
	Net::CPacketViewer packetView;
	packetView.SetView(pPacket, len);
	RequestProc(sessionId, &packetView);
}

void CChatServer::OnSend(uint64_t sessionId, bool isValid)
{

}

//-----------------------------------------------------------------
// 미로그인/ 타임아웃 체크 스레드
//-----------------------------------------------------------------
void CChatServer::TimeoutCheckThread(CChatServer* nowServer)
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

	
	Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"TimeOut and WaitLogin Thread Start!");

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

		PRO_BEGIN(L"TIMEOUT");
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
						// ** 더블 체킹 **
						// 만약 로그인 패킷을 받았다면
						// WAIT_LOGIN 상태가 아니고, _lastRecvTime도 갱신됨
						//----------------------------------------
						deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
						if (deltaTime > WAIT_LOGIN_TIME && (pUserCheckCurrent->_state == CUser::STATE_WAIT_LOGIN))
						{
							if (timeOutOff.load() == true)
							{
								pUserCheckCurrent->_lastRecvTime = curTime;
								pUserCheckCurrent->Unlock();
								continue;
							}
							Log::logging().Log(TAG_MESSAGE, Log::en_ERROR,
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
						{
							sleepTime = deltaTime;
						}
					}
				}
				else
				{
					//--------------------------------------------
					// 유저 (_lastRecvTime == 마지막으로 메시지 받은 시간)
					//--------------------------------------------
					int deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
					if (deltaTime > WAIT_HEARTBEAT_TIME)
					{
						pUserCheckCurrent->Lock();
						//----------------------------------------
						// ** 더블 체킹 **
						// 로그인 이거나 필드에 있거나인 상태의 유저를
						// 마지막 메시지 수신시간을 통해 deltaTime계산
						//----------------------------------------
						deltaTime = (int)(curTime - pUserCheckCurrent->_lastRecvTime);
						if (deltaTime > WAIT_HEARTBEAT_TIME &&
							(pUserCheckCurrent->_state == CUser::STATE_LOGIN ||
							pUserCheckCurrent->_state == CUser::STATE_INFIELD))
						{
							if (timeOutOff.load() == true)
							{
								pUserCheckCurrent->_lastRecvTime = curTime;
								pUserCheckCurrent->Unlock();
								continue;
							}
							Log::logging().Log(TAG_MESSAGE, Log::en_ERROR,
								L"[sessionId: %016llx, accountNo: %lld, state: %d] 타임아웃으로 끊음(%d)",
								pUserCheckCurrent->_sessionId, pUserCheckCurrent->_accountNo,
								pUserCheckCurrent->_state, deltaTime);

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
						// 물론 이걸 넣는 순간에도 메시지가 올 수 있지만,
						// 그정도 오차는 넘기자
						//-----------------------------------------
						if (deltaTime < 0)
						{
							deltaTime = WAIT_HEARTBEAT_TIME - deltaTime;
						}
						if (sleepTime > deltaTime)
							sleepTime = deltaTime;
					}
				}
			}

			pUserCheckCurrent++;
		}
		PRO_END(L"TIMEOUT");

		dwSleepTime = (DWORD)sleepTime;
	}

	Log::logging().Log(TAG_CONTENTS, Log::en_SYSTEM, L"TimeOut and WaitLogin Thread Exit!");
	
	return;
}

bool CChatServer::RequestProc(uint64 sessionId, Net::CPacketViewer* pPacket)
{
	WORD type;
	*pPacket >> type;
	switch (type)
	{
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_LOGIN:
		return RequestLogin(sessionId, pPacket);

	case en_PACKET_TYPE::en_PACKET_CS_CHAT_RES_LOGIN:
		return RequestDefault(sessionId, pPacket);

	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		return RequestSectorMove(sessionId, pPacket);

	case en_PACKET_TYPE::en_PACKET_CS_CHAT_RES_SECTOR_MOVE:
		return RequestDefault(sessionId, pPacket);

	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_MESSAGE:
		return RequestMessage(sessionId, pPacket);

	case en_PACKET_TYPE::en_PACKET_CS_CHAT_RES_MESSAGE:
		return RequestDefault(sessionId, pPacket);

	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		return RequestHeartbeat(sessionId, pPacket);

	default:
		break;
	}
	return RequestDefault(sessionId, pPacket);
}

bool CChatServer::RequestLogin(uint64 sessionId, Net::CPacketViewer* pPacket)
{
	_monitoringJob->IncreaseLoginPacketCount();
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
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
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
		if (pUser->_state == CUser::STATE_LOGIN || pUser->_state == CUser::STATE_WAIT_IDENTIFY)
		{
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
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
		wchar_t id[ID_SIZE];
		wchar_t nickname[NICK_SIZE];
		char sessionKey[KEY_SIZE];

		//-----------------------------------------------------
		// 길이 먼저 확인 (가능한 메시지라서)
		//-----------------------------------------------------
		int checkLen = pPacket->GetDataSize() - sizeof(int64_t) - ID_SIZE*2 - NICK_SIZE*2 - KEY_SIZE;
		if (checkLen != 0)
		{
			Log::logging().LogHex(TAG_MESSAGE, Log::en_ERROR,
				(unsigned char*)pPacket->GetReadPtr(), pPacket->GetDataSize(),
				L"[sessionId: %016llx] Login패킷 길이가 이상함. 다 제거했는데 남음: 남은 양: %d", sessionId, checkLen);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		*pPacket >> accountNo;
		pPacket->GetData((char*)id, 40);
		pPacket->GetData((char*)nickname, 40);
		pPacket->GetData(sessionKey, 64);

		//-----------------------------------------------------
		// 범위 검사
		//-----------------------------------------------------
		int idlen = (int)wcslen(id);
		int nicklen = (int)wcslen(nickname);
		if (idlen > 19 || nicklen > 19 || idlen <= 0 || nicklen <= 0)
		{
			Log::logging().Log(TAG_MESSAGE, Log::en_ERROR, L"아이디, 닉네임 19자 넘음");

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();
			Disconnect(sessionId);
			return true;
		}

		//-----------------------------------------------------
		// 더미 전용 아이디 검사
		//-----------------------------------------------------
		if (accountNo < 10000)
		{
			if(wcscmp(pUser->_ipWstr, L"127.0.0.1") != 0)
			{
				Log::logging().Log(TAG_MESSAGE, Log::en_ERROR, L"더미 전용 아이디임");

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();
				Disconnect(sessionId);
				return true;
			}
		}
		else if (accountNo < 20000)
		{
			if(wcscmp(pUser->_ipWstr, L"10.0.1.2") != 0)
			{
				Log::logging().Log(TAG_MESSAGE, Log::en_ERROR, L"더미 전용 아이디임");

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();
				Disconnect(sessionId);
				return true;
			}
		}
		else if (accountNo < 30000)
		{
			if(wcscmp(pUser->_ipWstr, L"10.0.2.2") != 0)
			{
				Log::logging().Log(TAG_MESSAGE, Log::en_ERROR, L"더미 전용 아이디임");

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();
				Disconnect(sessionId);
				return true;
			}
		}

		//-----------------------------------------------------
		// 레디스 확인
		//-----------------------------------------------------
		pUser->_accountNo = accountNo;
		memcpy_s(pUser->_id, ID_SIZE * 2, id, ID_SIZE * 2);
		memcpy_s(pUser->_nickName, NICK_SIZE * 2, nickname, NICK_SIZE * 2);
		memcpy_s(pUser->_sessionKey, KEY_SIZE, sessionKey, KEY_SIZE);
		pUser->_state = CUser::STATE_WAIT_IDENTIFY;

		//CPACKET_CREATE(postIdentifyPacket);
		//postIdentifyPacket->SetRecvBuffer();
		//*postIdentifyPacket << PQCS_REQUEST_REDIS;
		//*postIdentifyPacket << sessionId;
		//*postIdentifyPacket << accountNo;
		//postIdentifyPacket->PushData(pUser->_sessionKey, 64);
		pUser->Unlock();

		//PostUserEvent(postIdentifyPacket.GetCPacketPtr());
		std::string_view clientKey(sessionKey, KEY_SIZE);
		g_redisAuth.RequestAuth(this, sessionId, accountNo, clientKey);
		
	}
	return true;
}
bool CChatServer::RequestSectorMove(uint64 sessionId, Net::CPacketViewer* pPacket)
{
	_monitoringJob->IncreaseMoveSectorCount();

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
		Profile pf(L"Request SectorMove");
#endif

		pUser->Lock();
		if (pUser->_sessionId != sessionId)
		{
			// 이미 해제됨
			pUser->Unlock();
			return true;
		}

		//--------------------------------------------
		// 로그인도 안하고 패킷 보냄
		//--------------------------------------------
		if (pUser->_state == CUser::STATE_WAIT_LOGIN)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx] 유저가 아닌데 패킷을 보냈습니다. ",
				sessionId);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);

			return true;
		}

		curTime = timeGetTime();
		int deltaTime = (curTime - (pUser->_lastSectorMoveTime));
		if (deltaTime < MIN_MSG_MOVE_DELAY)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			// . 너무 자주 보내는 경우. 특정횟수 이상이 아니면 일단 ok
			//----------------------------------------
			pUser->_strangeCnt++;
			if (pUser->_strangeCnt == MAX_STRANGE_MESSAGE_COUNT)
			{
				Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
					L"[sessionId: %016llx / AccountNo: %lld] 이동을 너무 많이해요. 끊을게요 (DeltaTime: %d)",
					sessionId, pUser->_accountNo, deltaTime);

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();

				Disconnect(sessionId);
				return true;
			}
		}

		pUser->_lastRecvTime = curTime;
		pUser->_lastSectorMoveTime = curTime;

		int64_t accountNo;
		WORD sectorX;
		WORD sectorY;
		//------------------------------------------------
		// 언마샬링 전 길이 체크부터
		//------------------------------------------------
		int checkLen = pPacket->GetDataSize() - sizeof(int64_t) - sizeof(WORD) - sizeof(WORD);
		if (checkLen != 0)
		{
			Log::logging().LogHex(TAG_MESSAGE, Log::en_ERROR,
				(unsigned char*)pPacket->GetReadPtr(), pPacket->GetDataSize(),
				L"[sessionId: %016llx, accountNo: %lld] SectorMove 패킷 길이가 이상함. 다 제거했는데 남음: 남은 양: %d",
				sessionId, pUser->_accountNo, checkLen);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		//------------------------------------------------
		// 언마샬링
		//------------------------------------------------
		*pPacket >> accountNo >> sectorX >> sectorY;

		//-------------------------------------------------
		// AccountNo 다르면 문제
		//-------------------------------------------------
		if (accountNo != pUser->_accountNo)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx / AccountNo: %lld] Request Message, accountno다름 [메시지:%lld])",
				sessionId, pUser->_accountNo, accountNo);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		//-------------------------------------------------
		// 섹터 범위가 문제
		//-------------------------------------------------
		if (CField::InSectorRange(sectorY, sectorX) == false)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx / AccountNo: %d] 이상한 섹터 범위 [%d, %d]",
				sessionId, pUser->_accountNo, sectorX, sectorY);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		WORD befSectorX = pUser->_curPos.iX;
		WORD befSectorY = pUser->_curPos.iY;
		// 기존에 섹터에 들어가 있었다면
		if (CField::InSectorRange(befSectorY, befSectorX) == true)
		{
			//---------------------------------------------
			// 섹터가 같으면 이상횟수를 늘려줘야 하는게 맞는가?
			// 맞다고 보지만 현재 더미가 난수를 뽑았는데 동일할 확률이 존재해서
			// 일단은 pass (일단 공격적으로 보내면 위에서 걸리니까(시간차))
			//---------------------------------------------
			// 섹터가 다르다면 
			if(befSectorX != sectorX || befSectorY != sectorY)
			{
				g_field.RemoveAndAddSector(befSectorY, befSectorX,
					sectorY, sectorX, sessionId);
			}
		}
		else
		{	// 원래 섹터가 없던 로그인 유저
			g_field.AddSector(sectorY, sectorX, pUser->_sessionId);
			pUser->_state = CUser::STATE_INFIELD;
		}
		// 섹터 교체
		pUser->_curPos.iY = sectorY;
		pUser->_curPos.iX = sectorX;
		pUser->Unlock();

		CPACKET_CREATE(resMovePacket);
		*resMovePacket << (int16_t)en_PACKET_TYPE::en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
		*resMovePacket << pUser->_accountNo;
		*resMovePacket << (uint16_t)sectorX;
		*resMovePacket << (uint16_t)sectorY;
		SendPacket(sessionId, resMovePacket.GetCPacketPtr());
	}
	return true;
}
bool CChatServer::RequestMessage(uint64 sessionId, Net::CPacketViewer* pPacket)
{
	_monitoringJob->IncreaseChatCount();

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
		Profile pf(L"Request Message");
#endif

		pUser->Lock();
		if (pUser->_sessionId != sessionId)
		{
			// 이미 해제됨
			pUser->Unlock();
			return true;
		}

		//--------------------------------------------
		// 로그인도 안하고 패킷 보냄
		//--------------------------------------------
		if (pUser->_state == CUser::STATE_WAIT_LOGIN)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx] 유저가 아닌데 패킷을 보냈습니다. ",
				sessionId);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);

			return true;
		}

		curTime = timeGetTime();
		int deltaTime = (curTime - (pUser->_lastChatTime));
		if (deltaTime < MIN_MSG_CHAT_DELAY)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			pUser->_strangeCnt++;
			if (pUser->_strangeCnt == MAX_STRANGE_MESSAGE_COUNT)
			{
				Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
					L"[sessionId: %016llx / AccountNo: %lld] 채팅 너무 많이 보내요 (DeltaTime: %d)",
					sessionId, pUser->_accountNo, deltaTime);

				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();

				Disconnect(sessionId);
				return true;
			}
		}
		pUser->_lastRecvTime = curTime;
		pUser->_lastChatTime = curTime;
		int64_t accountNo;
		*pPacket >> accountNo;

		if (accountNo != pUser->_accountNo)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx] Request Message, accountno다름(서버:%lld, 메시지:%lld)",
				sessionId, pUser->_accountNo, accountNo);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		WORD messageLen;
		*pPacket >> messageLen;

		//---------------------------------------------
		// TAG ABNORMAL: 악성 유저, TODO: 차단?
		// . 길이를 수정했음
		//---------------------------------------------
		if (pPacket->GetDataSize() != messageLen)
		{
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx / AccountNo: %lld] Request Message, 메시지 길이다름(서버:%d, 메시지:%d)",
				sessionId, pUser->_accountNo, pPacket->GetDataSize(), messageLen);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		//---------------------------------------------
		// TAG ABNORMAL: 악성 유저, TODO: 차단?
		// . 길이가 홀수 (wide-char이라 항상 바이트수는 짝수)
		// . 길이가 정해진 최소 길이보다 길음
		//---------------------------------------------
		if (messageLen > MAX_MSG_CHAT_BYTE_LEN || messageLen % 2 == 1)
		{
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx / AccountNo: %lld] 채팅 바이트 수: %d (최대길이 이상 or 홀수)",
				sessionId, pUser->_accountNo, messageLen);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		CPACKET_CREATE(resMessagePacket);
		*resMessagePacket << (WORD)en_PACKET_TYPE::en_PACKET_CS_CHAT_RES_MESSAGE;
		*resMessagePacket << pUser->_accountNo;
		resMessagePacket->PushData((char*)pUser->_id, 40);
		resMessagePacket->PushData((char*)pUser->_nickName, 40);
		*resMessagePacket << messageLen;
		int retPushMessage = resMessagePacket->PushData(pPacket->GetReadPtr(),
			messageLen);

		//---------------------------------------------------
		// 정상 유져
		//---------------------------------------------------

		SendPacket(sessionId, resMessagePacket.GetCPacketPtr());
		g_field.GetAroundUserSessionId(&pUser->_curPos, tls_pBuffer);

		pUser->Unlock();

		uint64_t* pCur = tls_pBuffer->GetBufferPtr();
		uint64_t* pEnd = pCur + tls_pBuffer->GetCount();
		while (pCur != pEnd)
		{
			if(*pCur != sessionId)
				SendPacket(*pCur, resMessagePacket.GetCPacketPtr());
			++pCur;
		}

	}
	return true;
}
bool CChatServer::RequestHeartbeat(uint64 sessionId, Net::CPacketViewer* pPacket)
{
	_monitoringJob->IncreaseHeartBeatCount();

#ifdef _EVENT_PROFILE
	PRO_BEGIN(L"Find User");
#endif

	DWORD curTime = timeGetTime();
	CUser* pUser = g_userManager.FindUser(sessionId);

#ifdef _EVENT_PROFILE
	PRO_END(L"Find User");
#endif

	if (pUser != nullptr)
	{
#ifdef _EVENT_PROFILE
		Profile pf(L"Request HeartBeat");
#endif

		pUser->Lock();
		if (pUser->_sessionId != sessionId)
		{
			// 이미 해제됨
			pUser->Unlock();
			return true;
		}

		//--------------------------------------------
		// 로그인도 안하고 패킷 보냄
		//--------------------------------------------
		if (pUser->_state == CUser::STATE_WAIT_LOGIN)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx] 유저가 아닌데 패킷을 보냈습니다. ",
				sessionId);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);
			return true;
		}

		if (pPacket->GetDataSize() != 0)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			// 하트비트 패킷 변조
			//----------------------------------------
			Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
				L"[sessionId: %016llx] 하트빝 패킷에 뭔가 있어요. ",
				sessionId);

			g_userManager.ReleaseUser(pUser);
			pUser->Unlock();

			Disconnect(sessionId);

			return true;
		}

		//-------------------------------------------------------
		// 디큐 했으니까 1 올려주고, 많이 보낸애는 아닌지 검사
		//-------------------------------------------------------
		curTime = timeGetTime();
		int deltaTime = (int)(curTime - pUser->_lastHeartBeatTime);
		if (deltaTime < MIN_MSG_HEARTBEAT_DELTATIME)
		{
			//----------------------------------------
			// TAG ABNORMAL: 악성 유저, TODO: 차단?
			//----------------------------------------
			pUser->_strangeCnt++;
			if(pUser->_strangeCnt == MAX_STRANGE_MESSAGE_COUNT)
			{
				Log::logging().Log(TAG_MESSAGE, Log::en_SYSTEM,
					L"[sessionId: %016llx / AccountNo: %lld] 하트비트 너무 많이보내요. 끊을게요 (DeltaTime: %d)",
					sessionId, pUser->_accountNo, deltaTime);
				g_userManager.ReleaseUser(pUser);
				pUser->Unlock();

				Disconnect(sessionId);
				return true;
			}
		}
		pUser->_lastRecvTime = curTime;
		pUser->_lastHeartBeatTime = curTime;
		pUser->Unlock();
	}
	return true;
}
bool CChatServer::RequestDefault(uint64 sessionId, Net::CPacketViewer* pPacket)
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
			Log::logging().LogHex(TAG_MESSAGE, Log::en_SYSTEM,
				(const unsigned char*)pPacket->GetReadPtr(), pPacket->GetDataSize(),
				L"[sessionId: %016llx / AccountNo: %lld] 메시지 이상함 이상함", sessionId, pUser->_accountNo);
		}
		else
		{
			Log::logging().LogHex(TAG_MESSAGE, Log::en_SYSTEM,
				(const unsigned char*)pPacket->GetReadPtr(), pPacket->GetDataSize(),
				L"[sessionId: %016llx] 메시지 이상함 이상함", sessionId);
		}

		g_userManager.ReleaseUser(pUser);
		pUser->Unlock();

		Disconnect(sessionId);
	}
	return true;
}