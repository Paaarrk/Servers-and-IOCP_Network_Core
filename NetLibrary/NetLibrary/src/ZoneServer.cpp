#include "ZoneServer.h"
#include "logclassV1.h"
#include "NetProtocol.h"
#include "NetProcess.h"
#include "Zone.h"
#include "RingBufferV4.h"

#include "ProfilerV2.hpp"


#include <process.h>
#include <timeapi.h>
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "winmm")


/////////////////////////////////////////////////////////
// NetServer
/////////////////////////////////////////////////////////

using namespace Core;

#pragma region ServerMonitoringJob

Net::CZoneServerMonitoringJob::CZoneServerMonitoringJob()
{
	_startTime = timeGetTime();
	_serverStartTime = time(nullptr);
}

void Net::CZoneServerMonitoringJob::Excute()
{
	_acceptTps = _InterlockedExchange(&_acceptCount, 0);
	_recvMessageTps = _InterlockedExchange(&_recvMessageCount, 0);
	_sendMessageTps = _InterlockedExchange(&_sendMessageCount, 0);

	if (_recvMessageTps != 0)
	{
		uint32 befCount = _monitoringCount++;
		_recvMessageTpsAvg = (uint32)(((uint64)_recvMessageTpsAvg * (uint64)befCount + (uint64)_recvMessageTps) / _monitoringCount);
	}

	long workTime;
	for (int i = 0; i < _workerNum; i++)
	{
		workTime = _InterlockedExchange(&_workTime[i], 0);
		_workRate[i] = (float)workTime / SERVER_MONITORING_TICK * 100;
	}

	DWORD curTime = timeGetTime();
	int32 deltaTime = SERVER_MONITORING_TICK - (int32)(curTime - _startTime);
	
	if (deltaTime < 0)
	{
		_startTime = curTime + SERVER_MONITORING_TICK;
		Core::c_syslog::logging().Log(TAG_TIMER, Core::c_syslog::en_ERROR, L"CZoneServer Monitoring TickUpdate(): [delta ms: %d]  문제있음 (빡빡)", -deltaTime);
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, SERVER_MONITORING_TICK, curTime);
	}
	else
	{
		_startTime += SERVER_MONITORING_TICK;
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, deltaTime, curTime);
	}
}


#pragma endregion


/////////////////////////////////////////////////////////
// 세션 관리자(배열로 세션을 관리)
/////////////////////////////////////////////////////////
#pragma region SessionStructure

Net::CZoneServer::SessionStructure::SessionStructure() 
	:_sessionsArray(nullptr), _sessionCnt(0), _maxSessionCnt(0) 
{

}

Net::CZoneServer::SessionStructure::~SessionStructure()
{
	if (_sessionsArray != nullptr)	//생성이 되어있음
		delete[] _sessionsArray;
}

//-------------------------------------------------------
// 세션 구조 초기화, maxCnt: 배열 확보
//-------------------------------------------------------
bool Net::CZoneServer::SessionStructure::Init(int maxCnt)
{
	_maxSessionCnt = maxCnt;
	_sessionsArray = new Net::stZoneSession[maxCnt];
	if (_sessionsArray == nullptr)
		return false;

	for (int i = maxCnt - 1; i >= 0; i--)
	{
		_indexStack.push(i);
	}
	return true;
}

//-------------------------------------------------------
// 세션 획득, 자리가 없으면 nullptr반환, 자리가 없으면 이상한 것
// index, session* 둘다 얻음, 
// 실패 시 index = -1, 반환은 nullptr
//-------------------------------------------------------
Net::stZoneSession* Net::CZoneServer::SessionStructure::AcquireSession(int* index)
{
	if (_indexStack.pop(*index) == false)
	{
		*index = -1;
		return nullptr;
	}
	_InterlockedIncrement(&_sessionCnt);
	return &_sessionsArray[*index];
}

//-------------------------------------------------------
// 세션 반환 (클리어 하고 반환하세요)
// index가 이상하면 false반환
//-------------------------------------------------------
bool Net::CZoneServer::SessionStructure::ReleaseSession(Net::stZoneSession* pSession)
{
	int index = (int)(pSession - _sessionsArray);
	if (index < 0 || index >= _maxSessionCnt)
		return false;
	_indexStack.push(index);
	_InterlockedDecrement(&_sessionCnt);
	return true;
}

Net::stZoneSession* Net::CZoneServer::SessionStructure::FindSession(uint64_t sessionId)
{
	//---------------------------------------------------
	// 가볍게 인덱스 유효성만.
	// 어짜피 여기서 검사 다하고 반환해도, 쓰려는 타이밍에
	// 찾은게 내가 아닐 수 있으니 검사는 쓰는쪽에서
	//---------------------------------------------------
	int index = (int)(sessionId & 0x000F'FFFF);
	if (index < 0 || index >= _maxSessionCnt)
		return nullptr;

	return &_sessionsArray[index];
}

#pragma endregion


/////////////////////////////////////////////////////////
// CZoneServer
/////////////////////////////////////////////////////////

long Net::CZoneServer::s_threadId = -1;

Net::CZoneServer::CZoneServer() : _lsock(INVALID_SOCKET), _sid(0), _option{}, _isStop(0),
_hThreads{}, _hIOCP(0), _threadNum(0), _hEventForAccept(NULL), _hEventForExit(NULL), _isCallInit(0)
, _serverCrypto{}, _wsaStart(), _zoneManager(this)/*, _sendThread(NULL)*/
{

}

#pragma region CServer_Functions


bool Net::CZoneServer::Init(const stServerOpt* pOpt)
{
	long retIsInit = _InterlockedExchange(&_isCallInit, 1);
	if (retIsInit == 1)
		return true;	// 이미 해서 안해도 됨

	do
	{
		// stZoneSession 관리객체 미리 생성
		bool retInitSessionStructure = _sessionStructure.Init(pOpt->iMaxConcurrentUsers);
		if (retInitSessionStructure == false)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"CZoneServer::Init() 세션 준비 실패");
			break;
		}

		_option = *pOpt;

		_serverCrypto.code = pOpt->server_code;
		_serverCrypto.static_key = pOpt->static_key;
		InitMonitoringJob();

		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"CZoneServer::Init()!! [최대 접속자수:%d, 오픈IP: %s, 포트: %d, 워커스레드: [%d / %d], 노딜레이: %d, SNDBUF=0: %d]",
			_option.iMaxConcurrentUsers, _option.openIP, _option.port, _option.iWorkerThreadRunCnt, _option.iWorkerThreadCreateCnt, _option.bUseTCP_NODELAY, _option.bUseSO_SNDBUF);

		// MAKE IOCP
		_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, _option.iWorkerThreadRunCnt);
		if (_hIOCP == NULL)
		{
			DWORD dwCreateIOCPErr = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, dwCreateIOCPErr, Core::c_syslog::en_ERROR, L"CreateIOCP failed");
			break;
		}

		// MAKE hEventFor AcceptThread (중지, 사용)
		_hEventForAccept = (HANDLE)CreateEventW(NULL, TRUE, FALSE, NULL);
		if (_hEventForAccept == NULL)
		{
			DWORD gle = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, gle, Core::c_syslog::en_ERROR, L"Make _hEventForAccept Failed");
			break;
		}

		// MAKE hEventForExit (종료)
		_hEventForExit = (HANDLE)CreateEventW(NULL, TRUE, FALSE, NULL);
		if (_hEventForExit == NULL)
		{
			DWORD gle = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, gle, Core::c_syslog::en_ERROR, L"Make _hEventForExit Failed");
			break;
		}

		//----------------------------------------------------------------
		// 얘가 여기로 와야 OnWorkerStart(), End()가 가능함
		//----------------------------------------------------------------
		if (OnInit(pOpt) == false)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[errno: %d] Contents Init Failed");
			break;
		}

		int createCnt = _option.iWorkerThreadCreateCnt;
		for (int i = 0; i < createCnt; i++)
		{
			_hThreads[i] = (HANDLE)_beginthreadex(NULL, NULL, NetServerWorkerFunc, this, 0, NULL);
			if (_hThreads[i] == 0)
			{
				int retCreateWorkerThreadErr = errno;
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[errno: %d] Create WorkerThread failed", retCreateWorkerThreadErr);
				break;
			}
			_threadNum++;
		}


		// MAKE AcceptThread
		_hThreads[_threadNum] = (HANDLE)_beginthreadex(NULL, NULL, NetServerAcceptFunc, this, 0, NULL);
		if (_hThreads[_threadNum] == 0)
		{
			int retCreateAcceptThreadErr = errno;
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[errno: %d] Create AcceptThread failed", retCreateAcceptThreadErr);
			break;
		}
		_threadNum++;

		

		return true;
	} while (0);

	Init_Rollback();
	return false;
}

