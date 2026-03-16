#include "NetClient.h"
#include "logclassV1.h"
#include "NetProcess.h"
#include "NetProtocol.h"

#include <process.h>
#include <timeapi.h>
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "winmm")


#include "ProfilerV2.hpp"


using Log = Core::c_syslog;
using namespace Core;

// CClientMonitoringJob ================================//

Net::CClientMonitoringJob::CClientMonitoringJob()
{
	_startTime = timeGetTime();
}

void Net::CClientMonitoringJob::Excute()
{
	_recvMessageTps = _InterlockedExchange(&_recvMessageCount, 0);
	_sendMessageTps = _InterlockedExchange(&_sendMessageCount, 0);

	DWORD curTime = timeGetTime();
	int32 deltaTime = CLIENT_MONITORING_TICK - (int32)(curTime - _startTime);
	if (deltaTime < 0)
	{
		_startTime = curTime + CLIENT_MONITORING_TICK;
		Core::c_syslog::logging().Log(TAG_TIMER, Core::c_syslog::en_ERROR, L"CClient Monitoring TickUpdate(): [delta ms: %d]  문제있음 (빡빡)", -deltaTime);
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, CLIENT_MONITORING_TICK, curTime);
	}
	else
	{
		_startTime += CLIENT_MONITORING_TICK;
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, deltaTime, curTime);
	}
}

// CClient ================================//

bool Net::CClient::Init(const stClientOpt* pOpt)
{
	long retIsInit = _InterlockedExchange(&_isCallInit, 1);
	if (retIsInit == 1)
		return true;	// 이미 해서 안해도 됨

	InitMonitoringJob();
	do
	{
		_option = *pOpt;

		_clientCrypto.code = pOpt->client_code;
		_clientCrypto.static_key = pOpt->static_key;

		OnInit();

		// MAKE IOCP
		_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, _option.iWorkerThreadRunCnt);
		if (_hIOCP == NULL)
		{
			DWORD dwCreateIOCPErr = GetLastError();
			Core::c_syslog::logging().LogEx(TAG_NET, dwCreateIOCPErr, Core::c_syslog::en_ERROR, L"CreateIOCP failed");
			break;
		}

		int createCnt = _option.iWorkerThreadCreateCnt;
		for (int i = 0; i < createCnt; i++)
		{
			_hThreads[i] = (HANDLE)_beginthreadex(NULL, NULL, NetClientWorkerProc, this, 0, NULL);
			if (_hThreads[i] == 0)
			{
				int retCreateWorkerThreadErr = errno;
				Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"[errno: %d] Create WorkerThread failed", retCreateWorkerThreadErr);
				break;
			}
			_threadNum++;
		}

		return true;
	} while (0);

	Init_Rollback();
	return false;
}

void Net::CClient::Init_Rollback()
{
	_InterlockedExchange(&_isCallInit, 0);

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

	ExitMonitoringJob();
}

void Net::CClient::Exit()
{
	_InterlockedExchange(&_isExit, 1);
	Disconnect();

	for(int i = 0; i < _threadNum; i++)
		PostQueuedCompletionStatus(_hIOCP, CLIENT_MSG_EXIT, 0, nullptr);
	WaitForMultipleObjects(_threadNum, _hThreads, TRUE, INFINITE);
	ExitMonitoringJob();
	CloseHandle(_hIOCP);
	OnExit();
}

Net::CClient::CClient() :_session(), _option{}, _isCallInit(0), _isExit(0),
_clientCrypto{}, _hThreads{}, _hIOCP(NULL), _threadNum(0),_wsa(), _sid(0)
{
	_session.isDisconnect = 1;
}

bool Net::CClient::Connect(int32 count)
{
	bool ret = false;
	int32 cnt = 0;
	while (cnt < count || count == -1)
	{
		if (_isExit == 1)
			break;

		if (InitSession() == true)
		{
			ret = true;
			break;
		}
		Sleep(100);
		cnt++;
	}
	uint64 sessionId = _session.sessionId;
	if (ret)
		PostQueuedCompletionStatus(_hIOCP, CLIENT_MSG_CONNECT, (ULONG_PTR)sessionId, nullptr);
	return ret;
}

