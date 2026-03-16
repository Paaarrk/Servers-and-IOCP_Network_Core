#ifndef __CHAT_SERVER_H__
#define __CHAT_SERVER_H__
#include "NetServer.h"
#include "UserV2_Multi.h"
#include "FieldV2_Multi.h"
#include "CommonProtocol.h"
#include "AuthContainer.h"
#include <thread>
#include <vector>
// #include <unordered_map>

//#define _EVENT_PROFILE

namespace Net
{
	class CPacket;
	class CPacketViewer;
}

constexpr const char* CONFIG_FILE_PATH = "..\\Config\\Config.cnf";
constexpr int SERVER_MONITORING_TICK = 1000;

constexpr const wchar_t* TAG_CONTENTS = L"Contents";
constexpr const wchar_t* TAG_MESSAGE = L"Message";
constexpr int WAIT_LOGIN_TIME = 21000;
constexpr int WAIT_HEARTBEAT_TIME = 40000;

constexpr int MIN_MSG_HEARTBEAT_DELTATIME = 20000;
constexpr int MIN_MSG_MOVE_DELAY = 1;
constexpr int MIN_MSG_CHAT_DELAY = 1;
constexpr int MAX_STRANGE_MESSAGE_COUNT = 20;
constexpr int MAX_MSG_CHAT_BYTE_LEN = 510;

class CWhiteList
{
public:
	void AddWhiteList(in_addr whiteIp)
	{
		_whiteList.push_back(whiteIp);
	}
	bool IsWhiteIp(in_addr ip)
	{
		for (in_addr& wip : _whiteList)
		{
			if (wip.s_addr == ip.s_addr)
				return true;
		}

		return false;
	}

private:
	std::vector<in_addr> _whiteList;
};


class CChatServerMonitoringJob : public Core::CTimerJob
{
public:
	virtual void Excute();
	void IncreaseLoginCount()
	{
		++_totalLoginCount;
	}

	void IncreaseLoginPacketCount()
	{
		++_loginRecvCnt;
	}

	void IncreaseMoveSectorCount()
	{
		++_moveSectorRecvCnt;
	}

	void IncreaseChatCount()
	{
		++_chatRecvCnt;
	}

	void IncreaseHeartBeatCount()
	{
		++_heartBeatRecvCnt;
	}

	void IncreaseDuplicateDisconnectCnt()
	{
		++_duplicateDisconnectCnt;
	}


	uint32_t GetLoginRecvTps() const { return _loginRecvTps; }
	uint32_t GetMoveSectorRecvTps() const { return _moveSectorRecvTps; }
	uint32_t GetChatRecvTps() const { return _chatRecvTps; }
	uint32_t GetHeartbeatRecvTps() const { return _heartBeatRecvTps; }
	uint64_t GetTotalLoginCount() const { return _totalLoginCount.load(std::memory_order_relaxed); }
	uint64_t GetDuplicateDisconnectCnt() const { return _duplicateDisconnectCnt; }
	CChatServerMonitoringJob();

private:
	DWORD _startTime = 0;
	std::atomic_uint64_t _totalLoginCount;
	std::atomic_uint32_t _duplicateDisconnectCnt;

	std::atomic_uint32_t _loginRecvCnt;
	std::atomic_uint32_t _moveSectorRecvCnt;
	std::atomic_uint32_t _chatRecvCnt;
	std::atomic_uint32_t _heartBeatRecvCnt;

	int32_t _loginRecvTps = 0;
	int32_t _moveSectorRecvTps = 0;
	int32_t _chatRecvTps = 0;
	int32_t _heartBeatRecvTps = 0;
};

