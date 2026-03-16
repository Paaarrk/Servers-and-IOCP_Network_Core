#ifndef __ZONE_ECHO_H__
#define __ZONE_ECHO_H__
#include "Zone.h"
#include <atomic>
#include <unordered_map>

//------------------------------------------------------
// 모니터링용 TimerJob
//------------------------------------------------------
class CEchoTimerJob : public Core::CTimerJob
{
	friend class CEcho;
public:
	uint32 GetEchoFps() const { return _echoFps; }
	uint32 GetRecvEchoTps() const { return _recvEchoTps; }
	uint32 GetRecvHeartbeatTps() const { return _recvHeartbeatTps; }

	CEchoTimerJob();
private:
	virtual void Excute();
	void IncreaseExcuteCount();
	void IncreaseRecvEchoCount();
	void IncreaseRecvHeartbeatCount();

private:
	DWORD _startTime;

	uint32 _excuteCount;
	uint32 _echoFps;

	uint32 _recvEchoCount;
	uint32 _recvEchoTps;

	uint32 _recvHeartbeatCount;
	uint32 _recvHeartbeatTps;
};

namespace Core
{
	class RingBuffer;
}
class CUser;
class CEcho : public Net::CZone
{
public:
	CEcho();
	~CEcho();

	void ToggleTimeout();
	bool isUseTimeout() { return _bUseTimeout.load(); }

	CUser* FindUser(uint64 sessionId);
	void SetDead(uint64 sessionId);
	bool ReleaseAccountNo(int64 accountNo, uint64 sessionId);
	
	int32 GetUserCnt() const { return static_cast<int32>(_userMap.size()); }
	int32 GetFps() const { return _monitorJob->GetEchoFps(); }
	int32 GetRecvEchoTps() const { return _monitorJob->GetRecvEchoTps(); }
	int32 GetRecvHeartbeatTps() const { return _monitorJob->GetRecvHeartbeatTps(); }

	void OnUpdate();
	void OnEnter(uint64 sessionId, void* playerPtr, std::wstring* ip = nullptr);
	void OnLeave(uint64 sessionId, bool bNeedPlayerDelete);
	void OnMessage(uint64 sessionId, const char* readPtr, int payloadlen);

	void RequestEcho(uint64 sessionId, const char* readPtr, int payloadlen);
	void RequestHeartbeat(uint64 sessionId, const char* readPtr, int payloadlen);
	
	void SetLobby(uint64 lobbyId) { _lobbyId = lobbyId; }
private:
	std::unordered_map<uint64, CUser*> _userMap;
	std::atomic_bool _bUseTimeout;
	uint64 _lobbyId;

	std::shared_ptr<CEchoTimerJob> _monitorJob;
};

#endif
