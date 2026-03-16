#ifndef __CRASH_DUMP_H__
#define __CRASH_DUMP_H__
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#pragma comment(lib, "DbgHelp")


namespace dump
{
	class c_crashdump
	{
	public:
		c_crashdump()
		{
			_dumpcount = 0;

			_invalid_parameter_handler oldHandler, newHandler;
			newHandler = myInvalidParameterHandler;

			oldHandler = _set_invalid_parameter_handler(newHandler);	// crt함수에 nullptr등을 넣었을 때
			_CrtSetReportMode(_CRT_WARN, 0);	// CRT오류메시지 중단, 덤프남게 유도
			_CrtSetReportMode(_CRT_ASSERT, 0);	// CRT오류메시지 중단, 덤프남게 유도
			_CrtSetReportMode(_CRT_ERROR, 0);	// CRT오류메시지 중단, 덤프남게 유도

			_CrtSetReportHook(_custom_report_hook);		// CRT리포트가 내 리포트 타도록
			_set_purecall_handler(myPureCallHandler);	// 퓨어컬 오류가 내 핸들러 타도록

			SetHandlerDump();
		}

		static void Crash(void)
		{
			 __debugbreak();
			// RaiseException(EXCEPTION_BREAKPOINT, 0, 0, nullptr);
			//int* p = nullptr;
			//*p = 0;
		}

		static void SetHandlerDump()
		{
			SetUnhandledExceptionFilter(MyExceptionFilter);
			wprintf_s(L"덤프 세팅 완료");
		}

		static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
		{
			int iWorkingMemory = 0;
			SYSTEMTIME stNowTime;

			long DumpCount = _InterlockedIncrement(&_dumpcount);


			/* 현재 날짜와 시간을 알아온다 */
			wchar_t fileName[MAX_PATH];

			GetLocalTime(&stNowTime);
			wsprintfW(fileName, L"Dump_%d%02d%02d_%02d.%02d.%02d [%d].dmp", stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay,
				stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, DumpCount);
			
			wprintf(L"\n\n\n!!! Crash Error!! %d.%d.%d / %d.%d.%d\n", stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay,
				stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);
			wprintf(L"Now Saving Dump File ... \n");

			HANDLE hDumpFile = CreateFileW(fileName,
				GENERIC_WRITE,
				FILE_SHARE_WRITE,
				NULL,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, NULL);
			if (hDumpFile != INVALID_HANDLE_VALUE)
			{
				_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInformation;
				MinidumpExceptionInformation.ThreadId = GetCurrentThreadId();
				MinidumpExceptionInformation.ExceptionPointers = pExceptionPointer;
				MinidumpExceptionInformation.ClientPointers = FALSE;

				MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
					hDumpFile, MiniDumpWithFullMemory, &MinidumpExceptionInformation, NULL, NULL);
				CloseHandle(hDumpFile);

				wprintf(L"CrashDump Save Finish!\n");
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		// Invalid Parameter Handler
		static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
		{
			Crash();
		}

		static int _custom_report_hook(int ireposttype, char* message, int* returnvalue)
		{
			Crash();
			return (int)true;
		}

		static void myPureCallHandler(void)
		{
			Crash();
		}

		static long _dumpcount;
	};
	long c_crashdump::_dumpcount = 0;
}

#endif