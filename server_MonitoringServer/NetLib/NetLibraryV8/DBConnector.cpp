#include "DBConnector.h"

#include <synchapi.h>
#include <windows.h>
#pragma comment(lib, "winmm")

#include "TlsPacketV4.h"

//----------------------------------------------------------
// 일반 버전
//----------------------------------------------------------

int CMySqlConnector::Connect(int tryCnt, DWORD tick)
{
	MYSQL* connection = NULL;
	int cnt;
	for(cnt = 0; cnt < tryCnt; cnt++)
	{
#ifndef MYSQL_MULTI_CONNECTION
		connection = mysql_real_connect(&_conn,
			_host, _id, _pw, _schema, _port, NULL, 0);
#else
		connection = mysql_real_connect(&_conn,
			_host, _id, _pw, _schema, _port, NULL, CLIENT_MULTI_STATEMENTS);
#endif


		if (connection != NULL)
		{
			_connectCnt += cnt;
			break;
		}
		LogSQLError();
		Sleep(10);
		mysql_close(&_conn);
		mysql_init(&_conn);
	}
	if (connection == NULL)
		return 1;
	return 0;
}

int CMySqlConnector::RequestQuery(const wchar_t* wformat, ...)
{
	va_list args;
	va_start(args, wformat);
	vswprintf_s(_curWQuery, QUERY_SIZE, wformat, args);
	va_end(args);
	WideCharToMultiByte(CP_UTF8, 0, _curWQuery, -1, _curAQuery, QUERY_SIZE * 4, 0, 0);

	if (mysql_ping(&_conn) != 0)
	{
		if (Connect() != 0)
		{
			LogSQLError();
			return 1;
		}
	}
	DWORD startTime = timeGetTime();
	DWORD endTime;
	uint64_t deltaTime;
	if (mysql_query(&_conn, _curAQuery) == 0)
	{
		endTime = timeGetTime();
		deltaTime = (endTime - startTime);
		_avgTime = (_avgTime * _count + deltaTime) / (_count + 1);
		if (deltaTime > _maxDeltaTime)
			_maxDeltaTime = deltaTime;
		RegisterSlowQuery(_curWQuery, deltaTime);
		++_count;
		return 0;
	}

	unsigned int err = mysql_errno(&_conn);
	if (err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST)
	{
		int ret = Connect();
		if (ret != 0)
		{
			LogSQLError();
			return 1;
		}

		startTime = timeGetTime();
		if (mysql_query(&_conn, _curAQuery) == 0)
		{
			endTime = timeGetTime();
			deltaTime = (endTime - startTime);
			_avgTime = (_avgTime * _count + deltaTime) / (_count + 1);
			if (deltaTime > _maxDeltaTime)
				_maxDeltaTime = deltaTime;

			if (deltaTime >= TIMING_HOW_MS_WRITE_SLOW_SET)
			{
				_log->Log(TAG_DB, c_syslog::en_SYSTEM, L"[** WARN **] [%s, %dms]",
					_curWQuery, deltaTime);
			}

			RegisterSlowQuery(_curWQuery, deltaTime);
			++_count;
			return 0;
		}

		err = mysql_errno(&_conn);
	}

	LogSQLError();
	if (err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST)
		return 1;

	_log->Log(TAG_DB, c_syslog::en_ERROR, L"Context Error: (Query) %s", _curWQuery);
	return err;
}

void CMySqlConnector::RegisterSlowQuery(const wchar_t* wQuery, uint64_t deltaTime)
{
	if (_querySendCnt++ < REGISTER_SLOW_QUERY_TIMING)
	{
		return;
	}
	uint64_t flag = 0xFFFF'FFFF'FFFF'FFFF;
	int minIndex = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (_slowQueryTop5[i].deltaTime < flag)
		{
			flag = _slowQueryTop5[i].deltaTime;
			minIndex = i;
		}
	}

	stQueryInfo* pQi = &_slowQueryTop5[minIndex];
	if (pQi->deltaTime < deltaTime)
	{
		pQi->deltaTime = deltaTime;
		wcscpy_s(pQi->query, QUERY_SIZE, wQuery);
	}

	WriteSlowQuery(true);
}

