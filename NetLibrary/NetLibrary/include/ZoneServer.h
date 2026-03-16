#ifndef __ZONE_SERVER_H__
#define __ZONE_SERVER_H__
//////////////////////////////////////////////////////////////
// Zone Server
//////////////////////////////////////////////////////////////
#include "NetBase.h"
#include "ZoneSession.h"
#include "TimerJob.h"
#include "NetCrypto.h"
#include "ZoneManager.h"
#include "LockFreeStack.hpp"
#include "LockFreeQueue.hpp"


namespace Net
{



	//------------------------------------------------------
	// 모니터링용 TimerJob
	//------------------------------------------------------
	class CZoneServerMonitoringJob : public Core::CTimerJob
	{
		friend class CZoneServer;
	public:
		uint32 GetRecvMessageTpsAvg() const { return _recvMessageTpsAvg; }
		uint32 GetRecvMessageTps() const { return _recvMessageTps; }
		uint32 GetSendMessageTps() const { return _sendMessageTps; }
		uint32 GetAcceptTps() const { return _acceptTps; }
		time_t GetServerStartTime() const { return static_cast<uint32>(_serverStartTime); }
		uint64 GetTotalAcceptCount() const { return _totalAcceptCount; }
		float  GetWorkerWorkRate(int32 index) const
		{
			if (index < 0 || index >= _workerNum)
				return 0.0f;
			return _workRate[index];
		}

		CZoneServerMonitoringJob();
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
		long _workTime[NET_MAX_THREADS_CNT] = { 0, };
		float _workRate[NET_MAX_THREADS_CNT] = { 0.0f, };
	};

	//////////////////////////////////////////////////////////////
	// CZoneServer
	//////////////////////////////////////////////////////////////
	class CZoneServer
	{
		friend class CZoneTimerJob;
		friend class CZone;
		friend class CZoneManager;
		friend class CReferenceZoneSession;
	public:
#pragma region Structs
		//-------------------------------------------------------
		// 세션 저장 배열
		// stZoneSession*으로 캐스팅해서 배열 사용가능
		//-------------------------------------------------------
		class SessionStructure
		{
			friend class CZoneServer;
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
			stZoneSession* AcquireSession(int* index);
			//-------------------------------------------------------
			// 세션 반환 (클리어 하고 반환하세요)
			// index가 이상하면 false반환
			//-------------------------------------------------------
			bool ReleaseSession(Net::stZoneSession* pSession);
			//-------------------------------------------------------
			// 검색. index가 이상하면 nullptr반환
			//-------------------------------------------------------
			Net::stZoneSession* FindSession(uint64_t sessionId);
		private:
			Net::stZoneSession* _sessionsArray;
			long _sessionCnt;
			int _maxSessionCnt;

			Core::CLockFreeStack<int> _indexStack;
		};
		//-------------------------------------------------------
		// CZoneServer 의 옵션
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

		bool DisconnectZoneZero(uint64_t sessionId);

		//-------------------------------------------------------
		// Info: 센드 요청
		// Param: uint64_t sessionId
		// 
		// 기본 true, 제거는 OnRelease에서 한번 하는것 
		// false는 세션아이디가 잘못되었을 때
		// 실패사유: 잘못된 sessionId, 컨텐츠의 잘못
		//-------------------------------------------------------
		bool SendPacket(uint64_t sessionId, Net::CPacket* pPacket);

		// Zone의 OnMessage전용
		bool SendPacket_Fast(uint64_t sessionId, Net::CPacket* pPacket);

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


		using ZoneServerMonitor = std::shared_ptr<Net::CZoneServerMonitoringJob>;
		//-------------------------------------------------------
		// Info: 각종 모니터링 정보 확인
		// Param: -
		// Return: const ZoneServerMonitor&
		//-------------------------------------------------------
		const ZoneServerMonitor& GetServerMonitor() { return _monitorJob; }

		CZoneManager& GetZoneManager() { return _zoneManager; }

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
		CZoneServer();
		virtual ~CZoneServer() {}

		bool SetUserPointer(uint64_t sessionId, void* userPtr);

		// // FOR ZONE, 참조없이 refcount체크
		// bool IsNeedReleasedByZone(uint64 sessionId)
		// {
		// 	stZoneSession* pSession = FindSession(sessionId);
		// 	if (pSession == nullptr)
		// 		return false;
		// 	return pSession->IsNeedReleasedByZone();
		// }
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
		std::shared_ptr<CZoneServerMonitoringJob> _monitorJob;
		stPacketCrypto _serverCrypto;
		/* 진행 체크용 플래그 */
		long _isCallInit;

		CZoneManager _zoneManager;

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
		void DecrementRefcount(Net::stZoneSession* pSession);

		//-------------------------------------------------------
		// Info: 악의적으로 1바이트 보내는 유저 or
		//       네트워크 상태 안좋은 유저 거르기위한 
		//	     메시지 완성까지 걸리는 횟수 기록용
		// Return: true (괜찮음), false(끊어야함)
		//-------------------------------------------------------
		bool IncreaseRecvCntForMessage(Net::stZoneSession* pSession);

		void ClearRecvCntForMessage(Net::stZoneSession* pSession);

		//-------------------------------------------------------
		// Info: refcount == 0일 때 세션 반환
		// 내부에서 세션 Clear 후 스택에 반환, OnRelease() 발동 
		//-------------------------------------------------------
		void ReleaseSession(Net::stZoneSession* pSession);

		//-------------------------------------------------------
		// Info: Accept 에서 세션 생성, 진입 전 MaxUser고려하세요
		//-------------------------------------------------------
		Net::stZoneSession* InitNewSession(SOCKET newSocket, SOCKADDR_IN* caddr);

		//-------------------------------------------------------
		// Info: SendPacket에서 WSASend를 위해 SendPost()를 우회
		//-------------------------------------------------------
		void SendPQCS(Net::stZoneSession* pSession);

		//-------------------------------------------------------
		// Overlapped = 0 일때 수행할 것들,
		// return false면 exit
		//-------------------------------------------------------
		bool IOCP_RequestProc(DWORD message, void* key);

		void IOCP_CbTransferred_Zero(Net::stZoneSession* pSession, Net::stZoneSession::myoverlapped* pOverlapped);
	
		void ZonePQCS (uint64 zoneId) const;

		void RequestZoneExcute(uint64 zoneId);

		// id이상하면 null반환
		stZoneSession* FindSession(uint64 sessionId)
		{
			return _sessionStructure.FindSession(sessionId);
		}

		
		// FOR PQCS를 우회
		// Core::RingBuffer _sendRequests;
		// HANDLE			 _sendThread;
		// static unsigned int SendThreadProc(void* param);
		// public: int32 GetSendRequestsCnt() { return _sendRequests.GetUseSize() / sizeof(Net::stZoneSession*); }
	};


	class CReferenceZoneSession
	{
	public:
		CReferenceZoneSession(uint64 sessionId, CZoneServer* pZoneServer);
		~CReferenceZoneSession();
		stZoneSession* GetZoneSession() const { return _pZoneSession; }
		bool isAlive() const { return (_pZoneSession != nullptr); }
	private:
		stZoneSession* _pZoneSession;
		CZoneServer* _pZoneServer;
	};
}

#endif