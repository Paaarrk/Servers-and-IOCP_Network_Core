#include "WinType.h"
#include <windows.h>


void Core::Is64BitSystem_Crash()
{
	//----------------------------------------------
	// 64비트가 아니면 생성을 막자
	//----------------------------------------------
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	if (sysinfo.lpMaximumApplicationAddress != (LPVOID)0x00007FFF'FFFEFFFF)
	{
		__debugbreak();
	}
}

uint32 Core::WinGetLastError()
{
	return (uint32)GetLastError();
}
