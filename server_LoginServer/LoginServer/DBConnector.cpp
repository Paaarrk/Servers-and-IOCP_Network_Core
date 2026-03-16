#include "DBConnector.h"
#include "errmsg.h"
#pragma comment(lib, "mysqlclient.lib")

#include <synchapi.h>
#include <windows.h>
#pragma comment(lib, "winmm")

#include "logclassV1.h"
using Log = Core::c_syslog;

//----------------------------------------------------------
// Tls버전
//----------------------------------------------------------

thread_local CTlsMySqlConnector::stConn* CTlsMySqlConnector::tls_conn;

CTlsMySqlConnector::CTlsMySqlConnector(const char* hostIP,
	const char* id, const char* pw,
	const char* schema, unsigned short port)
	: _host{}, _id(nullptr), _pw(nullptr), _schema(nullptr),
	_port(port)
{
	mysql_library_init(0, NULL, NULL);

	if (hostIP != nullptr)
		strcpy_s(_host, hostIP);

	if (id != nullptr)
	{
		size_t idlen = strlen(id) + 1;
		_id = (char*)malloc(idlen);
		if (_id == nullptr)
			__debugbreak();
		strcpy_s(_id, idlen, id);
	}

	if (pw != nullptr)
	{
		size_t pwlen = strlen(pw) + 1;
		_pw = (char*)malloc(pwlen);
		if (_pw == nullptr)
			__debugbreak();
		strcpy_s(_pw, pwlen, pw);
	}

	if (schema != nullptr)
	{
		size_t schemalen = strlen(schema) + 1;
		_schema = (char*)malloc(schemalen);
		if (_schema == nullptr)
			__debugbreak();
		strcpy_s(_schema, schemalen, schema);
	}
}

CTlsMySqlConnector::~CTlsMySqlConnector()
{
	if (_id != nullptr)
		free(_id);
	if (_pw != nullptr)
		free(_pw);
	if (_schema != nullptr)
		free(_schema);
}

void CTlsMySqlConnector::SetConnector(const char* hostIP, const char* id, const char* pw,
	const char* schema, unsigned short port)
{
	if (hostIP != nullptr)
		strcpy_s(_host, hostIP);

	if (id != nullptr)
	{
		size_t idlen = strlen(id) + 1;
		_id = (char*)malloc(idlen);
		if (_id == nullptr)
			__debugbreak();
		strcpy_s(_id, idlen, id);
	}

	if (pw != nullptr)
	{
		size_t pwlen = strlen(pw) + 1;
		_pw = (char*)malloc(pwlen);
		if (_pw == nullptr)
			__debugbreak();
		strcpy_s(_pw, pwlen, pw);
	}

	if (schema != nullptr)
	{
		size_t schemalen = strlen(schema) + 1;
		_schema = (char*)malloc(schemalen);
		if (_schema == nullptr)
			__debugbreak();
		strcpy_s(_schema, schemalen, schema);
	}

	_port = port;
}

int CTlsMySqlConnector::Connect(int tryCnt, unsigned long tick)
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
			Log::logging().Log(TAG_DB, Log::en_SYSTEM, L"[** WARN **] [%s, %dms]",
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

	Log::logging().Log(TAG_DB, Log::en_ERROR, L"Context Error: (Query) %s", pConn->_curWQuery);
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

	Log::logging().Log(TAG_DB, Log::en_ERROR,
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
			Log::logging().Log(TAG_DB, Log::en_SYSTEM, L"[%s, %dms]",
				pQi->query, pQi->deltaTime);
		}
	}
}




/*
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
	return 2;
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
*/


