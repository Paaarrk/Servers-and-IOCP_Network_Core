#include "MonitoringServer.h"
#include "21_TextParser.h"

CConnectionManager g_manager;

////////////////////////////////////////////////////////////////////
// ConnectServer
////////////////////////////////////////////////////////////////////

#pragma region ConnectServer

#pragma region Event
bool CConnectServer::OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip)
{
	g_manager.CreateServerConnection(sessionId);
	return true;
}
bool CConnectServer::OnConnectionRequest(in_addr ip)
{

	return true;
}
void CConnectServer::OnRelease(uint64_t sessionId)
{
	g_manager.ReleaseServerConnection(sessionId);
}
void CConnectServer::OnMessage(uint64_t sessionId, CPacket* pPacket, int len)
{
	// LAN서버라서 len은 신뢰하자, 문제잇으면 패킷문제
	CPacketViewer refPacket;
	refPacket.SetView(pPacket, len);
	RequestProc(sessionId, refPacket);
}
void CConnectServer::OnWorkerStart()
{

}
void CConnectServer::OnWorkerEnd()
{

}
void CConnectServer::OnUserEvent(CPacket* pPacket)
{

}

bool CConnectServer::OnInit(const stServerOpt* pOpt)
{
	_hDBEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (_hDBEvent == NULL)
	{
		CServer::GetLog()->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"db큐용 이벤트 생성 실패");
		return false;
	}

	_connector.SetConnector(_connectServerOption.mysqlIP,
		_connectServerOption.mysqlId, _connectServerOption.mysqlPw,
		_connectServerOption.mysqlSchema, _connectServerOption.mysqlPort);
	_connector.SetLog(CServer::GetLog());

	_hDBThread = (HANDLE)_beginthreadex(NULL, NULL, CConnectServer::DBWriterProc,
		this, 0, NULL);
	if (_hDBThread == NULL)
	{
		CServer::GetLog()->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"db writer thread 생성 실패");
	}
	

	return true;
}
void CConnectServer::OnStop()
{

}
void CConnectServer::OnExit()
{
	uint64_t buffer[50];
	int32_t cnt;
	g_manager.GetMonitoringClientSessionIds(buffer, cnt);
	for (int i = 0; i < cnt; i++)
		Disconnect(buffer[i]);


	WaitForSingleObject(_hDBThread, INFINITE);
	CloseHandle(_hDBEvent);

}
void CConnectServer::OnSend(uint64_t sessionId, bool isValid)
{

}

void CConnectServer::OnMonitor()
{
	_monitorCount++;
	ServerComputerStatusUpdate();

	if(_monitorCount == MONITOR_SAVE_SECONDS)
	{
		ServerStatusSnapshot();

		_monitorCount = 0;
	}
}
#pragma endregion

#pragma region Functions

