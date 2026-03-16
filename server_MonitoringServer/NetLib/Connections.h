#ifndef __CONNECTIONS_H__
#define __CONNECTIONS_H__
#include <stdint.h>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include "CommonProtocol.h"
#include "TLSObjectPool_IntrusiveList_V2.h"


struct stTypeInfo
{
	int count = 0;
	int timeStamp = -1;
	int avr = 0;
	int min = 2100000000;
	int max = -1;
	void clear()
	{
		count = 0;
		timeStamp = -1;
		avr = 0;
		min = 2100000000;
		max = -1;
	}
	void set(int _value, int _timeStamp)
	{
		if (timeStamp == -1)
			timeStamp = _timeStamp;
		int befcount = count++;
		avr = (avr * befcount + _value) / count;
		if (min > _value)
			min = _value;
		if (max < _value)
			max = _value;
	}
};

struct stServerMonitorInfo
{
	int serverNo = -1;
	int startNum = 0;
	int endIndex = 0;
	stTypeInfo data[NEED_MAX_INDEX] = {};
};

struct stConnection
{
	enum enConnection
	{
		STATUS_NONE = 0,
		STATUS_WAIT_LOGIN,
		STATUS_CLIENT,
		STATUS_SERVER_LOGIN,
		STATUS_SERVER_GAME,
		STATUS_SERVER_CHAT,
		STATUS_SERVER_MONITOR,
		
		POOL_KEY = 0x000F'E245
	};

	std::shared_mutex lock;
	uint64_t sessionId = 0;
	int status = STATUS_NONE;
	int serverNo = -1;
	DWORD connectTime = 0;
	DWORD recvTime = 0;
	stTypeInfo info[NEED_MAX_INDEX] = {};

	void clear()
	{
		sessionId = 0;
		status = STATUS_NONE;
		serverNo = -1;
		connectTime = 0;
		recvTime = 0;
		for (int i = 0; i < NEED_MAX_INDEX; i++)
			info[i].clear();
	}

	void clearInfo()
	{
		for (int i = 0; i < NEED_MAX_INDEX; i++)
			info[i].clear();
	}

	static stConnection* Alloc()
	{
		stConnection* pConn = s_pool.Alloc();
		return pConn;
	}
	static int Release(stConnection* pConn)
	{
		return s_pool.Free(pConn);
	}

	static int GetPoolUseChunk()
	{
		return s_pool.GetAllocChunkPoolCreateNum();
	}
	static int GetPoolSize()
	{
		return s_pool.GetAllocChunkPoolSize();
	}

	static CTlsObjectPool<stConnection, POOL_KEY, TLS_OBJECTPOOL_USE_CALLONCE> s_pool;
};


class c_syslog;
class CConnectServer;
class CClientServer;

constexpr const wchar_t* WFORMAT_DB_LOG_TABLE_NAME = L"monitorlog_%02d%02d";
constexpr const wchar_t* WFORMAT_DB_LOG_DATETIME = L"%04d-%02d-%02d %02d:%02d:%02d";
constexpr const wchar_t* WFORMAT_DB_LOG_WRITE_LOG = L"INSERT INTO %s (log_logtime, log_serverno, log_type, log_avr_m, log_min_m, log_max_m, log_count) VALUES (\'%s\', %d, %d, %d, %d, %d, %d);";


class CConnectionManager
{
public:
	enum enConnectionManager
	{
		CHECK_TIMEOUT_PERIOD = 5000,
		CONNECT_SERVER_TIMEOUT = 10000,
		CLIENT_WAIT_LOGIN_TIMEOUT = 10000,
	};

	CConnectionManager(int maxServerConn = 0, int maxClientConn = 0, c_syslog* pLog = nullptr,
		CConnectServer* pConnectServer = nullptr, CClientServer* pClientServer = nullptr)
		:_maxClient(maxClientConn), _pLog(pLog), _pClientServer(pClientServer), _pConnectServer(pConnectServer)
	{
		if(maxServerConn != 0)
			_serverMap.rehash(maxServerConn);
		if(maxClientConn)
			_clientMap.rehash(maxClientConn);
	}
	~CConnectionManager()
	{
		if (_thTimeout.joinable())
			_thTimeout.join();
	}

