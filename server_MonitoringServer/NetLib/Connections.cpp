#include "MonitoringServer.h"
#include "Connections.h"
#include "logclassV1.h"

///////////////////////////////////////////////////
// stConnection
///////////////////////////////////////////////////

CTlsObjectPool<stConnection, stConnection::POOL_KEY,
	TLS_OBJECTPOOL_USE_CALLONCE> stConnection::s_pool;

///////////////////////////////////////////////////
// CConnectionManager
///////////////////////////////////////////////////

//-------- Server --------

void CConnectionManager::CreateServerConnection(uint64_t sessionId)
{
	stConnection* pConn = stConnection::Alloc();
	pConn->sessionId = sessionId;
	pConn->status = stConnection::STATUS_WAIT_LOGIN;
	pConn->connectTime = timeGetTime();
	pConn->recvTime = pConn->connectTime;
	pConn->clearInfo();

	_serverLock.lock();
	
	_serverMap.insert({ sessionId, pConn });

	_serverLock.unlock();
}

bool CConnectionManager::LoginServerConnection(uint64_t sessionId, int ServerNo, uint64_t& needDisconnectSessionId)
{
	bool ret = true;
	_serverLock.lock();

	auto it = _serverMap.find(sessionId);
	if (it == _serverMap.end())
	{
		// 이미 끊긴 연결
		_serverLock.unlock();
		return true;
	}
	stConnection* pConn = it->second;

	auto it2 = _serverNoMap.find(ServerNo);
	if (it2 != _serverNoMap.end())
	{
		needDisconnectSessionId = it2->second;
		auto it = _serverMap.find(needDisconnectSessionId);
		if (it != _serverMap.end())
		{
			stConnection* pRelease = it->second;
			_serverMap.erase(needDisconnectSessionId);
			pRelease->lock.lock();
			stConnection::Release(pRelease);
			pRelease->lock.unlock();
		}

		it2->second = sessionId;
		ret = false;
	}
	else
	{
		_serverNoMap.insert({ ServerNo, sessionId });
	}
	_serverLock.unlock();

	pConn->lock.lock();

	if (pConn->sessionId != sessionId)
	{
		pConn->lock.unlock();
		return true;
	}
	
	switch (ServerNo)
	{
	case en_SERVER_NO::Login:
		pConn->status = stConnection::STATUS_SERVER_LOGIN;
		break;
	case en_SERVER_NO::Chat:
		pConn->status = stConnection::STATUS_SERVER_CHAT;
		break;
	case en_SERVER_NO::Game:
		pConn->status = stConnection::STATUS_SERVER_GAME;
		break;
	default:
		__debugbreak();
		break;
	}
	pConn->serverNo = ServerNo;
	pConn->recvTime = timeGetTime();

	pConn->lock.unlock();

	

	return ret;
}

int CConnectionManager::UpdateServerConnection(uint64_t sessionId, BYTE dataType, int dataValue, int timeStamp)
{
	_serverLock.lock_shared();

	auto it = _serverMap.find(sessionId);
	if (it == _serverMap.end())
	{
		_serverLock.unlock_shared();
		return -1;
	}

	stConnection* pConn = it->second;
	_serverLock.unlock_shared();

	pConn->lock.lock();
	if (pConn->sessionId != sessionId)
	{
		// 이미 릴리즈됨
		pConn->lock.unlock();
		return -1;
	}
	int ServerNo = pConn->serverNo;
	pConn->recvTime = timeGetTime();

	int index = dataType;
	switch (pConn->serverNo)
	{
	case en_SERVER_NO::Login:
		index -= dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN;
		break;
	case en_SERVER_NO::Chat:
		index -= dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN;
		break;
	case en_SERVER_NO::Game:
		index -= dfMONITOR_DATA_TYPE_GAME_SERVER_RUN;
		break;
	default:
		__debugbreak();
		break;
	}

	pConn->info[index].set(dataValue, timeStamp);

	pConn->lock.unlock();
	return ServerNo;
}