bool CConnectServer::LoadOption(const char* path)
{
	stConnectServerOpt& opt = _connectServerOption;
	CParser config(path);
	if (config.LoadFile() == false)
	{
		wprintf_s(L"Config.cnf 파일 로드 오류");
		return true;
	}
	opt.bUseEncode = false;

	int bUseSO_SNDBUF;
	config.GetValue("MONITOR_SERVER", "bUseSO_SNDBUF", &bUseSO_SNDBUF);
	opt.bUseSO_SNDBUF = (bool)bUseSO_SNDBUF;

	int bUseTCP_NODELAY;
	config.GetValue("MONITOR_SERVER", "bUseTCP_NODELAY", &bUseTCP_NODELAY);
	opt.bUseTCP_NODELAY = (bool)bUseTCP_NODELAY;

	int iMaxConcurrentUsers;
	config.GetValue("MONITOR_SERVER", "serveriMaxConcurrentUsers", &iMaxConcurrentUsers);
	opt.iMaxConcurrentUsers = iMaxConcurrentUsers;

	int iWorkerThreadCreateCnt;
	config.GetValue("MONITOR_SERVER", "serveriWorkerThreadCreateCnt", &iWorkerThreadCreateCnt);
	opt.iWorkerThreadCreateCnt = iWorkerThreadCreateCnt;

	int iWorkerThreadRunCnt;
	config.GetValue("MONITOR_SERVER", "serveriWorkerThreadRunCnt	", &iWorkerThreadRunCnt);
	opt.iWorkerThreadRunCnt = iWorkerThreadRunCnt;

	char openIP[16];
	config.GetValue("MONITOR_SERVER", "serverOpenIP", openIP);
	MultiByteToWideChar(CP_ACP, 0, openIP, 16, opt.openIP, 16);

	int port;
	config.GetValue("MONITOR_SERVER", "serverPort", &port);
	opt.port = (unsigned short)port;

	// DB
	config.GetValue("DB_MYSQL_LOGDB", "mysqlIP", opt.mysqlIP);
	int mysqlport;
	config.GetValue("DB_MYSQL_LOGDB", "mysqlPort", &mysqlport);
	opt.mysqlPort = (unsigned short)mysqlport;
	config.GetValue("DB_MYSQL_LOGDB", "mysqlId", opt.mysqlId);
	config.GetValue("DB_MYSQL_LOGDB", "mysqlPw", opt.mysqlPw);
	config.GetValue("DB_MYSQL_LOGDB", "mysqlSchema", opt.mysqlSchema);
	
	return true;
}

void CConnectServer::ServerStatusSnapshot()
{
	std::vector<std::unique_ptr<stServerMonitorInfo>> v_monitorInfo = g_manager.GetServerStatusSnapshot();
	std::mutex& dbLock = _dbLock;
	auto it_end = v_monitorInfo.end();

	// 모니터링 서버에서 측정하는 현재 서버컴퓨터 상황
	std::unique_ptr<stServerMonitorInfo> uqptrCurServerStatus = std::make_unique<stServerMonitorInfo>();
	uqptrCurServerStatus->serverNo = en_SERVER_NO::System;
	uqptrCurServerStatus->endIndex = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_MONITOR_END
		- en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL;
	uqptrCurServerStatus->startNum = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL;
	memcpy(uqptrCurServerStatus->data, _serverStatusInfo, sizeof(stTypeInfo) * (uqptrCurServerStatus->endIndex));


	for (int i = 0; i < NEED_MAX_INDEX; i++)
		_serverStatusInfo[i].clear();

	{
		std::lock_guard<std::mutex> guard(dbLock);
		for (auto it = v_monitorInfo.begin(); it != it_end; ++it)
		{
			_dbLogQueue.push(std::move(*it));
		}

		_dbLogQueue.push(std::move(uqptrCurServerStatus));
	}
	

	SetEvent(_hDBEvent);
}

void CConnectServer::ServerComputerStatusUpdate()
{
	stServerMonitorViewer* pView = CServer::GetMonitor();
	
	int curtimestamp = static_cast<int>(time(NULL));

	int cputotal = static_cast<int>(pView->fProcessorTotal);
	int nonpagedBytes = static_cast<int>(pView->llNonpagedBytes / 1024 / 1024);
	int netBytesRecved = static_cast<int>(pView->llNetworkTotalBytesRecved / 1024);
	int netBytesSent = static_cast<int>(pView->llNetworkTotalBytesSent / 1024);
	int availableBytes = static_cast<int>(pView->llAvailableBytes / 1024 / 1024);

	int datavalue[5] = { cputotal, nonpagedBytes, netBytesRecved, netBytesSent, availableBytes };

	_serverStatusInfo[0].set(cputotal, curtimestamp);
	_serverStatusInfo[1].set(nonpagedBytes, curtimestamp);
	_serverStatusInfo[2].set(netBytesRecved, curtimestamp);
	_serverStatusInfo[3].set(netBytesSent, curtimestamp);
	_serverStatusInfo[4].set(availableBytes, curtimestamp);

	uint64_t sessionIdBuffer[50];
	int cnt;
	int8_t datatype;
	bool ret = g_manager.GetMonitoringClientSessionIds(sessionIdBuffer, cnt);
	if (ret)
	{
		int endNum = dfMONITOR_DATA_TYPE_MONITOR_END - dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL;
		for(int i = 0; i < endNum; i++)
		{
			datatype = dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL + i;
			CPACKET_CREATE(pDataUpdatePacket);
			*pDataUpdatePacket << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE
				<< (int8_t)en_SERVER_NO::System
				<< datatype
				<< datavalue[i]
				<< curtimestamp;

			for (int i = 0; i < cnt; i++)
			{
				g_manager.GetClientServer().SendPacket(sessionIdBuffer[i], pDataUpdatePacket.GetCPacketPtr());
			}
		}
	}
}

