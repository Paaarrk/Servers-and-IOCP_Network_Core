#ifndef __CONTENTS_H__
#define __CONTENTS_H__
#include "Type.h"

constexpr const wchar_t* TAG_CONTENTS = L"Contents";
constexpr const wchar_t* TAG_MESSAGE = L"Message";
constexpr const char* CONFIG_FILE_PATH = "..\\Config\\Config.cnf";

enum en_CONTENTS
{
	CONTENTS_ID_LOBBY = 1,
	CONTENTS_ID_ECHO = 2,

	MONITORING_TICK = 1000,

	TIME_OUT_MS_LOBBY = 40000,
	TIME_OUT_MS_ECHO = 40000,
};

#endif