void CConnectionManager::ReleaseServerConnection(uint64_t sessionId)
{
	_serverLock.lock();

	auto it = _serverMap.find(sessionId);
	if (it == _serverMap.end())
	{
		_serverLock.unlock();
		return;
	}
	
	int serverNo;
	stConnection* pConn = it->second;
	_serverMap.erase(sessionId);

	_serverLock.unlock();

	pConn->lock.lock();
	if (pConn->sessionId != sessionId)
	{
		pConn->lock.unlock();
		return;
	}
	
	serverNo = pConn->serverNo;
	pConn->clear();
	pConn->lock.unlock();

	stConnection::Release(pConn);

	_serverLock.lock();

	auto it2 = _serverNoMap.find(serverNo);
	if (it2 != _serverNoMap.end() && (it2->second == sessionId))
	{
		_serverNoMap.erase(serverNo);
	}

	_serverLock.unlock();
}

bool CConnectionManager::GetServerNosOfConnectedServer(int* pBuffer, int& cnt)
{
	cnt = 0;

	_serverLock.lock_shared();

	auto end = _serverMap.end();
	for (auto it = _serverMap.begin(); it != end; ++it)
	{
		stConnection* pServerConn = it->second;
		if (pServerConn->status != stConnection::STATUS_WAIT_LOGIN)
		{
			pBuffer[cnt++] = pServerConn->serverNo;
		}
	}

	_serverLock.unlock_shared();

	return (cnt != 0);
}

std::vector<std::unique_ptr<stServerMonitorInfo>> CConnectionManager::GetServerStatusSnapshot()
{
	std::vector<std::unique_ptr<stServerMonitorInfo>> v;
	std::vector<uint64_t> v_sids;
	std::vector<stConnection*> v_conns;
	int cnt = 0;

	{
		std::shared_lock<std::shared_mutex> guard(_serverLock);

		size_t size = _serverMap.size();
		v.reserve(size);
		v_sids.reserve(size);
		v_conns.reserve(size);

		auto it_end = _serverMap.end();
		for (auto it = _serverMap.begin(); it != it_end; ++it)
		{
			stConnection* pConn = it->second;
			v_sids.push_back(it->first);
			v_conns.push_back(pConn);
			cnt++;
		}
	}

	for (int i = 0; i < cnt; i++)
	{
		uint64_t sessionId = v_sids[i];
		stConnection* pConn = v_conns[i];

		{
			std::unique_lock<std::shared_mutex> guard(pConn->lock);
			if (sessionId != pConn->sessionId)
			{
				continue;
			}

			std::unique_ptr<stServerMonitorInfo> uqptrInfo = std::make_unique<stServerMonitorInfo>();

			uqptrInfo->serverNo = pConn->serverNo;
			switch (pConn->serverNo)
			{
			case en_SERVER_NO::System:
				uqptrInfo->endIndex = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_MONITOR_END
					- en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL;
				uqptrInfo->startNum = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL;
				break;
			case en_SERVER_NO::Login:
				uqptrInfo->endIndex = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_END
					- en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN;
				uqptrInfo->startNum = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN;
				break;
			case en_SERVER_NO::Chat:
				uqptrInfo->endIndex = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_END
					- en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN;
				uqptrInfo->startNum = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN;
				break;
			case en_SERVER_NO::Game:
				uqptrInfo->endIndex = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_END
					- en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SERVER_RUN;
				uqptrInfo->startNum = en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SERVER_RUN;
				break;
			default:
				_pLog->Log(TAG_CONTENTS, c_syslog::en_ERROR,
					L"GetServerStatusSnapshot(): serverNo 알수 없음, serverNo: %d", pConn->serverNo);
				continue;
			}

			memcpy(uqptrInfo->data, pConn->info, sizeof(stTypeInfo) * (uqptrInfo->endIndex));
			v.emplace_back(std::move(uqptrInfo));
			pConn->clearInfo();
		}
	}

	return v;
}

//-------- Client --------

void CConnectionManager::CreateClientConnection(uint64_t sessionId)
{
	stConnection* pConn = stConnection::Alloc();
	pConn->sessionId = sessionId;
	pConn->status = stConnection::STATUS_WAIT_LOGIN;
	pConn->connectTime = timeGetTime();

	_clientLock.lock();

	_clientMap.insert({ sessionId, pConn });

	_clientLock.unlock();
}