/*
void CDBWriter::DBWriterProc()
{
	DWORD tid = GetCurrentThreadId();
	_plog->Log(TAG_DB, c_syslog::en_SYSTEM, L"DBWriter Start!!");
	_conn.SetConnector("127.0.0.1", "", "", "testdb", 3306);
	_conn.SetLog(_plog);

	HANDLE hEvents[2] = { _hExit, _hEnqueueEvent};
	while (1)
	{
		QueueProc();

		DWORD ret = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
		if (ret == WAIT_OBJECT_0)
		{
			QueueProc();
			break;
		}
	}

	_conn.ReleaseConn();
	_plog->Log(TAG_DB, c_syslog::en_SYSTEM, L"DBWriter End!!");
	return;
}

void CDBWriter::QueueProc()
{
	while (_queue.isEmpty() == false)
	{
		CPacket* pMessage;
		WORD type;
		if (_queue.Dequeue(pMessage))
		{
			_InterlockedIncrement(&curDeqCnt);
			*pMessage >> type;
			switch (type)
			{
			case en_DBQUERY::CREATE_WEAPON:
				RequestCreateWeapon(pMessage);
				break;

			case en_DBQUERY::GET_WEAPON_INFO:
				RequestGetWeaponInfo(pMessage);
				break;

			case en_DBQUERY::UPDATE_WEAPON:
				RequestUpdateWeapon(pMessage);
				break;

			default:
				_plog->Log(TAG_DB, c_syslog::en_ERROR,
					L"타입이 이상한 메시지 (%d)", type);
				break;
			}

			CPACKET_FREE(pMessage);
		}
	}
}

void CDBWriter::RequestCreateWeapon(CPacket* pMessage)
{
#ifdef MYSQL_MULTI_CONNECTION
	st_DBQUERY_CREATE_ITEM message;
	LARGE_INTEGER enqueueTime;
	*pMessage >> message.itemId >> message.itemAttack >>
		message.itemDefense >> message.itemStr >> message.itemDex
		>> message.itemInt >> message.itemLuk >> enqueueTime.QuadPart;

	char querystring[2048] = {};
	int len = 0;
#ifdef TRANSACTION
	len += sprintf_s(querystring, 2048, "begin;");
#endif

	len += sprintf_s(querystring + len, 2047 - len, st_DBQUERY_CREATE_ITEM::QUERY_FORMAT,
		message.itemId, message.itemAttack, message.itemDefense,
		message.itemStr, message.itemDex, message.itemInt,
		message.itemLuk);

	//len += sprintf_s(querystring + len, 2047 - len, st_DBQUERY_CREATE_ITEM::QUERY_FORMAT,
	//	message.itemId, message.itemAttack, message.itemDefense,
	//	message.itemStr, message.itemDex, message.itemInt,
	//	message.itemLuk);
	//
	//len += sprintf_s(querystring + len, 2047 - len, st_DBQUERY_CREATE_ITEM::QUERY_FORMAT,
	//	message.itemId, message.itemAttack, message.itemDefense,
	//	message.itemStr, message.itemDex, message.itemInt,
	//	message.itemLuk);

#ifdef TRANSACTION
	len += sprintf_s(querystring + len, 2047 - len, "commit;");
#endif

	LARGE_INTEGER startTime;
	QueryPerformanceCounter(&startTime);

	int retQueryB = _conn.RequestQuery(querystring);
	if (retQueryB != 0)
	{
		__debugbreak();
	}

	LARGE_INTEGER finTime;
	QueryPerformanceCounter(&finTime);
	
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	double ldbTime = (double)(finTime.QuadPart - startTime.QuadPart) / freq.QuadPart * 1000'000;
	double ltotaltime = (double)(finTime.QuadPart - enqueueTime.QuadPart) / freq.QuadPart * 1000'000;
	cnt++;
	dbTotalTime += ldbTime;
	totalTime += ltotaltime;
	if (dbMaxTime < ldbTime)
		dbMaxTime = ldbTime;
	if (maxTime < ltotaltime)
		maxTime = ltotaltime;

	do
	{
		MYSQL_RES* res = _conn.GetResult();
		if (res) 
			mysql_free_result(res);
	} while (mysql_next_result(&_conn._conn) == 0);
#else
	st_DBQUERY_CREATE_ITEM message;
	LARGE_INTEGER enqueueTime;
	*pMessage >> message.itemId >> message.itemAttack >>
		message.itemDefense >> message.itemStr >> message.itemDex
		>> message.itemInt >> message.itemLuk >> enqueueTime.QuadPart;

	char querystring[512] = {};

#ifdef TRANSACTION
	wchar_t begin[16] = L"begin;";
	wchar_t commit[16] = L"commit";
#endif

	LARGE_INTEGER startTime;
	QueryPerformanceCounter(&startTime);
#ifdef TRANSACTION
	int retQueryA = _conn.RequestQuery(begin);
	if (retQueryA != 0)
	{
		__debugbreak();
	}
#endif
	int retQueryB = _conn.RequestQuery(st_DBQUERY_CREATE_ITEM::QUERY_FORMAT, message.itemId,
		message.itemAttack, message.itemDefense, message.itemStr, message.itemDex, message.itemInt,
		message.itemLuk);
	if (retQueryB != 0)
	{
		__debugbreak();
	}
	int retQueryC = _conn.RequestQuery(st_DBQUERY_CREATE_ITEM::QUERY_FORMAT, message.itemId,
		message.itemAttack, message.itemDefense, message.itemStr, message.itemDex, message.itemInt,
		message.itemLuk);
	if (retQueryC != 0)
	{
		__debugbreak();
	}
	int retQueryD = _conn.RequestQuery(st_DBQUERY_CREATE_ITEM::QUERY_FORMAT, message.itemId,
		message.itemAttack, message.itemDefense, message.itemStr, message.itemDex, message.itemInt,
		message.itemLuk);
	if (retQueryD != 0)
	{
		__debugbreak();
	}

#ifdef TRANSACTION
	int retQueryE = _conn.RequestQuery(commit);
	if (retQueryE != 0)
	{
		__debugbreak();
	}
#endif
	LARGE_INTEGER finTime;
	QueryPerformanceCounter(&finTime);

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	double ldbTime = (double)(finTime.QuadPart - startTime.QuadPart) / freq.QuadPart * 1000'000;
	double ltotaltime = (double)(finTime.QuadPart - enqueueTime.QuadPart) / freq.QuadPart * 1000'000;
	cnt++;
	dbTotalTime += ldbTime;
	totalTime += ltotaltime;
	if (dbMaxTime < ldbTime)
		dbMaxTime = ldbTime;
	if (maxTime < ltotaltime)
		maxTime = ltotaltime;
#endif
}

void CDBWriter::RequestCreateWeapon()
{
	LARGE_INTEGER enqStart;
	QueryPerformanceCounter(&enqStart);
	CPACKET_CREATE(createWeapon);
	CPACKET_ADDREF(createWeapon.GetCPacketPtr());
	createWeapon->SetRecvBuffer();
	*createWeapon << en_DBQUERY::CREATE_WEAPON
	<< (int)10000 + rand() % 1000 << (int)rand() % 100 << (int)0
		<< (int)rand() % 10 << (int)rand() % 10 << (int)rand() % 10 << (int)rand() % 10
		<< enqStart.QuadPart;

	_queue.Enqueue_NotFail(createWeapon.GetCPacketPtr());
	SetEvent(_hEnqueueEvent);
}


void CDBWriter::RequestGetWeaponInfo(CPacket* pMessage)
{
	st_DBQUERY_GET_WEAPON_INFO message;
	LARGE_INTEGER enqueueTime;
	*pMessage >> message.itemKey >>  enqueueTime.QuadPart;

	LARGE_INTEGER startTime;
	QueryPerformanceCounter(&startTime);
	int retQuery = _conn.RequestQuery(st_DBQUERY_GET_WEAPON_INFO::QUERY_FORMAT,
		message.itemKey);
	if (retQuery != 0)
	{
		__debugbreak();
	}


	LARGE_INTEGER finTime;
	QueryPerformanceCounter(&finTime);

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	double ldbTime = (double)(finTime.QuadPart - startTime.QuadPart) / freq.QuadPart * 1000'000;
	double ltotaltime = (double)(finTime.QuadPart - enqueueTime.QuadPart) / freq.QuadPart * 1000'000;
	cnt++;
	dbTotalTime += ldbTime;
	totalTime += ltotaltime;
	if (dbMaxTime < ldbTime)
		dbMaxTime = ldbTime;
	if (maxTime < ltotaltime)
		maxTime = ltotaltime;

	MYSQL_RES* pRes = _conn.GetResult();
	MYSQL_ROW sql_row;
	while ((sql_row = mysql_fetch_row(pRes)) != NULL)
	{
		_plog->Log(TAG_DB, c_syslog::en_SYSTEM,
			L"[itemId: %d (Key: %d): Attack: %d | Defense: %d | Str: %d | Dex: %d | Int: %d | Luk: %d]",
			atoi(sql_row[1]), atoi(sql_row[0]), atoi(sql_row[2]), atoi(sql_row[3]), atoi(sql_row[4]),
			atoi(sql_row[5]), atoi(sql_row[6]), atoi(sql_row[7]));
	}
	mysql_free_result(pRes);
}

void CDBWriter::RequestGetWeaponInfo()
{
	LARGE_INTEGER enqStart;
	QueryPerformanceCounter(&enqStart);
	CPACKET_CREATE(createWeapon);
	createWeapon->SetRecvBuffer();
	*createWeapon << en_DBQUERY::GET_WEAPON_INFO
		<< (int)605935 + rand() % 6099
		<< enqStart.QuadPart;

	CPACKET_ADDREF(createWeapon.GetCPacketPtr());
	_queue.Enqueue_NotFail(createWeapon.GetCPacketPtr());
	SetEvent(_hEnqueueEvent);
}

void CDBWriter::RequestUpdateWeapon(CPacket* pMessage)
{
	st_DBQUERY_UPDATE_WEAPON message;
	LARGE_INTEGER enqueueTime;
	*pMessage >> message.itemKey >> message.itemAttack >>
		message.itemDefense >> message.itemStr >> message.itemDex
		>> message.itemInt >> message.itemLuk >> enqueueTime.QuadPart;

	char querystring[512];
	sprintf_s(querystring, 512, st_DBQUERY_UPDATE_WEAPON::QUERY_FORMAT,
		message.itemAttack, message.itemDefense,
		message.itemStr, message.itemDex, message.itemInt, message.itemLuk,
		message.itemKey);

	LARGE_INTEGER startTime;
	QueryPerformanceCounter(&startTime);
	//int retQuery = _conn.RequestQuery(querystring);
	//if (retQuery != 0)
	//{
	//	__debugbreak();
	//}
	LARGE_INTEGER finTime;
	QueryPerformanceCounter(&finTime);

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	double ldbTime = (double)(finTime.QuadPart - startTime.QuadPart) / freq.QuadPart * 1000'000;
	double ltotaltime = (double)(finTime.QuadPart - enqueueTime.QuadPart) / freq.QuadPart * 1000'000;
	cnt++;
	dbTotalTime += ldbTime;
	totalTime += ltotaltime;
	if (dbMaxTime < ldbTime)
		dbMaxTime = ldbTime;
	if (maxTime < ltotaltime)
		maxTime = ltotaltime;
}

void CDBWriter::RequestUpdateWeapon()
{
	LARGE_INTEGER enqStart;
	QueryPerformanceCounter(&enqStart);
	CPACKET_CREATE(createWeapon);
	createWeapon->SetRecvBuffer();
	*createWeapon << en_DBQUERY::UPDATE_WEAPON
		<< (int)605935 + rand() % 6099
		<< (int)100 + rand() % 100
		<< (int)100 + rand() % 100
		<< (int)20 + rand()%20
		<< (int)20 + rand()%20
		<< (int)20 + rand()%20
		<< (int)20 + rand()%20
		<< enqStart.QuadPart;

	_queue.Enqueue_NotFail(createWeapon.GetCPacketPtr());
	SetEvent(_hEnqueueEvent);
}

void CDBWriter::Quit()
{
	SetEvent(_hExit);
	_thread.join();
}
*/