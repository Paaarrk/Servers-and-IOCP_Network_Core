#include "AuthContainer.h"
#include <cpp_redis/cpp_redis>
#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")
#pragma comment (lib, "ws2_32.lib")
#include "ChatServerV3.h"
#include "logclassV1.h"
#include "TlsPacket.hpp"
#include <charconv>

using Log = Core::c_syslog;
thread_local cpp_redis::client* CAuthContainer::tls_redis_client;
long CAuthContainer::s_threadId = -1;


CAuthContainer::CAuthContainer(): _hIOCP(NULL), _callInit(0), _requestLeft(0), _redisPort(0)
, _createThreadNum(0), _runningThreadNum(0), _hThreads(nullptr), _workerThreadNum(0)
{
	_redisIP.reserve(16);
}

CAuthContainer::~CAuthContainer()
{

}

bool CAuthContainer::Init(const std::string& redisIP, unsigned short redisPort,
	int32 createThreadNum, int32 runningThreadNum)
{
	if (_callInit == 1)
		return true;

	do
	{
		_redisIP = redisIP;
		_redisPort = redisPort;
		_createThreadNum = createThreadNum;
		_runningThreadNum = runningThreadNum;

		_hIOCP = (HANDLE)CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
		if (_hIOCP == NULL)
		{
			Log::logging().LogEx(TAG_AUTH_REDIS, GetLastError(), Log::en_ERROR, L"Create IOCP Failed");
			break;
		}

		_hThreads = new HANDLE[createThreadNum]();
		if (_hThreads == nullptr)
		{
			Log::logging().LogEx(TAG_AUTH_REDIS, GetLastError(), Log::en_ERROR, L"new[] failed");
			break;
		}

		for (int i = 0; i < createThreadNum; i++)
		{
			_hThreads[i] = (HANDLE)_beginthreadex(NULL, NULL, CAuthContainer::AuthThreadFunc, this, 0, NULL);
			if (_hThreads[i] == NULL)
			{
				uint32 ret = errno;
				Log::logging().LogEx(TAG_AUTH_REDIS, ret, Log::en_ERROR, L"beginthreadex failed");
				break;
			}
		}

		_InterlockedExchange(&_callInit, 1);
		return true;
	} while (0);
	
	Clear();
	return false;
}

void CAuthContainer::Clear()
{
	if (_hIOCP != NULL)
	{
		CloseHandle(_hIOCP);
		_hIOCP = NULL;
	}

	if(_hThreads != nullptr)
	{
		for (int i = 0; i < _createThreadNum; ++i)
		{
#pragma warning(push)
#pragma warning(disable : 6001)	// _hThreads는 NULL이 아님
			if (_hThreads[i] != NULL)
			{
				CloseHandle(_hThreads[i]);
			}
#pragma warning(pop)
		}
		delete[] _hThreads;
		_hThreads = nullptr;
	}
}

void CAuthContainer::Exit()
{
	if (_hIOCP != NULL)
	{
		for(int i = 0; i < _createThreadNum; i++)
			PostQueuedCompletionStatus(_hIOCP, 0, NULL, nullptr);
	}

	WaitForMultipleObjects(_createThreadNum, _hThreads, TRUE, INFINITE);

	Clear();
	_InterlockedExchange(&_callInit, 0);
}

void CAuthContainer::RequestAuth(CChatServer* pCallbackServer, uint64 sessionId, int64 accountNo, const std::string_view& sessionKey)
{
	CPACKET_CREATE(requestAuth);
	requestAuth->SetRecvBuffer();
	*requestAuth << ((uint64&)pCallbackServer);
	*requestAuth << (uint64)sessionId;
	*requestAuth << (int64)accountNo;
	requestAuth->PushData(sessionKey.data(), KEY_SIZE);

	CPACKET_ADDREF(requestAuth);
	PostQueuedCompletionStatus(_hIOCP, en_AUTH_REQUEST, (ULONG_PTR)requestAuth.GetCPacketPtr(), nullptr);
	_InterlockedIncrement(&_requestLeft);
}

bool  CAuthContainer::Connect()
{
	if (tls_redis_client == nullptr)
	{
		tls_redis_client = new cpp_redis::client;
	}

	bool connected = false;
	int cnt = 0;
	while (connected == false || cnt < 5)
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
			wchar_t buffer[512];
			MultiByteToWideChar(CP_ACP, 0, e.what(), -1,
				buffer, _countof(buffer));
			Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
				L"Redis 재연결 시도. (횟수: %d) [%s]",
				cnt, buffer);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	return connected;
}

void CAuthContainer::Disconnect()
{
	if (tls_redis_client == nullptr)
	{
		return;
	}

	tls_redis_client->disconnect();
	delete tls_redis_client;
	tls_redis_client = nullptr;
}

