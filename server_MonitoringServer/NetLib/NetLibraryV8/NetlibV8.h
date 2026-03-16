#ifndef __NETLIB_H__
#define __NETLIB_H__
//////////////////////////////////////////////////////////////
// NetLibrary V8 h
// 
// < V8.2 : OnInit위치 변경 (12.22) >
// . 워커 스레드가 생성되기 전에 상속받은 클래스의 Init을 호출해줌
// 
// < V8.1 : Session::SendPost()를 비동기 이벤트로 >
// . 싱글스레드의 경우 SendPacket()내부에서 WSASend를 타는 커널진입 행위를 하는것이 
//   느리다고 판단. 
// . 아직 WSASend자체가 이렇게 비동기 이벤트로 바꿀 경우 루프백 테스트 시 3배 더 빨라지는
//   원인은 정확히는 모르겠지만 추정만 했음 (블로그에 정리)
// ** 아무튼 이벤트로 돌렸다!
// 
// < V8 : OnRelease()를 비동기 이벤트로 >
// . ReleaseSesison()은 세션정리만 하고, PQCS로 워커스레드에게 세션아이디 전달
// . 전달받은 세션아이디에 대한 OnRelease()이벤트 발생
// 
// [이유]
// . Disconnect(), SendPacket()등의 함수에서 OnRelease()이벤트가 발생
// . 구조적으로 이상하기 때문에 비동기 방식으로 변경
//   => (On~~핸들러 함수는 네트워크라이브러리 스레드 에서 발동해야함)
// . 예로 OnMessage()에서 Disconnect()호출 시 OnRelease()가 발생하는
//   핸들러에서 핸들러가 재귀적으로 호출되는 형태가 되었음.
// . 지금 채팅서버에서는 문제가 없을 수 있지만, 구조가 커진다면 문제가
//   생긴다고 판단
// 
// < V7.1 : 핫패스 최적화 (25.11.19) >
// . 지금 이전까지 개발 편의성을 위해 구조체로 감싼게 많았다.
// . 1만명 루프에서 tps가 생각보다 낮고, 큐가 낮아서 최적화를 시작
//   (버티긴 하지만, 메시지 전달 최대치가 너무 느림)
// . 일단 리시브 버퍼에서 -> 컨텐츠 큐 까지 넘겨주는 곳에 불필요한 구조체를 지우고
//   최대한 포인터로 빠른 넘김 하는 방식으로 가는중이다.
// 
// . 이후 컨텐츠 스레드를 손보면서 여기도 최적화 할 예정
// 
// < V7 : 타임아웃, 유저판단 지우기 >
// . 유저인지, 타임아웃인지는 컨텐츠 입니다. 네트워크 라이브러리가
//   가지고 있을 것은 아니라고 판단했습니다.
// 
// < V6 : CPacket* recvQ >
// . 리시브 버퍼를 CPacket을 사용해 외부에서 참조할 수 있게 하여
//   복사 횟수를 줄이자
// 
// < V5 : 타임아웃 >
// . 옵션으로 타임아웃 스레드를 킬 수 있음
// 
// < V4 : 인코딩/디코딩 >
// . 인코딩 /디코딩 사용 여부 추가
// . 관련 기능 추가
// 
// < V3 : CPacket에 참조카운트가 추가됨 >
// . 따라서 SendPacket()에서도 이를 관리함
// 
// V2: 락프리 자료구조 삽입
// . refcount 도입
// . 기존에 신경쓰지 못햇던 문제들을 하나씩 해결
// 
// V1_3: CPacket 직렬화 버퍼 풀 클래스 내에 넣은 버전 삽입
// 
// V1_2: < 센드 버퍼, 리시브 버퍼의 복사 없애기 >
//       => 이것 관련 새로운 버퍼를 넣기
// 
// <문제 발생>
// 1. Send 완료통지에서의 문제
// [1] isSending -> 0으로 변경
// [2] 로직수행              <----- 이때 1로바꾸고 누가 센드
// [3] sendbyte > cbtransferred 면 해제시키기
// => 멀티스레드니까 isSending = 0을 확인 할거 하고 햇어야 한다
// 
// 
//////////////////////////////////////////////////////////////
#include <ws2tcpip.h>
#include <windows.h>

#include "LockFreeStackV3.h"
#include "LockFreeQueueV5.h"
#include "TlsPacketV4.h"
#include "MonitorV1.h"
#include "logclassV1.h"

