#include "MonitorConnector.h"
#include "NetProcess.h"

#include "logclassV1.h"
#include "21_TextParser.h"
#include "CommonProtocol.h"

#include "LoginServerV1.h"
#include <timeapi.h>
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "winmm")
CMonitorConnector g_client;
using Log = Core::c_syslog;

////////////////////////////////////////////////////////////
// Monitoring Job
////////////////////////////////////////////////////////////
using namespace Core;
using namespace Net;

CMonitorConnectorJob::CMonitorConnectorJob()
{
	_startTime = timeGetTime();
}

void CMonitorConnectorJob::Excute()
{
	//dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN = 1,		// 로그인서버 실행여부 ON / OFF
	//dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU = 2,		// 로그인서버 CPU 사용률
	//dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM = 3,		// 로그인서버 메모리 사용 MByte
	//dfMONITOR_DATA_TYPE_LOGIN_SESSION = 4,		// 로그인서버 세션 수 (컨넥션 수)
	//dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS = 5,		// 로그인서버 인증 처리 초당 횟수
	//dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL = 6,		// 로그인서버 패킷풀 사용량
	//dfMONITOR_DATA_TYPE_LOGIN_END,

	CLoginServer* pLogin = _pLoginServer;
	if (pLogin != nullptr)
	{
		uint32 Timestamp = (int32)time(nullptr);
		// RUN
		int32 CpuTotal = (int32)Net::CNetProcess::GetProcess_CpuTotal();
		int32 PrivateBytes = (int32)Net::CNetProcess::GetProcess_PrivateBytes() / 1024 / 1024;
		int32 SessionCnt = pLogin->GetSessionCnt();
		int32 AuthTps = (int32)(pLogin->GetServerMonitor()->GetRecvMessageTps());
		int32 PacketPool = CPacket::GetUsePacketCnt();

		CPACKET_CREATE(pktCpu);
		*pktCpu << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU
			<< (int32)CpuTotal
			<< (uint32)Timestamp;
		g_client.SendPacket(pktCpu.GetCPacketPtr());

		CPACKET_CREATE(pktMem);
		*pktMem << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM
			<< (int32)PrivateBytes
			<< (uint32)Timestamp;
		g_client.SendPacket(pktMem.GetCPacketPtr());

		CPACKET_CREATE(pktSession);
		*pktSession << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_LOGIN_SESSION
			<< (int32)SessionCnt
			<< (uint32)Timestamp;
		g_client.SendPacket(pktSession.GetCPacketPtr());

		CPACKET_CREATE(pktAuthTps);
		*pktAuthTps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS
			<< (int32)AuthTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktAuthTps.GetCPacketPtr());

		CPACKET_CREATE(pktPacketPool);
		*pktPacketPool << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL
			<< (int32)PacketPool
			<< (uint32)Timestamp;
		g_client.SendPacket(pktPacketPool.GetCPacketPtr());

		_InterlockedIncrement(&_sendCount);
	}

	DWORD curTime = timeGetTime();
	int32 deltaTime = CLIENT_MONITORING_TICK - (int32)(curTime - _startTime);
	//Log::logging().Log(L"Debug MC", Log::en_SYSTEM, L"[MC excute: %d | deltaTime: %d]", curTime % 10000, deltaTime);
	if (deltaTime < 0)
	{
		_startTime = curTime + CLIENT_MONITORING_TICK;
		Log::logging().Log(TAG_TIMER, Log::en_ERROR, L"CClient Monitoring Excute(): [delta ms: %d]  문제있음 (빡빡)", -deltaTime);
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, CLIENT_MONITORING_TICK, curTime);
	}
	else
	{
		_startTime += CLIENT_MONITORING_TICK;
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, deltaTime, curTime);
	}
}


////////////////////////////////////////////////////////////
// Connector Opt
////////////////////////////////////////////////////////////
bool CMonitorConnector::stMonitorConnectorOpt::LoadOption(const char* path)
{
	CParser config(path);

	try
	{
		config.LoadFile();

		bUseEncode = false;
		
		int bUseBind;
		config.GetValue("CLIENT_INFO", "bUseBind", &bUseBind);
		this->bUseBind = (bool)bUseBind;

		if (bUseBind)
		{
			char bindIP[16];
			config.GetValue("CLIENT_INFO", "bindIP", bindIP);
			MultiByteToWideChar(CP_ACP, 0, bindIP, 16, this->bindIP, 16);

			int bindPort;
			config.GetValue("CLIENT_INFO", "bindPort", &bindPort);
			this->bindPort = (unsigned short)bindPort;
		}

		int bUseSO_SNDBUF;
		config.GetValue("CLIENT_INFO", "bUseSO_SNDBUF", &bUseSO_SNDBUF);
		this->bUseSO_SNDBUF = (bool)bUseSO_SNDBUF;

		int bUseTCP_NODELAY;
		config.GetValue("CLIENT_INFO", "bUseTCP_NODELAY", &bUseTCP_NODELAY);
		this->bUseTCP_NODELAY = (bool)bUseTCP_NODELAY;

		int iWorkerThreadCreateCnt;
		config.GetValue("CLIENT_INFO", "iWorkerThreadCreateCnt", &iWorkerThreadCreateCnt);
		this->iWorkerThreadCreateCnt = iWorkerThreadCreateCnt;

		int iWorkerThreadRunCnt;
		config.GetValue("CLIENT_INFO", "iWorkerThreadRunCnt", &iWorkerThreadRunCnt);
		this->iWorkerThreadRunCnt = iWorkerThreadRunCnt;

		wcscpy_s(this->targetIP, L"127.0.0.1");
		
		int port;
		config.GetValue("MONITOR_SERVER", "serverPort", &port);
		this->targetPort = (unsigned short)port;

		int clientCode;
		config.GetValue("MONITOR_SERVER", "serverCode", &clientCode);
		client_code = (uint8)clientCode;

		int staticKey;
		config.GetValue("MONITOR_SERVER", "staticKey", &staticKey);
		this->static_key = (uint8)staticKey;
	}
	catch (std::invalid_argument& e)
	{
		wchar_t buffer[256];
		MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, 256);
		Log::logging().Log(TAG_MONITOR, Core::c_syslog::en_ERROR,
			L"%s", buffer);
		return false;
	}
	return true;
}


////////////////////////////////////////////////////////////
// CMonitor Connector
////////////////////////////////////////////////////////////

bool CMonitorConnector::OnInit()
{
	InitMonitoringJob();
	return true;
}

void CMonitorConnector::OnExit()
{
	ExitMonitoringJob();
}

void CMonitorConnector::OnEnterJoinServer()
{
	Log::logging().Log(TAG_MONITOR, Log::en_ERROR, L"Cannot Connect Monitoring Server");

	CPACKET_CREATE(loginPkt);
	*loginPkt << (uint16)en_PACKET_SS_MONITOR_LOGIN;
	*loginPkt << (int32)en_SERVER_NO::Login;
	SendPacket(loginPkt.GetCPacketPtr());
}

void CMonitorConnector::OnLeaveServer()
{
	Log::logging().Log(TAG_MONITOR, Log::en_ERROR, L"Disconnected Monitoring Server");
	Connect(-1);
}

void CMonitorConnector::OnMessage(Net::CPacket* pPacket, int len)
{
	// none;
}

void CMonitorConnector::InitMonitoringJob()
{
	_job = std::make_shared<CMonitorConnectorJob>();
	Net::CNetProcess::GetProcessTimer().RequestTimerJob(_job, 0);
}

void CMonitorConnector::ExitMonitoringJob()
{
	SetMonitoringServer(nullptr);
	_job->CancelJob();
}