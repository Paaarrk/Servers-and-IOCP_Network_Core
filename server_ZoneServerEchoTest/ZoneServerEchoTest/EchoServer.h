#ifndef __ECHO_SERVER_H__
#define __ECHO_SERVER_H__
#include "ZoneServer.h"
#include "Contents.h"

class CLobby;
class CEcho;
class CEchoServer : public Net::CZoneServer
{
public:
	struct stEchoServerOpt :public Net::CZoneServer::stServerOpt
	{
		int32 lobbyMaxUsers;
		int32 gameMaxUsers;
		int32 lobbyMinimumTick;
		int32 gameMinimumTick;
		int32 maxZoneCnt;

		bool LoadOption(const char* path = CONFIG_FILE_PATH);
	};

	virtual bool OnInit(const stServerOpt* pOpt);
	virtual bool OnAccept(uint64_t sessionId, in_addr ip, wchar_t* wip);
	virtual bool OnConnectionRequest(in_addr ip);

	// NetInterface

	virtual void OnRelease(uint64_t sessionId);
	virtual void OnMessage(uint64_t sessionId, Net::CPacket* pPacket, int len);

	// IOCP차원

	virtual void OnWorkerStart();
	virtual void OnWorkerEnd();
	virtual void OnUserEvent(Net::CPacket* pPacket);

	// 중지와 종료

	virtual void OnStop();
	virtual void OnExit();

	void ToggleUseTimeout();

	uint64 GetLobbyId() const { return _lobbyId; }
	uint64 GetEchoId() const { return _echoId; }

	int32 GetAccountCnt() const;
	int64 GetDuplicateLoginCnt() const;
	int32 GetLobbyPlayerCnt() const;
	int32 GetLobbySessionCnt() const;
	int32 GetEchoPlayerCnt() const;
	int32 GetEchoSessionCnt() const;
	int32 GetLobbyFps() const;
	int32 GetEchoFps() const;
	int32 GetLobbyDeltaTick() const;
	int32 GetEchoDeltaTick() const;
	bool GetLobbyTImeout() const;
	bool GetEchoTimeout() const;
private:
	uint64 _lobbyId = 0;
	uint64 _echoId = 0;
	CLobby* _pLobby = nullptr;
	CEcho* _pEcho = nullptr;
};

#endif