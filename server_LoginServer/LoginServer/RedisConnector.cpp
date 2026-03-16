#include "RedisConnector.h"
#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")
#pragma comment (lib, "ws2_32.lib")
#include "logclassV1.h"

using Log = Core::c_syslog;
thread_local cpp_redis::client* CRedisConnector::tls_redis_client;


void CRedisConnector::Init(const std::string& redisIP, unsigned short redisPort,int tryConnectCnt)
{
	_redisIP = redisIP;
	_redisPort = redisPort;
	_tryConnectCnt = tryConnectCnt;
}

bool  CRedisConnector::Connect()
{
	if (tls_redis_client == nullptr)
	{
		tls_redis_client = new cpp_redis::client;
	}

	bool connected = false;
	int cnt = 0;
	while (connected == false)
	{
		cnt++;
		try
		{
			tls_redis_client->connect(_redisIP, _redisPort, nullptr);
			connected = true;
			break;
		}
		catch (const cpp_redis::redis_error& e)
		{
			if(cnt % 10 == 1)
			{
				wchar_t buffer[512];
				MultiByteToWideChar(CP_ACP, 0, e.what(), -1,
					buffer, _countof(buffer));
				Log::logging().Log(TAG_REDIS, Log::en_ERROR,
					L"Redis 재연결 시도. (횟수: %d) [%s]", 
					cnt, buffer);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (_tryConnectCnt != 0 && cnt > _tryConnectCnt)
		{
			break;
		}
	}
	
	return connected;
}

void CRedisConnector::Disconnect()
{
	if (tls_redis_client == nullptr)
	{
		return;
	}

	tls_redis_client->disconnect();
	delete tls_redis_client;
}

bool CRedisConnector::SendKeyValueEx(const std::string& key, const std::string& value, const std::string& validTimeSecond)
{
	if (tls_redis_client == nullptr)
	{
		Connect();
	}

	wchar_t buffer[512];
	bool connected = true;

	for (int i = 0; i < 3; i++)
	{
		try
		{
			if (connected == false)
			{
				tls_redis_client->connect(_redisIP, _redisPort, nullptr);
				connected = true;
			}

			cpp_redis::reply send_reply;
			tls_redis_client->send({
			"SETEX", key.c_str(), validTimeSecond.c_str(), value.c_str() },
			[&send_reply](cpp_redis::reply& reply) { send_reply = reply; });
			tls_redis_client->sync_commit();
			if (send_reply.is_error())
			{
				MultiByteToWideChar(CP_ACP, 0, send_reply.error().c_str(), -1, buffer, _countof(buffer));

				Log::logging().Log(TAG_REDIS, Log::en_ERROR,
					L"Redis Context오류: %s", buffer);
				return false;
			}
			return true;
		}
		catch (cpp_redis::redis_error& e)
		{
			MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, _countof(buffer));

			Log::logging().Log(TAG_REDIS, Log::en_ERROR,
				L"Redis Connect오류(횟수: %d): %s", i + 1, buffer);
			connected = false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return false;
}

std::string CRedisConnector::GetValue(const std::string& key)
{
	std::string value;

	if (tls_redis_client == nullptr)
	{
		Connect();
	}

	wchar_t buffer[512];
	bool connected = true;

	for (int i = 0; i < 3; i++)
	{
		try
		{
			if (connected == false)
			{
				tls_redis_client->connect(_redisIP, _redisPort, nullptr);
				connected = true;
			}
			cpp_redis::reply value_reply;
			tls_redis_client->get(key, [&value_reply](cpp_redis::reply& reply) {value_reply = reply; });
			tls_redis_client->sync_commit();
			if (value_reply.is_null())
			{
				return value;
			}
			if (value_reply.is_error())
			{
				MultiByteToWideChar(CP_ACP, 0, value_reply.error().c_str(), -1, buffer, _countof(buffer));

				Log::logging().Log(TAG_REDIS, Log::en_ERROR,
					L"Redis Context오류: %s", buffer);
				return value;
			}
			else
			{
				value = value_reply.as_string();
			}
			return value;
		}
		catch (cpp_redis::redis_error& e)
		{
			MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, _countof(buffer));

			Log::logging().Log(TAG_REDIS, Log::en_ERROR,
				L"Redis Connect오류(횟수: %d): %s", i + 1, buffer);
			connected = false;
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}


	return value;
}