bool Net::CClient::InitSession()
{
	SOCKET sock = INVALID_SOCKET;
	do
	{
		if (_isCallInit == 0)
		{
			Log::logging().Log(TAG_NET, Log::en_ERROR, L"CClient() not init");
			break;
		}

		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			Log::logging().LogEx(TAG_NET, GetLastError(), Log::en_ERROR, L"CClient::InitSession(): socket create failed");
			break;
		}

		SOCKADDR_IN serverAddr;
		serverAddr.sin_family = AF_INET;
		InetPtonW(AF_INET, _option.targetIP, &serverAddr.sin_addr);
		serverAddr.sin_port = htons(_option.targetPort);

		if (_option.bUseBind == true)
		{
			SOCKADDR_IN clientAddr;
			clientAddr.sin_family = AF_INET;
			InetPtonW(AF_INET, _option.bindIP, &clientAddr.sin_addr);
			clientAddr.sin_port = htons(_option.bindPort);
			int32 ret = bind(sock, (sockaddr*)&clientAddr, sizeof(clientAddr));
			if (ret == SOCKET_ERROR)
			{
				Log::logging().LogEx(TAG_NET, WSAGetLastError(), Log::en_ERROR, L"CClient::InitSession(): bind failed");
				break;
			}
		}

		if (_option.bUseSO_SNDBUF == true)
		{
			DWORD sndbuflen = 0;
			int retZeroSNDBUF = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuflen, sizeof(sndbuflen));
			if (retZeroSNDBUF == SOCKET_ERROR)
			{
				Log::logging().LogEx(TAG_NET, WSAGetLastError(), Log::en_ERROR, L"CClient::InitSession(): so_snbuf set failed");
				break;
			}
		}

		if (_option.bUseTCP_NODELAY == true)
		{
			DWORD tcpNodelay = TRUE;
			int retTcpNodelay = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&tcpNodelay, sizeof(tcpNodelay));
			if (retTcpNodelay == SOCKET_ERROR)
			{
				Log::logging().LogEx(TAG_NET, WSAGetLastError(), Log::en_ERROR, L"CClient::InitSession(): tcp nodelay set failed");
				break;
			}
		}

		LINGER rstLinger = { 1, 0 };
		int retRstLinger = setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&rstLinger, sizeof(rstLinger));
		if (retRstLinger == SOCKET_ERROR)
		{
			Log::logging().LogEx(TAG_NET, WSAGetLastError(), Log::en_ERROR, L"CClient::InitSession(): rst linger set failed");
			break;
		}

		int retConn = connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
		if (retConn == SOCKET_ERROR)
		{
			Log::logging().LogEx(TAG_NET, WSAGetLastError(), Log::en_ERROR, L"CClient::InitSession(): connect failed");
			break;
		}
		HANDLE retRegister = CreateIoCompletionPort((HANDLE)sock, _hIOCP, (ULONG_PTR)&_session, 0);
		if (retRegister == NULL)
		{
			DWORD registerOnIOCPErr = GetLastError();
			Log::logging().LogEx(TAG_NET, registerOnIOCPErr, Log::en_ERROR, L"Cclient connect IOCP 등록 error");
			break;
		}

		_session.Init(sock, 0, &serverAddr, _sid++);
		GetClientMonitor()->IncreaseConnectCount();
		//-----------------------------------------------
		// 첫 Recv!!
		// . 센드 한적 없음
		// . 리시브 한 적 없음
		// => 여기서 pNewSession이 바뀌었을 가능성 0%
		//-----------------------------------------------
		bool retRecv = _session.RecvPost();
		if (retRecv == false)
		{
			ReleaseSession();
		}
		return true;
	} while (0);

	if (sock != INVALID_SOCKET)
		closesocket(sock);
	return false;
}

void Net::CClient::Disconnect()
{
	//---------------------------------------------------
	// * 참조 카운트를 올려서 릴리즈 진입 못하도록
	// * 릴리즈 진행중인지 확인하기
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&_session.refcount);
	if ((retNum & SESSION_RELEASE_FLAG) == SESSION_RELEASE_FLAG)
	{
		DecrementRefcount();
	}

	//---------------------------------------------------
	// 여기서부터는 세션 id 변화가 없음.
	// (릴리즈가 불가능해서)
	// 
	// . 내것이 맞으면 디스커넥트 플래그 활성화
	// . I / O 캔슬도 해주기
	// . WSASend, WSARecv가 인지하고 역시 알아서 취소 시도할 것이다
	//---------------------------------------------------
	_InterlockedExchange(&_session.isDisconnect, 1);
	CancelIoEx((HANDLE)_session.sock, &_session.sendOl.ol);
	CancelIoEx((HANDLE)_session.sock, &_session.recvOl.ol);

	DecrementRefcount();
}