bool Net::CZoneServer::Start()
{
	do
	{
		_InterlockedExchange(&_isStop, 0);
		if (_isCallInit == 0)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"CZoneServer::Start() 실패, Init()을 호출하지 않음");
			break;
		}


		// MAKE LISTEN socket
		SOCKADDR_IN saddr;
		saddr.sin_family = AF_INET;
		InetPtonW(AF_INET, _option.openIP, &saddr.sin_addr);
		saddr.sin_port = htons(_option.port);
		_lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (_lsock == INVALID_SOCKET)
		{
			DWORD retMakeLSockErr = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, retMakeLSockErr, Core::c_syslog::en_ERROR, L"리슨소켓, socket() failed ");
			break;
		}
		int retBind = bind(_lsock, (SOCKADDR*)&saddr, sizeof(saddr));
		if (retBind)
		{
			DWORD retLsockBindErr = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, retLsockBindErr, Core::c_syslog::en_ERROR, L"리슨소켓, bind() failed");
			break;
		}

		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"CZoneServer::Start()!!");
		int retListen = listen(_lsock, SOMAXCONN_HINT(5000));
		if (retListen == SOCKET_ERROR)
		{
			DWORD listenError = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, listenError, Core::c_syslog::en_ERROR, L"CZoneServer::Start() - listen(), Listen 에러");
			break;
		}
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"Listening...");

		SetEvent(_hEventForAccept);
		return true;

	} while (0);

	Init_Rollback();
	return false;
}

void Net::CZoneServer::Stop(int opt = 0)
{
	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"CZoneServer::Stop()!!");
	//---------------------------------------------------
	// 어셉트 막음
	//---------------------------------------------------
	ResetEvent(_hEventForAccept);
	//---------------------------------------------------
	// 리슨 소켓 종료
	//---------------------------------------------------
	int ret = closesocket(_lsock);
	if (ret)
	{
		DWORD gle = GetLastError();
		Core::c_syslog::logging().LogEx(TAG_NET, gle, Core::c_syslog::en_ERROR, L"리슨 소켓 종료 오류");
		// 종료는 된것
	}
	_lsock = INVALID_SOCKET;

	//---------------------------------------------------
	// 세션 정리
	//---------------------------------------------------
	int maxUser = _option.iMaxConcurrentUsers;
	for (int i = 0; i < maxUser; i++)
	{
		stZoneSession* pSession = &_sessionStructure._sessionsArray[i];
		uint64_t sessionId = pSession->sessionId;
		if (sessionId != 0)
		{
			//-------------------------------------------
			// 연결 되어있는 세션 정리
			//-------------------------------------------
			Disconnect(sessionId);
		}
	}

	Sleep(500);

	bool retTraceResult = true;
#ifdef CPACKET_TRACING
	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"패킷 트레이스 시작");
	retTraceResult = CPacket::CheckTrace();
	if (retTraceResult == false)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 트레이스 검사로 문제가 발견됨");
	}
	else
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"모든 패킷이 반환됨");
	}

	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"패킷 트레이스 종료");
#endif

	OnStop();

	_InterlockedExchange(&_isStop, 1);
	if (opt == 1)
	{
		for (int i = 0; i < _option.iWorkerThreadCreateCnt; i++)
			PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_EXIT, 0, nullptr);
		SetEvent(_hEventForExit);

		WaitForMultipleObjects(_threadNum, _hThreads, TRUE, INFINITE);
		if (retTraceResult == false)
		{
			wprintf_s(L"미반환 CPacket 로그 확인!!");
		}

		Init_Rollback();

		OnExit();

		_zoneManager.Clear();
	}
}

bool Net::CZoneServer::Disconnect(uint64_t sessionId)
{
	stZoneSession* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] Disconnect() 비정상적인 세션 아이디(out of index)", sessionId);
		return false;
	}
	//---------------------------------------------------
	// * 참조 카운트를 올려서 릴리즈 진입 못하도록
	// * 릴리즈 진행중인지 확인하기
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SESSION_RELEASE_FLAG) == SESSION_RELEASE_FLAG)
	{
		DecrementRefcount(pSession);
		return true;
	}

	//---------------------------------------------------
	// 여기서부터는 세션 id 변화가 없음.
	// (릴리즈가 불가능해서)
	// 
	// . 내것이 맞으면 디스커넥트 플래그 활성화
	// . I / O 캔슬도 해주기
	// . WSASend, WSARecv가 인지하고 역시 알아서 취소 시도할 것이다
	//---------------------------------------------------
	if (pSession->sessionId == sessionId)
	{
		_InterlockedExchange(&pSession->isDisconnect, 1);
		CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl->ol);
	}

	DecrementRefcount(pSession);
	return true;
}