constexpr const wchar_t* TAG_NET = L"Net";
constexpr const int SERVER_MAX_THREADS_CNT = 64;
constexpr const int SERVER_RECVQ_SIZE = 1395;
constexpr const int SERVER_RECVQ_MIN_LEFT_SIZE = 50;
constexpr const int SERVER_SEND_WSABUF_MAX = 200;
constexpr const int SERVER_MONITORING_TICK = 1000;

constexpr const DWORD SERVER_MSG_EXIT = 0;
constexpr const DWORD SERVER_MSG_RELEASE = 1;
constexpr const DWORD SERVER_MSG_DELAYSEND = 2;
constexpr const DWORD SERVER_MSG_USER_EVENT = 3;

#define SERVER_RELEASE_FLAG		0x8000'0000
#define SERVER_INDEX_MASK		0x0000'0000'000F'FFFF
class c_syslog;
//-------------------------------------------------------
// 확인용 모니터링 항목
//------------------------------------------------------
struct stServerMonitorViewer
{
	long lAcceptTps;
	long lRecvMessageTps;
	long lSendMessageTps;
	float fProcessUsageTotal;
	float fProcessUsageUser;
	float fProcessUsageKernel;
	long long llProcessNopagedBytes;
	long long llProcessPrivateBytes;

	float fSystemCacheFault;
	float fNetworkSegmentRecved;
	float fNetworkSegmentSent;
	float fNetworkSegmentRetransmitted;
	float fProcessorTotal;
	float fProcessorUser;
	float fProcessorKernel;
	long long llSystemCommitBytes;
	long long llNonpagedBytes;
	long long llAvailableBytes;

	long long llNetworkTotalBytesRecved;
	long long llNetworkTotalBytesSent;
};

//////////////////////////////////////////////////////////////
// CServer V8
// 
// 
// < V4 : 타임아웃 >
// . 옵션으로 타임아웃 스레드를 킬 수 있음
// 
// < V4 : 인코딩/디코딩 >
// . 인코딩 /디코딩 사용 여부 추가
// . 관련 기능 추가
// 
// < V3 : CPacket에 참조카운트가 추가됨 >
// . 따라서 SendPacket()에서도 이를 관리함
//////////////////////////////////////////////////////////////
class CServer
{
	friend class CLoginServer;
	friend class CChatServer;
public:
#pragma region Structs
	//-------------------------------------------------------
	// 세션 구조체 (Init 필수)
	//-------------------------------------------------------
	struct Session
	{
		//-------------------------------------------------------
		// 오버랩 구조체
		//-------------------------------------------------------
		struct myoverlapped
		{
			WSAOVERLAPPED ol;
			bool isSend;
			int sendbyte;
		};

		SOCKET sock;
		uint64_t sessionId;
		volatile unsigned long refcount;
		CPacket* recvQ;
		CLockFreeQueue<CPacket*> sendQ;
		myoverlapped recvOl;
		myoverlapped sendOl;
		volatile long isSending;	// 1: 센딩중, 0: 센드안하는중
		volatile long isDisconnect;	// 0: 사용중, 1: 디스커넥트 예정
		wchar_t ip[16];
		uint16_t port;
		CPacket** sendPackets;
		int sendPacketsCnt;
		Session() :sock(INVALID_SOCKET), sessionId(0), refcount(SERVER_RELEASE_FLAG), recvQ(nullptr), 
			sendQ(100000),
			recvOl{}, sendOl{}, isSending(0), isDisconnect(0), ip{}, port(0), sendPackets{}, sendPacketsCnt(0)
		{
			recvOl.isSend = false;
			sendOl.isSend = true;
			sendPackets = new CPacket* [SERVER_SEND_WSABUF_MAX];
		}
		~Session() { delete sendPackets; }
		//-------------------------------------------------------
		// 스택에서 얻어온 index 바탕으로 소켓을 초기화
		//-------------------------------------------------------
		void Init(SOCKET _sock, int index, SOCKADDR_IN* caddr, uint64_t sid);
		//-------------------------------------------------------
		// ReleaseSession에서만 호출!
		// 소켓 초기화 후 스택에 반환해야 함, refcount == 0일때만!
		//-------------------------------------------------------
		void Clear();
		//-------------------------------------------------------
		// refcount == 0이면 false반환
		// . 얘는 세션이 생기면 올라가는 refcount를 내리는 역할이라서
		// . 따로 참조하지 않고 들어가도되고 (always 참조상태)
		// . 이 함수 내부에서 내리는 작업을 함
		// 
		// . 자신의 IO요청은 자기가 직접 필요시 해제
		//-------------------------------------------------------
		bool RecvPost();
		//-------------------------------------------------------
		// . false: refcount를 내려야되면(완료 통지가 안와서)
		//                               + 내부에서 올려서 내려야함
		// . true: IOCP 완료통지에서 내려야한다!
		// . 1: 보낼 게 없어서 (외부에서 참조 내리긴 해야됨)
		// . 2: Disconnect인지 (외부에서 참조 내리긴 해야함)
		// 
		// . 자신의 IO요청은 자기가 직접 필요시 해제
		// 
		// . 반드시 외부에서 참조카운트가 올라간 상태로 진입
		// . 반드시 isSending = 1로 만들었을 경우만 진입
		// . => 반드시 외부에서 isDisconnect == 0 확인하고 진입해라
		// . false반환시 disconnect플래그 필요하면 올려둠
		//-------------------------------------------------------
		bool SendPost();