bool Net::CClient::SendPacket(Net::CPacket* pPacket)
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
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"CClient() 보내려는 데이터가 0바이트");
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
	// 일단 릴리즈 안되게 참조카운트 올리기
	// . 플래그 확인을 위해 값 꺼내기
	// . 릴리즈 진행중이면 올리고 나감
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&_session.refcount);
	if ((retNum & SESSION_RELEASE_FLAG) == SESSION_RELEASE_FLAG)
	{
		//-----------------------------------------------
		// 안쓰는 패킷 해제
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		DecrementRefcount();
		return true;
	}

	//---------------------------------------------------
	// ** 여기서부터는 확실히 내꺼
	// . Disconnect는 올라가있을 수 있음
	// . 다만, 올라갔다고 해서 인큐가 문제되지 않음
	//   (해제 루틴을 타는 중이 아니기 때문에)
	//---------------------------------------------------
	if (_session.isDisconnect)
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

		DecrementRefcount();
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
		header.code = _clientCrypto.code;
		header.len = pPacket->GetDataSize() - Net::NET_HEADER_LEN;
		header.randkey = (uint8_t)(rand() % 256);
		header.checksum = Net::CCryptoUtils::GetCheckSum(pPacket);
		memcpy(pPacket->GetBufferPtr(), &header, sizeof(Net::stNetHeader));
		pPacket->SetEncoded();

		if (_option.bUseEncode)
		{
			Net::CCryptoUtils::Encode(pPacket, _clientCrypto);
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
	bool retEnq = _session.sendQ.Enqueue(pPacket);
	if (retEnq == false)
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"CClient 센드 버퍼가 다 찬 클라입니다");

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
		_InterlockedExchange(&_session.isDisconnect, 1);
		CancelIoEx((HANDLE)_session.sock, &_session.recvOl.ol);

		DecrementRefcount();
		return true;
	}

	GetClientMonitor()->IncreaseSendMessageCount();
	long isSending = _InterlockedExchange(&_session.isSending, 1);
	if (isSending == 1)
	{
		DecrementRefcount();
		return true;	// 못 보내니 나옴
	}

	//---------------------------------------------------
	// isSending이 0인경우임. 즉 내가 1로 만듦!
	//---------------------------------------------------
	if (_session.isDisconnect == 1)
	{
		_InterlockedExchange(&_session.isSending, 0);
		DecrementRefcount();
		return true;
	}

	PostQueuedCompletionStatus(_hIOCP, CLIENT_MSG_DELAYSEND, (ULONG_PTR)&_session, nullptr);
	return true;
}

void Net::CClient::ReleaseSession()
{
	if (_InterlockedCompareExchange(&_session.refcount, SESSION_RELEASE_FLAG, 0) != 0)
	{
		return;
	}
	uint64_t sessionId = _session.sessionId;
	_session.Clear();

	PostQueuedCompletionStatus(_hIOCP, CLIENT_MSG_RELEASE, (ULONG_PTR)sessionId, nullptr);
}

void Net::CClient::DecrementRefcount()
{
	unsigned long retNum = _InterlockedDecrement(&_session.refcount);
	if (retNum == 0)
	{
		ReleaseSession();
	}
	else
	{
		if (retNum == 0xFFFF'FFFF)
			__debugbreak();
	}
}

void Net::CClient::InitMonitoringJob()
{
	_monitoringJob = std::make_shared<CClientMonitoringJob>();
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_monitoringJob, 0);
}

void Net::CClient::ExitMonitoringJob()
{
	_monitoringJob->CancelJob();
}

void Net::CClient::SendPQCS()
{
	bool isFine = _session.SendPost();
	if (isFine == false)
	{
		//-----------------------------------------------
		// isSending은 0이 된 상태. 디스커넥트가 올라갔는지 
		// 확인이 필요하다.
		//-----------------------------------------------
		DecrementRefcount();
	}
	else
	{
		//-----------------------------------------------
		// 여기는 SendPost가 성공적으로 끝남.
		// 완료통지에서 참조카운트 해제
		//-----------------------------------------------
	}
}

bool Net::CClient::IOCP_RequestProc(DWORD message, void* key)
{
	switch (message)
	{
	case CLIENT_MSG_EXIT:
	{
		// 종료 시그널
		return false;
	}

	case CLIENT_MSG_RELEASE:
	{
		if(_isExit == 0)
			OnLeaveServer();
		break;
	}

	case CLIENT_MSG_DELAYSEND:
	{
		SendPQCS();
		break;
	}

	case CLIENT_MSG_CONNECT:
	{
		OnEnterJoinServer();
		break;
	}

	case 4:
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"WorkerThread 잘못된 시그널 (cb:%d key: %p)", message, key);
		__debugbreak();
	}

	case 5:
	{
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"WorkerThread 잘못된 시그널 (cb:%d key: %p)", message, key);
		__debugbreak();
	}

	default:
		Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_ERROR, L"WorkerThread 잘못된 시그널 (cb:%d key: %p)", message, key);
		__debugbreak();
	}

	return true;
}

