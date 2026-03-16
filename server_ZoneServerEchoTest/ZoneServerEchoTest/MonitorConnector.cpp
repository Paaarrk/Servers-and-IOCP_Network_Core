#include "MonitorConnector.h"
#include "NetProcess.h"
#include "TimerManager.h"
#include "logclassV1.h"
#include "21_TextParser.h"
#include "CommonProtocol.h"

#include "EchoServer.h"
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
	_startTime = timeGetTime() + 1;
}

void CMonitorConnectorJob::Excute()
{
	//dfMONITOR_DATA_TYPE_GAME_SERVER_CPU = 11,		// GameServer CPU 사용률
	//dfMONITOR_DATA_TYPE_GAME_SERVER_MEM = 12,		// GameServer 메모리 사용 MByte
	//dfMONITOR_DATA_TYPE_GAME_SESSION = 13,		// 게임서버 세션 수 (컨넥션 수)
	//dfMONITOR_DATA_TYPE_GAME_AUTH_PLAYER = 14,		// 게임서버 AUTH MODE 플레이어 수
	//dfMONITOR_DATA_TYPE_GAME_GAME_PLAYER = 15,		// 게임서버 GAME MODE 플레이어 수
	//dfMONITOR_DATA_TYPE_GAME_ACCEPT_TPS = 16,		// 게임서버 Accept 처리 초당 횟수
	//dfMONITOR_DATA_TYPE_GAME_PACKET_RECV_TPS = 17,		// 게임서버 패킷처리 초당 횟수
	//dfMONITOR_DATA_TYPE_GAME_PACKET_SEND_TPS = 18,		// 게임서버 패킷 보내기 초당 완료 횟수
	//dfMONITOR_DATA_TYPE_GAME_DB_WRITE_TPS = 19,		// 게임서버 DB 저장 메시지 초당 처리 횟수
	//dfMONITOR_DATA_TYPE_GAME_DB_WRITE_MSG = 20,		// 게임서버 DB 저장 메시지 큐 개수 (남은 수)
	//dfMONITOR_DATA_TYPE_GAME_AUTH_THREAD_FPS = 21,		// 게임서버 AUTH 스레드 초당 프레임 수 (루프 수)
	//dfMONITOR_DATA_TYPE_GAME_GAME_THREAD_FPS = 22,		// 게임서버 GAME 스레드 초당 프레임 수 (루프 수)
	//dfMONITOR_DATA_TYPE_GAME_PACKET_POOL = 23,		// 게임서버 패킷풀 사용량

	CEchoServer* pEcho = _pEchoServer;
	if (pEcho != nullptr)
	{
		uint32 Timestamp = (int32)time(nullptr);
		int32 CpuTotal = (int32)Net::CNetProcess::GetProcess_CpuTotal();
		int32 PrivateBytes = (int32)Net::CNetProcess::GetProcess_PrivateBytes() / 1024 / 1024;
		int32 SessionCnt = (int32)pEcho->GetSessionCnt();
		int32 AuthPlayerCnt = (int32)pEcho->GetLobbyPlayerCnt();
		int32 GamePlayerCnt = (int32)pEcho->GetEchoPlayerCnt();
		int32 AcceptTps = (int32)(pEcho->GetServerMonitor()->GetAcceptTps());
		int32 RecvTps = (int32)(pEcho->GetServerMonitor()->GetRecvMessageTps());
		int32 SendTps = (int32)(pEcho->GetServerMonitor()->GetSendMessageTps());
		int32 DbWriteTps = 0;
		int32 DbWriteMsg = 0;
		int32 AuthFps = (int32)(pEcho->GetLobbyFps());
		int32 GameFps = (int32)(pEcho->GetEchoFps());
		int32 PacketPool = (int32)CPacket::GetUsePacketCnt();

		CPACKET_CREATE(pktCpu);
		*pktCpu << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_SERVER_CPU
			<< (int32)CpuTotal
			<< (uint32)Timestamp;
		g_client.SendPacket(pktCpu.GetCPacketPtr());

		CPACKET_CREATE(pktMem);
		*pktMem << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_SERVER_MEM
			<< (int32)PrivateBytes
			<< (uint32)Timestamp;
		g_client.SendPacket(pktMem.GetCPacketPtr());

		CPACKET_CREATE(pktSession);
		*pktSession << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_SESSION
			<< (int32)SessionCnt
			<< (uint32)Timestamp;
		g_client.SendPacket(pktSession.GetCPacketPtr());

		CPACKET_CREATE(pktAuthPlayer);
		*pktAuthPlayer << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_AUTH_PLAYER
			<< (int32)AuthPlayerCnt
			<< (uint32)Timestamp;
		g_client.SendPacket(pktAuthPlayer.GetCPacketPtr());

		CPACKET_CREATE(pktGamePlayer);
		*pktGamePlayer << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_GAME_PLAYER
			<< (int32)GamePlayerCnt
			<< (uint32)Timestamp;
		g_client.SendPacket(pktGamePlayer.GetCPacketPtr());

		CPACKET_CREATE(pktAcceptTps);
		*pktAcceptTps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_ACCEPT_TPS
			<< (int32)AcceptTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktAcceptTps.GetCPacketPtr());

		CPACKET_CREATE(pktRecvTps);
		*pktRecvTps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_PACKET_RECV_TPS
			<< (int32)RecvTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktRecvTps.GetCPacketPtr());

		CPACKET_CREATE(pktSendTps);
		*pktSendTps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_PACKET_SEND_TPS
			<< (int32)SendTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktSendTps.GetCPacketPtr());

		CPACKET_CREATE(pktDBWriteTps);
		*pktDBWriteTps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_DB_WRITE_TPS
			<< (int32)DbWriteTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktDBWriteTps.GetCPacketPtr());

		CPACKET_CREATE(pktDbWriteMsg);
		*pktDbWriteMsg << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_DB_WRITE_MSG
			<< (int32)DbWriteTps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktDbWriteMsg.GetCPacketPtr());

		CPACKET_CREATE(pktAuthFps);
		*pktAuthFps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_AUTH_THREAD_FPS
			<< (int32)AuthFps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktAuthFps.GetCPacketPtr());

		CPACKET_CREATE(pktGameFps);
		*pktGameFps << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_GAME_THREAD_FPS
			<< (int32)GameFps
			<< (uint32)Timestamp;
		g_client.SendPacket(pktGameFps.GetCPacketPtr());

		CPACKET_CREATE(pktUsePacketPool);
		*pktUsePacketPool << (uint16)en_PACKET_SS_MONITOR_DATA_UPDATE
			<< (uint8)dfMONITOR_DATA_TYPE_GAME_PACKET_POOL
			<< (int32)PacketPool
			<< (uint32)Timestamp;
		g_client.SendPacket(pktUsePacketPool.GetCPacketPtr());
	}

	DWORD curTime = timeGetTime();
	int32 deltaTime = CLIENT_MONITORING_TICK - (int32)(curTime - _startTime);
	// Log::logging().Log(L"Debug MC", Log::en_SYSTEM, L"[MC excute: %d | deltaTime: %d]", curTime % 10000, deltaTime);
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
	*loginPkt << (int32)en_SERVER_NO::Game;
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