		//-------------------------------------------------------
		// 실제 세션 id 획득 (합쳐지지 않은) (43비트)
		//-------------------------------------------------------
		uint64_t GetRealSessionId() const
		{
			return (int)(sessionId >> 20);
		}
		//-------------------------------------------------------
		// 인덱스 획득
		//-------------------------------------------------------
		int GetIndex() const
		{
			return (int)(sessionId & SERVER_INDEX_MASK);
		}
	};
	//-------------------------------------------------------
	// 세션 저장 배열
	// Session*으로 캐스팅해서 배열 사용가능
	//-------------------------------------------------------
	class SessionStructure
	{
		friend class CServer;
	public:
		SessionStructure() :_sessionsArray(nullptr), _sessionCnt(0), _maxSessionCnt(0) {}
		~SessionStructure()
		{
			if (_sessionsArray != nullptr)	//생성이 되어있음
				delete[] _sessionsArray;
		}
		//-------------------------------------------------------
		// 세션 구조 초기화, maxCnt: 배열 확보
		//-------------------------------------------------------
		bool Init(int maxCnt);
		//-------------------------------------------------------
		// 세션 획득, 자리가 없으면 nullptr반환, 자리가 없으면 이상한 것
		// index, session* 둘다 얻음, 
		// 실패 시 index = -1, 반환은 nullptr
		//-------------------------------------------------------
		Session* AcquireSession(int* index);
		//-------------------------------------------------------
		// 세션 반환 (클리어 하고 반환하세요)
		// index가 이상하면 false반환
		//-------------------------------------------------------
		bool ReleaseSession(Session* pSession);
		//-------------------------------------------------------
		// 검색. index가 이상하면 nullptr반환
		//-------------------------------------------------------
		Session* FindSession(uint64_t sessionId)
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
	private:
		Session* _sessionsArray;
		volatile long _sessionCnt;
		int _maxSessionCnt;

		CLockFreeStack<int> _indexStack;
	};
	//-------------------------------------------------------
	// CServer 의 옵션
	//-------------------------------------------------------
	struct stServerOpt
	{
		bool bUseEncode;
		int iMaxConcurrentUsers;
		wchar_t	openIP[16];
		unsigned short	port;
		int iWorkerThreadRunCnt;
		int iWorkerThreadCreateCnt;
		bool bUseTCP_NODELAY;
		bool bUseSO_SNDBUF;
	};
	//-------------------------------------------------------
	// 모니터링 항목
	//-------------------------------------------------------
	struct stServerMonitor
	{
		volatile long AcceptTPS = 0;
		volatile long RecvMessageTPS = 0;
		volatile long SendMessageTPS = 0;
		CMonitorProcess ProcessMonitor;
		CMonitorSystem SystemMonitor;
	};
	//-------------------------------------------------------
	// 헤더
	//-------------------------------------------------------
#pragma pack(push)
#pragma pack(1)
	struct stHeader
	{
		enum enHeader
		{
			SERVER_CODE = 109,
			SERVER_STATIC_KEY = 30
		};
		uint8_t code;	//코드
		uint16_t len;	//페이로드 길이
		uint8_t randkey;	//랜덤키
		uint8_t checksum;	//체크썸
	};
#pragma pack(pop)
#pragma endregion