#pragma endregion

#pragma region Protocols

void CConnectServer::RequestProc(uint64_t sessionId, CPacketViewer& refPacket)
{
	int16_t type;
	refPacket >> type;
	switch (type)
	{
	case en_PACKET_TYPE::en_PACKET_SS_MONITOR_LOGIN:
		RequestServerLogin(sessionId, refPacket);
		break;

	case en_PACKET_TYPE::en_PACKET_SS_MONITOR_DATA_UPDATE:
		RequestDataUpdate(sessionId, refPacket);
		break;

	default:
		GetLog()->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"[sessionId: %d] (type: %d)이상한 타입의 RequestPacket",
			sessionId, type);
		break;
	}
}

void CConnectServer::RequestServerLogin(uint64_t sessionId, CPacketViewer& refPacket)
{
	int serverNo;
	refPacket >> (int32_t&)serverNo;

	uint64_t needDisconnectSessionId;
	bool ret = g_manager.LoginServerConnection(sessionId, serverNo, needDisconnectSessionId);
	if (ret == false)
	{
		Disconnect(needDisconnectSessionId);
	}
}

void CConnectServer::RequestDataUpdate(uint64_t sessionId, CPacketViewer& refPacket)
{
	int8_t datatype;
	int datavalue;
	int timestamp;
	refPacket >> datatype >> datavalue >> timestamp;

	int serverNo = g_manager.UpdateServerConnection(sessionId, datatype, datavalue, timestamp);

	if (serverNo == -1)
	{
		GetLog()->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"[sessionId: %llx] 이미 끊어진 서버의 업데이트내용... 나오면 안될텐데", sessionId);
		return;
	}

	uint64_t sessionIdBuffer[50];
	int cnt;
	bool ret = g_manager.GetMonitoringClientSessionIds(sessionIdBuffer, cnt);
	if (ret)
	{
		if (datatype == en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU
			|| datatype == en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SERVER_CPU
			|| datatype == en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU)
		{
			CPACKET_CREATE(pRunUpdatePacket);
			*pRunUpdatePacket << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE
				<< (uint8_t)serverNo
				<< (uint8_t)(datatype - 1)
				<< (int32_t)1
				<< (int32_t)timestamp;

			for (int i = 0; i < cnt; i++)
			{
				g_manager.GetClientServer().SendPacket(sessionIdBuffer[i], pRunUpdatePacket.GetCPacketPtr());
			}
		}

		CPACKET_CREATE(pDataUpdatePacket);
		*pDataUpdatePacket << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE
			<< (uint8_t)serverNo
			<< (uint8_t)datatype
			<< (int32_t)datavalue
			<< (int32_t)timestamp;

		for (int i = 0; i < cnt; i++)
		{
			g_manager.GetClientServer().SendPacket(sessionIdBuffer[i], pDataUpdatePacket.GetCPacketPtr());
		}

	}
}

#pragma endregion


#pragma region DB

