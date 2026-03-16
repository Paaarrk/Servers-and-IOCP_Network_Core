#include "MonitoringServer.h"
#include "_CrashDump.h"
#include <locale.h>
#include <conio.h>

static CConnectServer g_connectServer;
static CClientServer g_clientServer;


dump::c_crashdump g_crash;
int main()
{
	mysql_library_init(0, NULL, NULL);
	timeBeginPeriod(1);
	setlocale(LC_ALL, "korean");

	g_connectServer.LoadOption();
	g_clientServer.LoadOption();

	bool retInit = g_connectServer.Init(g_connectServer.GetOption());
	if (retInit == false)
		__debugbreak();

	retInit = g_clientServer.Init(g_clientServer.GetOption());
	if (retInit == false)
		__debugbreak();

	g_manager.Init(g_connectServer.GetOption()->iMaxConcurrentUsers,
		((CClientServer::stClientServerOpt*)g_clientServer.GetOption())->iMaxUsers, &g_connectServer,
		&g_clientServer, g_connectServer.GetLog());

	bool retStart = g_connectServer.Start();
	if (retStart == false)
		__debugbreak();

	retStart = g_clientServer.Start();
	if (retStart == false)
		__debugbreak();

	wprintf_s(L"서버를 시작합니다. \n");

	DWORD curTime = timeGetTime();
	int tick;
	stServerMonitorViewer* pConnectMonitor = g_connectServer.GetMonitor();
	stServerMonitorViewer* pClientMonitor = g_clientServer.GetMonitor();
	wchar_t buffer[5000];
	int storeMonitor = 0;

	

	while (1)
	{
		tm tm;
		time_t mytime = time(NULL);
		localtime_s(&tm, &mytime);

		swprintf_s(buffer, L"[%2d:%2d:%2d] <** Monitoring Server Status **> (Server Running: (Conn) %d / (Client) %d) (Monitor Save: %d) \n",
			tm.tm_hour, tm.tm_min, tm.tm_sec,(int)g_connectServer.IsServerRunning(), g_clientServer.IsServerRunning(), storeMonitor);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"\n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[Connected Server Status]-------------------------------------- \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" SessionCnt: %d / CUserCnt: %d\n", 
			g_connectServer.GetSessionCnt(), g_manager.GetServerCnt());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" AcceptTps: %d /sec \n", pConnectMonitor->lAcceptTps);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" SendMessageTps: %d /sec \n", pConnectMonitor->lSendMessageTps);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" RecvMessageTps: %d /sec \n\n", pConnectMonitor->lRecvMessageTps);


		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[Monitoring Client Server Status]------------------------------ \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" SessionCnt: %d / CUserCnt: %d (Logined) / %d \n",
			g_clientServer.GetSessionCnt(), g_manager.GetLoginedClientCnt(), g_manager.GetClientCnt());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" AcceptTps: %d /sec \n", pClientMonitor->lAcceptTps);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" SendMessageTps: %d /sec \n", pClientMonitor->lSendMessageTps);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" RecvMessageTps: %d /sec \n\n", pClientMonitor->lRecvMessageTps);

		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[DB Status]---------------------------------------------------- \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" Queue Size: %d\n", g_connectServer.DBQueueSize());

		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[풀 관련]------------------------------------------------------ \n");
		//swprintf_s(_buffer + wcslen(_buffer), 5000 - wcslen(_buffer), L"* CPacket: 생성: %d / 풀내부: %d, (배열버퍼)생성: %d, 풀내부: %d\n",
		//	CPacket::GetCreateChunkNum(), CPacket::GetLeftChunkNum(), CPacket::GetCreateBufferChunkNum(), CPacket::GetLeftBufferChunkNum());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" CPacket: 생성: %d / 풀내부: %d \n",
			CPacket::GetCreateChunkNum(), CPacket::GetLeftChunkNum());
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" 센드큐: 생성: %d / 풀내부: %d \n* 인덱스 스택: 생성: %d / 풀내부: %d \n", 
			CLockFreeQueue<CPacket*>::GetCreateChunkNum(), CLockFreeQueue<CPacket*>::GetInPoolChunkNum(),
			CLockFreeStack<int>::GetCreateChunkNum(), CLockFreeStack<int>::GetLeftChunkNum());
		// swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"* CAroundSesssionId: 생성: %d / 풀내부: %d / 리사이즈 횟수: %d\n", 
		// 	CAroundSessionId::GetCreateChunkNum(), CAroundSessionId::GetLeftChunkNum(), CAroundSessionId::GetResizeCnt());
