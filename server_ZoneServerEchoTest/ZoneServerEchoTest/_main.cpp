#include "EchoServer.h"
#include "Zone_Lobby.h"
#include "Zone_Echo.h"
#include "NetProcess.h"
#include "MonitorConnector.h"

#include "User.h"

#include "_CrashDump.hpp"
#include "logclassV1.h"

#include <locale.h>
#include <conio.h>
#include <timeapi.h>
#pragma comment(lib, "winmm")

static CEchoServer g_server;
Core::CCrashDump g_crash;

using namespace Core;
using namespace Net;
using Log = Core::c_syslog;

int main()
{
	timeBeginPeriod(1);
	setlocale(LC_ALL, "korean");

	CEchoServer::stEchoServerOpt opt;
	if (opt.LoadOption() == false)
		return 0;

	CMonitorConnector::stMonitorConnectorOpt clientOpt;
	if (clientOpt.LoadOption() == false)
		return 0;

	bool retInit = g_server.Init(&opt);
	if (retInit == false)
		return 0;
	bool retStart = g_server.Start();
	if (retStart == false)
		return 0;

	bool clientInit = g_client.Init(&clientOpt);
	if (clientInit == false)
		return 0;

	g_client.Connect(-1);
	g_client.SetMonitoringServer(&g_server);

	DWORD curTime = timeGetTime();
	int tick;
	const CZoneServer::ZoneServerMonitor& pSMonitor = g_server.GetServerMonitor();
	const CClient::ClientMonitor& pCmonitor = g_client.GetClientMonitor();
	wchar_t buffer[5000];
	int storeMonitor = 0;

	int threadNum = opt.iWorkerThreadCreateCnt;

	tm starttm;
	time_t starttime = pSMonitor->GetServerStartTime();
	localtime_s(&starttm, &starttime);
	std::wstring_view processName = CNetProcess::GetProcess_Name();

	while (1)
	{
		tm tm;
		time_t mytime = time(NULL);
		localtime_s(&tm, &mytime);


#pragma region CONSOLE_WRITE
		swprintf_s(buffer,
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[ Start : %04d%02d%02d %02d:%02d:%02d]  ** %s ** \n\n",
			starttm.tm_year + 1900, starttm.tm_mon + 1, starttm.tm_mday, starttm.tm_hour, starttm.tm_min, starttm.tm_sec, processName.data());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[ Current Time : %02d:%02d:%02d] | use White: %d | Server Running: %d | Monitor Save: %d \n",
			tm.tm_hour, tm.tm_min, tm.tm_sec, 0, (int)g_server.IsServerRunning(), storeMonitor);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| SessionCnt : %5d / %5d | Total Accept: %llu \n", g_server.GetSessionCnt(), opt.iMaxConcurrentUsers, pSMonitor->GetTotalAcceptCount());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  AcceptTps : %5d /sec    | SendMessageTps: %7d | RecvMessageTps : %5d / sec (Avg : %5d)\n", pSMonitor->GetAcceptTps(), pSMonitor->GetSendMessageTps(), pSMonitor->GetRecvMessageTps(), pSMonitor->GetRecvMessageTpsAvg());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[Lobby (Auth)] Timeout:%2d                [Echo (Game)] Timeout:%2d\n", 
			g_server.GetLobbyTImeout(), g_server.GetEchoTimeout());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| FPS      :   %3d                       | FPS      :   %3d \n",
			g_server.GetLobbyFps(), g_server.GetEchoFps());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Tick     :   %3d                       | Tick     :   %3d \n",
			g_server.GetLobbyDeltaTick(), g_server.GetEchoDeltaTick());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Player   :%6d                       | Player   :%6d \n",
			g_server.GetLobbyPlayerCnt(), g_server.GetEchoPlayerCnt());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Sessions :%6d                       | Sessions :%6d \n",
			g_server.GetLobbySessionCnt(), g_server.GetEchoSessionCnt());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Account  :%6d \n",
			g_server.GetAccountCnt());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Duplicate:%6lld \n",
			g_server.GetDuplicateLoginCnt());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[Server Worker Working Info]\n");
		for (int i = 0; i < threadNum; i++)
		{
			swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
				L"| Worker[%2d] : %5.2f %% ", i, g_server.GetServerMonitor()->GetWorkerWorkRate(i));
			if (i % 4 == 3 || i == threadNum - 1)
				swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"\n");
		}
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Total Connect : %2d | SendMessageTps: %2d | RecvMessageTps : %2d / sec\n", pCmonitor->GetConnectCnt(), pCmonitor->GetSendMessageTps(), pCmonitor->GetRecvMessageTps());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| U = Use Node | C = Create Chunk | L = LeftChunk  \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  Packet Pool | U : %10d | C : %10d | L : %10d\n", Net::CPacket::GetUsePacketCnt(), Net::CPacket::GetCreateChunkNum(), Net::CPacket::GetLeftChunkNum());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  +--- RChunk | U : %10d | C : %10d | L : %10d\n", 0, Net::CPacket::GetCreateRChunkNum(), Net::CPacket::GetLeftRChunkNum());
#ifdef CPACKET_TRACING
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Packet Trace | L : %10d \n", Net::CPacket::m_tracePacket.GetLeftIndexNum());
#endif
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|   SendQ Pool |                | C : %10d | L : %10d \n", CLockFreeQueue<Net::CPacket*>::GetCreateChunkNum(), CLockFreeQueue<Net::CPacket*>::GetInPoolChunkNum());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|   Index Pool |                | C : %10d | L : %10d \n", CLockFreeStack<int>::GetCreateChunkNum(), CLockFreeStack<int>::GetLeftChunkNum());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|    User Pool | U : %10d | C : %10d | L : %10d \n", CUser::PoolUseSize(), CUser::UserPoolCreateChunk(), CUser::UserPoolLeftChunk());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|    Job Block | U : %10d | C : %10d | L : %10d \n", Core::stJobBlock::GetUseSize(), Core::stJobBlock::GetCapacity(), Core::stJobBlock::GetSize());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[ Process ]\n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|       CPU | T: %.7f%% | U: %.7f%% | K: %.7f%% \n", CNetProcess::GetProcess_CpuTotal(), CNetProcess::GetProcess_CpuUser(), CNetProcess::GetProcess_CpuTotal());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  Nonpaged | %.7fKB \n", (float)(CNetProcess::GetProcess_NonpagedBytes()) / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  Privates | %.7fMB \n", (float)CNetProcess::GetProcess_PrivateBytes() / 1024 / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[ System ]\n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|       CPU | T: %.7f%% | U: %.7f%% | K: %.7f%% \n", CNetProcess::GetSystem_CpuTotal(), CNetProcess::GetSystem_CpuUser(), CNetProcess::GetSystem_CpuKernel());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  Nonpaged | %.7f MB \n", (float)CNetProcess::GetSystem_NonpagedBytes() / 1024 / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Available | %.7f MB \n", (float)CNetProcess::GetSystem_AvailableBytes() / 1024 / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Committed | %.7f MB \n", (float)CNetProcess::GetSystem_CommitBytes() / 1024 / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"[ Network ]\n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"| Recv Bytes : %lld KB | Send Bytes : %lld KB \n",
			CNetProcess::GetSystem_TotalNetworkBytesRecved() / 1024, CNetProcess::GetSystem_TotalNetworkBytesSent() / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  Recv Seg : %.10f /sec | Send Seg : %.10f / sec\n", CNetProcess::GetSystem_SegmentRecved(), CNetProcess::GetSystem_SegmentSent());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"|  Retr Seg : %.10f /sec \n", CNetProcess::GetSystem_SegmentRetransmitted());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer),
			L"============================================================================================================ \n\n");

		wprintf(L"%s", buffer);