	// 타임아웃 스레드가 이때 On
	void Init(int maxServerConn, int maxClientConn, CConnectServer* pConnServer, CClientServer* pClientServer, c_syslog* pLog)
	{
		_maxClient = maxClientConn;
		_serverMap.reserve(maxServerConn);
		_serverNoMap.reserve(maxServerConn);
		_clientMap.reserve(maxClientConn);
		_pConnectServer = pConnServer;
		_pClientServer = pClientServer;
		_pLog = pLog;

		_thTimeout = std::thread(&CConnectionManager::TimeOutFunc, this);
	}
	//////////////////////////////////////////////////////////////////
	//-------- Server --------
	//////////////////////////////////////////////////////////////////

	void CreateServerConnection(uint64_t sessionId);

	//---------------------------------------------------
	// 반환: true (지울 게 없음) 
	//       false (Disconnect필요한 세션아이디)
	//---------------------------------------------------
	bool LoginServerConnection(uint64_t sessionId, int ServerNo, uint64_t& needDisconnectSessionId);
	
	//---------------------------------------------------
	// 반환: 현재 업데이트된 ServerNo
	//       서버가 모종의이유로 끊어졌으면 -1반환
	//---------------------------------------------------
	int UpdateServerConnection(uint64_t sessionId, BYTE dataType, int dataValue, int timeStamp);

	void ReleaseServerConnection(uint64_t sessionId);

	//---------------------------------------------------
	// 반환: cnt > 0이면 true, cnt == 0 이면 false
	//       pBuffer에서 순회해서 RUN패킷 뿌리면된다.
	//       serverNo로 어떤 서버인지구분 가능
	//---------------------------------------------------
	bool GetServerNosOfConnectedServer(int* pBuffer, int& cnt);

	std::vector<std::unique_ptr<stServerMonitorInfo>> GetServerStatusSnapshot();

	int GetServerCnt() { return static_cast<int>(_serverMap.size()); }

	//////////////////////////////////////////////////////////////////
	//-------- Client --------
	//////////////////////////////////////////////////////////////////

	void CreateClientConnection(uint64_t sessionId);

	//---------------------------------------------------
	// 반환: true (성공)
	//       false (끊어야함, Disconnect(sessionId)필요)
	//---------------------------------------------------
	bool LoginClientConnection(uint64_t sessionId, const char* loginKey, int len);

	void ReleaseClientConnection(uint64_t sessionId);

	//---------------------------------------------------
	// 반환: 접속된 모니터링 클라이언트가 없으면 false
	//       하나라도 접속되어있으면 true반환
	//       * 접속되어있으면 버퍼에 갯수만큼 sessionId채움
	//---------------------------------------------------
	bool GetMonitoringClientSessionIds(uint64_t* pBuffer, int& cnt);

	int GetClientCnt() { return static_cast<int>(_clientMap.size()); }

	int GetLoginedClientCnt() { return _loginedClientNum.load(); }

	CConnectServer& GetConnectServer() { return *_pConnectServer; }
	CClientServer& GetClientServer() { return *_pClientServer; }

	//////////////////////////////////////////////////////////////////
	// TimeOut 
	//////////////////////////////////////////////////////////////////

	//----------------------------------------------------------------
	// 락을 걸어서 정확한 시간을 보기 보다는, 간단히 본 찰나로 판단하게 
	// 하였다. 시간을 널널하게 잡았기 때문에 저 상황이 오는것 자체가 흔치
	// 않은 상황이기 때문이다. 서버의 연결 끊김에 대한 잔여 연결(rst가 
	// 유실되서)이거나 연결만 한 클라이언트 이거나
	//----------------------------------------------------------------
private: void TimeOutFunc();

private:
	std::shared_mutex _serverLock;
	std::unordered_map<uint64_t, stConnection*> _serverMap;
	std::unordered_map<int, uint64_t> _serverNoMap;

	std::shared_mutex _clientLock;
	std::atomic_int32_t _loginedClientNum;
	int _maxClient;
	std::unordered_map<uint64_t, stConnection*> _clientMap;

	CConnectServer* _pConnectServer;
	CClientServer* _pClientServer;
	c_syslog* _pLog;

	std::thread _thTimeout;
};




#endif