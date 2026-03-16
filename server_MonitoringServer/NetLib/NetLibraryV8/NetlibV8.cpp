#include "NetlibV8.h"
#pragma comment(lib, "ws2_32")

#include <process.h>
#include <time.h>
#pragma comment(lib, "winmm")


#include "ProfilerV2.h"
c_syslog CServer::s_ServerSyslog;

/////////////////////////////////////////////////////////
// NetLibrary V8 cpp
/////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////
// 세션
/////////////////////////////////////////////////////////
#pragma region Session


//-------------------------------------------------------
// 스택에서 얻어온 index 바탕으로 소켓을 초기화
//-------------------------------------------------------
void CServer::Session::Init(SOCKET _sock, int index, SOCKADDR_IN* caddr, uint64_t sid)
{
	_InterlockedIncrement(&refcount);
	sock = _sock;
	sid = ((sid << 20) | index);
	sessionId = sid;
	InetNtopW(AF_INET, &caddr->sin_addr, ip, 16);
	port = ntohs(caddr->sin_port);
	isDisconnect = 0;
	recvQ = CPACKET_ALLOC();
	recvQ->SetRecvBuffer();
	//---------------------------------------------------
	// . 사용을 시작한다는 신호탄이자
	// . mfence의 역할
	// . 상위 1비트가 계속 1이면 다른곳에서는 나를 참조해도
	//   바로 참조를 풀음
	//---------------------------------------------------
	_InterlockedAnd((long*)&refcount, (~SERVER_RELEASE_FLAG));
}


//-------------------------------------------------------
// ReleaseSession에서만 호출!
// 소켓 초기화 후 스택에 반환해야 함, refcount == 0일때만!
//-------------------------------------------------------
void CServer::Session::Clear()
{
	//---------------------------------------------------
	// ** 소켓 닫고 초기화
	// . Release Flag를 올린 스레드만 접근할 수 있다.
	// . 다만 iocount는 어디서든 올리고 내릴 수 있으므로
	// 초기화 하지 않는다.
	//---------------------------------------------------
	int retClose = closesocket(sock);
	if (retClose)
	{
		DWORD closeErr = GetLastError();
		s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"[sessionId: %016llx] 소켓 종료 오류(소켓 핸들: %016llx)", sessionId, sock);
	}
	sock = INVALID_SOCKET;
	sessionId = 0; 
	CPACKET_FREE(recvQ);
	//---------------------------------------------------
	// 센드큐는 직접 비우자..
	//---------------------------------------------------
	CPacket* freePacket;
	while (sendQ.isEmpty() == false)
	{
		if (sendQ.Dequeue(freePacket))
		{
			int retFree = CPACKET_FREE(freePacket);
			if (retFree)
			{
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
				__debugbreak();
			}
		}
	}

	//---------------------------------------------------
	// 센드 안한거 남았으면 정리
	//---------------------------------------------------
	for (int i = 0; i < sendPacketsCnt; i++)
	{
		int retFree = CPACKET_FREE(sendPackets[i]);
		if (retFree)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
	}
	sendPacketsCnt = 0;
	memset(&recvOl, 0, sizeof(recvOl));
	memset(&sendOl, 0, sizeof(sendOl));
	recvOl.isSend = false;
	sendOl.isSend = true;
	isSending = 0;
	memset(ip, 0, sizeof(ip));
	port = 0;
	//---------------------------------------------------
	// . 여기는 mfence가 필요없다.
	// . 어짜피 Release Flag가 올라가있는 상태라 다른곳에서
	// 참조하더라도 아무것도 못한다
	//---------------------------------------------------
}

