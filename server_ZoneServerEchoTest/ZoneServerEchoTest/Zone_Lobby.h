#ifndef __ZONE_LOBBY_H__
#define __ZONE_LOBBY_H__
#include "Zone.h"
#include <atomic>
#include <unordered_map>


//------------------------------------------------------
// 모니터링용 TimerJob
//------------------------------------------------------
class CLobbyTimerJob : public Core::CTimerJob
{
	friend class CLobby;
public:
	uint32 GetLobbyFps() const { return _lobbyFps; }
	uint32 GetRecvLoginTps() const { return _recvLoginTps; }
	uint64 GetTotalDuplicateLogin() const { return _totalDuplicateLoginCount; }

	CLobbyTimerJob();
private:
	virtual void Excute();
	void IncreaseExcuteCount();
	void IncreaseRecvLoginCount();
	void IncreaseDuplicateLoginCount();
private:
	DWORD _startTime;

	uint32 _excuteCount;
	uint32 _lobbyFps;

	uint32 _recvLoginCount;
	uint32 _recvLoginTps;
	
	uint64 _totalDuplicateLoginCount;
};

class CUser;
class CLobby : public Net::CZone
{
public:
	CLobby();
	~CLobby();

	void ToggleTimeout();
	bool isUseTimeout() { return _bUseTimeout.load(); }

	void DeleteLogin(uint64 sessionId);
	CUser* FindUser(uint64 sessionId);
	void SetDead(uint64 sessionId);
	void ReleaseAccountNo(int64 accountNo, uint64 sessionId);
	
	int32 GetLoginedAccountNum() const { return static_cast<int32>(_accountMap.size()); }
	int32 GetUserCnt() const { return static_cast<int32>(_userMap.size()); }
	int32 GetFps() const { return _monitorJob->GetLobbyFps(); }
	int32 GetRecvLoginTps() const { return _monitorJob->GetRecvLoginTps(); }
	int64 GetTotalDuplicatedLoginCount() const { return _monitorJob->GetTotalDuplicateLogin(); }

	void OnUpdate();
	void OnEnter(uint64 sessionId, void* playerPtr, std::wstring* ip = nullptr);
	void OnLeave(uint64 sessionId, bool bNeedPlayerDelete);
	void OnMessage(uint64 sessionId, const char* readPtr, int payloadlen);

	void RequestLogin(uint64 sessionId, const char* readPtr, int payloadlen);

private:
	std::unordered_map<uint64, CUser*> _userMap;
	std::unordered_map<int64, uint64> _accountMap;
	std::atomic_bool _bUseTimeout;
	std::shared_ptr<CLobbyTimerJob> _monitorJob;
};

#endif