	//-------------------------------------------------------
	// Info: 서버 세팅 및 시작 준비
	// Param: stServerOpt* 옵션 (복사됨)
	// Return: 실패시 false (종료해야함), 성공시 true
	//-------------------------------------------------------
	bool Init(const stServerOpt* pOpt);
	//-------------------------------------------------------
	// Info: 서버를 시작 (Listen)
	// Param: -
	// Return: 실패시 false (종료해야함), 성공시 true
	//-------------------------------------------------------
	bool Start();

	//-------------------------------------------------------
	// Info: 서버를 중지 (옵션 1: 종료, 0: 중지만)
	// Param: -
	// Return: -
	//-------------------------------------------------------
	void Stop(int opt);

	//-------------------------------------------------------
	// Info: 서버를 중지/시작 (오토 토글)
	// Param: -
	// Return: -
	//-------------------------------------------------------
	bool ToggleStopStart()
	{
		if (_isStop == 1)
			return Start();
		else
			Stop(0);
		return true;
	}

	//-------------------------------------------------------
	// Info: 연결 종료 요청
	// Param: uint64_t sessionId
	// Return: 성공: true, 실패: false
	// 실패사유: 비정상 세션아이디 (서버 종료사유)
	//-------------------------------------------------------
	bool Disconnect(uint64_t sessionId);

	//-------------------------------------------------------
	// < V7.1 >
	// Info: 연결 종료된 세션인지 확인
	// Param: uint64_t sessionId
	// Return: 종료되었으면 true, 살아있으면 false
	//-------------------------------------------------------
	bool isDisconnect(uint64_t sessionId);

	// 이거 SendPacket 이렇게 하면 문제가 있어서
	// //Return: 성공: 0,
	// //        실패: -1 (잘못된 세션 id), 1 (Disconnect가능, SendPost실패)
	// //              2  (보내려는 데이터가 없음)

	//-------------------------------------------------------
	// Info: 센드 요청
	// Param: uint64_t sessionId
	// 
	// 기본 true, 제거는 OnRelease에서 한번 하는것 
	// false는 세션아이디가 잘못되었을 때
	// 실패사유: 잘못된 sessionId, 컨텐츠의 잘못
	//-------------------------------------------------------
	bool SendPacket(uint64_t sessionId, CPacket* pPacket);
	
	//-------------------------------------------------------
	// Info: 세션 수 확인
	// Param: -
	// Return: 세션 수 (int)
	//-------------------------------------------------------
	int GetSessionCnt() const
	{
		return _sessionStructure._sessionCnt;
	}
	//-------------------------------------------------------
	// Info: 모니터링 가져오기
	// Param: -
	// Return: stServerMonitorViewer*
	//-------------------------------------------------------
	stServerMonitorViewer* GetMonitor(void)
	{
		return &_monitorViewer;
	}

	//-------------------------------------------------------
	// Info: 네트워크 스레드 핸들정보
	// Param: -
	// Return: HANDLE*, netThreadNum에는 개수
	//-------------------------------------------------------
	HANDLE* GetThreadsHandle(int& netThreadNum)
	{
		if (_threadNum == 0)
		{
			netThreadNum = 0;
			return nullptr;
		}

		netThreadNum = _threadNum;
		return _hThreads;
	}
	//-------------------------------------------------------
	// Info: 서버 실행 여부
	// Param: -
	// Return: true: 실행중, false: 중지
	//-------------------------------------------------------
	bool IsServerRunning() const { return (_isStop != 1); }

	//-------------------------------------------------------
	// Info: 서버 로그 레벨 변경(디버그<->시스템)
	// Param: -
	// Return: -
	//-------------------------------------------------------
	static void Log_ChangeLevel()
	{
		s_ServerSyslog.ChangeLevel();
	}
	//-------------------------------------------------------
	// Info: 서버 로그 프린트할건지 (디버그<->시스템)
	// Param: -
	// Return: -
	//-------------------------------------------------------
	static void Log_TogglePrint()
	{
		s_ServerSyslog.TogglePrint();
	}