//-------------------------------------------------------
// refcount == 0이면 false반환
// . 얘는 세션이 생기면 올라가는 refcount를 내리는 역할이라서
// . 따로 참조하지 않고 들어가도되고 (always 참조상태)
// . 이 함수 내부에서 내리는 작업을 함
// 
// . 자신의 IO요청은 자기가 직접 필요시 해제
//-------------------------------------------------------
bool CServer::Session::RecvPost()
{
	//---------------------------------------------------
	// 리시브는 일단 무조건! 항상 refcount가 올라간 상태
	// (처음 세션 생성할때 올려서 생성함)
	// 따라서 따로 검사하지 않음.
	// 
	// Recv에 대한 refcount를 내리는 주체도 RecvPost이므로
	// 걱정할 것이 없다. 
	// (만약 Recv완료통지에서 내렸으면 RecvPost로 들어오지 않는다)
	//---------------------------------------------------
	if (isDisconnect == 1)
	{
		unsigned long retNum = _InterlockedDecrement(&refcount);
		if (retNum == 0)
		{
			return false;
		}
		else
		{
			// TODO:
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
			return true;
		}
	}

	// 얼마나 받을지 모르니 포인터 이동은 받고 나서 하는 것으로 하자
	WSABUF wsabufs[1];
	wsabufs[0].buf = recvQ->GetWritePtr();
	//wsabufs[0].len = (int)(recvQ->m_chrbuffer + SERVER_RECVQ_SIZE - recvQ->m_writeptr);
	wsabufs[0].len = recvQ->GetFreeSize();

	DWORD flags = 0;
	uint64_t curSessionId = sessionId;
	int retRecv = WSARecv(sock, wsabufs, 1, NULL, &flags, (WSAOVERLAPPED*)&recvOl, NULL);
	if (retRecv == SOCKET_ERROR)
	{
		DWORD err = GetLastError();
		if (err != WSA_IO_PENDING)
		{	// IO 시도조차 무시된 경우
			_InterlockedExchange(&isDisconnect, 1);

			//----------------------------------------------
			// ** 중요한것을 빼먹음 **...
			//----------------------------------------------
			CancelIoEx((HANDLE)sock, &sendOl.ol);

			unsigned long retNum = _InterlockedDecrement(&refcount);
			if (retNum == 0)
			{
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] Recv실패, refcount == 0, false반환", sessionId);
				return false;
			}
			else
			{
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] Recv실패, refcount == 0 대기 (%d)", sessionId, retNum);
				if (retNum == 0xFFFF'FFFF)
					__debugbreak();
				return true;
			}
		}
		else
		{
			// Recv Pending
		}
	}
	else
	{
		// Recv 버퍼에 남은거 긁어옴
	}
	//---------------------------------------------------
	// 수신 종료 (Disconnect라면)
	// 
	// ** 중요 **
	// 여기부터는 이미 내 세션이 해제 되어있을 수 있음
	// (완료 통지 이후 refcount감소로)
	// 이제 여기서 다시 세션 검사가 필요
	// 그냥 검사하고 하면 안됨. 참조카운트 올리고 해야함
	//---------------------------------------------------
	if (isDisconnect == 1)
	{
		uint64_t retNum = _InterlockedIncrement(&refcount);
		if ((refcount & SERVER_RELEASE_FLAG) == SERVER_RELEASE_FLAG)
		{
			//-----------------------------------------------
			// 리시브는 내가 직접 내리고, 결과만 알림
			// 여기에서 ReleaseSession 호출이 안됨
			//-----------------------------------------------
			retNum = _InterlockedDecrement(&refcount);
			if (retNum == 0)
			{
				//---------------------------------------
				// 외부에 Release 요청
				//---------------------------------------
				return false;
			}
			else
			{
				if (retNum == 0xFFFF'FFFF)
					__debugbreak();
			}
			return true;
		}
		//-----------------------------------------------
		// 릴리즈 중이 아니였다면, 내가 내 세션 획득
		//-----------------------------------------------
		if (sessionId != curSessionId)
		{
			//-------------------------------------------
			// 세션이 바뀌었으니 외부에 _InterDec 요청
			//-------------------------------------------
			retNum = _InterlockedDecrement(&refcount);
			if (retNum == 0)
			{
				//---------------------------------------
				// 외부에 Release 요청
				//---------------------------------------
				return false;
			}
			else
			{
				if (retNum == 0xFFFF'FFFF)
					__debugbreak();
			}
			return true;
		}
		//-----------------------------------------------
		// 성공하지 않으면 이미 완료통지가 처리중이라는 뜻
		// 왜냐하면 내가 보낸다음 릴리즈를 막았고,
		// 내가 직접 내가보낸것을 취소하는 처리를 하기 때문
		//-----------------------------------------------
		CancelIoEx((HANDLE)sock, &recvOl.ol);
		//-----------------------------------------------
		// 일단 내부에서 올린 인터락 해제 해줄 주체가 필요
		//-----------------------------------------------
		retNum = _InterlockedDecrement(&refcount);
		if (retNum == 0)
		{
			//---------------------------------------
			// 외부에 Release 요청
			//---------------------------------------
			return false;
		}
		else
		{
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
		return true;
	}

	return true;
}

//-------------------------------------------------------
// . false: refcount를 내려야되면(완료 통지가 안와서)
//                               + 내부에서 올려서 내려야함
// . true: IOCP 완료통지에서 내려야한다!
// 
// . 자신의 IO요청은 자기가 직접 필요시 해제
// 
// . 반드시 외부에서 참조카운트가 올라간 상태로 진입
// . 반드시 isSending = 1로 만들었을 경우만 진입
// . => 반드시 외부에서 isDisconnect == 0 확인하고 진입해라
// . false반환시 disconnect플래그 필요하면 올려둠
//-------------------------------------------------------
bool CServer::Session::SendPost()
{
	WSABUF wsabufs[SERVER_SEND_WSABUF_MAX];
	//--------------------------------------------------
	// 빈 큐인지 검사
	//--------------------------------------------------
	if (sendQ.isEmpty())
	{
		//-----------------------------------------------
		// 보낼게 없음
		// ** 바로 리턴하면 이 사이에 뭔가 들어왔다가 리턴햇을 수 있음
		//    => 그렇게 되면 아얘 Send를 못하는 현상 발생(여기서도리턴)
		// ** 따라서 0으로 바꾼 후 한번 더 확인 필요
		// 
		// 왜 가능하낙?
		// . 만약 큐의 ABA문제 인큐 발생시 최소 사이즈는 0 -> 1
		// . 디큐에서 테일이 이동하면서 2가 됨
		// . 따라서 문제 없음
		//-----------------------------------------------
		_InterlockedExchange(&isSending, 0);
		if (sendQ.isEmpty())
		{
			//----------------------------------------
			// 이건 진짜 없어서 나감
			// WSASend안하니까 false반환
			//----------------------------------------
			//return 1;
			return false;
		}
		else
		{
			long trySend = _InterlockedExchange(&isSending, 1);
			if (trySend == 0)
			{
				//----------------------------------------
				// 내가 획득함
				//----------------------------------------
				if (isDisconnect == 1)
				{
					//------------------------------------
					// 끊어야 되는애라 보낼 이유 X
					//------------------------------------
					_InterlockedExchange(&isSending, 0);
					//return 2;
					return false;
				}
				else
				{
					//------------------------------------
					// 여기를 나가서  아래 센드를 하러감
					//------------------------------------
				}
			}
			else
			{
				//----------------------------------------
				// 이건 다른애가 보내고있는거라 나감
				// WSASend안하니까 false 반환
				//----------------------------------------
				//return 1;
				return false;
			}
		}
	}

TRY_SEND:
	int useSize = 0;
	int i = 0;
	CPacket* pPacket = 0;
	//---------------------------------------------------
	// 일단 꺼내보고
	//---------------------------------------------------
	while (i < SERVER_SEND_WSABUF_MAX)
	{
		bool ret = sendQ.Dequeue(pPacket);
		if (ret)
		{
			CPACKET_UPDATE_TRACE(pPacket);
			wsabufs[i].buf = pPacket->m_chrbuffer;
			wsabufs[i].len = pPacket->GetDataSize();
			useSize += wsabufs[i].len;
			sendPackets[i] = pPacket;
			i++;
		}
		else
		{
			break;
		}
	}
	//---------------------------------------------------
	// 없으면 리턴
	// i == 0을 못피하는이유
	// 첫 Empty == false,
	// 두번째 Empty ->이미 누가 너헝서 true
	// 여기서 자고 온 사이 누가 다 보냈음.
	// 깨어나서 재획득 -> Empty
	//---------------------------------------------------
	if (i == 0)
	{
		//-----------------------------------------------
		// 보낼게 없음
		//-----------------------------------------------
		_InterlockedExchange(&isSending, 0);
		if (sendQ.isEmpty())
		{
			//------------------------------------------
			// 진짜 빈거, 외부에서 참조카운트 내려야함
			//------------------------------------------
			return false;
		}
		else
		{
			long trySend = _InterlockedExchange(&isSending, 1);
			if (trySend == 0)
			{
				//----------------------------------------
				// 내가 획득함
				//----------------------------------------
				if (isDisconnect == 1)
				{
					//------------------------------------
					// 끊어야 되는애라 보낼 이유 X
					//------------------------------------
					_InterlockedExchange(&isSending, 0);
					return false;
				}
				else
				{
					//------------------------------------
					// 여기를 나가서 다시 위로
					//------------------------------------
					goto TRY_SEND;
				}
			}
			else
			{
				//------------------------------------------
				// 다른애가 보내고있는거, 
				// 외부에서 참조카운트 내려야함
				//------------------------------------------
				// return 1;
				return false;
			}
		}
	}

	sendOl.sendbyte = useSize;
	sendPacketsCnt = i;
	//---------------------------------------------------
	// . 여기서 올리면 안됨. 안그러면 SendPost 시작 ~ 여기
	// 까지 refcount == 0일 수 있는 상태가 됨.
	// refcount == 0인상태로 다른 sendQ를 꺼낼 수 있음
	// . 즉 패킷이 섞일 수 있음!! (이미끊긴애 + 이제 보내야하는애)
	// => 사실 초기화 할 때 i도 초기화를 시키지만, 이렇게되면
	// sendPacketsCnt가 원자적이지 않아짐 (수정될 수 있음)
	// => 보내면 안되는것을 보내고, sendBytes도 이상해짐
	// refcount는 여기서는 관리하면 안되는게 맞네...
	//---------------------------------------------------
	uint64_t curSessionId = sessionId;	// WSASend보낸게 누구인지 미리 등록 (재사용 발생하면 다를 수 있음)

	PRO_BEGIN(L"WSA_SEND");
	int retSend = WSASend(sock, wsabufs, i, NULL, 0, (WSAOVERLAPPED*)&sendOl, NULL);
	if (retSend == SOCKET_ERROR)
	{
		DWORD err = GetLastError();
		PRO_END(L"WSA_SEND");
		if (err != WSA_IO_PENDING)
		{
			//-------------------------------------------
			// 끊긴 연결이니 외부에서 참조카운트 감소해서
			// 0이라면 해제하면된다!
			// . LogEx는 특수한 연결종료일 경우 로그 남겨줌
			//-------------------------------------------
			s_ServerSyslog.LogEx(TAG_NET, err, c_syslog::en_SYSTEM, L"[sessionId: %016llx] WSASend실패, 연결 종료된 세션", sessionId);

			//-------------------------------------------
			// 내가 보내려고 설정한거도 해제, 
			// 완료통지가 안오기 때문
			//-------------------------------------------
			for (int cnt = 0; cnt < sendPacketsCnt; cnt++)
			{
				int retFree = CPACKET_FREE(sendPackets[cnt]);
				if (retFree)
				{
					s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
					__debugbreak();
				}
			}
			sendPacketsCnt = 0;
			sendOl.sendbyte = 0;

			_InterlockedExchange(&isDisconnect, 1);

			//----------------------------------------------
			// ** 중요한것을 빼먹음 **...
			// . 이게 없으면 리시브는 여원히 걸릴 수 있음
			//----------------------------------------------
			CancelIoEx((HANDLE)sock, &recvOl.ol);

			_InterlockedExchange(&isSending, 0);
			
			return false;
		}
		else
		{
			// Send Pending
		}
	}
	else
	{
		// Send 송신 버퍼에다가
		PRO_END(L"WSA_SEND");
	}

	//---------------------------------------------------
	// 송신 종료 (Disconnect라면)
	// 
	// ** 중요 **
	// 여기부터는 이미 내 세션이 해제 되어있을 수 있음
	// (완료 통지 이후 refcount감소로)
	// 이제 여기서 다시 세션 검사가 필요
	// 그냥 검사하고 하면 안됨. 참조카운트 올리고 해야함
	//---------------------------------------------------
	if (isDisconnect == 1)
	{
		uint64_t retNum = _InterlockedIncrement(&refcount);
		if ((refcount & SERVER_RELEASE_FLAG) == SERVER_RELEASE_FLAG)
		{
			//-----------------------------------------------
			// 외부에서 _InterlockedDecremnet유도
			// 여기에서 ReleaseSession 호출이 안됨
			//-----------------------------------------------
			
			return false;
		}
		//-----------------------------------------------
		// 릴리즈 중이 아니였다면, 내가 내 세션 획득
		//-----------------------------------------------
		if (sessionId != curSessionId)
		{
			//-------------------------------------------
			// 세션이 바뀌었으니 외부에 _InterDec 요청
			//-------------------------------------------
			
			return false;
		}
		//-----------------------------------------------
		// 성공하지 않으면 이미 완료통지가 처리중이라는 뜻
		//-----------------------------------------------
		CancelIoEx((HANDLE)sock, &sendOl.ol);
		//-----------------------------------------------
		// 일단 내부에서 올린 인터락 해제 해줄 주체가 필요
		//-----------------------------------------------
		
		return false;
	}

	
	return true;
}


#pragma endregion


/////////////////////////////////////////////////////////
// 세션 관리자(배열로 세션을 관리)
/////////////////////////////////////////////////////////
#pragma region SessionStructure


//-------------------------------------------------------
// 세션 구조 초기화, maxCnt: 배열 확보
//-------------------------------------------------------
bool CServer::SessionStructure::Init(int maxCnt)
{
	_maxSessionCnt = maxCnt;
	_sessionsArray = new Session[maxCnt];
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
CServer::Session* CServer::SessionStructure::AcquireSession(int* index)
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
bool CServer::SessionStructure::ReleaseSession(Session* pSession)
{
	int index = (int)(pSession - _sessionsArray);
	if (index < 0 || index >= _maxSessionCnt)
		return false;
	_indexStack.push(index);
	_InterlockedDecrement(&_sessionCnt);
	return true;
}


#pragma endregion


/////////////////////////////////////////////////////////
// CServer V3
/////////////////////////////////////////////////////////
#pragma region CLanServer_Functions


//-------------------------------------------------------
// Info: 서버 세팅 및 시작 준비
// Param: stServerOpt* 옵션 (복사됨)
// Return: 실패시 false (종료해야함), 성공시 true
//-------------------------------------------------------
bool CServer::Init(const stServerOpt* pOpt)
{
	long retIsInit = _InterlockedExchange(&_isCallInit, 1);
	if (retIsInit == 1)
		return true;	// 이미 해서 안해도 됨


	bool retSetDirectory = s_ServerSyslog.SetDirectoryEx(L"Log_NetLib\\");
	if (retSetDirectory == false)
	{
		_InterlockedExchange(&_isCallInit, 0);
		__debugbreak();
		return false;
	}


	// Session 관리객체 미리 생성
	bool retInitSessionStructure = _sessionStructure.Init(pOpt->iMaxConcurrentUsers);
	if (retInitSessionStructure == false)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"CServer::Init() 세션 준비 실패");
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}

	_option = *pOpt;
	
s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"CServer::Init()!! [최대 접속자수:%d, 오픈IP: %s, 포트: %d, 워커스레드: [%d / %d], 노딜레이: %d, SNDBUF=0: %d]",
		_option.iMaxConcurrentUsers, _option.openIP, _option.port, _option.iWorkerThreadRunCnt, _option.iWorkerThreadCreateCnt, _option.bUseTCP_NODELAY, _option.bUseSO_SNDBUF);
	// WSASTART
	WSADATA wsa;
	int retWSAStart = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (retWSAStart)
	{
		s_ServerSyslog.LogEx(TAG_NET, retWSAStart, c_syslog::en_ERROR, L"WSAStartup() failed");
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}

	// MAKE IOCP
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, _option.iWorkerThreadRunCnt);
	if (_hIOCP == NULL)
	{
		DWORD dwCreateIOCPErr = GetLastError();
		s_ServerSyslog.LogEx(TAG_NET, dwCreateIOCPErr, c_syslog::en_ERROR, L"CreateIOCP failed");
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}

	// MAKE hEventFor AcceptThread (중지, 사용)
	_hEventForAccept = (HANDLE)CreateEventW(NULL, TRUE, FALSE, NULL);
	if (_hEventForAccept == NULL)
	{
		DWORD gle = GetLastError();
		s_ServerSyslog.LogEx(TAG_NET, gle, c_syslog::en_ERROR, L"Make _hEventForAccept Failed");
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}

	// MAKE hEventForExit (종료)
	_hEventForExit = (HANDLE)CreateEventW(NULL, TRUE, FALSE, NULL);
	if (_hEventForExit == NULL)
	{
		DWORD gle = GetLastError();
		s_ServerSyslog.LogEx(TAG_NET, gle, c_syslog::en_ERROR, L"Make _hEventForExit Failed");
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}

	//----------------------------------------------------------------
	// 얘가 여기로 와야 OnWorkerStart(), End()가 가능함
	//----------------------------------------------------------------
	if (OnInit(pOpt) == false)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[errno: %d] Contents Init Failed");
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}

	int createCnt = _option.iWorkerThreadCreateCnt;
	for (int i = 0; i < createCnt; i++)
	{
		_hThreads[i] = (HANDLE)_beginthreadex(NULL, NULL, WorkerThreadFunc, this, 0, NULL);
		if (_hThreads[i] == 0)
		{
			int retCreateWorkerThreadErr = errno;
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[errno: %d] Create WorkerThread failed", retCreateWorkerThreadErr);
			clear();
			_InterlockedExchange(&_isCallInit, 0);
			return false;
		}
		_threadNum++;
	}


	// MAKE AcceptThread
	_hThreads[_threadNum] = (HANDLE)_beginthreadex(NULL, NULL, AcceptThreadFunc, this, 0, NULL);
	if (_hThreads[_threadNum] == 0)
	{
		int retCreateAcceptThreadErr = errno;
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[errno: %d] Create AcceptThread failed", retCreateAcceptThreadErr);
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}
	_threadNum++;

	// MAKE Monitor Thread
	_hThreads[_threadNum] = (HANDLE)_beginthreadex(NULL, NULL, MonitorThreadFunc, this, 0, NULL);
	if (_hThreads[_threadNum] == 0)
	{
		int retCreateMonitorThreadErr = errno;
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[errno: %d] Create MonitorThread failed", retCreateMonitorThreadErr);
		clear();
		_InterlockedExchange(&_isCallInit, 0);
		return false;
	}
	_threadNum++;

	return true;
}