bool CConnectionManager::LoginClientConnection(uint64_t sessionId, const char* loginKey, int len)
{
	if (len != SESSION_KEY_LEN)
	{
		_pLog->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"[sessionId: %llx] 로그인 키 길이가 다름", sessionId);
		return false;
	}

	if (memcmp(loginKey, SESSION_KEY, SESSION_KEY_LEN) != 0)
	{
		_pLog->Log(TAG_CONTENTS, c_syslog::en_ERROR,
			L"[sessionId: %llx] 로그인 키 내용이 다름", sessionId);
		return false;
	}

	_clientLock.lock();
	
	auto it = _clientMap.find(sessionId);
	if (it == _clientMap.end())
	{
		_clientLock.unlock();
		return true;
	}

	stConnection* pConn = it->second;

	_clientLock.unlock();

	int retnum = _loginedClientNum.fetch_add(1);
	if (retnum == _maxClient)
	{
		_loginedClientNum.fetch_sub(1);
		_pLog->Log(TAG_CONTENTS, c_syslog::en_SYSTEM,
			L"[sessionId: %llx] 모니터링 클라이언트 꽉참, 끊어주세요");
		return false;
	}

	pConn->lock.lock();
	if (pConn->sessionId != sessionId)
	{
		pConn->lock.unlock();
		return true;	// OnRelease호출 예정이라서
	}

	pConn->status = stConnection::STATUS_CLIENT;

	pConn->lock.unlock();
	return true;
}

void CConnectionManager::ReleaseClientConnection(uint64_t sessionId)
{
	_clientLock.lock();

	auto it = _clientMap.find(sessionId);
	if (it == _clientMap.end())
	{
		// 이미 사라진 세션
		_clientLock.unlock();
		return;
	}

	stConnection* pConn = it->second;

	_clientMap.erase(sessionId);
	_clientLock.unlock();

	pConn->lock.lock();
	if (pConn->sessionId != sessionId)
	{
		pConn->lock.unlock();
		return;
	}

	if (pConn->status == stConnection::STATUS_CLIENT)
		_loginedClientNum.fetch_sub(1);
	
	pConn->clear();
	pConn->lock.unlock();

	stConnection::Release(pConn);
}

bool CConnectionManager::GetMonitoringClientSessionIds(uint64_t* pBuffer, int& cnt)
{
	_clientLock.lock_shared();

	cnt = 0;
	stConnection* pConn;
	auto it_end = _clientMap.end();
	for (auto it = _clientMap.begin(); it!=it_end; ++it)
	{
		pConn = it->second;
		// map에 락을 걸어서 pConn이 지워질 일은 없고,
		// status만 확인하면 된다. 
		// (락이 필요없음, 읽는 도중 상태가 변경되는것은 현 상황에서 문제가 없음)
		// 여기서는 waitlogin -> status_client의 상태 변화만 존재하기 때문
		if (pConn->status == stConnection::STATUS_CLIENT)
		{
			pBuffer[cnt++] = pConn->sessionId;
		}
	}

	_clientLock.unlock_shared();

	return (cnt != 0);
}


//////////////////////////////////////////////////////////////////
// TimeOut 
//////////////////////////////////////////////////////////////////
void CConnectionManager::TimeOutFunc()
{
	DWORD tid = GetCurrentThreadId();
	_pLog->Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"Timeout-Thread Start!!");

	HANDLE hExit = _pConnectServer->GetExitHandle();

	while (1)
	{
		DWORD ret = WaitForSingleObject(hExit, CHECK_TIMEOUT_PERIOD);
		if (ret == WAIT_OBJECT_0)
		{
			break;
		}

		DWORD curtime = timeGetTime();

		if (_serverMap.size() != 0)
		{
			std::shared_lock<std::shared_mutex> guard(_serverLock);
			
			auto it_end = _serverMap.end();
			for (auto it = _serverMap.begin(); it != it_end; ++it)
			{
				stConnection* pConn = it->second;
				if ((int)(curtime - pConn->recvTime) > CONNECT_SERVER_TIMEOUT)
				{
					_pLog->Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"서버 연결이 한번 끊김");
					_pConnectServer->Disconnect(it->first);
				}
			}
		}

		if (_clientMap.size() != 0)
		{
			std::shared_lock<std::shared_mutex> guard(_clientLock);
			
			auto it_end = _clientMap.end();
			for (auto it = _clientMap.begin(); it != it_end; ++it)
			{
				stConnection* pConn = it->second;
				if ((int)(curtime - pConn->connectTime) > CLIENT_WAIT_LOGIN_TIMEOUT
					&& pConn->status == stConnection::STATUS_WAIT_LOGIN)
				{
					_pClientServer->Disconnect(it->first);
				}
			}
		}
	}

	_pLog->Log(TAG_CONTENTS, c_syslog::en_SYSTEM, L"Timeout-Thread Exit!!");
}
