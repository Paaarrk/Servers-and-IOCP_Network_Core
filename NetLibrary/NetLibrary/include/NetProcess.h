#ifndef __NET_PROCESS_H__
#define __NET_PROCESS_H__
#include "TimerJob.h"
#include "MonitorV1.h"
#include "TimerManager.h"

constexpr int32 NET_MONITORING_TICK = 1000;

namespace Net
{
	class CNetProcess
	{
	public:

		static Core::CTimerManager& GetProcessTimer() { return s_netProcess._ProcessTimer; }

		// 기본으로 꺼져있습니다.
		static void OffSystemMonitor();
		// 기본으로 꺼져있습니다.
		static void OnSystemMonitor();

		// Monitoring Getter
#pragma region Monitoring Getter
		static const std::wstring_view GetProcess_Name() { return s_netProcess._job->process.GetInstanceName(); }
		static float GetProcess_CpuTotal() { return s_netProcess._job->process.ProcessTotal(); }
		static float GetProcess_CpuUser() { return s_netProcess._job->process.ProcessUser(); }
		static float GetProcess_CpuKernel() { return s_netProcess._job->process.ProcessKernel(); }
		static int64 GetProcess_NonpagedBytes() { return s_netProcess._job->process.ProcessNonpagedBytes(); }
		static int64 GetProcess_PrivateBytes() { return s_netProcess._job->process.ProcessPrivateBytes(); }

		static float GetSystem_CpuTotal() { return s_netProcess._job->system.ProcessorTotal(); }
		static float GetSystem_CpuUser() { return s_netProcess._job->system.ProcessorUser(); }
		static float GetSystem_CpuKernel() { return s_netProcess._job->system.ProcessorKernel(); }
		static float GetSystem_CacheFault() { return s_netProcess._job->system.SystemCacheFault(); }
		static int64 GetSystem_CommitBytes() { return s_netProcess._job->system.SystemCommitBytes(); }
		static int64 GetSystem_NonpagedBytes() { return s_netProcess._job->system.SystemNonpagedBytes(); }
		static int64 GetSystem_AvailableBytes() { return s_netProcess._job->system.SystemAvailableBytes(); }
		static float GetSystem_SegmentRecved() { return s_netProcess._job->system.SystemSegmentRecved(); }
		static float GetSystem_SegmentSent() { return s_netProcess._job->system.SystemSegmentSent(); }
		static float GetSystem_SegmentRetransmitted() { return s_netProcess._job->system.SystemSegmentRetransmitted(); }
		static int64 GetSystem_TotalNetworkBytesRecved() { return s_netProcess._job->system.SystemTotalNetworkBytesRecved(); }
		static int64 GetSystem_TotalNetworkBytesSent() { return s_netProcess._job->system.SystemTotalNetworkBytesSent(); }
		static void GetSystem_NIC_Names(std::vector<std::wstring>& nameBuffer) { nameBuffer = s_netProcess._job->system.GetNICnames(); }
		static void GetSystem_NetworkBytesRecved(std::vector<int64>& name_mached_vector) { name_mached_vector = s_netProcess._job->system.SystemNetworkBytesRecved(); }
		static void GetSystem_NetworkBytesSent(std::vector<int64>& name_mached_vector) { name_mached_vector = s_netProcess._job->system.SystemNetworkBytesSent(); }
#pragma endregion

		~CNetProcess();
	private:
		struct stMonitoringJob :public Core::CTimerJob
		{
			DWORD startTime = 0;
			long isMonitoringSystem = 1;
			Core::CMonitorSystem system;
			Core::CMonitorProcess process;
			virtual void Excute();
			virtual ~stMonitoringJob()
			{
				wprintf(L"Monitoring Exit");
			}
		};
		CNetProcess();
		CNetProcess(const Net::CNetProcess& ref) = delete;
		Net::CNetProcess& operator=(const Net::CNetProcess& ref) = delete;
		
		
		Core::CTimerManager _ProcessTimer;
		std::shared_ptr<stMonitoringJob> _job;

		static CNetProcess s_netProcess;
	};
}

#endif