//-------------------------------------------------------
// Info: 서버를 시작 (Listen)
// Param: -
// Return: 실패시 false (종료해야함), 성공시 true
//-------------------------------------------------------
bool CServer::Start()
{
	_InterlockedExchange(&_isStop, 0);
	if (_isCallInit == 0)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"CServer::Start() 실패, Init()을 호출하지 않음");
		clear();
		return false;	// 아직 Init안됨
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
		s_ServerSyslog.LogEx(TAG_NET, retMakeLSockErr, c_syslog::en_ERROR, L"리슨소켓, socket() failed ");
		clear();
		return false;
	}
	int retBind = bind(_lsock, (SOCKADDR*)&saddr, sizeof(saddr));
	if (retBind)
	{
		DWORD retLsockBindErr = GetLastError();
		s_ServerSyslog.LogEx(TAG_NET, retLsockBindErr, c_syslog::en_ERROR, L"리슨소켓, bind() failed");
		clear();
		return false;
	}

	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"CServer::Start()!!");
	int retListen = listen(_lsock, SOMAXCONN_HINT(5000));
	if (retListen == SOCKET_ERROR)
	{
		DWORD listenError = GetLastError();
		s_ServerSyslog.LogEx(TAG_NET, listenError, c_syslog::en_ERROR, L"CServer::Start() - listen(), Listen 에러");
		clear();
		return false;
	}
	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"Listening...");

	SetEvent(_hEventForAccept);
	return true;
}

//-------------------------------------------------------
// Info: 서버를 중지 (옵션 1: 종료, 0: 중지만)
// Param: -
// Return: -
//-------------------------------------------------------
void CServer::Stop(int opt = 0)
{
	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"CServer::Stop()!!");
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
		s_ServerSyslog.LogEx(TAG_NET, gle, c_syslog::en_ERROR, L"리슨 소켓 종료 오류");
		// 종료는 된것
	}

	//---------------------------------------------------
	// 세션 정리
	//---------------------------------------------------
	int maxUser = _option.iMaxConcurrentUsers;
	for (int i = 0; i < maxUser; i++)
	{
		Session* pSession = &_sessionStructure._sessionsArray[i];
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
	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"패킷 트레이스 시작");
	retTraceResult = CPacket::m_tracePacket.CheckTrace(&s_ServerSyslog);
	if(retTraceResult == false)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 트레이스 검사로 문제가 발견됨");
	}
	else
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"모든 패킷이 반환됨");
	}

	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"패킷 트레이스 종료");
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
			__debugbreak();
		OnExit();
	}
}