#pragma endregion

		if (_kbhit())
		{
			int key = _getch();
			if (key == 's' || key == 'S')
			{
				if (g_server.ToggleStopStart() == false)
					__debugbreak();
			}
			else if (key == 'q' || key == 'Q')
			{
				g_client.Exit();
				g_server.Stop(1);
				break;
			}
			else if (key == 'd' || key == 'D')
			{
				Log::logging().ChangeLevel();
			}
			else if (key == 'p' || key == 'P')
			{
				Log::logging().TogglePrint();
			}
			else if (key == 'e' || key == 'E')
			{
				if (storeMonitor)
					storeMonitor = 0;
				else
					storeMonitor = 1;
			}
			else if (key == 't' || key == 'T')
			{
				g_server.ToggleUseTimeout();
			}
		}
		

		if (storeMonitor == 1)
		{
			FILE* file;
			wchar_t fileName[64];
			swprintf_s(fileName, 64, L"Monitoring_%2d_%2d.txt", tm.tm_mon + 1, tm.tm_mday);
			_wfopen_s(&file, fileName, L"ab");
			if (file != nullptr)
			{
				if (_ftelli64(file) == 0)
					fputwc(0xFEFF, file);
				fwprintf_s(file, L"%s", buffer);
				fclose(file);
			}
		}

		tick = timeGetTime() - curTime;
		if (tick < 1000)
			Sleep(1000 - tick);
		curTime += 1000;
	}



	return 0;
}