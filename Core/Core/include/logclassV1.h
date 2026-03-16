#ifndef __LOG_CLASS_H__
#define __LOG_CLASS_H__
#include <stdio.h>
#include <windows.h>
#include <wchar.h>
#include <strsafe.h>
#include <map>
#include <synchapi.h>
#include <time.h>

namespace Core
{
	constexpr const wchar_t* LOG_DIRECTORY = L"SYSLOG\\";

	constexpr const wchar_t* LOG_DEBUG = L" Debug";
	constexpr const wchar_t* LOG_ERROR = L" Error";
	constexpr const wchar_t* LOG_SYSTEM = L"System";
	constexpr const wchar_t* LOG_FILENAME_FORMAT = L"%s%4d_%02d LOG_%s.txt";
	constexpr const wchar_t* LOG_NORMALTEXT_FORMAT = L"[%s] [%04d-%02d-%02d %2d:%2d:%2d / %s / %20llu] [tid: %05d] %s\n";
	constexpr const wchar_t* LOG_NORMALTEXT_FORMAT_EX = L"[%s] [%04d-%02d-%02d %2d:%2d:%2d / %s / %20llu] [GetLastError: %5d] [tid: %05d] %s\n";
	constexpr const wchar_t* LOG_HEX_TEXT_FORMAT = L"[%s] [%04d-%02d-%02d %2d:%2d:%2d / %s / %20llu] [tid: %05d] %s \n--Mem:[%p, %d]--\n%s\n";
	constexpr const wchar_t* LOG_HEX_TEXT_FORMAT_EX = L"[%s] [%04d-%02d-%02d %2d:%2d:%2d / %s / %20llu] [GetLastError: %5d] [tid: %05d] %s \n--Mem:[%p, %d]--\n%s\n";
	constexpr const wchar_t* LOG_LEVEL_ARRAY[3] = { LOG_DEBUG, LOG_ERROR, LOG_SYSTEM };
	constexpr const wchar_t LOG_HEX_VALUE[16] = { L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'A', L'B', L'C', L'D', L'E', L'F' };

	/////////////////////////////////////////////////////////////////////////
	// 로그 클래스 V1 (wchar, utf16-le) 
	// . LogEx(), Log() 분리되어있음. GetLastError 남길시 Ex버전 이용
	// . 태그에 동적 문자열 NEVER 넣지마라
	// 
	//  25.08.19 : 첫 작업
	/////////////////////////////////////////////////////////////////////////
	class c_syslog
	{
	public:
		enum en_sysLogLevel
		{
			en_DEBUG,
			en_ERROR,
			en_SYSTEM,

			TOTAL_CNT
		};
		//------------------------------------------------------
		// Info: 디렉토리 경로 설정
		// Param: const wchar_t* dirName (반드시 끝에 \\)
		// return: true: 성공, false: 실패
		//------------------------------------------------------
		void SetDirectory(const wchar_t* dirName)
		{
			_directory = dirName;
		}
		//------------------------------------------------------
		// Info: 디렉토리 경로 설정 (생성까지 시도) 
		//       ** 다중경로불가 **
		// Param: const wchar_t* dirName (반드시 끝에 \\)
		// return: true: 성공, false: 실패
		//------------------------------------------------------
		bool SetDirectoryEx(const wchar_t* dirName)
		{
			BOOL result = CreateDirectoryW(dirName, NULL);
			if (!result && GetLastError() != ERROR_ALREADY_EXISTS)
			{
				return false;
			}
			_directory = dirName;
			return true;
		}
		//------------------------------------------------------
		// Info: 로그를 남기는 레벨을 토글 (DEBUG <-> ERROR)
		//       * 등록된 로그보다 높은 레벨 지정 시 변화 없음
		// Param: -
		// return: en_syslogLevel
		//------------------------------------------------------
		en_sysLogLevel ChangeLevel()
		{
			en_sysLogLevel ret;
			if (_logLevel == en_sysLogLevel::en_DEBUG)
			{
				_InterlockedExchange((long*)&_logLevel, en_sysLogLevel::en_ERROR);
				ret = en_ERROR;
			}
			else
			{
				_InterlockedExchange((long*)&_logLevel, en_sysLogLevel::en_DEBUG);
				ret = en_DEBUG;
			}
			return ret;
		}
		//------------------------------------------------------
		// Info: 로그를 남기는 레벨을 직접 변경
		//       * 등록된 로그보다 높은 레벨 지정 시 변화 없음
		// Param: en_syslogLevel level
		// return: -
		//------------------------------------------------------
		void ChangeLevelEx(en_sysLogLevel level)
		{
			if (0 < level && level < TOTAL_CNT)
				_InterlockedExchange((long*)&_logLevel, level);
		}