//-------------------------------------------------------
// Info: 연결 종료 요청
// Param: uint64_t sessionId
// Return: 성공: true, 실패: false
// 실패사유: 비정상 세션아이디 (서버 종료사유)
//-------------------------------------------------------
bool CServer::Disconnect(uint64_t sessionId)
{
	Session* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] Disconnect() 비정상적인 세션 아이디(out of index)", sessionId);
		return false;
	}
	//---------------------------------------------------
	// * 참조 카운트를 올려서 릴리즈 진입 못하도록
	// * 릴리즈 진행중인지 확인하기
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SERVER_RELEASE_FLAG) == SERVER_RELEASE_FLAG)
	{
		//-----------------------------------------------
		// 자신의 참조는 자신이 제거하기
		// . 새로 생성중이더라도 직접 참조를 올려 0될 수 없음
		// ** 아니다. 여기서 0이 될 수 있다.
		// ** 비교 후 자고나서 봤는데 0일 수 있음
		//-----------------------------------------------
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
		CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl.ol);
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl.ol);
	}
	//---------------------------------------------------
	// 올렸던 참조 내려주고 refcount == 0 이면 릴리즈 시켜줌
	// 내가 올렸으므로 릴리즈가 안됬을 수 있기 때문
	// 
	// . 내꺼든 아니든 0이되면 해제 해줘야함
	//---------------------------------------------------

	retNum = _InterlockedDecrement(&pSession->refcount);
	if (retNum == 0)
	{
		ReleaseSession(pSession);
	}
	else
	{
		if (retNum == 0xFFFF'FFFF)
			__debugbreak();
	}

	return true;
}

//-------------------------------------------------------
// Info: 연결 종료 요청
// Param: uint64_t sessionId
// Return: 성공: true, 실패: false
// 실패사유: 비정상 세션아이디 (서버 종료사유)
//-------------------------------------------------------
bool CServer::isDisconnect(uint64_t sessionId)
{
	Session* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] Disconnect() 비정상적인 세션 아이디(out of index)", sessionId);
		return true;
	}
	//---------------------------------------------------
	// * 참조 카운트를 올려서 릴리즈 진입 못하도록
	// * 릴리즈 진행중인지 확인하기
	// 
	// . 릴리즈 진행중이면 
	// ** 이미 내세션이였든, 다른 놈 세션이든 해제 된건 마찬가지
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SERVER_RELEASE_FLAG) == SERVER_RELEASE_FLAG)
	{
		//-----------------------------------------------
		// 자신의 참조는 자신이 제거하기
		// . 새로 생성중이더라도 직접 참조를 올려 0될 수 없음
		// ** 아니다. 여기서 0이 될 수 있다.
		// ** 비교 후 자고나서 봤는데 0일 수 있음
		//-----------------------------------------------
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
		return true;
	}

	//---------------------------------------------------
	// 내거인지 확인
	//---------------------------------------------------
	bool ret = false;
	if (pSession->sessionId == sessionId)
	{
		if (pSession->isDisconnect == 1)
			ret = true;
	}
	//---------------------------------------------------
	// 올렸던 참조 내려주고 refcount == 0 이면 릴리즈 시켜줌
	// 내가 올렸으므로 릴리즈가 안됬을 수 있기 때문
	// 
	// . 내꺼든 아니든 0이되면 해제 해줘야함
	//---------------------------------------------------

	retNum = _InterlockedDecrement(&pSession->refcount);
	if (retNum == 0)
	{
		ReleaseSession(pSession);
	}
	else
	{
		if (retNum == 0xFFFF'FFFF)
			__debugbreak();
	}

	return ret;
}

//-------------------------------------------------------
// Info: 센드 요청
// Param: uint64_t sessionId
// Return: 성공: 0,
//         실패: -1 (잘못된 세션 id), 1 (Disconnect가능, SendPost실패)
// 실패사유: 잘못된 sessionId, 컨텐츠의 잘못
//-------------------------------------------------------
bool CServer::SendPacket(uint64_t sessionId, CPacket* pPacket)
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
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] 보내려는 데이터가 0바이트", sessionId);
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		
		return true;
	}

	//---------------------------------------------------
	// 서버 종료급 이벤트. 세션아이디가 비정상
	//---------------------------------------------------
	Session* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] SendPacket() 인덱스 유효하지 않음", sessionId);
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		
		return false;
	}

	//---------------------------------------------------
	// 일단 릴리즈 안되게 참조카운트 올리기
	// . 플래그 확인을 위해 값 꺼내기
	// . 릴리즈 진행중이면 올리고 나감
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SERVER_RELEASE_FLAG) == SERVER_RELEASE_FLAG)
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
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}

		retNum = _InterlockedDecrement(&pSession->refcount);
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
		
		return true;
	}
	//---------------------------------------------------
	// 이제 내가 사용권한 획득, 세션 변화 없음
	// . 세션 아이디가 확실함
	//---------------------------------------------------
	if (pSession->sessionId != sessionId)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] SendPacket() 시도, 재사용된 세션, 내것아님", sessionId);
		//-----------------------------------------------
		// 안쓰는 패킷 해제
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}
		retNum = _InterlockedDecrement(&pSession->refcount);
		//-----------------------------------------------
		// 나때문에 릴리즈 못했으면 해줘야지
		//-----------------------------------------------
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
		
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
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}

		retNum = _InterlockedDecrement(&pSession->refcount);
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
		
		return true;
	}

	
	
	//---------------------------------------------------
	// < V4 >
	// . 인코딩이 필요하면 해주자
	//---------------------------------------------------
	if (pPacket->m_isEncoded == 0)
	{
		//---------------------------------------------------
		// < V3 >
		// . 인큐 하기 직전에 헤더를 세팅해주자
		//---------------------------------------------------
		stHeader header;
		header.code = stHeader::SERVER_CODE;
		header.len = pPacket->GetDataSize() - CPACKET_HEADER_LEN;
		header.randkey = (uint8_t)(rand() % 256);
		header.checksum = GetCheckSum(pPacket);
		memcpy(pPacket->GetBufferPtr(), &header, sizeof(stHeader));
		pPacket->m_isEncoded = 1;
		if (_option.bUseEncode)
		{
			Encode(pPacket);
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
	bool retEnq = pSession->sendQ.Enqueue(pPacket);
	if (retEnq == false)
	{	// 센드버퍼가 다 찼다는 것은, 상대가 recv를 안하는 중. -> send io가 해제되면 자연스레 0?
		// 근데 recv가 1 걸려있는데 ...  흐으으음
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] 센드 버퍼가 다 찬 클라입니다. iocount == 0이 되면 해제합니다.", sessionId);

		//-----------------------------------------------
		// 인큐 실패했으니 해제까지
		//-----------------------------------------------
		int retFree = CPACKET_FREE(pPacket);
		if (retFree)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
			__debugbreak();
		}

		// 디스커넥트 플래그 활성화
		_InterlockedExchange(&pSession->isDisconnect, 1);

		//----------------------------------------------
		// ** 중요한것을 빼먹음 **...
		// . 이게 없으면 리시브는 여원히 걸릴 수 있음
		//----------------------------------------------
		CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl.ol);

		retNum = _InterlockedDecrement(&pSession->refcount);
		//-----------------------------------------------
		// 내가 참조해서 릴리즈를 못했다면 바로 해제까지
		//-----------------------------------------------
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			// 이 공간은 이미 끊긴 연결
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}

		// return 1;
		return true;
	}


	//----------------------------------------------------
	// 인큐에 성공함
	// . 보낼지 안보낼지는 모른다! (디스커넥트 상태에 따라)
	//----------------------------------------------------
	_InterlockedIncrement(&_monitor.SendMessageTPS);

	long isSending = _InterlockedExchange(&pSession->isSending, 1);
	if (isSending == 1)
	{
		retNum = _InterlockedDecrement(&pSession->refcount);
		//-----------------------------------------------
		// 내가 참조해서 릴리즈를 못했다면 바로 해제까지
		//-----------------------------------------------
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
		// return 0;	// 못 보내니 나옴
		return true;	// 못 보내니 나옴
	}

	//---------------------------------------------------
	// isSending이 0인경우임. 즉 내가 1로 만듦!
	//---------------------------------------------------
	if (pSession->isDisconnect == 1)
	{
		_InterlockedExchange(&pSession->isSending, 0);
		unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			// 리시브 취소를 해야되는지 이건 고민되네..
			// WSASend가 실패했거나, 정상
			// TODO: 여기는 추가적인 행동이 필요한지 생각해보기
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
		// return 1;
		return true;
	}

	PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_DELAYSEND, (ULONG_PTR)pSession, nullptr);

	// if (isFine == 2)
	// 	return 1;
	// return 0;
	return true;
}