unsigned int CConnectServer::DBWriterProc(void* param)
{
	CConnectServer* pNowServer = (CConnectServer*)param;
	HANDLE _hEvents[2] = { pNowServer->GetExitHandle(), pNowServer->_hDBEvent};

	CServer::GetLog()->Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"DB Writer Thread Start!!");

	CMySqlConnector& DBConnector = pNowServer->_connector;
	while (DBConnector.Connect() != 0);

	std::queue<std::unique_ptr<stServerMonitorInfo>>& logQueue = pNowServer->_dbLogQueue;
	std::mutex& dbLock = pNowServer->_dbLock;

	while (1)
	{
		DWORD ret = WaitForMultipleObjects(2, _hEvents, FALSE, INFINITE);
		if (ret == WAIT_OBJECT_0)
		{
			break;
		}

		while (1)
		{
			std::unique_ptr<stServerMonitorInfo> uqptrMonitorInfo;
			{
				std::lock_guard<std::mutex> guard(dbLock);
				if (logQueue.empty())
					break;

				uqptrMonitorInfo = std::move(logQueue.front());
				logQueue.pop();
			}

			DBWriteLog(DBConnector, uqptrMonitorInfo);
		}
	}

	// 나가기 전 큐 비우기
	while (1)
	{
		std::unique_ptr<stServerMonitorInfo> info;
		{
			std::lock_guard<std::mutex> guard(dbLock);
			if (logQueue.empty())
				break;

			info = std::move(logQueue.front());
			logQueue.pop();
		}
	}

	DBConnector.ReleaseConn();
	CServer::GetLog()->Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"DB Writer Thread End!!");
	return 0;
}

void CConnectServer::DBWriteLog(CMySqlConnector& conn, std::unique_ptr<stServerMonitorInfo>& uqptrInfo)
{
	wchar_t wtablename[40];
	wchar_t wdatetime[40];

	int serverNo = uqptrInfo->serverNo;
	conn.RequestQuery(L"begin");

	int plusDataType = uqptrInfo->startNum;
	int endindex = uqptrInfo->endIndex;

	for (int index = 0; index < endindex; index++)
	{
		stTypeInfo* pType = &uqptrInfo->data[index];
		if (pType->count == 0)
			continue;
		tm thistm;
		time_t timestamp = static_cast<time_t>(pType->timeStamp);
		localtime_s(&thistm, &timestamp);

		swprintf_s(wtablename, 40, WFORMAT_DB_LOG_TABLE_NAME, thistm.tm_year + 1900, thistm.tm_mon + 1);
		swprintf_s(wdatetime, 40, WFORMAT_DB_LOG_DATETIME,
			thistm.tm_year + 1900, thistm.tm_mon + 1, thistm.tm_mday, thistm.tm_hour, thistm.tm_min, thistm.tm_sec);

		int ret = conn.RequestQuery(WFORMAT_DB_LOG_WRITE_LOG,
			wtablename, wdatetime, serverNo, index + plusDataType, pType->avr, pType->min, pType->max, pType->count);
		if (ret == 1146)
		{
			conn.RequestQuery(L"rollback");
			conn.RequestQuery(L"CREATE TABLE %s LIKE monitorlog_template", wtablename);
			conn.RequestQuery(L"begin");
			index = -1;
			continue;
		}
	}

	conn.RequestQuery(L"commit");

}

#pragma endregion


#pragma endregion ConnectServer

////////////////////////////////////////////////////////////////////
// ClientServer
////////////////////////////////////////////////////////////////////
#pragma region ClientServer

#pragma region Event
bool CClientServer::OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip)
{
	g_manager.CreateClientConnection(sessionId);
	return true;
}
bool CClientServer::OnConnectionRequest(in_addr ip)
{

	return true;
}
void CClientServer::OnRelease(uint64_t sessionId)
{
	g_manager.ReleaseClientConnection(sessionId);
}
void CClientServer::OnMessage(uint64_t sessionId, CPacket* pPacket, int len)
{
	CPacketViewer refpacket;
	refpacket.SetView(pPacket, len);
	RequestProc(sessionId, refpacket);
}
void CClientServer::OnWorkerStart()
{

}
void CClientServer::OnWorkerEnd()
{

}
void CClientServer::OnUserEvent(CPacket* pPacket)
{

}