		//------------------------------------------------------
		// Info: 현재 레벨을 획득
		// Param: -
		// return: en_sysLogLevel (int)
		//------------------------------------------------------
		en_sysLogLevel GetLogLevel()
		{
			return _logLevel;
		}

		//------------------------------------------------------
		// Info: 콘솔 출력여부 토글 (기본 false)
		// Param: en_logLevel level
		// return: -
		//------------------------------------------------------
		void TogglePrint()
		{
			_InterlockedExchange((long*)&_isUsePrint, (long)!_isUsePrint);
		}

		//------------------------------------------------------
		// Info: 로그 남기는 함수
		// Param: const wchar_t* 태그, en_logLevel 시스템 레벨, 
		//        const wchar_t* 포맷, 가변인자들
		// return: -
		//------------------------------------------------------
		void Log(const wchar_t* wszTag, en_sysLogLevel logLevel, const wchar_t* wszFormat, ...);

		//------------------------------------------------------
		// Info: GetLastError 로그 남기는 함수
		// Param: const wchar_t* 태그, GetLastError()값, 
		//        en_logLevel 시스템 레벨, 
		//        const wchar_t* 포맷, 가변인자들
		// return: -
		//------------------------------------------------------
		void LogEx(const wchar_t* wszTag, unsigned long dwError, en_sysLogLevel logLevel, const wchar_t* wszFormat, ...);

		//------------------------------------------------------
		// Info: 메모리상태를 로그로 남기는 함수
		// Param: const wchar_t* 태그, en_logLevel 시스템 레벨,
		//        unsigned char* 시작점, int 길이,
		//        const wchar_t* 포맷, 가변인자들
		// return: -
		//------------------------------------------------------
		void LogHex(const wchar_t* wszTag, en_sysLogLevel logLevel, const unsigned char* pStart, int len, const wchar_t* wszFormat, ...);

		//------------------------------------------------------
		// Info: GetLastError() +메모리상태를 로그로 남기는 함수
		// Param: const wchar_t* 태그, GetLastError() 값,
		//        en_logLevel 시스템 레벨,
		//        unsigned char* 시작점, int 길이,
		//        const wchar_t* 포맷, 가변인자들
		// return: -
		//------------------------------------------------------
		void LogHexEx(const wchar_t* wszTag, unsigned long dwError, en_sysLogLevel logLevel, unsigned char* pStart, int len, const wchar_t* wszFormat, ...);

		//------------------------------------------------------
		// 전역 로그 획득
		//------------------------------------------------------
		static c_syslog& logging()
		{
			static c_syslog s_log;
			return s_log;
		}