#ifdef CPACKET_TRACING
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" 패킷 트레이싱 인덱스 스택 남은 수: %d \n", CPacket::m_tracePacket.GetLeftIndexNum());
#endif

		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[프로세스]----------------------------------------------------- \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" CPU사용량: %f%% (유저: %f%%, 커널: %f%%) \n", pConnectMonitor->fProcessUsageTotal, pConnectMonitor->fProcessUsageUser, pConnectMonitor->fProcessUsageKernel);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" 논페이지풀: %fKBytes, 프라이빗: %fMBytes \n", (double)pConnectMonitor->llProcessNopagedBytes / 1024, (double)pConnectMonitor->llProcessPrivateBytes / 1024 / 1024);

#ifdef _DEBUG_TLS_OBJECT_POOL_INTRUSIVE_LIST_H
#ifdef _DEBUG_TLS_OBJECT_POOL_ARRAY
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[디버그 풀]----------------------------------------------------\n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"큐: %lld, 스택: %lld, 패킷: %lld\n",
			CLockFreeQueue<CPacket*>::s_nodePool.DebugInfo.nodesSet.size(),
			CLockFreeStack<int>::s_nodePools.DebugInfo.nodesSet.size(), CPacket::_cPacketPools.DebugInfo.nodesSet.size());
#endif
#endif

		//swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[네트워크]----------------------------------------------------- \n");
		//swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" Recv 세그먼트: %f /sec \n", pConnectMonitor->fNetworkSegmentRecved);
		//swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" Send 세그먼트: %f /sec \n", pConnectMonitor->fNetworkSegmentSent);
		//swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" Retr 세그먼트: %f /sec \n", pConnectMonitor->fNetworkSegmentRetransmitted);
		//
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"[ 시스템 ]----------------------------------------------------- \n");
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" CPU사용량: %f%% (유저: %f%%, 커널: %f%%) \n", pConnectMonitor->fProcessorTotal, pConnectMonitor->fProcessorUser, pConnectMonitor->fProcessorKernel);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" 논페이지풀: %fMBytes, 사용가능: %fMBytes \n", (double)pConnectMonitor->llNonpagedBytes / 1024 / 1024, (double)pConnectMonitor->llAvailableBytes / 1024 / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" 커밋: %fMBytes \n", (double)pConnectMonitor->llSystemCommitBytes / 1024 / 1024);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" 캐시미스: %f /sec \n", pConnectMonitor->fSystemCacheFault);
		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L" NetRecved / NetSent: %lld KB / %0lld KB \n", pConnectMonitor->llNetworkTotalBytesRecved, pConnectMonitor->llNetworkTotalBytesSent);

		swprintf_s(buffer + wcslen(buffer), 5000 - wcslen(buffer), L"\n");

		wprintf(L"%s", buffer);

		if (_kbhit())
		{
			int key = _getch();
			if (key == 's' || key == 'S')
			{
				if (g_clientServer.ToggleStopStart() == false)
					__debugbreak();
			}
			else if (key == 'q' || key == 'Q')
			{
				g_connectServer.Stop(1);
				g_clientServer.Stop(1);
				break;
			}
			else if (key == 'd' || key == 'D')
			{
				CServer::Log_ChangeLevel();
			}
			else if (key == 'p' || key == 'P')
			{
				CServer::Log_TogglePrint();
			}
			else if (key == 'e' || key == 'E')
			{
				if (storeMonitor)
					storeMonitor = 0;
				else
					storeMonitor = 1;
			}
		}
		if (storeMonitor == 1)
		{
			FILE* file;
			_wfopen_s(&file, L"Monitoring.txt", L"ab");
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