bool Net::CZoneServer::DisconnectZoneZero(uint64_t sessionId)
{
	stZoneSession* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] Disconnect() 비정상적인 세션 아이디(out of index)", sessionId);
		return false;
	}
	//---------------------------------------------------
	// * 참조 카운트를 올려서 릴리즈 진입 못하도록
	// * 릴리즈 진행중인지 확인하기
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SESSION_RELEASE_FLAG) == SESSION_RELEASE_FLAG)
	{
		DecrementRefcount(pSession);
		return true;
	}

	//---------------------------------------------------
	// 여기서부터는 세션 id 변화가 없음.
	// (릴리즈가 불가능해서)
	// 
	// . 내것이 맞으면 디스커넥트 플래그 활성화
	// . I / O 캔슬도 해주기
	// . WSASend, WSARecv가 인지하고 역시 알아서 취소 시도할 것이다
	//---------------------------------------------------
	if (pSession->sessionId == sessionId)
	{
		_InterlockedExchange(&pSession->zoneId, 0);
		_InterlockedExchange(&pSession->isDisconnect, 1);
		CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl->ol);
	}

	DecrementRefcount(pSession);
	return true;
}

bool Net::CZoneServer::SendPacket(uint64_t sessionId, Net::CPacket* pPacket)
{
	//---------------------------------------------------
	// V3: 일단 CPacket의 참조카운트를 올리고 시작
	// . 인큐에 성공하면 이 참조카운트는 디큐하는쪽이 내릴것이고,
	// . 인큐에 실패하기 전에는 SendPacket에서 내려줘야함 (Free가 그 역할)
	//---------------------------------------------------
	CPACKET_ADDREF(pPacket);

	//---------------------------------------------------
	// 패킷을 넣은건 맞는지
	//---------------------------------------------------
	if (pPacket->GetDataSize() < CPACKET_HEADER_LEN)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] 보내려는 데이터가 0바이트", sessionId);
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		// return 2;
		return true;
	}

	//---------------------------------------------------
	// 서버 종료급 이벤트. 세션아이디가 비정상
	//---------------------------------------------------
	stZoneSession* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] SendPacket() 인덱스 유효하지 않음", sessionId);
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		// return -1;
		return false;
	}

	//---------------------------------------------------
	// 일단 릴리즈 안되게 참조카운트 올리기
	// . 플래그 확인을 위해 값 꺼내기
	// . 릴리즈 진행중이면 올리고 나감
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SESSION_RELEASE_FLAG) == SESSION_RELEASE_FLAG)
	{
		//-----------------------------------------------
		// 자신의 참조는 자신이 제거하기
		// . 새로 생성중이더라도 직접 참조를 올려 0될 수 없음
		// ** 아니다. 비교와 카운트를 내리는건 원자적이지 않아서
		//    0이 될 수 있다.
		//-----------------------------------------------
		//-----------------------------------------------
		// 안쓰는 패킷 해제
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		DecrementRefcount(pSession);
		return true;
	}
	//---------------------------------------------------
	// 이제 내가 사용권한 획득, 세션 변화 없음
	// . 세션 아이디가 확실함
	//---------------------------------------------------
	if (pSession->sessionId != sessionId)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_DEBUG, L"[sessionId: %016llx] SendPacket() 시도, 재사용된 세션, 내것아님", sessionId);
		//-----------------------------------------------
		// 안쓰는 패킷 해제
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		DecrementRefcount(pSession);
		return true;
	}

	//---------------------------------------------------
	// ** 여기서부터는 확실히 내꺼
	// . Disconnect는 올라가있을 수 있음
	// . 다만, 올라갔다고 해서 인큐가 문제되지 않음
	//   (해제 루틴을 타는 중이 아니기 때문에)
	//---------------------------------------------------
	if (pSession->isDisconnect)
	{
		//-----------------------------------------------
		// 인큐 시도를 안하기 위해서 끊을 예정인애는 보내지
		// 맙시다
		// 
		// . 원래 센드 완료통지에서 패킷을 해제
		// . 따라서 인큐를 안할거면 내가 해줘야함
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}

		DecrementRefcount(pSession);
		return true;
	}



	//---------------------------------------------------
	// < V4 >
	// . 인코딩이 필요하면 해주자
	//---------------------------------------------------
	if (pPacket->isEncoded() == 0)
	{
		//---------------------------------------------------
		// < V3 >
		// . 인큐 하기 직전에 헤더를 세팅해주자
		//---------------------------------------------------
		Net::stNetHeader header;
		header.code = _serverCrypto.code;
		header.len = pPacket->GetDataSize() - Net::NET_HEADER_LEN;
		header.randkey = (uint8_t)(rand() % 256);
		header.checksum = Net::CCryptoUtils::GetCheckSum(pPacket);
		memcpy(pPacket->GetBufferPtr(), &header, sizeof(Net::stNetHeader));
		pPacket->SetEncoded();

		if (_option.bUseEncode)
		{
			Net::CCryptoUtils::Encode(pPacket, _serverCrypto);
		}
	}

	//---------------------------------------------------
	// 여기서 Disconnect올라가있을 수 있지만,
	// 알아서 초기화할때 큐 비우니까 괜춘
	// 
	// < V3 >
	// . 인큐에 성공하면 이제 패킷은 큐 소유이기 때문에
	// ** 참조 카운트는 디큐하고나서 알아서 해제 해준다.
	//---------------------------------------------------
	CPACKET_UPDATE_TRACE(pPacket);
	bool retEnq = pSession->sendQ->Enqueue(pPacket);
	if (retEnq == false)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"[sessionId: %016llx] 센드 버퍼가 다 찬 클라입니다. iocount == 0이 되면 해제합니다.", sessionId);

		//-----------------------------------------------
		// 인큐 실패했으니 해제까지
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}

		// 디스커넥트 플래그 활성화
		_InterlockedExchange(&pSession->isDisconnect, 1);
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl->ol);

		DecrementRefcount(pSession);
		return true;
	}


	//----------------------------------------------------
	// 인큐에 성공함
	// . 보낼지 안보낼지는 모른다! (디스커넥트 상태에 따라)
	//----------------------------------------------------
	_monitorJob->IncreaseSendMessageCount();

	long isSending = _InterlockedExchange(&pSession->isSending, 1);
	if (isSending == 1)
	{
		DecrementRefcount(pSession);
		return true;	// 못 보내니 나옴
	}

	//---------------------------------------------------
	// isSending이 0인경우임. 즉 내가 1로 만듦!
	//---------------------------------------------------
	if (pSession->isDisconnect == 1)
	{
		_InterlockedExchange(&pSession->isSending, 0);
		DecrementRefcount(pSession);
		return true;
	}

	PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_DELAYSEND, (ULONG_PTR)pSession, nullptr);
	return true;
}

