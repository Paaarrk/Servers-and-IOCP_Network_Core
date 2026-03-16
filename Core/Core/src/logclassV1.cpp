#include "logclassV1.h"

using namespace Core;


// dwError가 일상적인 에러면 함수 종료
#define LOG_IS_RETURN_ERROR(dwError)			do{								\
	switch ((dwError))															\
	{																			\
	case 10004: /* 블로킹 소켓 대상 함수 호출 중단. 리슨소켓 close혹은 io캔슬시 블로킹 모드로 받고있었다면 발생 */	\
	case 10035: /* WSAEWOULDBLOCK여기 찍을 이유는 없음 */						\
	case 10053: /* WSAECONNABORTED 클라에서 주로 나왔는데 일단 10054랑 쌍 */	\
	case 10054:	/* WSAECONNRESET, 클라가 먼저 끔 */								\
	case 10060: /* WSAETIMEOUT, 클라 응답이 없는 경우 */						\
		return;																	\
		break;																	\
	default:																	\
		break;																	\
	}																			\
} while(0)	

void c_syslog::Log(const wchar_t* wszTag, en_sysLogLevel logLevel, const wchar_t* wszFormat, ...)
{	
	// 태그는 중간에 사라지지 않는다. 추가될 뿐
	if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
		return;

	/* 맵에서 락 획득 */
	SRWLOCK* pSrwTaglock;
	AcquireSRWLockExclusive(&_tagsLock);
	std::pair<std::map<const wchar_t*, SRWLOCK>::iterator, bool> pairResult = _tags.insert({ wszTag, SRWLOCK_INIT });
	pSrwTaglock = &(pairResult.first)->second;
	ReleaseSRWLockExclusive(&_tagsLock);

	/* 맵에서 얻은 락으로 파일 접근 */
	AcquireSRWLockExclusive(pSrwTaglock);

	wchar_t fileName[256];
	wchar_t wsztext[2048];
	FILE* file = nullptr;
	// 시간 설정
	tm tm;
	time_t mytime = time(NULL);
	localtime_s(&tm, &mytime);

	// 파일 이름 만들기
	swprintf_s(fileName, 128, LOG_FILENAME_FORMAT, _directory, tm.tm_year + 1900, tm.tm_mon + 1, wszTag);

	// 파일 내용 만들기
	va_list va;
	va_start(va, wszFormat);
	HRESULT hResult = StringCchVPrintfW(wsztext, 2048, wszFormat, va);
	va_end(va);

	// 파일 열기 (처음 만들면 bom삽입)
	_wfopen_s(&file, fileName, L"ab");
	if (file == nullptr)
	{
		wprintf_s(L"파일이 안열려요 \n");
		ReleaseSRWLockExclusive(pSrwTaglock);
		return;
	}
	if (_ftelli64(file) == 0)
		fputwc(0xFEFF, file);

	// 파일에 쓰기
	unsigned long long logCount = _InterlockedIncrement(&_logcount);
	fwprintf_s(file, LOG_NORMALTEXT_FORMAT, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, GetCurrentThreadId(), wsztext);
	if (_isUsePrint)
		wprintf_s(LOG_NORMALTEXT_FORMAT, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, GetCurrentThreadId(), wsztext);
	// 이건 잘렸을 경우
	if (hResult == STRSAFE_E_INSUFFICIENT_BUFFER)
	{
		fwprintf_s(file, L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요[logCount: %llu]\n", logCount);
		if (_isUsePrint)
			wprintf_s(L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요[logCount: %llu]\n", logCount);
	}

	// 파일 닫기
	fclose(file);

	ReleaseSRWLockExclusive(pSrwTaglock);
}


void c_syslog::LogEx(const wchar_t* wszTag, unsigned long dwError, en_sysLogLevel logLevel, const wchar_t* wszFormat, ...)
{
	// 태그는 중간에 사라지지 않는다. 추가될 뿐
	if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
		return;

	LOG_IS_RETURN_ERROR(dwError);

	/* 맵에서 락 획득 */
	SRWLOCK* pSrwTaglock;
	AcquireSRWLockExclusive(&_tagsLock);
	std::pair<std::map<const wchar_t*, SRWLOCK>::iterator, bool> pairResult = _tags.insert({ wszTag, SRWLOCK_INIT });
	pSrwTaglock = &(pairResult.first)->second;
	ReleaseSRWLockExclusive(&_tagsLock);

	/* 맵에서 얻은 락으로 파일 접근 */
	AcquireSRWLockExclusive(pSrwTaglock);

	wchar_t fileName[128];
	wchar_t wsztext[2048];
	FILE* file = nullptr;
	// 시간 설정
	tm tm;
	time_t mytime = time(NULL);
	localtime_s(&tm, &mytime);

	// 파일 이름 만들기
	swprintf_s(fileName, 128, LOG_FILENAME_FORMAT, _directory, tm.tm_year + 1900, tm.tm_mon + 1, wszTag);

	// 파일 내용 만들기
	va_list va;
	va_start(va, wszFormat);
	HRESULT hResult = StringCchVPrintfW(wsztext, 2048, wszFormat, va);
	va_end(va);

	// 파일 열기 (처음 만들면 bom삽입)
	_wfopen_s(&file, fileName, L"ab");
	if (file == nullptr)
	{
		wprintf_s(L"파일이 안열려요 \n");
		ReleaseSRWLockExclusive(pSrwTaglock);
		return;
	}
	if (_ftelli64(file) == 0)
		fputwc(0xFEFF, file);

	// 파일에 쓰기
	unsigned long long logCount = _InterlockedIncrement(&_logcount);
	fwprintf_s(file, LOG_NORMALTEXT_FORMAT_EX, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, dwError, GetCurrentThreadId(), wsztext);

	if (_isUsePrint)
	{
		wprintf_s(LOG_NORMALTEXT_FORMAT_EX, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, dwError, GetCurrentThreadId(), wsztext);
	}

	// 이건 잘렸을 경우
	if (hResult == STRSAFE_E_INSUFFICIENT_BUFFER)
	{
		fwprintf_s(file, L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요[logCount: %llu]\n", logCount);
		if (_isUsePrint)
			wprintf_s(L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요[logCount: %llu]\n", logCount);
	}

	// 파일 닫기
	fclose(file);

	ReleaseSRWLockExclusive(pSrwTaglock);
}


void c_syslog::LogHex(const wchar_t* wszTag, en_sysLogLevel logLevel, const unsigned char* pStart, int len, const wchar_t* wszFormat, ...)
{	
	// 태그는 중간에 사라지지 않는다. 추가될 뿐
	if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
		return;

	/* 맵에서 락 획득 */
	SRWLOCK* pSrwTaglock;
	AcquireSRWLockExclusive(&_tagsLock);
	std::pair<std::map<const wchar_t*, SRWLOCK>::iterator, bool> pairResult = _tags.insert({ wszTag, SRWLOCK_INIT });
	pSrwTaglock = &(pairResult.first)->second;
	ReleaseSRWLockExclusive(&_tagsLock);

	/* 맵에서 얻은 락으로 파일 접근 */
	AcquireSRWLockExclusive(pSrwTaglock);

	wchar_t fileName[128];
	wchar_t wsztext[2048];
	wchar_t wszhex[2048];
	FILE* file = nullptr;
	// 시간 설정
	tm tm;
	time_t mytime = time(NULL);
	localtime_s(&tm, &mytime);

	// 파일 이름 만들기
	swprintf_s(fileName, 128, LOG_FILENAME_FORMAT, _directory, tm.tm_year + 1900, tm.tm_mon + 1, wszTag);

	// 헥스로 표현하기
	bool isCut;
	PtrToWszhex(wszhex, 2048, pStart, len, &isCut);

	// 파일 내용 만들기
	va_list va;
	va_start(va, wszFormat);
	HRESULT hResult = StringCchVPrintfW(wsztext, 2048, wszFormat, va);
	va_end(va);

	// 파일 열기 (처음 만들면 bom삽입)
	_wfopen_s(&file, fileName, L"ab");
	if (file == nullptr)
	{
		wprintf_s(L"파일이 안열려요 \n");
		ReleaseSRWLockExclusive(pSrwTaglock);
		return;
	}
	if (_ftelli64(file) == 0)
		fputwc(0xFEFF, file);

	// 파일에 쓰기
	unsigned long long logCount = _InterlockedIncrement(&_logcount);
	fwprintf_s(file, LOG_HEX_TEXT_FORMAT, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, GetCurrentThreadId(), wsztext, pStart, len, wszhex);
	if (_isUsePrint)
	{
		wprintf_s(LOG_HEX_TEXT_FORMAT, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, GetCurrentThreadId(), wsztext, pStart, len, wszhex);
	}
	// 이건 잘렸을 경우
	if (hResult == STRSAFE_E_INSUFFICIENT_BUFFER)
	{
		fwprintf_s(file, L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		if (_isUsePrint)
		{
			wprintf_s(L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		}
	}
	if (isCut)
	{
		fwprintf_s(file, L"\t=> 넣은 길이가 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		if (_isUsePrint)
		{
			wprintf_s(L"\t=> 넣은 길이가 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		}
	}

	// 파일 닫기
	fclose(file);

	ReleaseSRWLockExclusive(pSrwTaglock);
}


void c_syslog::LogHexEx(const wchar_t* wszTag, unsigned long dwError, en_sysLogLevel logLevel, unsigned char* pStart, int len, const wchar_t* wszFormat, ...)
{	
	// 태그는 중간에 사라지지 않는다. 추가될 뿐
	if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
		return;

	LOG_IS_RETURN_ERROR(dwError);

	/* 맵에서 락 획득 */
	SRWLOCK* pSrwTaglock;
	AcquireSRWLockExclusive(&_tagsLock);
	std::pair<std::map<const wchar_t*, SRWLOCK>::iterator, bool> pairResult = _tags.insert({ wszTag, SRWLOCK_INIT });
	pSrwTaglock = &(pairResult.first)->second;
	ReleaseSRWLockExclusive(&_tagsLock);

	/* 맵에서 얻은 락으로 파일 접근 */
	AcquireSRWLockExclusive(pSrwTaglock);

	wchar_t fileName[128];
	wchar_t wsztext[2048];
	wchar_t wszhex[2048];
	FILE* file = nullptr;
	// 시간 설정
	tm tm;
	time_t mytime = time(NULL);
	localtime_s(&tm, &mytime);

	// 파일 이름 만들기
	swprintf_s(fileName, 128, LOG_FILENAME_FORMAT, _directory, tm.tm_year + 1900, tm.tm_mon + 1, wszTag);

	// 헥스로 표현하기
	bool isCut;
	PtrToWszhex(wszhex, 2048, pStart, len, &isCut);

	// 파일 내용 만들기
	va_list va;
	va_start(va, wszFormat);
	HRESULT hResult = StringCchVPrintfW(wsztext, 2048, wszFormat, va);
	va_end(va);

	// 파일 열기 (처음 만들면 bom삽입)
	_wfopen_s(&file, fileName, L"ab");
	if (file == nullptr)
	{
		wprintf_s(L"파일이 안열려요 \n");
		ReleaseSRWLockExclusive(pSrwTaglock);
		return;
	}
	if (_ftelli64(file) == 0)
		fputwc(0xFEFF, file);

	// 파일에 쓰기
	unsigned long long logCount = _InterlockedIncrement(&_logcount);
	fwprintf_s(file, LOG_HEX_TEXT_FORMAT, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, GetCurrentThreadId(), wsztext, pStart, len, wszhex);
	if (_isUsePrint)
	{
		wprintf_s(LOG_HEX_TEXT_FORMAT, wszTag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, LOG_LEVEL_ARRAY[logLevel], logCount, GetCurrentThreadId(), wsztext, pStart, len, wszhex);
	}
	// 이건 잘렸을 경우
	if (hResult == STRSAFE_E_INSUFFICIENT_BUFFER)
	{
		fwprintf_s(file, L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		if (_isUsePrint)
		{
			wprintf_s(L"\t=> 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		}
	}
	if (isCut)
	{
		fwprintf_s(file, L"\t=> 넣은 길이가 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		if (_isUsePrint)
		{
			wprintf_s(L"\t=> 넣은 길이가 2048보다 길어서 잘렸습니다. 확인 후 버퍼를 늘려주세요 [logCount: %llu]\n", logCount);
		}
	}

	// 파일 닫기
	fclose(file);

	ReleaseSRWLockExclusive(pSrwTaglock);
}