//-------------------------------------------------------
// Info: refcount == 0일 때 세션 반환
// 내부에서 세션 Clear 후 스택에 반환, OnRelease() 발동 
//-------------------------------------------------------
void CServer::ReleaseSession(Session* pSession)
{
	if (_InterlockedCompareExchange(&pSession->refcount, SERVER_RELEASE_FLAG, 0) != 0)
	{
		return;
	}
	uint64_t sessionId = pSession->sessionId;
	// 세션 초기화 후 반환
	s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] refcount == 0이되어 ReleaseUser() 합니다", sessionId);
	// OnRelease(sessionId);
	pSession->Clear();
	bool isOk = _sessionStructure.ReleaseSession(pSession);
	if (isOk == false)
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] 인덱스가 이상합니다", sessionId);

	//---------------------------------------------------
	// OnRelease가 이벤트로써 발동되게 iocp큐에 넣자.
	// ** sessionId는 0이 나올 수 없는 설계를 위해
	//    인덱스 0번을 사용하지 않게 하자
	//---------------------------------------------------
	BOOL ret = PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_RELEASE, (ULONG_PTR)sessionId, nullptr);
	if (ret == FALSE)
	{
		CServer::s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR,
			L"[%u] ReleaseSession의 PQCS 실패.... ", GetLastError());
	}
}

//-------------------------------------------------------
// Info: Accept 에서 세션 생성, 진입 전 MaxUser고려하세요
//-------------------------------------------------------
CServer::Session* CServer::InitNewSession(SOCKET newSocket, SOCKADDR_IN* caddr)
{
	int index = -1;
	Session* pNewSession = _sessionStructure.AcquireSession(&index);
	if (pNewSession == nullptr)
	{
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"세션이 부족한데 이상하네(GetSession() 실패)");
		return nullptr;
	}

	pNewSession->Init(newSocket, index, caddr, _InterlockedIncrement(&_sid));
	return pNewSession;
}

