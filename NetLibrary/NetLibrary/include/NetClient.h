#ifndef __NET_CLIENT_H__
#define __NET_CLIENT_H__
#include "NetBase.h"
#include "TimerJob.h"
#include "NetSession.h"
#include "NetCrypto.h"
#include "LockFreeStack.hpp"
#include "LockFreeQueue.hpp"
namespace Net
{
	//------------------------------------------------------
	// 모니터링용 TimerJob
	//------------------------------------------------------
	class CClientMonitoringJob : public Core::CTimerJob
	{
		friend class CClient;
	public:
		uint32 GetRecvMessageTps() const { return _recvMessageTps; }
		uint32 GetSendMessageTps() const { return _sendMessageTps; }
		uint32 GetConnectCnt() const { return _connectCnt; }

		CClientMonitoringJob();
	private:
		virtual void Excute();

		void IncreaseConnectCount()
		{
			_InterlockedIncrement(&_connectCnt);
		}

		void IncreaseSendMessageCount()
		{
			_InterlockedIncrement(&_sendMessageCount);
		}

		void IncreaseRecvMessageCount()
		{
			_InterlockedIncrement(&_recvMessageCount);
		}

	private:
		DWORD  _startTime = 0;
		uint32 _connectCnt = 0;
		uint32 _recvMessageCount = 0;
		uint32 _sendMessageCount = 0;
		uint32 _recvMessageTps = 0;
		uint32 _sendMessageTps = 0;
	};

	class CClient
	{
	public:
#pragma region Structs
		//-------------------------------------------------------
		// CServer 의 옵션
		//-------------------------------------------------------
		struct stClientOpt
		{
			bool	bUseEncode;
			bool	bUseBind;
			wchar_t	bindIP[16];
			uint16	bindPort;
			wchar_t targetIP[16];
			uint16	targetPort;
			int32	iWorkerThreadRunCnt;
			int32	iWorkerThreadCreateCnt;
			bool	bUseTCP_NODELAY;
			bool	bUseSO_SNDBUF;

			uint8	client_code;
			uint8	static_key;
		};
#pragma endregion

		//-------------------------------------------------------
		// Info: 클라이언트 세팅 및 시작 준비
		// Param: stServerOpt* 옵션 (서버쪽에 stServerOpt부분만 복사본 가짐)
		//        stServerOpt를 상속받은 구조체를 넣어도 됩니다 (OnInit()에 활용)
		// Return: 실패시 false (종료해야함), 성공시 true
		//-------------------------------------------------------
		bool Init(const stClientOpt* pOpt);
		//-------------------------------------------------------
		// Info: 클라이언트 연결 (시도 횟수)
		// Param: 시도횟수 (-1)이면 무한
		// Return: 실패시 false, 성공시 true
		//-------------------------------------------------------
		bool Connect(int32 count);

		//-------------------------------------------------------
		// Info: 연결 종료
		//-------------------------------------------------------
		void Disconnect();

		//-------------------------------------------------------
		// Info: 센드 요청
		// Param: uint64_t sessionId
		// 
		// 기본 true, 제거는 OnRelease에서 한번 하는것 
		// false는 세션아이디가 잘못되었을 때
		// 실패사유: 잘못된 sessionId, 컨텐츠의 잘못
		//-------------------------------------------------------
		bool SendPacket(Net::CPacket* pPacket);


		void Exit();

		// 상속자 구현 함수 ==================//

		virtual bool OnInit() = 0;
		virtual void OnExit() = 0;

		virtual void OnEnterJoinServer() = 0;
		virtual void OnLeaveServer() = 0;
		virtual void OnMessage(Net::CPacket* pPacket, int len) = 0;
		
		// 생성자
		CClient();
		virtual ~CClient() {}

		using ClientMonitor = std::shared_ptr<Net::CClientMonitoringJob>;
		const ClientMonitor& GetClientMonitor() { return _monitoringJob; }
	private:
		stNetSession	_session;
		stClientOpt		_option;

		long			_isCallInit;
		long			_isExit;
		stPacketCrypto _clientCrypto;

		HANDLE			_hThreads[NET_MAX_THREADS_CNT];
		HANDLE			_hIOCP;
		int32			_threadNum;

		uint64			_sid;
		std::shared_ptr<CClientMonitoringJob> _monitoringJob;

		CWSAStart		_wsa;
		//-------------------------------------------------------
		// Info: 워커 스레드 function
		//-------------------------------------------------------
		static unsigned int NetClientWorkerProc(void* param);

		//-------------------------------------------------------
		// Info: 소켓, 핸들 등의 자원 정리 (초기화) - Init에서
		//-------------------------------------------------------
		void Init_Rollback();

		//-------------------------------------------------------
		// Info: 세션 참조카운트 내리기, 0이되면 ReleaseSession
		//-------------------------------------------------------
		void DecrementRefcount();

		//-------------------------------------------------------
		// Info: refcount == 0일 때 세션 반환
		// 내부에서 세션 Clear 후 스택에 반환, OnRelease() 발동 
		//-------------------------------------------------------
		void ReleaseSession();

		//-------------------------------------------------------
		// Info: Accept 에서 세션 생성, 진입 전 MaxUser고려하세요
		//-------------------------------------------------------
		bool InitSession();

		void InitMonitoringJob();
		void ExitMonitoringJob();

		//-------------------------------------------------------
		// Info: SendPacket에서 WSASend를 위해 SendPost()를 우회
		//-------------------------------------------------------
		void SendPQCS();

		bool IOCP_RequestProc(DWORD message, void* key);

		void IOCP_CbTransferred_Zero(Net::stNetSession* pSession, Net::stNetSession::myoverlapped* pOverlapped);
	};
}

#endif