bool CAuthContainer::SendKeyValueEx(const std::string& key, const std::string& value, const std::string& validTimeSecond)
{
	if (tls_redis_client == nullptr)
	{
		bool ret = Connect();
		if (ret == false)
			return false;
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

				Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
					L"Redis Context오류: %s", buffer);
				return false;
			}
			return true;
		}
		catch (cpp_redis::redis_error& e)
		{
			MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, _countof(buffer));

			Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
				L"Redis Connect오류(횟수: %d): %s", i + 1, buffer);
			connected = false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return false;
}

std::string CAuthContainer::GetValue(const std::string& key)
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

				Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
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

			Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
				L"Redis Connect오류(횟수: %d): %s", i + 1, buffer);
			connected = false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}


	return value;
}

void CAuthContainer::DelValue(const std::string& key)
{
	wchar_t buffer[512];
	try
	{
		// 삭제 시도만, 60초뒤에 사라져서 결과가 중요하지는 않다
		if (tls_redis_client != nullptr)
		{
			tls_redis_client->del({ key }, nullptr);
			tls_redis_client->commit();
		}
	}
	catch (cpp_redis::redis_error& e)
	{
		MultiByteToWideChar(CP_ACP, 0, e.what(), -1, buffer, _countof(buffer));

		Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
			L"DelValue(): %s", buffer);
	}


	return;
}

unsigned int CAuthContainer::AuthThreadFunc(void* param)
{
	int32 myId = (int32)_InterlockedIncrement(&s_threadId);
	DWORD tid = GetCurrentThreadId();
	Log::logging().Log(TAG_AUTH_REDIS, Log::en_SYSTEM, L"[%2d] Auth Thread Start!", myId);
	CAuthContainer& myAuth = *((CAuthContainer*)param);

	bool exit = false;
	while (exit == false)
	{
		DWORD cbTransferred = 0;
		Net::CPacket* pMessage = nullptr;
		OVERLAPPED* pOverlapped = nullptr;

		GetQueuedCompletionStatus(myAuth._hIOCP, &cbTransferred, (ULONG_PTR*)&pMessage, (WSAOVERLAPPED**)&pOverlapped, INFINITE);

		Net::CPacketPtr autoFree(pMessage);
		if (pOverlapped == nullptr)
		{
			switch (cbTransferred)
			{
			case en_EXIT:
				exit = true;
				break;
			case  en_AUTH_REQUEST:
				_InterlockedDecrement(&myAuth._requestLeft);
				myAuth.RequestAuth(pMessage);
				break;
			}
		}
		else
		{
			Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR, L"[%p] overlap null아님", pOverlapped);
		}
	}

	Log::logging().Log(TAG_AUTH_REDIS, Log::en_SYSTEM, L"[%2d] Auth Thread End!", myId);

	if (tls_redis_client != nullptr)
		Disconnect();

	return 0;
}

void CAuthContainer::RequestAuth(Net::CPacket* pMessage)
{
	CChatServer* pServer;
	uint64 sessionId;
	int64 accountNo;
	do
	{
		*pMessage >> (uint64&)pServer;
		*pMessage >> (uint64&)sessionId;
		*pMessage >> (int64&)accountNo;
		pMessage->GetReadPtr();
		std::string_view clientKey(pMessage->GetReadPtr(), KEY_SIZE);

		char buffer[24];
		std::to_chars_result ret = std::to_chars(buffer, buffer + sizeof(buffer), accountNo);
		if (ret.ec != std::errc())
		{
			Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR, L"RequestAuth(message): accountNo가 제대로 번역이 안됨");
			break;
		}
		*ret.ptr = '\0';
		// 레디스에서 얻기
		std::string identifiedKey = std::move(GetValue(buffer));
		DelValue(buffer);

		if (clientKey != identifiedKey)
		{
			Log::logging().Log(TAG_AUTH_REDIS, Log::en_ERROR,
				L"[sessionId: %lld / accountNo:%lld] 세션 키가 다름. (Redis세션키 길이: %lld, 0이면 연결 끊겼거나 로그인 서버 안거침))"
				, sessionId, accountNo, identifiedKey.size());
			break;
		}


		//----------------------------------------
		// 결과를 알림
		//----------------------------------------
		CPACKET_CREATE(postIdentifyResultPacket);
		postIdentifyResultPacket->SetRecvBuffer();
		*postIdentifyResultPacket << CChatServer::PQCS_IDENTIFYING;
		*postIdentifyResultPacket << sessionId;
		*postIdentifyResultPacket << accountNo;
		pServer->PostUserEvent(postIdentifyResultPacket.GetCPacketPtr());
		return;
	} while (0);

	pServer->Disconnect(sessionId);
	return;
}