//-------------------------------------------------------
// Info: 소켓, 핸들 등의 자원 정리 (초기화) - Init에서
//-------------------------------------------------------
void CServer::clear()
{
	if (_lsock != INVALID_SOCKET)
	{
		int retClose = closesocket(_lsock);
		if (retClose == SOCKET_ERROR)
		{
			s_ServerSyslog.LogEx(TAG_NET, GetLastError(), c_syslog::en_ERROR, L"Netlib::clear() = 리슨소켓 close 오류");
		}
		_lsock = INVALID_SOCKET;
	}

	if (_hIOCP != 0)
	{
		BOOL retCloseIOCP = CloseHandle(_hIOCP);
		if (retCloseIOCP == FALSE)
		{
			s_ServerSyslog.LogEx(TAG_NET, GetLastError(), c_syslog::en_ERROR, L"Netlib::clear() = IOCP close 오류");
		}
		_hIOCP = NULL;
	}

	for (int i = 0; i < SERVER_MAX_THREADS_CNT; i++)
	{
		if (_hThreads[i] != 0)
		{
			BOOL retCloseThreads = CloseHandle(_hThreads[i]);
			if (retCloseThreads == FALSE)
			{
				s_ServerSyslog.LogEx(TAG_NET, GetLastError(), c_syslog::en_ERROR, L"Netlib::clear() = 스레드 close 오류");
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
}

//--------------------------------------------------------
// 체크썸을 구합니다.
//--------------------------------------------------------
unsigned char CServer::GetCheckSum(CPacket* pPacket)
{
	unsigned char* pCur = (unsigned char*)pPacket->m_chrbuffer + CPACKET_HEADER_LEN;
	unsigned int sum = 0;
	unsigned char* pEnd = (unsigned char*)pPacket->m_writeptr;
	while (pCur != pEnd)
	{
		sum += (*pCur);
		pCur++;
	}
	return (unsigned char)(sum % 256);
}

//--------------------------------------------------------
// 체크썸을 구합니다.
//--------------------------------------------------------
unsigned char CServer::GetCheckSum(unsigned char* pRead, int payloadLen)
{
	unsigned char* pCur = pRead + CPACKET_HEADER_LEN;
	unsigned int sum = 0;
	unsigned char* pEnd = pCur + payloadLen;
	while (pCur != pEnd)
	{
		sum += (*pCur);
		pCur++;
	}
	return (unsigned char)(sum % 256);
}

//--------------------------------------------------------
// 패킷을 인코드합니다.
//--------------------------------------------------------
void CServer::Encode(CPacket* pPacket)
{
	//-----------------------------------------
	// 체크섬 부터 인코드 시작
	//-----------------------------------------
	unsigned char randKey = *(unsigned char*)(pPacket->m_chrbuffer + 3);
	unsigned char* pEncryptStart = (unsigned char*)pPacket->m_chrbuffer + 4;
	unsigned char* pEncryptEnd = (unsigned char*)pPacket->GetWritePtr();

	unsigned char i = 1;
	unsigned char Pn = 0;
	unsigned char En = 0;
	while (pEncryptStart != pEncryptEnd)
	{
		Pn = (*pEncryptStart) ^ (Pn + randKey + i);

		En = (Pn) ^ (En + stHeader::SERVER_STATIC_KEY + i);

		*pEncryptStart = En;

		i++;
		pEncryptStart++;
	}
}


//--------------------------------------------------------
// 패킷을 디코드합니다.
//--------------------------------------------------------
void CServer::Decode(unsigned char* pRead, int payloadLen)
{
	unsigned char randKey = *(pRead + 3);
	unsigned char checkSum = *(pRead + 4);
	unsigned char* pDecryptStart = pRead + 4;
	unsigned char* pDecryptEnd = pRead + 5 + payloadLen;

	unsigned char i = 1;
	unsigned char Dn;
	unsigned char Pn;
	unsigned char En_1 = 0;
	unsigned char Pn_1 = 0;

	while (pDecryptStart != pDecryptEnd)
	{
		Pn = (*pDecryptStart) ^ (En_1 + stHeader::SERVER_STATIC_KEY + i);
		En_1 = *pDecryptStart;

		Dn = Pn ^ (Pn_1 + randKey + i);
		Pn_1 = Pn;

		*pDecryptStart = Dn;

		i++;
		pDecryptStart++;
	}
}

//--------------------------------------------------------
// 헤더를 체크합니다.
//--------------------------------------------------------
bool CServer::CheckHeader(unsigned char* pRead, uint16_t payloadLen, bool bNeedDecode)
{
	//----------------------------------------------------
	// 헤더 코드가 불일치 한다면
	//----------------------------------------------------
	if (*pRead != CServer::stHeader::SERVER_CODE)
		return false;

	if (bNeedDecode)
	{
		Decode(pRead, payloadLen);
	}
	//----------------------------------------------------
	// 체크썸이 불일치 한다면
	//----------------------------------------------------

	uint8_t calculateChecksum = GetCheckSum(pRead, payloadLen);
	if (calculateChecksum != *(pRead + 4))
		return false;

	return true;
}

//-------------------------------------------------------
// Info: SendPacket에서 WSASend를 위해 SendPost()를 우회
//-------------------------------------------------------
void CServer::SendPQCS(Session* pSession)
{
	bool isFine = pSession->SendPost();
	if (isFine == false)
	{
		//-----------------------------------------------
		// isSending은 0이 된 상태. 디스커넥트가 올라갔는지 
		// 확인이 필요하다.
		//-----------------------------------------------
		unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
		//-----------------------------------------------
		// 내가 참조해서 릴리즈를 못했다면 바로 해제까지
		//-----------------------------------------------
		if (retNum == 0)
		{
			ReleaseSession(pSession);
		}
		else
		{
			// 이 공간은 끊는 주체가 나임
			// WSASend가 실패했거나, 정상
			// TODO: 여기는 추가적인 행동이 필요한지 생각해보기
			if (retNum == 0xFFFF'FFFF)
				__debugbreak();
		}
	}
	else
	{
		//-----------------------------------------------
		// 여기는 SendPost가 성공적으로 끝남.
		// 완료통지에서 참조카운트 해제
		//-----------------------------------------------
	}
}


//-------------------------------------------------------
// Info: 연결 종료 요청
// Param: uint64_t sessionId
// Return: 성공: true, 실패: false
// 실패사유: 비정상 세션아이디 (서버 종료사유)
//-------------------------------------------------------
void CServer::GetSessionIP(uint64_t sessionId, wchar_t* ip)
{
	Session* pSession = _sessionStructure.FindSession(sessionId);
	if (pSession == nullptr)
	{
		memset(ip, 0, 32);
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"[sessionId: %016llx] Disconnect() 비정상적인 세션 아이디(out of index)", sessionId);
		return;
	}
	//---------------------------------------------------
	// * 참조 카운트를 올려서 릴리즈 진입 못하도록
	// * 릴리즈 진행중인지 확인하기
	//---------------------------------------------------
	unsigned long retNum = _InterlockedIncrement(&pSession->refcount);
	if ((retNum & SERVER_RELEASE_FLAG) == SERVER_RELEASE_FLAG)
	{
		//-----------------------------------------------
		// 자신의 참조는 자신이 제거하기
		// . 새로 생성중이더라도 직접 참조를 올려 0될 수 없음
		// ** 아니다. 여기서 0이 될 수 있다.
		// ** 비교 후 자고나서 봤는데 0일 수 있음
		//-----------------------------------------------
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
		memset(ip, 0, 32);
		return;
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
		wcscpy_s(ip, 16, pSession->ip);
	}
	else
	{
		memset(ip, 0, 32);
	}
	//---------------------------------------------------
	// 올렸던 참조 내려주고 refcount == 0 이면 릴리즈 시켜줌
	// 내가 올렸으므로 릴리즈가 안됬을 수 있기 때문
	// 
	// . 내꺼든 아니든 0이되면 해제 해줘야함
	//---------------------------------------------------

	retNum = _InterlockedDecrement(&pSession->refcount);
	if (retNum == 0)
	{
		ReleaseSession(pSession);
	}
	else
	{
		if (retNum == 0xFFFF'FFFF)
			__debugbreak();
	}

	return;
}

#pragma endregion

#pragma region CLanServer_ThreadProc


//-------------------------------------------------------
// Info: 워커 스레드 function
//-------------------------------------------------------
unsigned int CServer::WorkerThreadFunc(void* param)
{
	PRO_START();
	CServer* nowServer = (CServer*)param;
	HANDLE hcp = (HANDLE)nowServer->_hIOCP;
	DWORD tid = GetCurrentThreadId();
	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"WorkerThread Start!!");

	volatile long* pRecvCnt = &nowServer->_monitor.RecvMessageTPS;
	bool bNeedDecode = nowServer->_option.bUseEncode;

	nowServer->OnWorkerStart();

	while (1)
	{
		DWORD cbTransferred = 0;
		Session* pSession = nullptr;
		CServer::Session::myoverlapped* pOverlapped = nullptr;

		// nowServer->OnWorkerThreadEnd();
		PRO_BEGIN(L"GQCS");
		GetQueuedCompletionStatus(hcp, &cbTransferred, (ULONG_PTR*)&pSession, (WSAOVERLAPPED**)&pOverlapped, INFINITE);
		PRO_END(L"GQCS");

		// nowServer->OnWorkerThreadBegin();

		if (pOverlapped == nullptr)
		{	
			//------------------------------------------------------
			// 지금은 메시지가 종료, 릴리즈 이벤트 발동 두개라 이렇게
			//------------------------------------------------------
			if (cbTransferred == SERVER_MSG_RELEASE)
			{
				//--------------------------------------------------
				// PQCS (릴리즈 이벤트핸들러 발동), 
				// ** 인자가 세션id가들어옴
				//--------------------------------------------------
				nowServer->OnRelease((uint64_t)pSession);
				continue;
			}
			if (cbTransferred == SERVER_MSG_DELAYSEND)
			{
				nowServer->SendPQCS(pSession);
				continue;
			}
			if (cbTransferred == SERVER_MSG_USER_EVENT)
			{
				nowServer->OnUserEvent((CPacket*)pSession);
				continue;
			}

			s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"WorkerThread 종료 시그널 인식 (cbTransferred: %u, pSession: %p)", cbTransferred, pSession);
			break;
		}
		else if (cbTransferred == 0)
		{
			//PRO_BEGIN(L"GQCS: CB_TRANSFERRED 0");
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
						s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
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
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] 상대가 연결을 끊어 0 완료통지가 왔습니다. 오버랩: %d", pSession->sessionId, pOverlapped->isSend);

				nowServer->OnSend(pSession->sessionId, false);

				long retNum = _InterlockedDecrement(&pSession->refcount);
				if (retNum == 0)
				{
					//--------------------------------------------
					// I/O 취소 없이 제거 가능
					//--------------------------------------------
					s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] 세션의 cbTransferred == 0으로 ReleaseSession() 돌입", pSession->sessionId);
					nowServer->ReleaseSession(pSession);
				}
				else
				{
					//--------------------------------------------
					// TODO: 어떻게 돌릴까?
					// . 일단 Refcount가 올라가있으면 언젠간 걸음
					// . 근데 리시브가 다시 걸릴 일이 없는데 이미
					//   누가 참조중이여서 0이 안됨
					// while(refcount != 0) if(CioEx==true) break;
					// 이건 어때?
					//--------------------------------------------

					// 여기서 다른 세션일 수 있음. 내가 iocount를 내렸기 때문... 고민해라
					// CancelIoEx((HANDLE)pSession->sock, (WSAOVERLAPPED*)&pSession->recvOl);
					if (retNum == 0xFFFF'FFFF)
						__debugbreak();
				}
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

				s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] 상대가 연결을 끊어 0 완료통지가 왔습니다. 오버랩: %d", pSession->sessionId, pOverlapped->isSend);

				long retNum = _InterlockedDecrement(&pSession->refcount);
				if (retNum == 0)
				{
					//--------------------------------------------
					// I/O 취소 없이 제거 가능
					//--------------------------------------------
					s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] 세션의 cbTransferred == 0으로 ReleaseSession() 돌입", pSession->sessionId);
					nowServer->ReleaseSession(pSession);
				}
				else
				{
					if (retNum == 0xFFFF'FFFF)
						__debugbreak();
				}
			}

			//PRO_END(L"GQCS: CB_TRANSFERRED 0");
			continue;
		}

		//-----------------------------------------------------
		// 뭔가 받긴 받았을 때 하는 일
		// Send / Recv
		//-----------------------------------------------------
		if (pOverlapped->isSend)
		{
			Profile pf(L"GQCS: Send");

			//-------------------------------------------------
			// Send
			// . refcount는 아직 양수 (Sendpost전에 내리면 바뀔 수 있음)
			// . isSending 역시 여전히 1 (sendOl.sendbyte는 안바뀜)
			//-------------------------------------------------
			//PRO_BEGIN(L"GQCS: IS SEND");
			//-------------------------------------------------
			// 일단 보낸 결과물에 대한 해제는 진행 필요
			//-------------------------------------------------
			int sendPacketsCnt = pSession->sendPacketsCnt;
			for (int i = 0; i < sendPacketsCnt; i++)
			{
				int retFree = CPACKET_FREE(pSession->sendPackets[i]);
				if (retFree)
				{
					s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"패킷 반환중 오류, 패킷 클래스에 문제가 있습니다. (CPacket Free Error Code: %d)", retFree);
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
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] [sendbyte: %d, cbTransferred: %d] 상대가 연결을 끊었습니다(Send가 덜 보내진 경우). iocount == 0시 해제",
					pSession->sessionId, pOverlapped->sendbyte, cbTransferred);
				pOverlapped->sendbyte = 0;	//초기화
				_InterlockedExchange(&pSession->isDisconnect, 1);	// 세션 디스커넥트 올리기

				//----------------------------------------------
				// ** 중요한것을 빼먹음 **...
				// . 이게 없으면 리시브는 여원히 걸릴 수 있음
				//----------------------------------------------
				CancelIoEx((HANDLE)pSession->sock, &pSession->recvOl.ol);

				_InterlockedExchange(&pSession->isSending, 0);

				nowServer->OnSend(pSession->sessionId, false);

				unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
				if (retNum == 0)
				{
					//-----------------------------------------
					// 정상적인 해제를 할 타이밍
					//-----------------------------------------
					s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] 세션의 Send통지 완료로 refcount == 0되어 ReleaseSession() 돌입(send 실패사유)", pSession->sessionId);
					nowServer->ReleaseSession(pSession);
				}
				else
				{
					//--------------------------------------------
					// TODO: 어떻게 돌릴까?
					// . 일단 Refcount가 올라가있으면 언젠간 걸음
					// . 근데 리시브가 다시 걸릴 일이 없는데 이미
					//   누가 참조중이여서 0이 안됨
					// while(refcount != 0) if(CioEx==true) break;
					// 이건 어때?
					//--------------------------------------------
					// CancelIoEx((HANDLE)pSession->sock, (WSAOVERLAPPED*)&pSession->recvOl);
					if (retNum == 0xFFFF'FFFF)
						__debugbreak();
				}

				continue;
			}
			else
			{
				nowServer->OnSend(pSession->sessionId, true);
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
						unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
						if (retNum == 0)
						{
							nowServer->ReleaseSession(pSession);
						}
						else
						{
							// 리시브 취소를 해야되는지 이건 고민되네..
							// WSASend가 실패했거나, 정상
							// TODO: 여기는 추가적인 행동이 필요한지 생각해보기

							if (retNum == 0xFFFF'FFFF)
								__debugbreak();
						}
						continue;
					}

					bool isFine = pSession->SendPost();
					if(isFine == false)
					{
						//-----------------------------------------------
						// isSending은 0이 된 상태. 참조 카운트도 해제함
						//-----------------------------------------------
						unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
						//-----------------------------------------------
						// 내가 참조해서 릴리즈를 못했다면 바로 해제까지
						//-----------------------------------------------
						if (retNum == 0)
						{
							nowServer->ReleaseSession(pSession);
						}
						else
						{
							// 이 공간은 끊김을 감지하는 순간이 있을 수 있음
							// WSASend가 실패했거나, 정상
							// TODO: 여기는 추가적인 행동이 필요한지 생각해보기
							if (retNum == 0xFFFF'FFFF)
								__debugbreak();
						}
					}
				}
				else
				{
					//---------------------------------------------------
					// isSending 이미 1임. 누가 보내는중
					// 완료통지에서 참조카운트 내려줘야함
					// 
					// ** 중요 ** 여기서 잠들면 isSending이 0 될수 있음.
					// 그래서 refcount 확인 필요
					//---------------------------------------------------
					unsigned long retNum = _InterlockedDecrement(&pSession->refcount);
					if (retNum == 0)
					{
						nowServer->ReleaseSession(pSession);
					}
					else
					{
						// 이 공간은 끊김을 감지하는 순간이 있을 수 있음
						// WSASend가 실패했거나, 정상
						// TODO: 여기는 추가적인 행동이 필요한지 생각해보기
						if (retNum == 0xFFFF'FFFF)
							__debugbreak();
					}
				}
			}

			//PRO_END(L"GQCS: IS SEND");
		}
		else // Recv
		{
			Profile pf(L"GQCS: Recv");

			CPacket* recvQ = pSession->recvQ;
			//PRO_BEGIN(L"GQCS: IS RECV");
			if (pSession->isDisconnect == 1)
			{
				long retNum = _InterlockedDecrement(&pSession->refcount);
				if (retNum == 0)
				{
					s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] 세션의 Recv통지 완료했는데 사용중인 세션이 아님. iocount--; iocount == 0되어 ReleaseSession()", pSession->sessionId);
					nowServer->ReleaseSession(pSession);
				}
				else
				{
					//TODO: 고민 필요
					if (retNum == 0xFFFF'FFFF)
						__debugbreak();
				}
				continue;
			}
			recvQ->MoveWritePtr(cbTransferred);

			uint16_t payloadlen;
			bool canRecvPost = true;
			bool retHeaderCheck = true;
			bool needNewBuffer = false;
			while (recvQ->GetDataSize() > 0)
			{
				if (recvQ->GetDataSize() < sizeof(stHeader))
					break;

				// Peek 페이로드길이
				payloadlen = *(uint16_t*)(recvQ->m_readptr + 1);

				if (recvQ->GetDataSize() >= sizeof(stHeader) + payloadlen)
				{
					//---------------------------------------------
					// V4: 헤더 체크
					//---------------------------------------------
					unsigned char* pRead = (unsigned char*)recvQ->m_readptr;
					retHeaderCheck = CServer::CheckHeader(pRead, payloadlen, bNeedDecode);
					if (retHeaderCheck == false)
					{
						s_ServerSyslog.LogHex(TAG_NET, c_syslog::en_SYSTEM, pRead, payloadlen + sizeof(stHeader), 
							L"[sessionId: %016llx] 이상한 패킷 내용", pSession->sessionId);
						_InterlockedExchange(&pSession->isDisconnect, 1);

						//----------------------------------------------
						// ** 중요한것을 빼먹음 **...
						//----------------------------------------------
						CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl.ol);

						long retNum = _InterlockedDecrement(&pSession->refcount);
						if (retNum == 0)
						{
							nowServer->ReleaseSession(pSession);
						}
						else
						{
							if (retNum == 0xFFFF'FFFF)
								__debugbreak();
						}
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
					recvQ->MoveReadPtr(sizeof(stHeader));
					nowServer->OnMessage(pSession->sessionId, recvQ, payloadlen);
					recvQ->MoveReadPtr(payloadlen);
				}
				else
				{
					if (payloadlen > SERVER_RECVQ_SIZE - sizeof(stHeader) - CPACKET_HEADER_LEN)
					{
						s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"[sessionId: %016llx] 패킷이 너무 길어요. 읽을 수가 없음 [header payloadlen: %d]", pSession->sessionId, payloadlen);
						_InterlockedExchange(&pSession->isDisconnect, 1);

						//----------------------------------------------
						// ** 중요한것을 빼먹음 **...
						//----------------------------------------------
						CancelIoEx((HANDLE)pSession->sock, &pSession->sendOl.ol);

						long retNum = _InterlockedDecrement(&pSession->refcount);
						if (retNum == 0)
						{
							nowServer->ReleaseSession(pSession);
						}
						else
						{
							if (retNum == 0xFFFF'FFFF)
								__debugbreak();
						}
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
			if ((recvQ->GetFreeSize() < SERVER_RECVQ_MIN_LEFT_SIZE) || (needNewBuffer == true))
			{
				pSession->recvQ = CPACKET_ALLOC();	// 새로운 것 받음
				pSession->recvQ->SetRecvBuffer();

				pSession->recvQ->PushData(recvQ->GetReadPtr(), recvQ->GetDataSize());
				CPACKET_FREE(recvQ);
			}


			bool retRecv = pSession->RecvPost();
			if (retRecv == false)
			{
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] Recv완료통지 이후 Recv실패로 ReleaseSession() 돌입", pSession->sessionId);
				nowServer->ReleaseSession(pSession);
			}
			//PRO_END(L"GQCS: IS RECV");
		}
	}

	nowServer->OnWorkerEnd();

	PRO_EXIT();
	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"WorkerThread Exit!!");
	return 0;
}

