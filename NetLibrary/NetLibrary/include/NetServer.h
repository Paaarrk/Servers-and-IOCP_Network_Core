#ifndef __NET_SERVER_H__
#define __NET_SERVER_H__
//////////////////////////////////////////////////////////////
// NetServer
// 
// < V8.2 : OnInit위치 변경 (12.22) >
// . 워커 스레드가 생성되기 전에 상속받은 클래스의 Init을 호출해줌
// 
// < V8.1 : stNetSession::SendPost()를 비동기 이벤트로 >
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
#include "NetBase.h"
#include "NetSession.h"
#include "TimerJob.h"
#include "NetCrypto.h"
#include "LockFreeStack.hpp"
#include "LockFreeQueue.hpp"


namespace Net
{



	//------------------------------------------------------
	// 모니터링용 TimerJob
	//------------------------------------------------------
	class CServerMonitoringJob : public Core::CTimerJob
	{
		friend class CServer;
	public:
		uint32 GetRecvMessageTpsAvg() const { return _recvMessageTpsAvg; }
		uint32 GetRecvMessageTps() const { return _recvMessageTps; }
		uint32 GetSendMessageTps() const { return _sendMessageTps; }
		uint32 GetAcceptTps() const { return _acceptTps;  }
		time_t GetServerStartTime() const { return static_cast<uint32>(_serverStartTime); }
		uint64 GetTotalAcceptCount() const { return _totalAcceptCount; }
		float  GetWorkerWorkRate(int32 index) const
		{
			if (index < 0 || index >= _workerNum)
				return 0.0f;
			return _workRate[index];
		}

		CServerMonitoringJob();
	private:
		virtual void Excute();

		void IncreaseAcceptCount()
		{
			_InterlockedIncrement(&_totalAcceptCount);
			_InterlockedIncrement(&_acceptCount);
		}

		void IncreaseSendMessageCount()
		{
			_InterlockedIncrement(&_sendMessageCount);
		}

		void IncreaseRecvMessageCount()
		{
			_InterlockedIncrement(&_recvMessageCount);
		}

		void IncreaseWorkTime(int32 workTime, int id)
		{
			_InlineInterlockedAdd(&_workTime[id], workTime);
		}

	private:
		DWORD _startTime;
		time_t _serverStartTime;
		uint64 _totalAcceptCount = 0;
		uint32 _acceptCount = 0;
		uint32 _recvMessageCount = 0;
		uint32 _sendMessageCount = 0;
		uint32 _acceptTps = 0;
		uint32 _recvMessageTps = 0;
		uint32 _sendMessageTps = 0;

		uint32 _recvMessageTpsAvg = 0;
		uint32 _monitoringCount = 0;
		
		int32 _workerNum = 0;
		long _workTime[NET_MAX_THREADS_CNT] = {0,};
		float _workRate[NET_MAX_THREADS_CNT] = {0.0f,};
	};

	//////////////////////////////////////////////////////////////
	// CServer
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
	public:
#pragma region Structs
		//-------------------------------------------------------
		// 세션 저장 배열
		// stNetSession*으로 캐스팅해서 배열 사용가능
		//-------------------------------------------------------
		class SessionStructure
		{
			friend class CServer;
		public:
			SessionStructure();
			~SessionStructure();
			//-------------------------------------------------------
			// 세션 구조 초기화, maxCnt: 배열 확보
			//-------------------------------------------------------
			bool Init(int maxCnt);
			//-------------------------------------------------------
			// 세션 획득, 자리가 없으면 nullptr반환, 자리가 없으면 이상한 것
			// index, session* 둘다 얻음, 
			// 실패 시 index = -1, 반환은 nullptr
			//-------------------------------------------------------
			stNetSession* AcquireSession(int* index);
			//-------------------------------------------------------
			// 세션 반환 (클리어 하고 반환하세요)
			// index가 이상하면 false반환
			//-------------------------------------------------------
			bool ReleaseSession(Net::stNetSession* pSession);
			//-------------------------------------------------------
			// 검색. index가 이상하면 nullptr반환
			//-------------------------------------------------------
			Net::stNetSession* FindSession(uint64_t sessionId);
		private:
			Net::stNetSession* _sessionsArray;
			long _sessionCnt;
			int _maxSessionCnt;

			Core::CLockFreeStack<int> _indexStack;
		};
		//-------------------------------------------------------
		// CServer 의 옵션
		//-------------------------------------------------------
		struct stServerOpt
		{
			bool	bUseEncode;
			int32	iMaxConcurrentUsers;
			wchar_t	openIP[16];
			uint16	port;
			int32	iWorkerThreadRunCnt;
			int32	iWorkerThreadCreateCnt;
			bool	bUseTCP_NODELAY;
			bool	bUseSO_SNDBUF;

			uint8	server_code;
			uint8	static_key;
		};
#pragma endregion

