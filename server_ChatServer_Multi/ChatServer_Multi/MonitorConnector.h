#ifndef __MONITOR_CONNECTOR_H__
#define __MONITOR_CONNECTOR_H__
#include "NetClient.h"

constexpr const wchar_t* TAG_MONITOR = L"Monitor Client";

class CChatServer;
//------------------------------------------------------
// 서버로 보내기 위한 TimerJob
//------------------------------------------------------
class CMonitorConnectorJob : public Core::CTimerJob
{
public:
	uint64 GetSendCount() const { return _sendCount; }

	CMonitorConnectorJob();
	void SetMonitorServerObject(CChatServer* pChatServer)
	{
		_pChatServer = pChatServer;
	}
private:
	virtual void Excute();

private:
	DWORD _startTime = 0;
	uint64 _sendCount = 0;
	CChatServer* _pChatServer = nullptr;
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
	void SetMonitoringServer(CChatServer* pChatServer)
	{
		_job->SetMonitorServerObject(pChatServer);
	}

	void InitMonitoringJob();
	void ExitMonitoringJob();

	
private:
	std::shared_ptr<CMonitorConnectorJob> _job;
};

extern CMonitorConnector g_client;

#endif