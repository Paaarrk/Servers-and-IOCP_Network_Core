#ifndef __NET_SESSION_H__
#define __NET_SESSION_H__
#include "NetBase.h"
#include "LockFreeQueue.hpp"


namespace Net
{
	/////////////////////////////////////////////////////////////////
	// Net Session
	/////////////////////////////////////////////////////////////////
	struct stNetSession
	{
		enum enSession
		{
			SERVER_SEND_WSABUF_MAX = 200,
			SEND_Q_MAX_SIZE = 1000,
		};
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
		unsigned long refcount;
		uint64_t sessionId;
		int32_t recvCntForMessage;		//악의적으로 1바이트씩 보내는 유저 or 네트워크 이상한 유저 거르기 위해서
		Net::CPacket* recvQ;
		Core::CLockFreeQueue<Net::CPacket*> sendQ;
		myoverlapped recvOl;
		myoverlapped sendOl;
		long isSending;	// 1: 센딩중, 0: 센드안하는중
		long isDisconnect;	// 0: 사용중, 1: 디스커넥트 예정
		wchar_t ip[16];
		uint16_t port;
		Net::CPacket** sendPackets;
		int sendPacketsCnt;
		stNetSession();
		~stNetSession();
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
	};
}


#endif