		//-------------------------------------------------------
		// Info: 서버 세팅 및 시작 준비
		// Param: stServerOpt* 옵션 (서버쪽에 stServerOpt부분만 복사본 가짐)
		//        stServerOpt를 상속받은 구조체를 넣어도 됩니다 (OnInit()에 활용)
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
		// Info: 센드 요청
		// Param: uint64_t sessionId
		// 
		// 기본 true, 제거는 OnRelease에서 한번 하는것 
		// false는 세션아이디가 잘못되었을 때
		// 실패사유: 잘못된 sessionId, 컨텐츠의 잘못
		//-------------------------------------------------------
		bool SendPacket(uint64_t sessionId, Net::CPacket* pPacket);

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
		// Info: 서버 실행 여부
		// Param: -
		// Return: true: 실행중, false: 중지
		//-------------------------------------------------------
		bool IsServerRunning() const { return (_isStop != 1); }


		using ServerMonitor = std::shared_ptr<Net::CServerMonitoringJob>;
		//-------------------------------------------------------
		// Info: 각종 모니터링 정보 확인
		// Param: -
		// Return: const ServerMonitor&
		//-------------------------------------------------------
		const ServerMonitor& GetServerMonitor() { return _monitorJob; }

		//-------------------------------------------------------
		// 사용자가 만든 이벤트 Post
		//-------------------------------------------------------
		void PostUserEvent(Net::CPacket* pPacket) const
		{
			CPACKET_ADDREF(pPacket);
			PostQueuedCompletionStatus(_hIOCP, SERVER_MSG_USER_EVENT, (ULONG_PTR)pPacket, NULL);
		}

		/* 컨텐츠에서 구현해 줘야 하는 함수들 */
		// 서버차원

		virtual bool OnInit(const stServerOpt* pOpt) = 0;
		virtual bool OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip) = 0;
		virtual bool OnConnectionRequest(in_addr ip) = 0;

		// NetInterface

		virtual void OnRelease(uint64_t sessionId) = 0;
		virtual void OnMessage(uint64_t sessionId, Net::CPacket* pPacket, int len) = 0;
		//virtual void OnSend(uint64_t sessionId, bool isValid) = 0;

		// IOCP차원

		virtual void OnWorkerStart() = 0;
		virtual void OnWorkerEnd() = 0;
		virtual void OnUserEvent(Net::CPacket* pPacket) = 0;

		// 중지와 종료

		virtual void OnStop() = 0;
		virtual void OnExit() = 0;

		// 생성자
		CServer();
		virtual ~CServer() {}
	private:
		SOCKET _lsock;
		uint64_t _sid;
		HANDLE _hEventForAccept;
		HANDLE _hEventForExit;
		stServerOpt _option;
		HANDLE _hThreads[NET_MAX_THREADS_CNT];
		HANDLE _hIOCP;
		int _threadNum;
		long _isStop;

		SessionStructure _sessionStructure;
		std::shared_ptr<CServerMonitoringJob> _monitorJob;
		stPacketCrypto _serverCrypto;
		/* 진행 체크용 플래그 */
		long _isCallInit;

		CWSAStart _wsaStart;
		static long s_threadId;

		//-------------------------------------------------------
		// Info: 워커 스레드 function
		//-------------------------------------------------------
		static unsigned int NetServerWorkerFunc(void* param);
		//-------------------------------------------------------
		// Info: Accept 스레드 function
		//-------------------------------------------------------
		static unsigned int NetServerAcceptFunc(void* param);

		void InitMonitoringJob();

		void ExitMonitoringJob();

		//-------------------------------------------------------
		// Info: 소켓, 핸들 등의 자원 정리 (초기화) - Init에서
		//-------------------------------------------------------
		void Init_Rollback();

		//-------------------------------------------------------
		// Info: 세션 참조카운트 내리기, 0이되면 ReleaseSession
		//-------------------------------------------------------
		void DecrementRefcount(Net::stNetSession* pSession);

		//-------------------------------------------------------
		// Info: 악의적으로 1바이트 보내는 유저 or
		//       네트워크 상태 안좋은 유저 거르기위한 
		//	     메시지 완성까지 걸리는 횟수 기록용
		// Return: true (괜찮음), false(끊어야함)
		//-------------------------------------------------------
		bool IncreaseRecvCntForMessage(Net::stNetSession* pSession);

		void ClearRecvCntForMessage(Net::stNetSession* pSession);

		//-------------------------------------------------------
		// Info: refcount == 0일 때 세션 반환
		// 내부에서 세션 Clear 후 스택에 반환, OnRelease() 발동 
		//-------------------------------------------------------
		void ReleaseSession(Net::stNetSession* pSession);

		//-------------------------------------------------------
		// Info: Accept 에서 세션 생성, 진입 전 MaxUser고려하세요
		//-------------------------------------------------------
		Net::stNetSession* InitNewSession(SOCKET newSocket, SOCKADDR_IN* caddr);

		//-------------------------------------------------------
		// Info: SendPacket에서 WSASend를 위해 SendPost()를 우회
		//-------------------------------------------------------
		void SendPQCS(Net::stNetSession* pSession);

		//-------------------------------------------------------
		// Overlapped = 0 일때 수행할 것들,
		// return false면 exit
		//-------------------------------------------------------
		bool IOCP_RequestProc(DWORD message, void* key);
	
		void IOCP_CbTransferred_Zero(Net::stNetSession* pSession, Net::stNetSession::myoverlapped* pOverlapped);
	};




}


#endif