void Net::CClient::IOCP_CbTransferred_Zero(Net::stNetSession* pSession, Net::stNetSession::myoverlapped* pOverlapped)
{
	if (pSession != &_session)
		__debugbreak();
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
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl.ol);

		_InterlockedExchange(&pSession->isSending, 0);
		//OnSend(pSession->sessionId, false);

		DecrementRefcount();
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
		CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl.ol);
		DecrementRefcount();
	}
}

//-------------------------------------------------------
// Info: 워커 스레드 function
//-------------------------------------------------------
unsigned int Net::CClient::NetClientWorkerProc(void* param)
{
	PRO_START();
	CClient* nowClient = (CClient*)param;
	HANDLE hcp = (HANDLE)nowClient->_hIOCP;
	DWORD tid = GetCurrentThreadId();
	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"Client WorkerThread Start!!");

	bool bNeedDecode = nowClient->_option.bUseEncode;
	stPacketCrypto& nowServerCrypto = nowClient->_clientCrypto;

	while (1)
	{
		DWORD cbTransferred = 0;
		stNetSession* pSession = nullptr;
		stNetSession::myoverlapped* pOverlapped = nullptr;

		GetQueuedCompletionStatus(hcp, &cbTransferred, (ULONG_PTR*)&pSession, (WSAOVERLAPPED**)&pOverlapped, INFINITE);

		if (pOverlapped == nullptr)
		{
			if (nowClient->IOCP_RequestProc(cbTransferred, pSession) == false)
				break;
		}
		else if (cbTransferred == 0)
		{
			nowClient->IOCP_CbTransferred_Zero(pSession, pOverlapped);
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
				CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl.ol);

				_InterlockedExchange(&pSession->isSending, 0);
				nowClient->DecrementRefcount();
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
						nowClient->DecrementRefcount();
					}
					else
					{
						bool isFine = pSession->SendPost();
						if (isFine == false)
						{
							//-----------------------------------------------
							// isSending은 0이 된 상태. 참조 카운트도 해제함
							//-----------------------------------------------
							nowClient->DecrementRefcount();
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
					nowClient->DecrementRefcount();
				}
			}
		}
		else // Recv
		{
			Core::Profile pf(L"GQCS: Recv");

			if (pSession->isDisconnect == 1)
			{
				nowClient->DecrementRefcount();
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
						Core::c_syslog::logging().LogHex(TAG_NET, Core::c_syslog::en_SYSTEM, pRead, payloadlen + sizeof(Net::stNetHeader),
							L"[sessionId: %016llx] Client가 받은 이상한 패킷 헤더", pSession->sessionId);

						_InterlockedExchange(&pSession->isDisconnect, 1);
						CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl.ol);
						nowClient->DecrementRefcount();

						canRecvPost = false;
						break;
					}

					//--------------------------------------------
					// 컨텐츠로 넘기기
					//--------------------------------------------
					recvQ->MoveReadPtr(sizeof(Net::stNetHeader));
					nowClient->GetClientMonitor()->IncreaseRecvMessageCount();
					nowClient->OnMessage(recvQ, payloadlen);
					recvQ->MoveReadPtr(payloadlen);
				}
				else
				{
					if (payloadlen > NET_RECVQ_SIZE)
					{
						Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"CClient [sessionId: %016llx] 패킷이 너무 길어요. 읽을 수가 없음 [header payloadlen: %d]", pSession->sessionId, payloadlen);

						_InterlockedExchange(&pSession->isDisconnect, 1);
						CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl.ol);
						nowClient->DecrementRefcount();

						canRecvPost = false;
					}
					else if (payloadlen - pSession->recvQ->GetDataSize() > pSession->recvQ->GetFreeSize())
					{
						//----------------------------------------------
						// 더 받아도 못읽음
						//----------------------------------------------
						needNewBuffer = true;
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
				nowClient->ReleaseSession();
			}
		}
	}

	PRO_EXIT();
	Core::c_syslog::logging().Log(TAG_NET, Core::c_syslog::en_SYSTEM, L"Client WorkerThread Exit!!");
	return 0;
}