//-------------------------------------------------------
// Info: Accept 스레드 function
//-------------------------------------------------------
unsigned int CServer::AcceptThreadFunc(void* param)
{
	PRO_START();
	CServer* nowServer = (CServer*)param;
	DWORD tid = GetCurrentThreadId();

	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"AcceptThread Start!!");
	stServerMonitor* pMonitor = &nowServer->_monitor;
	int MaxUsers = nowServer->_option.iMaxConcurrentUsers;
	SessionStructure* pSessionStructure = &nowServer->_sessionStructure;
	HANDLE hcp = nowServer->_hIOCP;
	HANDLE hEvents[2] = { nowServer->_hEventForAccept, nowServer->_hEventForExit };

	SOCKADDR_IN caddr;
	int caddrlen;
	SOCKET newSocket;
	Session* pNewSession;
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
			s_ServerSyslog.LogEx(TAG_NET, dwAcceptErr, c_syslog::en_ERROR, L"어셉트 스레드의 accept()오류코드입니다.");
			continue;
		}
		// Accept모니터링
		_InterlockedIncrement(&pMonitor->AcceptTPS);

		LINGER rstLinger = { 1, 0 };
		int retLinger = setsockopt(newSocket, SOL_SOCKET, SO_LINGER, (char*)&rstLinger, sizeof(rstLinger));
		if (retLinger == SOCKET_ERROR)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"링거 설정에 실패하였습니다");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		bool isConnectOk = nowServer->OnConnectionRequest(caddr.sin_addr);
		if (isConnectOk == false)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"연결이 거부된 IP, Port여서 거부했습니다. 상세로그는 OnConnectionRequest에");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		if (nowServer->_option.bUseSO_SNDBUF == true)
		{
			DWORD sndbuflen = 0;
			int retZeroSNDBUF = setsockopt(newSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuflen, sizeof(sndbuflen));
			if (retZeroSNDBUF == SOCKET_ERROR)
			{
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"소켓 SNDBUF = 0 설정에 실패하였습니다.");
				int retClose = closesocket(newSocket);
				if (retClose == SOCKET_ERROR)
				{
					DWORD closeErr = GetLastError();
					s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
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
				s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"TCP NODELAY 설정에 실패하였습니다.");
				int retClose = closesocket(newSocket);
				if (retClose == SOCKET_ERROR)
				{
					DWORD closeErr = GetLastError();
					s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
				}
				continue;
			}
		}

		if (pSessionStructure->_sessionCnt >= MaxUsers)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"유저가 꽉 차서 끊음");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		pNewSession = nowServer->InitNewSession(newSocket, &caddr);
		if (pNewSession == nullptr)
		{
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_ERROR, L"서버 세션 관리가 이상함. AcceptThread에서 제대로 세션을 받지 못함");
			int retClose = closesocket(newSocket);
			if (retClose == SOCKET_ERROR)
			{
				DWORD closeErr = GetLastError();
				s_ServerSyslog.LogEx(TAG_NET, closeErr, c_syslog::en_ERROR, L"AcceptThread에서 소켓 닫던 중 오류");
			}
			continue;
		}

		//--------------------------------------------------------------
		// 여기부터 세션을 정상적으로 꺼냈으니 refcount사용
		//--------------------------------------------------------------
		s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[%s:%d][sessionId: %016llx] 새로운 세션이 생성되었습니다. ", pNewSession->ip, pNewSession->port, pNewSession->sessionId);

		unsigned long retNum;
		HANDLE retRegister = CreateIoCompletionPort((HANDLE)pNewSession->sock, hcp, (ULONG_PTR)pNewSession, 0);
		if (retRegister == NULL)
		{
			DWORD registerOnIOCPErr = GetLastError();
			s_ServerSyslog.LogEx(TAG_NET, registerOnIOCPErr, c_syslog::en_ERROR, L"[sessionId: %016llx] IOCP등록에 실패 RelaseSession()돌입", pNewSession->sessionId);
			retNum = _InterlockedDecrement(&pNewSession->refcount);
			if (retNum == 0)
			{
				nowServer->ReleaseSession(pNewSession);
			}
			continue;
		}

		// 이 작업 이제는 InitNewSession에서함
		//_InterlockedIncrement(&pNewSession->refcount);

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
			s_ServerSyslog.Log(TAG_NET, c_syslog::en_DEBUG, L"[sessionId: %016llx] AcceptThread에서 세션의 첫 Recv실패로 ReleaseSession() 돌입", pNewSession->sessionId);
			nowServer->ReleaseSession(pNewSession);
		}
	}

	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"AcceptThread Exit!!");
	PRO_EXIT();
	return 0;
}

