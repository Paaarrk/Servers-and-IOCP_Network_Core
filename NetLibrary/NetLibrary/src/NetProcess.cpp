#include "NetProcess.h"
#include "logclassV1.h"

Net::CNetProcess Net::CNetProcess::s_netProcess;

void Net::CNetProcess::stMonitoringJob::Excute()
{
	process.Update();
	if(isMonitoringSystem == 1)
		system.Update();

	DWORD curTime = timeGetTime();
	int32 deltaTime = NET_MONITORING_TICK - (int32)(curTime - startTime);
	//Core::c_syslog::logging().Log(L"Debug NP", Core::c_syslog::en_SYSTEM, L"[NP excute: %d | deltaTime: %d]", curTime % 10000, deltaTime);
	if (deltaTime < 0)
	{
		startTime = curTime + NET_MONITORING_TICK;
		Core::c_syslog::logging().Log(TAG_TIMER, Core::c_syslog::en_ERROR, L"[delta ms: %d] ProcessMonitor ¹®Á¦ÀÖÀ½ (ºýºý)", -deltaTime);
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, NET_MONITORING_TICK, curTime);
	}
	else
	{
		startTime += NET_MONITORING_TICK;
		Net::CNetProcess::GetProcessTimer().RequestTimerItSelf(this, deltaTime, curTime);
	}
}


Net::CNetProcess::CNetProcess()
{
	_job = std::make_shared<stMonitoringJob>();
	_job->startTime = timeGetTime();
	_ProcessTimer.RequestTimerJob(_job, 0);	//ÀÏ´Ü ÇÑ¹øÀº ½ÇÇàÇØ¾ßµÊ
}

Net::CNetProcess::~CNetProcess()
{
	_job->CancelJob();
}

void Net::CNetProcess::OffSystemMonitor()
{
	std::shared_ptr<stMonitoringJob> ref = s_netProcess._job;
	if(ref != nullptr)
		_InterlockedExchange(&(ref->isMonitoringSystem), 0);
}

void Net::CNetProcess::OnSystemMonitor()
{
	std::shared_ptr<stMonitoringJob> ref = s_netProcess._job;
	if(ref != nullptr)
		_InterlockedExchange(&(ref->isMonitoringSystem), 1);
}