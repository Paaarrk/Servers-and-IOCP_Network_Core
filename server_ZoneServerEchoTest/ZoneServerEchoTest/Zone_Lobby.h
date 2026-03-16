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
	uint32_t GetLobbyFps() const { return _lobbyFps; }
	uint32_t GetRecvLoginTps() const { return _recvLoginTps; }
	uint64_t GetTotalDuplicateLogin() const { return _totalDuplicateLoginCount; }

	CLobbyTimerJob();
private:
	virtual void Excute();
	void IncreaseExcuteCount();
	void IncreaseRecvLoginCount();
	void IncreaseDuplicateLoginCount();
private:
	DWORD _startTime;

	uint32_t _excuteCount;
	uint32_t _lobbyFps;

	uint32_t _recvLoginCount;
	uint32_t _recvLoginTps;
	
	uint64_t _totalDuplicateLoginCount;
};

class CUser;
class CLobby : public Net::CZone
{
public:
	CLobby();
	~CLobby();

	void ToggleTimeout();
	bool isUseTimeout() { return _bUseTimeout.load(); }

	void DeleteLogin(uint64_t sessionId);
	CUser* FindUser(uint64_t sessionId);
	void SetDead(uint64_t sessionId);
	void ReleaseAccountNo(int64_t accountNo, uint64_t sessionId);
	
	int32_t GetLoginedAccountNum() const { return static_cast<int32_t>(_accountMap.size()); }
	int32_t GetUserCnt() const { return static_cast<int32_t>(_userMap.size()); }
	int32_t GetFps() const { return _monitorJob->GetLobbyFps(); }
	int32_t GetRecvLoginTps() const { return _monitorJob->GetRecvLoginTps(); }
	int64_t GetTotalDuplicatedLoginCount() const { return _monitorJob->GetTotalDuplicateLogin(); }

	void OnUpdate();
	void OnEnter(uint64_t sessionId, void* playerPtr, std::wstring* ip = nullptr);
	void OnLeave(uint64_t sessionId, bool bNeedPlayerDelete);
	void OnMessage(uint64_t sessionId, const char* readPtr, int payloadlen);

	void RequestLogin(uint64_t sessionId, const char* readPtr, int payloadlen);

private:
	std::unordered_map<uint64_t, CUser*> _userMap;
	std::unordered_map<int64_t, uint64_t> _accountMap;
	std::atomic_bool _bUseTimeout;
	std::shared_ptr<CLobbyTimerJob> _monitorJob;
};

#endif