//-------------------------------------------------------
// Info: Monitor 스레드 function
//-------------------------------------------------------
unsigned int CServer::MonitorThreadFunc(void* param)
{
	CServer* nowServer = (CServer*)param;
	stServerMonitor* pMonitor = &nowServer->_monitor;
	stServerMonitorViewer* pMonitorView = &nowServer->_monitorViewer;
	DWORD tid = GetCurrentThreadId();
	HANDLE hEvent = nowServer->_hEventForExit;
	timeBeginPeriod(1);

	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"MonitorThread Start!!");

	long acceptTps;
	long recvMessageTps;
	long sendMessageTps;

	HANDLE hEventForExit = nowServer->_hEventForExit;

	DWORD timeCurTime = timeGetTime();
	int deltaTime;
	while (1)
	{
		//-----------------------------------------------------------------------
		// 모니터링 항목 뽑기
		//-----------------------------------------------------------------------
		acceptTps = _InterlockedExchange(&pMonitor->AcceptTPS, 0);
		recvMessageTps = _InterlockedExchange(&pMonitor->RecvMessageTPS, 0);
		sendMessageTps = _InterlockedExchange(&pMonitor->SendMessageTPS, 0);
		pMonitor->ProcessMonitor.Update();
		pMonitor->SystemMonitor.Update();
		
		//-----------------------------------------------------------------------
		// 모니터링 항목 셋팅
		//-----------------------------------------------------------------------
		pMonitorView->lAcceptTps = acceptTps;
		pMonitorView->lRecvMessageTps = recvMessageTps;
		pMonitorView->lSendMessageTps = sendMessageTps;
		pMonitorView->fProcessUsageTotal = pMonitor->ProcessMonitor.ProcessTotal();
		pMonitorView->fProcessUsageKernel = pMonitor->ProcessMonitor.ProcessKernel();
		pMonitorView->fProcessUsageUser = pMonitor->ProcessMonitor.ProcessUser();
		pMonitorView->llProcessNopagedBytes = pMonitor->ProcessMonitor.ProcessNonpagedBytes();
		pMonitorView->llProcessPrivateBytes = pMonitor->ProcessMonitor.ProcessPrivateBytes();

		pMonitorView->fSystemCacheFault = pMonitor->SystemMonitor.SystemCacheFault();
		pMonitorView->llSystemCommitBytes = pMonitor->SystemMonitor.SystemCommitBytes();
		pMonitorView->llNonpagedBytes = pMonitor->SystemMonitor.SystemNonpagedBytes();
		pMonitorView->llAvailableBytes = pMonitor->SystemMonitor.SystemAvailableBytes();
		pMonitorView->fProcessorTotal = pMonitor->SystemMonitor.ProcessorTotal();
		pMonitorView->fProcessorKernel = pMonitor->SystemMonitor.ProcessorKernel();
		pMonitorView->fProcessorUser = pMonitor->SystemMonitor.ProcessorUser();
		pMonitorView->fNetworkSegmentRecved = pMonitor->SystemMonitor.SystemSegmentRecved();
		pMonitorView->fNetworkSegmentSent = pMonitor->SystemMonitor.SystemSegmentSent();
		pMonitorView->fNetworkSegmentRetransmitted = pMonitor->SystemMonitor.SystemSegmentRetransmitted();

		pMonitorView->llNetworkTotalBytesRecved = pMonitor->SystemMonitor.SystemTotalNetworkBytesRecved();
		pMonitorView->llNetworkTotalBytesSent = pMonitor->SystemMonitor.SystemTotalNetworkBytesSent();

		nowServer->OnMonitor();

		deltaTime = (int)(timeGetTime() - timeCurTime);
		if (deltaTime < SERVER_MONITORING_TICK)
		{
			// Sleep(SERVER_MONITORING_TICK - deltaTime);
			DWORD ret = WaitForSingleObject(hEventForExit, (SERVER_MONITORING_TICK - deltaTime));
			if (ret == WAIT_OBJECT_0)
				break;
		}
		else
		{
			s_ServerSyslog.Log(L"Monitor", c_syslog::en_ERROR, L"MonitorThread 의 루프가 빡빡합니다. (잘 못돌고있어요)");
			DWORD ret = WaitForSingleObject(hEventForExit, 1);
			if (ret == WAIT_OBJECT_0)
				break;
		}
		timeCurTime += SERVER_MONITORING_TICK;
	}

	s_ServerSyslog.Log(TAG_NET, c_syslog::en_SYSTEM, L"MonitorThread Exit!!");

	return 0;
}

#pragma endregion


