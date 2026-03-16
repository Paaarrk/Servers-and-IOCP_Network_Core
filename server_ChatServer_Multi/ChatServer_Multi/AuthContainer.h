#ifndef __AUTH_CONTAINER_H__
#define __AUTH_CONTAINER_H__

#include "Type.h"
#include <vector>
#include <string>

namespace cpp_redis
{
	class client;
}

namespace Net
{
	class CPacket;
}
class CChatServer;

typedef void* HIOCP;
typedef void* HTHREAD;

inline static constexpr const wchar_t* TAG_AUTH_REDIS = L"AuthContainer";
class CAuthContainer
{
public:
	enum enAuthContainer
	{
		en_EXIT = 0,
		en_AUTH_REQUEST,
	};
	CAuthContainer();
	~CAuthContainer();

	bool Init(const std::string& redisIP, unsigned short redisPort,
		int32 createThreadNum, int32 runningThreadNum);

	void Clear();

	void Exit();

	void RequestAuth(CChatServer* pCallbackServer, uint64 sessionId, int64 accountNo, const std::string_view& sessionKey);

	int32  GetRequestSize() { return (int32)_requestLeft; }

private:
	bool Connect();

	static void Disconnect();

	//-----------------------------------------------
	// Key, Value, 세션 제거시간 세팅 
	// 내부적으로 Connect시도 (3회)
	// . 성공 시 true
	// . 실패 시 false, 로그화긴 (유저삭제처리)
	//-----------------------------------------------
	bool SendKeyValueEx(const std::string& key, const std::string& value, const std::string& validTimeSecond);

	//-----------------------------------------------
	// 성공시 value는 문자열이 있고, 
	// 실패시 value는 빈 문자열
	//-----------------------------------------------
	std::string GetValue(const std::string& key);
	void DelValue(const std::string& key);


	static unsigned int AuthThreadFunc(void* param);

	void RequestAuth(Net::CPacket* pMessage);
private:
	HIOCP				_hIOCP;
	long				_callInit;
	long				_requestLeft;
	std::string			_redisIP;
	uint16				_redisPort;
	int32				_createThreadNum;
	int32				_runningThreadNum;

	HTHREAD*			_hThreads;
	int32				_workerThreadNum;
	
	static long			s_threadId;
	static thread_local cpp_redis::client* tls_redis_client;
};


#endif