bool CClientServer::OnInit(const stServerOpt* pOpt)
{
	return true;
}
void CClientServer::OnStop()
{

}
void CClientServer::OnExit()
{

}
void CClientServer::OnSend(uint64_t sessionId, bool isValid)
{

}

void CClientServer::OnMonitor()
{

}

#pragma endregion

#pragma region Functions

bool CClientServer::LoadOption(const char* path)
{
	stClientServerOpt& opt = _clientServerOpt;
	CParser config(path);
	if (config.LoadFile() == false)
	{
		wprintf_s(L"Config.cnf 파일 로드 오류");
		return true;
	}

	int bUseEncode;
	config.GetValue("MONITOR_SERVER", "bUseEncode", &bUseEncode);
	opt.bUseEncode = (bool)bUseEncode;

	int bUseSO_SNDBUF;
	config.GetValue("MONITOR_SERVER", "bUseSO_SNDBUF", &bUseSO_SNDBUF);
	opt.bUseSO_SNDBUF = (bool)bUseSO_SNDBUF;

	int bUseTCP_NODELAY;
	config.GetValue("MONITOR_SERVER", "bUseTCP_NODELAY", &bUseTCP_NODELAY);
	opt.bUseTCP_NODELAY = (bool)bUseTCP_NODELAY;

	int iMaxConcurrentUsers;
	config.GetValue("MONITOR_SERVER", "clientiMaxConcurrentUsers", &iMaxConcurrentUsers);
	opt.iMaxConcurrentUsers = iMaxConcurrentUsers;

	int iMaxUsers;
	config.GetValue("MONITOR_SERVER", "clientiMaxUsers", &iMaxUsers);
	opt.iMaxUsers = iMaxUsers;

	int iWorkerThreadCreateCnt;
	config.GetValue("MONITOR_SERVER", "clientiWorkerThreadCreateCnt", &iWorkerThreadCreateCnt);
	opt.iWorkerThreadCreateCnt = iWorkerThreadCreateCnt;

	int iWorkerThreadRunCnt;
	config.GetValue("MONITOR_SERVER", "clientiWorkerThreadRunCnt", &iWorkerThreadRunCnt);
	opt.iWorkerThreadRunCnt = iWorkerThreadRunCnt;

	char openIP[16];
	config.GetValue("MONITOR_SERVER", "clientOpenIP", openIP);
	MultiByteToWideChar(CP_ACP, 0, openIP, 16, opt.openIP, 16);

	int port;
	config.GetValue("MONITOR_SERVER", "clientPort", &port);
	opt.port = (unsigned short)port;

	return true;
}

#pragma endregion

#pragma region Protocols

void CClientServer::RequestProc(uint64_t sessionId, CPacketViewer& refPacket)
{
	uint16_t type;
	refPacket >> type;
	switch (type)
	{
	case en_PACKET_TYPE::en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN:
		RequestClientLogin(sessionId, refPacket);
		break;

	default:
		GetLog()->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"[sessionId: %16llx] request type error: %d, Disconnect this session", type);
		Disconnect(sessionId);
		break;
	}
}

void CClientServer::RequestClientLogin(uint64_t sessionId, CPacketViewer& refPacket)
{
	bool isLogined = g_manager.LoginClientConnection(sessionId, refPacket.GetReadPtr(), refPacket.GetDataSize());
	if (isLogined == false)
	{
		Disconnect(sessionId);
		return;
	}

	CPACKET_CREATE(pReslogin);
	*pReslogin << (int16_t)en_PACKET_TYPE::en_PACKET_CS_MONITOR_TOOL_RES_LOGIN
		<< (int8_t)en_PACKET_CS_MONITOR_TOOL_RES_LOGIN::dfMONITOR_TOOL_LOGIN_OK;
}

#pragma endregion


#pragma endregion ClientServer
