#ifndef __REDIS_CONNECTOR_H__
#define __REDIS_CONNECTOR_H__

#include <vector>
#include <string>

namespace cpp_redis
{
	class client;
}

constexpr const wchar_t* TAG_REDIS = L"REDIS";
class CRedisConnector
{
public:
	CRedisConnector():_tryConnectCnt(0)
	{
		_redisIP.reserve(16);
	}
	~CRedisConnector()
	{
	}

	//-----------------------------------------------
	// 레디스 설정 (tryConnectCnt는 0이면 무한시도)
	//-----------------------------------------------
	void Init(const std::string& redisIP, unsigned short redisPort, int tryConnectCnt = 0);

	//-----------------------------------------------
	// 0일경우 무한 시도, 무한시도라면 항상 true
	//-----------------------------------------------
	bool Connect();

	void Disconnect();

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

private:
	int _tryConnectCnt;
	std::string _redisIP;
	unsigned short _redisPort = 0;
	static thread_local cpp_redis::client* tls_redis_client;
};

#endif