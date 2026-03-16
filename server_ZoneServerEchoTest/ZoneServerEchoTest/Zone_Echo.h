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
	uint32_t GetEchoFps() const { return _echoFps; }
	uint32_t GetRecvEchoTps() const { return _recvEchoTps; }
	uint32_t GetRecvHeartbeatTps() const { return _recvHeartbeatTps; }

	CEchoTimerJob();
private:
	virtual void Excute();
	void IncreaseExcuteCount();
	void IncreaseRecvEchoCount();
	void IncreaseRecvHeartbeatCount();

private:
	DWORD _startTime;

	uint32_t _excuteCount;
	uint32_t _echoFps;

	uint32_t _recvEchoCount;
	uint32_t _recvEchoTps;

	uint32_t _recvHeartbeatCount;
	uint32_t _recvHeartbeatTps;
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
	
	int32_t GetUserCnt() const { return static_cast<int32>(_userMap.size()); }
	int32_t GetFps() const { return _monitorJob->GetEchoFps(); }
	int32_t GetRecvEchoTps() const { return _monitorJob->GetRecvEchoTps(); }
	int32_t GetRecvHeartbeatTps() const { return _monitorJob->GetRecvHeartbeatTps(); }

	void OnUpdate();
	void OnEnter(uint64_t sessionId, void* playerPtr, std::wstring* ip = nullptr);
	void OnLeave(uint64_t sessionId, bool bNeedPlayerDelete);
	void OnMessage(uint64_t sessionId, const char* readPtr, int32 payloadlen);

	void RequestEcho(uint64_t sessionId, const char* readPtr, int payloadlen);
	void RequestHeartbeat(uint64_t sessionId, const char* readPtr, int payloadlen);
	
	void SetLobby(uint64_t lobbyId) { _lobbyId = lobbyId; }
private:
	std::unordered_map<uint64, CUser*> _userMap;
	std::atomic_bool _bUseTimeout;
	uint64_t _lobbyId;

	std::shared_ptr<CEchoTimerJob> _monitorJob;
};

#endif