bool Net::CZoneServer::SendPacket_Fast(uint64_t sessionId, Net::CPacket* pPacket)
{
	//---------------------------------------------------
	// V3: 일단 CPacket의 참조카운트를 올리고 시작
	// . 인큐에 성공하면 이 참조카운트는 디큐하는쪽이 내릴것이고,
	// . 인큐에 실패하기 전에는 SendPacket에서 내려줘야함 (Free가 그 역할)
	//---------------------------------------------------
	CPACKET_ADDREF(pPacket);

	//---------------------------------------------------
	// 서버 종료급 이벤트. 세션아이디가 비정상
	//---------------------------------------------------
	stZoneSession* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] SendPacket() 인덱스 유효하지 않음", sessionId);
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		// return -1;
		return false;
	}

	//---------------------------------------------------
	// 이미 참조는 잘 올라가있는데,
	// 이건 SendPost우회 시 감소로 릴리즈 문제 없게
	//---------------------------------------------------
	_InterlockedIncrement(&pSession->refcount);

	//---------------------------------------------------
	// < V4 >
	// . 인코딩이 필요하면 해주자
	//---------------------------------------------------
	if (pPacket->isEncoded() == 0)
	{
		//---------------------------------------------------
		// < V3 >
		// . 인큐 하기 직전에 헤더를 세팅해주자
		//---------------------------------------------------
		Net::stNetHeader header;
		header.code = _serverCrypto.code;
		header.len = pPacket->GetDataSize() - Net::NET_HEADER_LEN;
		header.randkey = (uint8_t)(rand() % 256);
		header.checksum = Net::CCryptoUtils::GetCheckSum(pPacket);
		memcpy(pPacket->GetBufferPtr(), &header, sizeof(Net::stNetHeader));
		pPacket->SetEncoded();

		if (_option.bUseEncode)
		{
			Net::CCryptoUtils::Encode(pPacket, _serverCrypto);
		}
	}

	//---------------------------------------------------
	// 여기서 Disconnect올라가있을 수 있지만,
	// 알아서 초기화할때 큐 비우니까 괜춘
	// 
	// < V3 >
	// . 인큐에 성공하면 이제 패킷은 큐 소유이기 때문에
	// ** 참조 카운트는 디큐하고나서 알아서 해제 해준다.
	//---------------------------------------------------
	CPACKET_UPDATE_TRACE(pPacket);
	bool retEnq = pSession->sendQ->Enqueue(pPacket);
	if (retEnq == false)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"[sessionId: %016llx] 센드 버퍼가 다 찬 클라입니다. iocount == 0이 되면 해제합니다.", sessionId);

		//-----------------------------------------------
		// 인큐 실패했으니 해제까지
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}

		// 디스커넥트 플래그 활성화
		_InterlockedExchange(&pSession->isDisconnect, 1);
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl->ol);

		DecrementRefcount(pSession);
		return true;
	}


	//----------------------------------------------------
	// 인큐에 성공함
	// . 보낼지 안보낼지는 모른다! (디스커넥트 상태에 따라)
	//----------------------------------------------------
	_monitorJob->IncreaseSendMessageCount();

	
	long isSending = _InterlockedExchange(&pSession->isSending, 1);
	if (isSending == 1)
	{
		DecrementRefcount(pSession);
		return true;	// 못 보내니 나옴
	}
	

	//---------------------------------------------------
	// isSending이 0인경우임. 즉 내가 1로 만듦!
	//---------------------------------------------------
	if (pSession->isDisconnect == 1)
	{
		_InterlockedExchange(&pSession->isSending, 0);
		DecrementRefcount(pSession);
		return true;
	}

	PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_DELAYSEND, (ULONG_PTR)pSession, nullptr);
	
	//{
	//	Profile pf(L"RingBuffer Enqueue");
	//	_sendRequests.exclusive_lock();
	//	if(sizeof(stZoneSession*) != _sendRequests.Enqueue((const char*)&pSession, sizeof(stZoneSession*)))
	//		PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_DELAYSEND, (ULONG_PTR)pSession, nullptr);
	//	_sendRequests.exclusive_unlock();
	//}

	return true;
}

bool Net::CZoneServer::SetUserPointer(uint64_t sessionId, void* userPtr)
{
	CReferenceZoneSession refSession(sessionId, this);
	if (refSession.isAlive() == false)
		return false;
	_InterlockedExchangePointer(&refSession.GetZoneSession()->userPtr, userPtr);
	return true;
}

//========== PRIVATE FUNCTIONS ==========//

void Net::CZoneServer::ReleaseSession(Net::stZoneSession* pSession)
{
	if (_InterlockedCompareExchange(&pSession->refcount, SESSION_RELEASE_FLAG, 0) != 0)
	{
		return;
	}
	uint64_t sessionId = pSession->sessionId;
	uint64_t zoneId = pSession->zoneId;
	pSession->Clear();
	bool isOk = _sessionStructure.ReleaseSession(pSession);
	if (isOk == false)
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] 인덱스가 이상합니다", sessionId);

	// 존 Leave (세션의 존ID 불변 상태)
	if (zoneId != 0)
	{
		// PQCS로 안넣어도됨. OnLeave()는 바로 여기서 실행이 아닌 큐에 들어가서 존 내부에서 꺼내서 호출되서 구분됨.
		_zoneManager.LeaveZone(zoneId, sessionId);
	}

	//---------------------------------------------------
	// OnRelease가 이벤트로써 발동되게 iocp큐에 넣자.
	// ** sessionId는 0이 나올 수 없는 설계를 위해
	//    인덱스 0번을 사용하지 않게 하자
	//---------------------------------------------------
	PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_RELEASE, (ULONG_PTR)sessionId, nullptr);
}

Net::stZoneSession* Net::CZoneServer::InitNewSession(SOCKET newSocket, SOCKADDR_IN* caddr)
{
	int index = -1;
	Net::stZoneSession* pNewSession = _sessionStructure.AcquireSession(&index);
	if (pNewSession == nullptr)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"세션이 부족한데 이상하네(GetSession() 실패)");
		return nullptr;
	}

	pNewSession->Init(newSocket, index, caddr, _InterlockedIncrement(&_sid));
	return pNewSession;
}