MYSQL_RES* CMySqlConnector::GetResult() 
{
	_sql_result = mysql_store_result(&_conn);
	return _sql_result; 
}

MYSQL_RES* CMySqlConnector::GetNextResult()
{
	if (mysql_next_result(&_conn) == 0)
	{
		_sql_result = mysql_store_result(&_conn);
		return _sql_result;
	}
	else
		return nullptr;
}

void CMySqlConnector::FreeResult()
{
	mysql_free_result(_sql_result);
}

void CMySqlConnector::LogSQLError()
{
	wchar_t error[512];
	unsigned int errorNo = mysql_errno(&_conn);
	MultiByteToWideChar(CP_UTF8, 0, mysql_error(&_conn), -1, error, _countof(error));

	_log->Log(TAG_DB, c_syslog::en_ERROR,
		L"RequestQuery MySql Error(%u), %s", errorNo, error);
}

void CMySqlConnector::ReleaseConn()
{
	mysql_close(&_conn);
	WriteSlowQuery();
}

void CMySqlConnector::WriteSlowQuery(bool useTiming)
{
	if (useTiming && (_querySendCnt % TIMING_WHEN_WRITE_SLOW_SET != 0))
	{
		return;
	}

	for (int i = 0; i < 5; i++)
	{
		stQueryInfo* pQi = &_slowQueryTop5[i];
		if (pQi->deltaTime > 0)
		{
			_log->Log(TAG_DB, c_syslog::en_SYSTEM, L"[%s, %dms]",
				pQi->query, pQi->deltaTime);
		}
	}
}


//----------------------------------------------------------
// Tls버전
//----------------------------------------------------------

thread_local CTlsMySqlConnector::stConn* CTlsMySqlConnector::tls_conn;

int CTlsMySqlConnector::Connect(int tryCnt, DWORD tick)
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		pConn = new stConn();
		mysql_init(&pConn->_conn);
		pConn->bInit = true;
		tls_conn = pConn;
	}

	MYSQL* connection = NULL;
	int cnt;
	for (cnt = 0; cnt < tryCnt; cnt++)
	{
#ifndef MYSQL_MULTI_CONNECTION
		connection = mysql_real_connect(&pConn->_conn,
			_host, _id, _pw, _schema, _port, NULL, 0);
#else
		connection = mysql_real_connect(&_conn,
			_host, _id, _pw, _schema, _port, NULL, CLIENT_MULTI_STATEMENTS);
#endif


		if (connection != NULL)
		{
			pConn->_connectCnt += cnt;
			break;
		}
		LogSQLError();
		Sleep(10);
		mysql_close(&pConn->_conn);
		memset(&pConn->_conn, 0, sizeof(MYSQL));
		mysql_init(&pConn->_conn);
	}
	if (connection == NULL)
		return 1;
	return 0;
}


int CTlsMySqlConnector::RequestQuery(const wchar_t* wformat, ...)
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		pConn = new stConn();
		mysql_init(&pConn->_conn);
		pConn->bInit = true;
		tls_conn = pConn;
		Connect();
	}

	va_list args;
	va_start(args, wformat);
	vswprintf_s(pConn->_curWQuery, QUERY_SIZE, wformat, args);
	va_end(args);
	WideCharToMultiByte(CP_UTF8, 0, pConn->_curWQuery, -1, pConn->_curAQuery, QUERY_SIZE * 4, 0, 0);

	if (mysql_ping(&pConn->_conn) != 0)
	{
		if (Connect() != 0)
		{
			LogSQLError();
			return 1;
		}
	}
	DWORD startTime = timeGetTime();
	DWORD endTime;
	uint64_t deltaTime;
	if (mysql_query(&pConn->_conn, pConn->_curAQuery) == 0)
	{
		endTime = timeGetTime();
		deltaTime = (endTime - startTime);
		pConn->_avgTime = (pConn->_avgTime * pConn->_count + deltaTime) / (pConn->_count + 1);
		if (deltaTime > pConn->_maxDeltaTime)
			pConn->_maxDeltaTime = deltaTime;
		if (deltaTime >= TIMING_HOW_MS_WRITE_SLOW_SET)
		{
			_log->Log(TAG_DB, c_syslog::en_SYSTEM, L"[** WARN **] [%s, %dms]",
				pConn->_curWQuery, deltaTime);
		}
		RegisterSlowQuery(pConn->_curWQuery, deltaTime);
		++pConn->_count;
		return 0;
	}

	unsigned int err = mysql_errno(&pConn->_conn);
	if (err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST)
	{
		int ret = Connect();
		if (ret != 0)
		{
			LogSQLError();
			return 1;
		}

		startTime = timeGetTime();
		if (mysql_query(&pConn->_conn, pConn->_curAQuery) == 0)
		{
			endTime = timeGetTime();
			deltaTime = (endTime - startTime);
			pConn->_avgTime = (pConn->_avgTime * pConn->_count + deltaTime) / (pConn->_count + 1);
			if (deltaTime > pConn->_maxDeltaTime)
				pConn->_maxDeltaTime = deltaTime;
			RegisterSlowQuery(pConn->_curWQuery, deltaTime);
			++pConn->_count;
			return 0;
		}

		err = mysql_errno(&pConn->_conn);
	}

	LogSQLError();
	if (err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST)
		return 1;

	_log->Log(TAG_DB, c_syslog::en_ERROR, L"Context Error: (Query) %s", pConn->_curWQuery);
	return 2;
}