////////////////////////////////////////////////////////////////////////
// ChatServer
// < V3.1 : 주변유저 긁어오는 버퍼 변경 >
// . malloc /free -> 메모리풀
// * 최종 정적 tls사용
// => 이유: 바로 쓰고 버려서 생성과 삭제(사용과 반환)가 오버헤드
//          다른 곳으로 넘길 일이 없음
//          해당 스레드가 혼자 얻어와서 혼자 쓰고 버림
// 
// < V3 : 멀티스레드 채팅 서버 >
// . 워커스레드가 모두 해결하는 구조의 채팅서버를 만들어 보자
// 
// < V2.1 : 핫패스 최적화 (25.11.19) >
// . 하트비트 타임아웃 검사는 '하트비트 패킷' 으로만 한다.
// . 이를 위해 매 패킷마다 리스트 뒤로 이동하는 것이 아닌,
//   하트비트 패킷이 왔을 때에만 이동시킨다.
// . 정상 유저처럼 일반적인 패킷만 보내고, 하트비트를 안보내는
//   유저는 자연스럽게 끊긴다.
// 
// 
// . 이후 컨텐츠 스레드를 손보면서 여기도 최적화 할 예정
// 
// < V2 >
// . 전체적인 컨텐츠 관리를 채팅 스레드가 전담하기 위해
//   (타임아웃, 연결만 한 유저) 일부 수정이 들어갔습니다.
// 
// < V1 >
// . 싱글스레드 채팅서버 입니다.
////////////////////////////////////////////////////////////////////////
class CChatServer:public Net::CServer
{
public:
	enum enChatServer
	{
		PQCS_REQUEST_REDIS = 0,
		PQCS_IDENTIFYING,
		DEFAULT1,
		DEFAULT2,
		DEFAULT3,
		DEFAULT4,
	};
	
	struct stChatServerOpt :public Net::CServer::stServerOpt
	{
		int32 iMaxUsers;
		int32 whiteNum;
		char ip[20][16];

		char redisIP[16];
		uint16 redisPort;
		int32 authThreadCreateNum;
		int32 authThreadRunNum;
		bool LoadOption(const char* path = CONFIG_FILE_PATH);
	};

	virtual bool OnInit(const stServerOpt* pSopt);
	virtual void OnStop();
	virtual void OnExit();

	virtual bool OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip);
	virtual bool OnConnectionRequest(in_addr ip);
	virtual void OnRelease(uint64_t sessionId);
	virtual void OnMessage(uint64_t sessionId, Net::CPacket* pPacket, int len);
	virtual void OnWorkerStart();
	virtual void OnWorkerEnd();
	virtual void OnUserEvent(Net::CPacket* pPacket);
	virtual void OnSend(uint64_t sessionId, bool isValid);

	CChatServer() :_useWhite(1), _hEventTimeoutCheckThread(NULL), _maxUser(0),
		_hEventForExit(NULL), _timeoutOff(0)
	{

	}

	bool GetUsingWhite() const { return _useWhite; }
	void ToggleUseWhite()
	{
		if (_useWhite)
			_useWhite = 0;
		else
			_useWhite = 1;
	}
	bool GetUsingTimeoutOff() { return _timeoutOff.load(); }

	//-----------------------------------------------------------------
	// 미로그인/ 타임아웃 체크 스레드
	//-----------------------------------------------------------------
	static void TimeoutCheckThread(CChatServer* nowServer);

	void ToggleUseTimeout()
	{
		if (_timeoutOff.load() == false)
			_timeoutOff.store(true);
		else
			_timeoutOff.store(false);
	}


	//-----------------------------------------------------------------
	// Request Proc
	// 내부 SendPacket호출 때문에 여기 종속됨..
	//-----------------------------------------------------------------
	bool RequestProc(uint64 sessionId, Net::CPacketViewer* pPacket);

	bool RequestLogin(uint64 sessionId, Net::CPacketViewer* pPacket);
	bool RequestSectorMove(uint64 sessionId, Net::CPacketViewer* pPacket);
	bool RequestMessage(uint64 sessionId, Net::CPacketViewer* pPacket);
	bool RequestHeartbeat(uint64 sessionId, Net::CPacketViewer* pPacket);
	bool RequestDefault(uint64 sessionId, Net::CPacketViewer* pPacket);

	using ChatMonitor = const std::shared_ptr<CChatServerMonitoringJob>;
	ChatMonitor& GetChatMonitor() { return _monitoringJob; }
private:
	bool _useWhite;
	int _maxUser;
	HANDLE _hEventTimeoutCheckThread;
	HANDLE _hEventForExit;
	std::thread _timeoutCheckThread;
	std::shared_ptr<CChatServerMonitoringJob> _monitoringJob;

	CWhiteList _whiteList;

	std::atomic_bool _timeoutOff;
};

extern CUserManager g_userManager;
extern CAuthContainer g_redisAuth;

// 주변 유저 얻어오는 버퍼
extern thread_local CAroundSessionId* tls_pBuffer;

#endif