void Net::CZoneServer::Init_Rollback()
{
	_InterlockedExchange(&_isCallInit, 0);

	if (_lsock != INVALID_SOCKET)
	{
		int retClose = closesocket(_lsock);
		if (retClose == SOCKET_ERROR)
		{
			Core::c_syslog::logging().LogEx(TAG_NET, GetLastError(), Core::c_syslog::en_ERROR, L"Netlib::Init_Rollback() = 리슨소켓 close 오류");
		}
		_lsock = INVALID_SOCKET;
	}

	if (_hIOCP != NULL)
	{
		BOOL retCloseIOCP = CloseHandle(_hIOCP);
		if (retCloseIOCP == FALSE)
		{
			Core::c_syslog::logging().LogEx(TAG_NET, GetLastError(), Core::c_syslog::en_ERROR, L"Netlib::Init_Rollback() = IOCP close 오류");
		}
		_hIOCP = NULL;
	}

	for (int i = 0; i < NET_MAX_THREADS_CNT; i++)
	{
		if (_hThreads[i] != 0)
		{
			BOOL retCloseThreads = CloseHandle(_hThreads[i]);
			if (retCloseThreads == FALSE)
			{
				Core::c_syslog::logging().LogEx(TAG_NET, GetLastError(), Core::c_syslog::en_ERROR, L"Netlib::Init_Rollback() = 스레드 close 오류");
			}
			_hThreads[i] = 0;
		}
	}

	_threadNum = 0;
	if (_hEventForAccept != NULL)
	{
		CloseHandle(_hEventForAccept);
	}

	if (_hEventForExit != NULL)
	{
		CloseHandle(_hEventForExit);
	}

	ExitMonitoringJob();
}

void Net::CZoneServer::DecrementRefcount(Net::stZoneSession* pSession)
{
	unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
	if (retNum == 0)
	{
		ReleaseSession(pSession);
	}
	else
	{
		if (retNum == 0xFFFF'FFFF)
			__debugbreak();
	}
}

bool Net::CZoneServer::IncreaseRecvCntForMessage(Net::stZoneSession* pSession)
{
	int cnt = ++pSession->recvCntForMessage;
	if (cnt > Net::NET_MAX_PACKET_COMPLETE)
	{
		Core::c_syslog::logging().Log(NET_STRANGE_PACKET, Core::c_syslog::en_SYSTEM,
			L"[cnt: %d] 메시지 하나 생성되는데 너무 오래걸려요;;; ", cnt);
		return false;
	}
	return true;
}

void Net::CZoneServer::ClearRecvCntForMessage(Net::stZoneSession* pSession)
{
	pSession->recvCntForMessage = 0;
}

void Net::CZoneServer::SendPQCS(Net::stZoneSession* pSession)
{
	bool isFine = pSession->SendPost();
	if (isFine == false)
	{
		//-----------------------------------------------
		// isSending은 0이 된 상태. 디스커넥트가 올라갔는지 
		// 확인이 필요하다.
		//-----------------------------------------------
		DecrementRefcount(pSession);
	}
	else
	{
		//-----------------------------------------------
		// 여기는 SendPost가 성공적으로 끝남.
		// 완료통지에서 참조카운트 해제
		//-----------------------------------------------
	}
}

void Net::CZoneServer::InitMonitoringJob()
{
	_monitorJob = std::make_shared<CZoneServerMonitoringJob>();
	_monitorJob->_workerNum = _option.iWorkerThreadCreateCnt;
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_monitorJob, 0);
}

void Net::CZoneServer::ExitMonitoringJob()
{
	_monitorJob->CancelJob();
}

bool Net::CZoneServer::IOCP_RequestProc(DWORD message, void* key)
{
	switch (message)
	{
	case SERVER_MSG_EXIT:
	{
		// 종료 시그널
		return false;
	}

	case SERVER_MSG_RELEASE:
	{
		OnRelease((uint64_t)key);
		break;
	}

	case SERVER_MSG_DELAYSEND:
	{
		SendPQCS((Net::stZoneSession*)key);
		break;
	}

	case SERVER_MSG_USER_EVENT:
	{
		OnUserEvent((CPacket*)key);
		break;
	}

	case SERVER_MSG_ZONE_UPDATE:
	{
		RequestZoneExcute((uint64)key);
		break;
	}

	case 5:
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"WorkerThread 잘못된 시그널 (cb:%d key: %p)", message, key);
		__debugbreak();
	}

	default:
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"WorkerThread 잘못된 시그널 (cb:%d key: %p)", message, key);
		__debugbreak();
	}

	}

	return true;
}

void Net::CZoneServer::IOCP_CbTransferred_Zero(Net::stZoneSession* pSession, Net::stZoneSession::myoverlapped* pOverlapped)
{
	//------------------------------------------------------
	// Send라면 오버랩 구조체에 실었던 버퍼들 해제 필수!
	//------------------------------------------------------
	int retFree;
	if (pOverlapped->isSend)
	{
		int sendPacketsCnt = pSession->sendPacketsCnt;
		for (int i = 0; i < sendPacketsCnt; i++)
		{
			retFree = CPACKET_FREE(pSession->sendPackets[i]);
			if (retFree)
			{
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
				__debugbreak();
			}
		}
		//-------------------------------------------------
		// 해제 표식 남기기
		//-------------------------------------------------
		pSession->sendPacketsCnt = 0;
		pOverlapped->sendbyte = 0;
		_InterlockedExchange(&pSession->isDisconnect, 1);

		//----------------------------------------------
		// ** 중요한것을 빼먹음 **...
		// . 이게 없으면 리시브는 여원히 걸릴 수 있음
		//----------------------------------------------
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl->ol);

		_InterlockedExchange(&pSession->isSending, 0);
		//OnSend(pSession->sessionId, false);

		DecrementRefcount(pSession);
	}
	else
	{
		//-------------------------------------------------
		// Recv 의 경우
		//-------------------------------------------------
		_InterlockedExchange(&pSession->isDisconnect, 1);

		//----------------------------------------------
		// ** 중요한것을 빼먹음 **...
		//----------------------------------------------
		CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
		DecrementRefcount(pSession);
	}
}

void Net::CZoneServer::ZonePQCS(uint64 zoneId) const
{
	PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_ZONE_UPDATE, (ULONG_PTR)zoneId, nullptr);
}