void CTlsMySqlConnector::RegisterSlowQuery(const wchar_t* wQuery, uint64_t deltaTime)
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return;
	}

	if (++(pConn->_querySendCnt) < TIMING_REGISTER_SLOW_QUERY)
	{
		return;
	}

	uint64_t flag = 0xFFFF'FFFF'FFFF'FFFF;
	int minIndex = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (pConn->_slowQueryTop5[i].deltaTime < flag)
		{
			flag = pConn->_slowQueryTop5[i].deltaTime;
			minIndex = i;
		}
	}

	stQueryInfo* pQi = &pConn->_slowQueryTop5[minIndex];
	if (pQi->deltaTime < deltaTime)
	{
		pQi->deltaTime = deltaTime;
		wcscpy_s(pQi->query, QUERY_SIZE, wQuery);
	}

	WriteSlowQuery(true);
}


MYSQL_RES* CTlsMySqlConnector::GetResult()
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return NULL;
	}
	pConn->_sql_result = mysql_store_result(&pConn->_conn);
	return pConn->_sql_result;
}

MYSQL_RES* CTlsMySqlConnector::GetNextResult()
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return NULL;
	}
	if (mysql_next_result(&pConn->_conn) == 0)
	{
		pConn->_sql_result = mysql_store_result(&pConn->_conn);
		return pConn->_sql_result;
	}
	else
		return nullptr;
}

void CTlsMySqlConnector::FreeResult()
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return;
	}
	mysql_free_result(pConn->_sql_result);
}

void CTlsMySqlConnector::LogSQLError()
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return;
	}
	wchar_t error[512];
	unsigned int errorNo = mysql_errno(&pConn->_conn);
	MultiByteToWideChar(CP_UTF8, 0, mysql_error(&pConn->_conn), -1, error, _countof(error));

	_log->Log(TAG_DB, c_syslog::en_ERROR,
		L"RequestQuery MySql Error(%u), %s", errorNo, error);
}

void CTlsMySqlConnector::ReleaseConn()
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return;
	}

	mysql_close(&pConn->_conn);

	WriteSlowQuery();

	delete pConn;
	tls_conn = nullptr;
}

void CTlsMySqlConnector::WriteSlowQuery(bool useTiming)
{
	stConn* pConn = tls_conn;
	if (pConn == nullptr)
	{
		return;
	}

	if (useTiming && (pConn->_querySendCnt % TIMING_WHEN_WRITE_SLOW_SET != 0))
	{
		return;
	}

	for(int i = 0; i < 5; i++)
	{
		stQueryInfo* pQi = &pConn->_slowQueryTop5[i];
		if (pQi->deltaTime > 0)
		{
			_log->Log(TAG_DB, c_syslog::en_SYSTEM, L"[%s, %dms]",
				pQi->query, pQi->deltaTime);
		}
	}
}

