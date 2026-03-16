#ifndef __WIN_TYPE_H__
#define __WIN_TYPE_H__
#include "Type.h"

namespace Core
{
	void Is64BitSystem_Crash();
	uint32 WinGetLastError();
}

#endif