void Net::CZoneServer::RequestZoneExcute(uint64 zoneId)
{
	int index = _zoneManager.GetZoneIndex(zoneId);
	CZone* pZone = _zoneManager.GetZonePtr(index);
	if (pZone == nullptr)
	{
		Core::c_syslog::logging().Log(TAG_ZONE, Core::c_syslog::en_ERROR,
			L"[zoneid: %16llx]zoneId가 이상함", zoneId);
		return;
	}

	if (pZone->isReleasing() == true)
	{
		_zoneManager.DecreaseRefcount(pZone);
		return;
	}

	if (pZone->_zoneId != zoneId)
	{
		_zoneManager.DecreaseRefcount(pZone);
	}

	pZone->TickUpdate();
	
	_zoneManager.DecreaseRefcount(pZone);
}


#pragma endregion

#pragma region CServer_ThreadProc


//-------------------------------------------------------
// Info: 워커 스레드 function
//-------------------------------------------------------
unsigned int Net::CZoneServer::NetServerWorkerFunc(void* param)
{
	PRO_START();
	int32 myId = (int32)_InterlockedIncrement(&Net::CZoneServer::s_threadId);
	CZoneServer* nowServer = (CZoneServer*)param;
	HANDLE hcp = (HANDLE)nowServer->_hIOCP;
	DWORD tid = GetCurrentThreadId();
	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"[%d] WorkerThread Start!!", myId);

	ZoneServerMonitor& ZoneServerMonitor = nowServer->_monitorJob;
	volatile uint32* pRecvCnt = &(nowServer->_monitorJob->_recvMessageCount);
	bool bNeedDecode = nowServer->_option.bUseEncode;
	stPacketCrypto& nowServerCrypto = nowServer->_serverCrypto;

	nowServer->OnWorkerStart();

	DWORD wakeTime = timeGetTime();
	DWORD sleepTime;
	while (1)
	{
		DWORD cbTransferred = 0;
		stZoneSession* pSession = nullptr;
		stZoneSession::myoverlapped* pOverlapped = nullptr;

		sleepTime = timeGetTime();
		ZoneServerMonitor->IncreaseWorkTime(int32(sleepTime - wakeTime), myId);
		GetQueuedCompletionStatus(hcp, &cbTransferred, (ULONG_PTR*)&pSession, (WSAOVERLAPPED**)&pOverlapped, INFINITE);
		wakeTime = timeGetTime();

		if (pOverlapped == nullptr)
		{
			if (nowServer->IOCP_RequestProc(cbTransferred, pSession) == false)
				break;
		}
		else if (cbTransferred == 0)
		{
			nowServer->IOCP_CbTransferred_Zero(pSession, pOverlapped);
		}
		else if (pOverlapped->isSend)
		{
			Core::Profile pf(L"GQCS: Send");

			//-------------------------------------------------
			// Send
			// . refcount는 아직 양수 (Sendpost전에 내리면 바뀔 수 있음)
			// . isSending 역시 여전히 1 (sendOl.sendbyte는 안바뀜)
			//-------------------------------------------------

			// 일단 보낸 결과물에 대한 해제는 진행 필요
			int sendPacketsCnt = pSession->sendPacketsCnt;
			for (int i = 0; i < sendPacketsCnt; i++)
			{
				int retFree = CPACKET_FREE(pSession->sendPackets[i]);
				if (retFree)
				{
					Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
					__debugbreak();
				}
			}
			//-------------------------------------------------
			// 다른 스레드가 아직 접근 못하는 영역이므로 
			// 직접 초기화 시켜줌
			//-------------------------------------------------
			pSession->sendPacketsCnt = 0;
			int sendbyte = pOverlapped->sendbyte;
			//-------------------------------------------------
			// . 아직 isSending = 1, 다른 스레드 sendbyte못건드림
			// . refcount역시 1로 이 세션의 세션아이디가 바뀔 일도 없음
			//-------------------------------------------------
			if (sendbyte > (int)cbTransferred)
			{
				//---------------------------------------------
				// 중간에 연결이 끊긴 경우
				//---------------------------------------------
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_DEBUG, L"[sessionId: %016llx] [sendbyte: %d, cbTransferred: %d] 상대가 연결을 끊었습니다(Send가 덜 보내진 경우). iocount == 0시 해제",
					pSession->sessionId, pOverlapped->sendbyte, cbTransferred);
				pOverlapped->sendbyte = 0;	//초기화

				_InterlockedExchange(&pSession->isDisconnect, 1);	// 세션 디스커넥트 올리기
				CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl->ol);

				_InterlockedExchange(&pSession->isSending, 0);
				
				nowServer->DecrementRefcount(pSession);
			}
			else
			{
				
				//--------------------------------------------------
				// 센드 성공! 
				// . 일단 isSending은 0으로
				// . 참조 카운트는 유지하고 센드 권한을 얻지 못하면
				//   내려준다.
				//--------------------------------------------------
				pOverlapped->sendbyte = 0;
				_InterlockedExchange(&pSession->isSending, 0);

				long isSending = _InterlockedExchange(&pSession->isSending, 1);
				if (isSending == 0)
				{
					if (pSession->isDisconnect == 1)
					{
						_InterlockedExchange(&pSession->isSending, 0);
						nowServer->DecrementRefcount(pSession);
					}
					else
					{
						bool isFine = pSession->SendPost();
						if (isFine == false)
						{
							//-----------------------------------------------
							// isSending은 0이 된 상태. 참조 카운트도 해제함
							//-----------------------------------------------
							nowServer->DecrementRefcount(pSession);
						}
					}
				}
				else
				{
					//---------------------------------------------------
					// isSending 이미 1임. 다른 워커가 보내는중
					// 완료통지에서 참조카운트 내려줘야함
					// ** 중요 ** 여기서 잠들면 isSending이 0 될수 있음.
					// 그래서 refcount 확인 필요
					//---------------------------------------------------
					nowServer->DecrementRefcount(pSession);
				}
			}
		}
		else // Recv
		{
			Core::Profile pf(L"GQCS: Recv");

			if (pSession->isDisconnect == 1)
			{
				nowServer->DecrementRefcount(pSession);
				continue;
			}

			CPacket* recvQ = pSession->recvQ;
			recvQ->MoveWritePtr(cbTransferred);

			uint16_t payloadlen;
			bool canRecvPost = true;
			bool retHeaderCheck = true;
			bool needNewBuffer = false;
			while (recvQ->GetDataSize() > 0)
			{
				if (recvQ->GetDataSize() < sizeof(Net::stNetHeader))
				{
					bool bRet = nowServer->IncreaseRecvCntForMessage(pSession);
					if (bRet == false)
					{
						nowServer->DecrementRefcount(pSession);
						canRecvPost = false;
					}
					break;
				}

				// Peek 페이로드길이
				payloadlen = *(uint16_t*)(recvQ->GetReadPtr() + 1);

				if (recvQ->GetDataSize() >= sizeof(Net::stNetHeader) + payloadlen)
				{
					//---------------------------------------------
					// V4: 헤더 체크
					//---------------------------------------------
					unsigned char* pRead = (unsigned char*)recvQ->GetReadPtr();
					retHeaderCheck = Net::CCryptoUtils::CheckHeader(pRead, payloadlen, bNeedDecode, nowServerCrypto);
					if (retHeaderCheck == false)
					{
						Core::c_syslog::logging().LogHex(NET_STRANGE_PACKET, Core::c_syslog::en_SYSTEM, pRead, payloadlen + sizeof(Net::stNetHeader),
							L"[sessionId: %016llx] 이상한 패킷 헤더", pSession->sessionId);

						_InterlockedExchange(&pSession->isDisconnect, 1);
						CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
						nowServer->DecrementRefcount(pSession);

						canRecvPost = false;
						break;
					}
					//--------------------------------------------
					// RecvTps증가
					//--------------------------------------------
					_InterlockedIncrement(pRecvCnt);

					//--------------------------------------------
					// 컨텐츠로 넘기기
					//--------------------------------------------
					recvQ->MoveReadPtr(sizeof(Net::stNetHeader));
					nowServer->ClearRecvCntForMessage(pSession);

					if(pSession->zoneId == 0)
					{
						nowServer->OnMessage(pSession->sessionId, recvQ, payloadlen);
					}
					else
					{
						// Core::Profile pf(L"LockFreeMessageParse_Recv");
						int ret = pSession->recvedPackets.Enqueue((const char*)&payloadlen, sizeof(payloadlen));
						if (ret != sizeof(payloadlen))
						{
							Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM,
								L"[sessionId: %016llx | zoneId: %016llx] 길이 넣기 실패/RecvedPackets 버퍼 풀, 존 바뀌었다면 악성 의심유저, 아니라면 존이 바쁨",
								pSession->sessionId, pSession->zoneId);

							_InterlockedExchange(&pSession->isDisconnect, 1);
							CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
							nowServer->DecrementRefcount(pSession);

							canRecvPost = false;
							break;
						}

						ret = pSession->recvedPackets.Enqueue(recvQ->GetReadPtr(), (int)payloadlen);
						if (ret != (int)payloadlen)
						{
							Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM,
								L"[sessionId: %016llx | zoneId: %016llx] 페이로드 넣기 실패/RecvedPackets 버퍼 풀, 존 바뀌었다면 악성 의심유저, 아니라면 존이 바쁨",
								pSession->sessionId, pSession->zoneId);

							_InterlockedExchange(&pSession->isDisconnect, 1);
							CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
							nowServer->DecrementRefcount(pSession);

							canRecvPost = false;
							break;
						}
					}

					recvQ->MoveReadPtr(payloadlen);
				}
				else
				{
					if (payloadlen > NET_RECVQ_SIZE)
					{
						Core::c_syslog::logging().Log(NET_STRANGE_PACKET, Core::c_syslog::en_SYSTEM, L"[sessionId: %016llx] 패킷이 너무 길어요. 읽을 수가 없음 [header payloadlen: %d]", pSession->sessionId, payloadlen);

						_InterlockedExchange(&pSession->isDisconnect, 1);
						CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl->ol);
						nowServer->DecrementRefcount(pSession);

						canRecvPost = false;
					}
					else if (payloadlen - pSession->recvQ->GetDataSize() > pSession->recvQ->GetFreeSize())
					{
						//----------------------------------------------
						// 더 받아도 못읽음
						//----------------------------------------------
						needNewBuffer = true;
					}
					else
					{
						//----------------------------------------------
						// 메시지 완성이 안되서 횟수 올리기
						//----------------------------------------------
						bool bRet = nowServer->IncreaseRecvCntForMessage(pSession);
						if (bRet == false)
						{
							nowServer->DecrementRefcount(pSession);
							canRecvPost = false;
						}
					}
					break;
				}
			}

			if (canRecvPost == false)
				continue;

			//-----------------------------------------------------------
			// 새로운 리시브 버퍼를 받자
			//-----------------------------------------------------------
			if ((recvQ->GetFreeSize() < NET_RECVQ_MIN_LEFT_SIZE) || (needNewBuffer == true))
			{
				pSession->recvQ = CPACKET_ALLOC();	// 새로운 것 받음
				pSession->recvQ->SetRecvBuffer();

				pSession->recvQ->PushData(recvQ->GetReadPtr(), recvQ->GetDataSize());
				CPACKET_FREE(recvQ);
			}


			bool retRecv = pSession->RecvPost();
			if (retRecv == false)
			{
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_DEBUG, L"[sessionId: %016llx] Recv완료통지 이후 Recv실패로 ReleaseSession() 돌입", pSession->sessionId);
				nowServer->ReleaseSession(pSession);
			}
		}
	}

	nowServer->OnWorkerEnd();

	PRO_EXIT();
	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"[%d] WorkerThread Exit!!", myId);
	return 0;
}

