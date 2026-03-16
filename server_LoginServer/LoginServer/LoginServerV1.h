#ifndef __LOGIN_SERVER_H__
#define __LOGIN_SERVER_H__

#include "NetServer.h"
#include "UserV2_Multi.h"
#include "CommonProtocol.h"
#include <thread>
#include <vector>
#include "RedisConnector.h"
#include "DBConnector.h"

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
constexpr int WAIT_SEND_TIME = 5000;

constexpr const char* SESSIONKEY_LIMIT_MS = "90";

enum en_LOGINSERVER_EVENT
{
	RequestMySql = 0,
	RequestRedis = 1,
};

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


class CLoginServerMonitoringJob : public Core::CTimerJob
{
public:
	virtual void Excute();
	void IncreaseLoginCount()
	{
		++_totalLoginCount;
		++_loginSuccessCnt;
	}

	void IncreaseLoginSuccessCount()
	{
		++_loginSuccessCnt;
	}

	void IncreaseDuplicateDisconnectCnt()
	{
		++_duplicateDisconnectCnt;
	}


	uint32_t GetLoginSuccessTps() const { return _loginRecvTps; }
	uint64_t GetTotalLoginCount() const { return _totalLoginCount.load(std::memory_order_relaxed); }
	uint64_t GetDuplicateDisconnectCnt() const { return _duplicateDisconnectCnt; }
	CLoginServerMonitoringJob();

private:
	DWORD _startTime = 0;
	std::atomic_uint64_t _totalLoginCount;
	std::atomic_uint32_t _duplicateDisconnectCnt;

	std::atomic_uint32_t _loginSuccessCnt;

	int32_t _loginRecvTps = 0;
};

////////////////////////////////////////////////////////////////////////
// LoginServer
// < V1.1 : 레디스 모듈화 >
// . cpp_redis는 연결실패하면 예외를 던지는 것을 이용해 무한 접속시도
// . 패킷을 받아서 set할때는 3번시도 후 실패하면 연결끊음
//   (나중에 재연결 시도를 노림)
// 
// < V1 >
// . 멀티스레드 로그인 서버
////////////////////////////////////////////////////////////////////////
class CLoginServer:public Net::CServer
{
public:
	struct stLoginServerOpt :public Net::CServer::stServerOpt
	{
		wchar_t gameIP[16] = {};
		unsigned short gamePort = 0;
		wchar_t chatIP[16] = {};
		unsigned short chatPort = 0;
		wchar_t chatIPforDummy1[16] = {};
		wchar_t chatIPforDummy2[16] = {};
		wchar_t chatIPforDummy3[16] = {};

		//redis
		std::string redisIP;
		unsigned short redisPort = 0;

		//mysql
		char mysqlIP[16] = {};
		unsigned short mysqlPort = 0;
		std::string mysqlId;
		std::string mysqlPw;
		std::string mysqlSchema;

		int iMaxUsers = 0;
		int whiteNum = 0;
		char ip[20][16] = {};

		in_addr dummy1 = {};
		in_addr dummy2 = {};
		in_addr dummy3 = {};

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

	CLoginServer() :_useWhite(1), _hEventTimeoutCheckThread(NULL), 
		_hEventForExit(NULL), _maxUser(0), _timeoutOff(0)
	{

	}

	bool GetUsingWhite() { return _useWhite; }
	void ToggleUseWhite()
	{
		if (_useWhite)
			_useWhite = 0;
		else
			_useWhite = 1;
	}
	bool GetUsingTimeoutOff() { return _timeoutOff.load(); }
	void ToggleUseTimeout()
	{
		if (_timeoutOff.load() == false)
			_timeoutOff.store(true);
		else
			_timeoutOff.store(false);
	}

	//-----------------------------------------------------------------
	// 미로그인/ 타임아웃 체크 스레드
	//-----------------------------------------------------------------
	static void TimeoutCheckThread(CLoginServer* nowServer);

	//-----------------------------------------------------------------
	// Request Proc
	// 내부 SendPacket호출 때문에 여기 종속됨..
	//-----------------------------------------------------------------
	bool RequestProc(uint64_t sessionId, Net::CPacketViewer& refPacket);
	bool RequestLogin(uint64_t sessionId, Net::CPacketViewer& refPacket);
	bool RequestDefault(uint64_t sessionId, Net::CPacketViewer& refPacket);
	
	using LoginMonitor = const std::shared_ptr<CLoginServerMonitoringJob>;
	LoginMonitor& GetLoginMonitor() { return _monitoringJob; }

	stLoginServerOpt& GetOption() { return _option; }
private:
	bool					_useWhite;
	int						_maxUser;
	HANDLE					_hEventTimeoutCheckThread;
	HANDLE					_hEventForExit;
	std::thread				_timeoutCheckThread;
	std::shared_ptr<CLoginServerMonitoringJob> _monitoringJob;
	CWhiteList				_whiteList;
	std::atomic_bool		_timeoutOff;
	stLoginServerOpt		_option;
	CRedisConnector			_redisConn;
	CTlsMySqlConnector		_mysqlConn;
};


extern CUserManager g_userManager;


#endif