#ifndef  __MONITORING_SERVER_H__
#define  __MONITORING_SERVER_H__

#include "NetlibV8.h"
#include "Connections.h"
#include "DBConnector.h"
#include <queue>
#include <mutex>

constexpr const char* OPTION_PATH = "..\\Config\\Config.cnf";
constexpr const wchar_t* TAG_CONTENTS = L"Monitor";

constexpr const char* SESSION_KEY = "ajfw@!cv980dSZ[fje#@fdj123948djf";
constexpr const int SESSION_KEY_LEN = 32;

constexpr const int MONITOR_SAVE_SECONDS = 60;

class CConnectServer: public CServer
{
public:
	struct stConnectServerOpt :public CServer::stServerOpt
	{
		char mysqlIP[16];
		unsigned short mysqlPort;
		char mysqlId[20];
		char mysqlPw[40];
		char mysqlSchema[20];
	};

	/* ÄÁÅÙÃ÷¿¡¼­ ±¸ÇöÇØ Áà¾ß ÇÏ´Â ÇÔ¼öµé */
	bool OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip);
	bool OnConnectionRequest(in_addr ip);
	void OnRelease(uint64_t sessionId);
	void OnMessage(uint64_t sessionId, CPacket* pPacket, int len);
	void OnWorkerStart();
	void OnWorkerEnd();
	void OnUserEvent(CPacket* pPacket);

	bool OnInit(const stServerOpt* pOpt);
	void OnStop();
	void OnExit();
	void OnSend(uint64_t sessionId, bool isValid);

	void OnMonitor();

	//////////////////////////////////////////////////////////////
	// Functions
	//////////////////////////////////////////////////////////////

	CServer::stServerOpt* GetOption()
	{
		return &_connectServerOption;
	}

	bool LoadOption(const char* path = OPTION_PATH);

private: void ServerStatusSnapshot();

private: void ServerComputerStatusUpdate();

public:

	//////////////////////////////////////////////////////////////
	// Protocols
	//////////////////////////////////////////////////////////////

	void RequestProc(uint64_t sessionId, CPacketViewer& refPacket);

	void RequestServerLogin(uint64_t sessionId, CPacketViewer& refPacket);

	void RequestDataUpdate(uint64_t sessionId, CPacketViewer& refPacket);

	//////////////////////////////////////////////////////////////
	// DB
	//////////////////////////////////////////////////////////////
	static unsigned int DBWriterProc(void* param);

	static void DBWriteLog(CMySqlConnector& conn, std::unique_ptr<stServerMonitorInfo>& uqptrInfo);

	int DBQueueSize() { return (int)_dbLogQueue.size(); }
	

private:
	int _monitorCount = 0;
	stTypeInfo _serverStatusInfo[NEED_MAX_INDEX];

	stConnectServerOpt _connectServerOption = {};
	HANDLE _hDBThread = 0;
	HANDLE _hDBEvent = 0;
	CMySqlConnector _connector;
	std::queue<std::unique_ptr<stServerMonitorInfo>> _dbLogQueue;
	std::mutex _dbLock;
};

class CClientServer: public CServer
{
public:
	struct stClientServerOpt :public CServer::stServerOpt
	{
		int iMaxUsers = 0;
	};
	/* ÄÁÅÙÃ÷¿¡¼­ ±¸ÇöÇØ Áà¾ß ÇÏ´Â ÇÔ¼öµé */
	virtual bool OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip);
	virtual bool OnConnectionRequest(in_addr ip);
	virtual void OnRelease(uint64_t sessionId);
	virtual void OnMessage(uint64_t sessionId, CPacket* pPacket, int len);
	virtual void OnWorkerStart();
	virtual void OnWorkerEnd();
	virtual void OnUserEvent(CPacket* pPacket);

	virtual bool OnInit(const stServerOpt* pOpt);
	virtual void OnStop();
	virtual void OnExit();
	virtual void OnSend(uint64_t sessionId, bool isValid);

	virtual void OnMonitor();

	CServer::stServerOpt* GetOption()
	{
		return &_clientServerOpt;
	}
	bool LoadOption(const char* path = OPTION_PATH);

	//////////////////////////////////////////////////////////////
	// Protocols
	//////////////////////////////////////////////////////////////

	void RequestProc(uint64_t sessionId, CPacketViewer& refPacket);

	void RequestClientLogin(uint64_t sessionId, CPacketViewer& refPacket);
private:
	stClientServerOpt _clientServerOpt;
};


extern CConnectionManager g_manager;
#endif 