	//-------------------------------------------------------
	// 사용자가 만든 이벤트 Post
	//-------------------------------------------------------
	void PostUserEvent(CPacket* pPacket)
	{
		CPACKET_ADDREF(pPacket);
		PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_USER_EVENT, (ULONG_PTR)pPacket, NULL);
	}

	//-------------------------------------------------------
	// 세션의 ip 얻어오기
	//-------------------------------------------------------
	void GetSessionIP(uint64_t sessionId, wchar_t* ip);

	/* 컨텐츠에서 구현해 줘야 하는 함수들 */
	virtual bool OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip) = 0;
	virtual bool OnConnectionRequest(in_addr ip) = 0;
	virtual void OnRelease(uint64_t sessionId) = 0;
	virtual void OnMessage(uint64_t sessionId, CPacket* pPacket, int len) = 0;
	virtual void OnWorkerStart() = 0;
	virtual void OnWorkerEnd() = 0;
	virtual void OnUserEvent(CPacket* pPacket) = 0;

	virtual bool OnInit(const stServerOpt* pOpt) = 0;
	virtual void OnStop() = 0;
	virtual void OnExit() = 0;
	virtual void OnSend(uint64_t sessionId, bool isValid) = 0;
	
	virtual void OnMonitor() = 0;

	CServer(): _lsock(INVALID_SOCKET), _sid(0), _option{}, _monitorViewer{}, _isStop(0),
		_hThreads{}, _hIOCP(0), _threadNum(0), _hEventForAccept(NULL), _hEventForExit(NULL), _isCallInit(0)
	{

	}

	static c_syslog* GetLog() { return &s_ServerSyslog; }

	HANDLE GetExitHandle() { return _hEventForExit; }

private:
	SOCKET _lsock;
	uint64_t _sid;
	HANDLE _hEventForAccept;
	HANDLE _hEventForExit;
	stServerOpt _option;
	stServerMonitor _monitor;
	stServerMonitorViewer _monitorViewer;
	HANDLE _hThreads[SERVER_MAX_THREADS_CNT];
	HANDLE _hIOCP;
	int _threadNum;
	long _isStop;

	SessionStructure _sessionStructure;

	/* 진행 체크용 플래그 */
	volatile long _isCallInit;

	//--------------------------------------------------------
	// 체크썸을 구합니다. (센드 용)
	//--------------------------------------------------------
	static unsigned char GetCheckSum(CPacket* pPacket);

	//--------------------------------------------------------
	// 체크썸을 구합니다.
	//--------------------------------------------------------
	static unsigned char GetCheckSum(unsigned char* pRead, int payloadLen);

	//--------------------------------------------------------
	// 패킷을 인코드합니다.
	//--------------------------------------------------------
	static void Encode(CPacket* pPacket);

	//--------------------------------------------------------
	// 패킷을 디코드합니다.
	//--------------------------------------------------------
	static void Decode(unsigned char* pRead, int payloadLen);

	//--------------------------------------------------------
	// 헤더를 체크합니다.
	// . read포인터, 페이로드 길이, 디코딩 할것인지여부 (o: 1)
	//--------------------------------------------------------
	static bool CheckHeader(unsigned char* pRead, uint16_t payloadLen, bool bNeedDecode);

	//-------------------------------------------------------
	// Info: 워커 스레드 function
	//-------------------------------------------------------
	static unsigned int WorkerThreadFunc(void* param);
	//-------------------------------------------------------
	// Info: Accept 스레드 function
	//-------------------------------------------------------
	static unsigned int AcceptThreadFunc(void* param);
	//-------------------------------------------------------
	// Info: Monitor 스레드 function
	//-------------------------------------------------------
	static unsigned int MonitorThreadFunc(void* param);

	//-------------------------------------------------------
	// Info: 소켓, 핸들 등의 자원 정리 (초기화) - Init에서
	//-------------------------------------------------------
	void clear();
	//-------------------------------------------------------
	// Info: refcount == 0일 때 세션 반환
	// 내부에서 세션 Clear 후 스택에 반환, OnRelease() 발동 
	//-------------------------------------------------------
	void ReleaseSession(Session* pSession);

	//-------------------------------------------------------
	// Info: Accept 에서 세션 생성, 진입 전 MaxUser고려하세요
	//-------------------------------------------------------
	Session* InitNewSession(SOCKET newSocket, SOCKADDR_IN* caddr);


	//-------------------------------------------------------
	// Info: SendPacket에서 WSASend를 위해 SendPost()를 우회
	//-------------------------------------------------------
	void SendPQCS(Session* pSession);
	
	static c_syslog s_ServerSyslog;
};


#endif