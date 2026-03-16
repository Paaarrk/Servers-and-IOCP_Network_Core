#include "ProfilerV2.h"

thread_local ProfileManager::st_profile** ProfileManager::_profiles = 0;
SRWLOCK ProfileManager::_fileWrite = SRWLOCK_INIT;