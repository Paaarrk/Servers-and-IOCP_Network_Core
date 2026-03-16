#ifndef __DB_CONNECTOR_H__
#define __DB_CONNECTOR_H__
#include "mysql.h"
#include "LockFreeQueue.hpp"


// #define MYSQL_MULTI_CONNECTION

#define TRANSACTION

constexpr const wchar_t* TAG_DB = L"DB";

class CTlsMySqlConnector
{
public:
	enum enMySqlConnector
	{
		// 초반 활성화 시간은 제외하기위해서
		TIMING_REGISTER_SLOW_QUERY = 200,
		// 쿼리 widechar기준 최대 문자 수
		QUERY_SIZE = 2048,
		// n회마다 느린 쿼리 목록을 로그로 남기기
		TIMING_WHEN_WRITE_SLOW_SET = 1000000,
		// maxDeltaTime이 어느정도일때 즉시 남길지
		TIMING_HOW_MS_WRITE_SLOW_SET = 50
	};
	CTlsMySqlConnector(const char* hostIP = nullptr,
		const char* id = nullptr, const char* pw = nullptr,
		const char* schema = nullptr, unsigned short port = 0);

	~CTlsMySqlConnector();


	void SetConnector(const char* hostIP, const char* id, const char* pw,
		const char* schema, unsigned short port);


	//---------------------------------------
	// 성공시 0, 실패시 1 반환
	// tryCnt = 실패시 시도할 횟수
	// tick = 시도 간격, 0이면 쉬지 않음
	//---------------------------------------
	int Connect(int tryCnt = 3, unsigned long tick = 10);

	//---------------------------------------
	// 쿼리 날리기, 단일 쿼리 전용
	// 멀티쿼리라면 사용자가 직접 조립하고주는게 맞지않나?
	// 성공시 0, 실패시 0아님
	// 실패 1: DB서버문제
	// 실패 2: 쿼리 문제
	//---------------------------------------
	int RequestQuery(const wchar_t* wformat, ...);

	//---------------------------------------
	// 응답 가져오기 / FreeResult()필수
	//---------------------------------------
	MYSQL_RES* GetResult();

	//---------------------------------------
	// 다음 응답 가져오기 / FreeResult()필수
	//---------------------------------------
	MYSQL_RES* GetNextResult();

	//---------------------------------------
	// 응답 해제하기
	//---------------------------------------
	void FreeResult();

	//---------------------------------------
	// 로그 sql 에러
	//---------------------------------------
	void LogSQLError();

	void ReleaseConn();

	struct stQueryInfo
	{
		uint64_t deltaTime = 0;
		wchar_t query[QUERY_SIZE] = {};
	};

	void RegisterSlowQuery(const wchar_t* wQuery, uint64_t deltaTime);

	void WriteSlowQuery(bool useTiming = false);

	struct stConn
	{
		MYSQL _conn = {};
		MYSQL_RES* _sql_result = nullptr;
		bool bInit = false;

		uint64_t _avgTime = 0;
		uint64_t _maxDeltaTime = 0;
		uint64_t _count = 0;
		stQueryInfo _slowQueryTop5[5] = {};
		int _connectCnt = 0;
		int _querySendCnt = 0;
		wchar_t _curWQuery[QUERY_SIZE] = {};
		char _curAQuery[QUERY_SIZE * 4] = {};	//최대 2바이트가 4바이트로 바뀌어서
	};
private:
	CTlsMySqlConnector(const CTlsMySqlConnector&) = delete;
	CTlsMySqlConnector& operator=(const CTlsMySqlConnector&) = delete;

	static thread_local stConn* tls_conn;

	char _host[16];
	char* _id;
	char* _pw;
	char* _schema;
	unsigned short _port;
};


#endif