//-------------------------------------------------------
// Info: Accept 스레드 function
//-------------------------------------------------------
unsigned int Net::CZoneServer::NetServerAcceptFunc(void* param)
{
	PRO_START();
	CZoneServer* nowServer = (CZoneServer*)param;
	DWORD tid = GetCurrentThreadId();

	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"AcceptThread Start!!");
	int MaxUsers = nowServer->_option.iMaxConcurrentUsers;
	const std::shared_ptr<Net::CZoneServerMonitoringJob>& pMonitor = nowServer->_monitorJob;
	SessionStructure* pSessionStructure = &nowServer->_sessionStructure;
	HANDLE hcp = nowServer->_hIOCP;
	HANDLE hEvents[2] = { nowServer->_hEventForAccept, nowServer->_hEventForExit };

	SOCKADDR_IN caddr;
	int caddrlen;
	SOCKET newSocket;
	stZoneSession* pNewSession;
	while (1)
	{
		DWORD retEvent = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
		if (retEvent == WAIT_OBJECT_0 + 1)
		{
			//---------------------------------------------------------------
			// 종료 이벤트
			//---------------------------------------------------------------
			break;
		}

		caddrlen = sizeof(caddr);
		newSocket = accept(nowServer->_lsock, (SOCKADDR*)&caddr, &caddrlen);
		if (newSocket == INVALID_SOCKET)
		{
			DWORD dwAcceptErr = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, dwAcceptErr, Core::c_syslog::en_ERROR, L"어셉트 스레드의 accept()오류코드입니다.");
			continue;
		}
		// Accept모니터링
		pMonitor->IncreaseAcceptCount();

		LINGER rstLinger = { 1, 0 };
		int retLinger = setsockopt(newSocket, SOL_SOCKET, SO_LINGER, (char*)&rstLinger, sizeof(rstLinger));
		if (retLinger == SOCKET_ERROR)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"링거 설정에 실패하였습니다");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				Core::c_syslog::logging().LogEx(TAG_NET, closeErr, Core::c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		bool isConnectOk = nowServer->OnConnectionRequest(caddr.sin_addr);
		if (isConnectOk == false)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_DEBUG, L"연결이 거부된 IP, Port여서 거부했습니다. 상세로그는 OnConnectionRequest에");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				Core::c_syslog::logging().LogEx(TAG_NET, closeErr, Core::c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		if (nowServer->_option.bUseSO_SNDBUF == true)
		{
			DWORD sndbuflen = 0;
			int retZeroSNDBUF = setsockopt(newSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuflen, sizeof(sndbuflen));
			if (retZeroSNDBUF == SOCKET_ERROR)
			{
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"소켓 SNDBUF = 0 설정에 실패하였습니다.");
				int retClose = closesocket(newSocket);
				if (retClose == SOCKET_ERROR)
				{
					DWORD closeErr = GetLastError();
					Core::c_syslog::logging().LogEx(TAG_NET, closeErr, Core::c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
				}
				continue;
			}
		}

		if (nowServer->_option.bUseTCP_NODELAY == true)
		{
			DWORD tcpNodelay = TRUE;
			int retTcpNodelay = setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&tcpNodelay, sizeof(tcpNodelay));
			if (retTcpNodelay == SOCKET_ERROR)
			{
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"TCP NODELAY 설정에 실패하였습니다.");
				int retClose = closesocket(newSocket);
				if (retClose == SOCKET_ERROR)
				{
					DWORD closeErr = GetLastError();
					Core::c_syslog::logging().LogEx(TAG_NET, closeErr, Core::c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
				}
				continue;
			}
		}

		if (pSessionStructure->_sessionCnt >= MaxUsers)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"유저가 꽉 차서 끊음");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				Core::c_syslog::logging().LogEx(TAG_NET, closeErr, Core::c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		pNewSession = nowServer->InitNewSession(newSocket, &caddr);
		if (pNewSession == nullptr)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"서버 세션 관리가 이상함. AcceptThread에서 제대로 세션을 받지 못함");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				Core::c_syslog::logging().LogEx(TAG_NET, closeErr, Core::c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		//--------------------------------------------------------------
		// 여기부터 세션을 정상적으로 꺼냈으니 refcount사용
		//--------------------------------------------------------------
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_DEBUG, L"[%s:%d][sessionId: %016llx] 새로운 세션이 생성되었습니다. ", pNewSession->ip, pNewSession->port, pNewSession->sessionId);

		unsigned long retNum;
		HANDLE retRegister = CreateIoCompletionPort((HANDLE)pNewSession->sock, hcp, (ULONG_PTR)pNewSession, 0);
		if (retRegister == NULL)
		{
			DWORD registerOnIOCPErr = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, registerOnIOCPErr, Core::c_syslog::en_ERROR, L"[sessionId: %016llx] IOCP등록에 실패 RelaseSession()돌입", pNewSession->sessionId);
			retNum = _InterlockedDecrement(&pNewSession->refcount);
			if (retNum == 0)
			{
				nowServer->ReleaseSession(pNewSession);
			}
			continue;
		}

		// OnAccept
		nowServer->OnAccept(pNewSession->sessionId, caddr.sin_addr, pNewSession->ip);

		//-----------------------------------------------
		// 첫 Recv!!
		// . 센드 한적 없음
		// . 리시브 한 적 없음
		// => 여기서 pNewSession이 바뀌었을 가능성 0%
		//-----------------------------------------------
		bool retRecv = pNewSession->RecvPost();
		if (retRecv == false)
		{
			// refcount == 0
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_DEBUG, L"[sessionId: %016llx] AcceptThread에서 세션의 첫 Recv실패로 ReleaseSession() 돌입", pNewSession->sessionId);
			nowServer->ReleaseSession(pNewSession);
		}
	}

	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"AcceptThread Exit!!");
	PRO_EXIT();
	return 0;
}

