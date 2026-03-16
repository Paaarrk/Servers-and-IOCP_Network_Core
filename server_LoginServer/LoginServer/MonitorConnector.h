#ifndef __MONITOR_CONNECTOR_H__
#define __MONITOR_CONNECTOR_H__
#include "NetClient.h"

constexpr const wchar_t* TAG_MONITOR = L"Monitor Client";

class CLoginServer;
//------------------------------------------------------
// 서버로 보내기 위한 TimerJob
//------------------------------------------------------
class CMonitorConnectorJob : public Core::CTimerJob
{
public:
	uint64 GetSendCount() const { return _sendCount; }

	CMonitorConnectorJob();
	void SetMonitorServerObject(CLoginServer* pLoginServer)
	{
		_pLoginServer = pLoginServer;
	}
private:
	virtual void Excute();

private:
	DWORD _startTime = 0;
	uint64 _sendCount = 0;
	CLoginServer* _pLoginServer = nullptr;
};

////////////////////////////////////////////////////////////
// CMonitor Connector
////////////////////////////////////////////////////////////
class CMonitorConnector : public Net::CClient
{
public:
	struct stMonitorConnectorOpt : public Net::CClient::stClientOpt
	{
		bool LoadOption(const char* path = "..\\Config\\Config.cnf");
	};

	virtual bool OnInit();
	virtual void OnExit();

	virtual void OnEnterJoinServer();
	virtual void OnLeaveServer();
	virtual void OnMessage(Net::CPacket* pPacket, int len);
	void SetMonitoringServer(CLoginServer* pLoginServer)
	{
		_job->SetMonitorServerObject(pLoginServer);
	}

	void InitMonitoringJob();
	void ExitMonitoringJob();

	
private:
	std::shared_ptr<CMonitorConnectorJob> _job;
};

extern CMonitorConnector g_client;

#endif