		// 헥스로 표현하기
	private: void PtrToWszhex(wchar_t* buffer, int  buflen, const unsigned char* ptr, int len, bool* isCut)
	{
		*isCut = false;
		int cnt = (buflen - 1) / 3;
		if (len >= cnt)
		{
			len = cnt;
			*isCut = true;
		}

		unsigned char pValue;
		for (int i = 0; i < len; i++)
		{
			pValue = ptr[i];
			buffer[3 * i] = LOG_HEX_VALUE[((pValue & 0xF0) >> 4)];
			buffer[3 * i + 1] = LOG_HEX_VALUE[(pValue & 0x0F)];
			if ((i + 1) % 8 != 0)
				buffer[3 * i + 2] = L' ';
			else
				buffer[3 * i + 2] = L'\n';
		}
		buffer[3 * len - 1] = L'\n';
		buffer[3 * len] = L'\0';
	}

	private:
		c_syslog() :_tags(), _tagsLock(SRWLOCK_INIT), _logLevel(en_ERROR), _logcount(0), _directory(LOG_DIRECTORY), _isUsePrint(false)
		{
			SetDirectoryEx(LOG_DIRECTORY);
		};
		~c_syslog()
		{
			// _tags.clear();
		}
		c_syslog(const c_syslog& log) = delete;
		c_syslog& operator = (const c_syslog& log) = delete;

		struct cmp
		{
			bool operator()(const wchar_t* a, const wchar_t* b) const
			{
				return wcscmp(a, b) < 0;
			}
		};
		std::map<const wchar_t*, SRWLOCK, cmp> _tags;
		SRWLOCK _tagsLock;
		en_sysLogLevel _logLevel;
		unsigned long long _logcount;
		const wchar_t* _directory;
		bool _isUsePrint;
	};
}

/////////////////////////////////////////////////////////////////////////
// 싱글스레드용 로그 클래스 V1 (wchar, utf16-le) 
// . LogEx(), Log() 분리되어있음. GetLastError 남길시 Ex버전 이용
// . 태그에 동적 문자열 NEVER 넣지마라
// 
//  25.08.19 : 첫 작업
/////////////////////////////////////////////////////////////////////////