#pragma endregion




Net::CReferenceZoneSession::CReferenceZoneSession(uint64 sessionId, CZoneServer* pZoneServer)
	: _pZoneServer(pZoneServer), _pZoneSession(nullptr)
{
	Net::stZoneSession* pZoneSession = pZoneServer->FindSession(sessionId);
	if (pZoneSession == nullptr)
	{
		return;
	}
	unsigned long retNum = _InterlockedIncrement(&pZoneSession->refcount);
	if ((retNum & SESSION_RELEASE_FLAG) == SESSION_RELEASE_FLAG)
	{
		pZoneServer->DecrementRefcount(pZoneSession);
		return;
	}
	if (pZoneSession->sessionId != sessionId)
	{
		pZoneServer->DecrementRefcount(pZoneSession);
		return;
	}
	if (pZoneSession->isDisconnect)
	{
		pZoneServer->DecrementRefcount(pZoneSession);
		return;
	}

	_pZoneSession = pZoneSession;
}

Net::CReferenceZoneSession::~CReferenceZoneSession()
{
	if (_pZoneSession != nullptr)
	{
		_pZoneServer->DecrementRefcount(_pZoneSession);
	}
}



// FOR PQCS를 우회
// unsigned int Net::CZoneServer::SendThreadProc(void* param)
// {
// 	CZoneServer* pMyServer = (CZoneServer*)param;
// 	Core::RingBuffer& sendRequests = pMyServer->_sendRequests;
// 	HANDLE exitEvent = pMyServer->_hEventForExit;
// 	HANDLE hIOCP = pMyServer->_hIOCP;
// 
// 	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"SendProc Start!!");
// 
// 	for (;;)
// 	{
// 		DWORD ret = WaitForSingleObject(exitEvent, 0);
// 		if (ret == WAIT_OBJECT_0)
// 			break;
// 
// 		while (sendRequests.GetUseSize() > 0)
// 		{
// 			stZoneSession* pSession;
// 			if( sizeof(stZoneSession*) == sendRequests.Dequeue((char*)&pSession, sizeof(stZoneSession*)))
// 				PostQueuedCompletionStatus(hIOCP, SERVER_MSG_DELAYSEND, (ULONG_PTR)pSession, nullptr);
// 		}
// 	}
// 
// 	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"SendProc End!!");
// 	return 0;
// }