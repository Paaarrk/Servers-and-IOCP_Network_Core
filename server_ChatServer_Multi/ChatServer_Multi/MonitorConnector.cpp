#include "MonitorConnector.h"
#include "NetProcess.h"
#include "TimerManager.h"
#include "logclassV1.h"
#include "21_TextParser.h"
#include "CommonProtocol.h"

#include "ChatServerV3.h"
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
	//dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN = 30,		// 채팅서버 ChatServer 실행 여부 ON / OFF
	//dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU = 31,		// 채팅서버 ChatServer CPU 사용률
	//dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM = 32,		// 채팅서버 ChatServer 메모리 사용 MByte
	//dfMONITOR_DATA_TYPE_CHAT_SESSION = 33,		// 채팅서버 세션 수 (컨넥션 수)
	//dfMONITOR_DATA_TYPE_CHAT_PLAYER = 34,		// 채팅서버 인증성공 사용자 수 (실제 접속자)
	//dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS = 35,		// 채팅서버 UPDATE 스레드 초당 초리 횟수
	//dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL = 36,		// 채팅서버 패킷풀 사용량
	//dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL = 37,
	CChatServer* pChat = _pChatServer;
	if (pChat != nullptr)
	{
		uint32 Timestamp = (int32)time(nullptr);
		int32 CpuTotal = (int32)Net::CNetProcess::GetProcess_CpuTotal();
		int32 PrivateBytes = (int32)Net::CNetProcess::GetProcess_PrivateBytes() / 1024 / 1024;
		int32 SessionCnt = (int32)pChat->GetSessionCnt();
		int32 PlayerCnt = (int32)g_userManager.GetUserCnt();
		int32 UpdateTps = (int32)(pChat->GetServerMonitor()->GetRecvMessageTps());
		int32 PacketPool = (int32)CPacket::GetUsePacketCnt();
		int32 RedisQueueLeft = (int32)g_redisAuth.GetRequestSize();

		CPACKET_CREATE(pktCpu);
		*pktCpu << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU
			<< (int32)CpuTotal
			<< (uint32)Timestamp;
		g_client.SendPacket(pktCpu.GetCPacketPtr());

		CPACKET_CREATE(pktMem);
		*pktMem << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM
			<< (int32)PrivateBytes
			<< (uint32)Timestamp;
		g_client.SendPacket(pktMem.GetCPacketPtr());

		CPACKET_CREATE(pktSession);
		*pktSession << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_SESSION
			<< (int32)SessionCnt
			<< (uint32)Timestamp;
		g_client.SendPacket(pktSession.GetCPacketPtr());

		CPACKET_CREATE(pktUser);
		*pktUser << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_PLAYER
			<< (int32)PlayerCnt
			<< (uint32)Timestamp;
		g_client.SendPacket(pktUser.GetCPacketPtr());

		CPACKET_CREATE(pktUpdateTps);
		*pktUpdateTps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS
			<< (int32)UpdateTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktUpdateTps.GetCPacketPtr());

		CPACKET_CREATE(pktPacketPool);
		*pktPacketPool << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL
			<< (int32)PacketPool
			<< (uint32)Timestamp;
		g_client.SendPacket(pktPacketPool.GetCPacketPtr());

		CPACKET_CREATE(pktRedisQueueLeft);
		*pktRedisQueueLeft << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL
			<< (int32)RedisQueueLeft
			<< (uint32)Timestamp;
		g_client.SendPacket(pktRedisQueueLeft.GetCPacketPtr());

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
	*loginPkt << (int32)en_SERVER_NO::Chat;
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