/*
class c_syslog_singlethread
{
public:
	enum en_sysLogLevel
	{
		en_DEBUG,
		en_ERROR,
		en_SYSTEM,

		TOTAL_CNT
	};
	//------------------------------------------------------
	// Info: 디렉토리 경로 설정
	// Param: const wchar_t* dirName (반드시 끝에 \\)
	// return: true: 성공, false: 실패
	//------------------------------------------------------
	void SetDirectory(const wchar_t* dirName)
	{
		_directory = dirName;
	}
	//------------------------------------------------------
	// Info: 디렉토리 경로 설정 (생성까지 시도) 
	//       ** 다중경로불가 **
	// Param: const wchar_t* dirName (반드시 끝에 \\)
	// return: true: 성공, false: 실패
	//------------------------------------------------------
	bool SetDirectoryEx(const wchar_t* dirName)
	{
		BOOL result = CreateDirectoryW(dirName, NULL);
		if (!result && GetLastError() != ERROR_ALREADY_EXISTS)
		{
			return false;
		}
		_directory = dirName;
		return true;
	}
	//------------------------------------------------------
	// Info: 로그를 남기는 레벨을 변경 (DEBUG <-> ERROR)
	//       * 등록된 로그보다 높은 레벨 지정 시 변화 없음
	// Param: en_logLevel level
	// return: -
	//------------------------------------------------------
	void ChangeLevel()
	{
		if (_logLevel == en_sysLogLevel::en_DEBUG)
			_logLevel = en_sysLogLevel::en_ERROR;
		else
			_logLevel = en_sysLogLevel::en_DEBUG;
	}
	//------------------------------------------------------
	// Info: 로그를 남기는 레벨을 직접 변경
	//       * 등록된 로그보다 높은 레벨 지정 시 변화 없음
	// Param: en_logLevel level
	// return: -
	//------------------------------------------------------
	void ChangeLevelEx(en_sysLogLevel level)
	{
		if (0 <= level && level < TOTAL_CNT)
			_logLevel = level;
	}

	//------------------------------------------------------
	// Info: 콘솔 출력여부 토글 (기본 false)
	// Param: en_logLevel level
	// return: -
	//------------------------------------------------------
	void TogglePrint()
	{
		_isUsePrint = !_isUsePrint;
	}

	//------------------------------------------------------
	// Info: 로그 남기는 함수
	// Param: const wchar_t* 태그, en_logLevel 시스템 레벨, 
	//        const wchar_t* 포맷, 가변인자들
	// return: -
	//------------------------------------------------------
	void Log(const wchar_t* wszTag, en_sysLogLevel logLevel, const wchar_t* wszFormat, ...)
	{	// 태그는 중간에 사라지지 않는다. 추가될 뿐
		if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
			return;

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
	}

	//------------------------------------------------------
	// Info: GetLastError 로그 남기는 함수
	// Param: const wchar_t* 태그, GetLastError()값, 
	//        en_logLevel 시스템 레벨, 
	//        const wchar_t* 포맷, 가변인자들
	// return: -
	//------------------------------------------------------
	void LogEx(const wchar_t* wszTag, unsigned long dwError, en_sysLogLevel logLevel, const wchar_t* wszFormat, ...)
	{	// 태그는 중간에 사라지지 않는다. 추가될 뿐
		if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
			return;

		LOG_IS_RETURN_ERROR(dwError);

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
	}

	//------------------------------------------------------
	// Info: 메모리상태를 로그로 남기는 함수
	// Param: const wchar_t* 태그, en_logLevel 시스템 레벨,
	//        unsigned char* 시작점, int 길이,
	//        const wchar_t* 포맷, 가변인자들
	// return: -
	//------------------------------------------------------
	void LogHex(const wchar_t* wszTag, en_sysLogLevel logLevel, unsigned char* pStart, int len, const wchar_t* wszFormat, ...)

	{	// 태그는 중간에 사라지지 않는다. 추가될 뿐
		if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
			return;

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
	}

	//------------------------------------------------------
	// Info: GetLastError() +메모리상태를 로그로 남기는 함수
	// Param: const wchar_t* 태그, GetLastError() 값,
	//        en_logLevel 시스템 레벨,
	//        unsigned char* 시작점, int 길이,
	//        const wchar_t* 포맷, 가변인자들
	// return: -
	//------------------------------------------------------
	void LogHexEx(const wchar_t* wszTag, unsigned long dwError, en_sysLogLevel logLevel, unsigned char* pStart, int len, const wchar_t* wszFormat, ...)
	{	// 태그는 중간에 사라지지 않는다. 추가될 뿐
		if (logLevel < _logLevel || logLevel >= TOTAL_CNT)
			return;

		LOG_IS_RETURN_ERROR(dwError);

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
	}

	c_syslog_singlethread() :_logLevel(en_ERROR), _logcount(0), _directory(LOG_DIRECTORY), _isUsePrint(false) {};
private:
	en_sysLogLevel _logLevel;
	unsigned long long _logcount;
	const wchar_t* _directory;
	bool _isUsePrint;

	// 헥스로 표현하기
	__forceinline void PtrToWszhex(wchar_t* buffer, int  buflen, unsigned char* ptr, int len, bool* isCut)
	{
		*isCut = false;
		int cnt = (buflen - 1) / 3;
		if (len >= cnt)
		{
			len = cnt;
			*isCut = true;
		}

		unsigned char pValue;
		for (int i = 0; i < len; i++)
		{
			pValue = ptr[i];
			buffer[3 * i] = LOG_HEX_VALUE[((pValue & 0xF0) >> 4)];
			buffer[3 * i + 1] = LOG_HEX_VALUE[(pValue & 0x0F)];
			if ((i + 1) % 8 != 0)
				buffer[3 * i + 2] = L' ';
			else
				buffer[3 * i + 2] = L'\n';
		}
		buffer[3 * len - 1] = L'\n';
		buffer[3 * len] = L'\0';
	}
};
*/

#endif