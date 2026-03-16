#include "NetBase.h"
#include "logclassV1.h"

Net::CWSAStart::CWSAStart()
{
	if (_callonce == nullptr)
	{
		AcquireSRWLockExclusive(&_lock);
		if (_callonce == nullptr)
		{
			WSADATA wsa;
			int retWSAStart = WSAStartup(MAKEWORD(2, 2), &wsa);
			if (retWSAStart)
			{
				Core::c_syslog::logging().LogEx(TAG_NET, retWSAStart, Core::c_syslog::en_ERROR, L"WSAStartup() failed");
				__debugbreak();
			}
			_callonce = (int32*)0x0000F0F0;
		}
		ReleaseSRWLockExclusive(&_lock);
	}
}