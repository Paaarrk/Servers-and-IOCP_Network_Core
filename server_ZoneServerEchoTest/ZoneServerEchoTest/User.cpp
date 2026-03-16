#include "User.h"

#include "timeapi.h"
#pragma comment(lib, "winmm")

CTlsObjectPool<CUser, CUser::POOL_KEY, TLS_OBJECTPOOL_USE_CALLONCE> CUser::s_pool;
int32_t CUser::s_useSize = 0;

void CUser::SetIp(std::wstring& ipWStr)
{
	_ipWstr = std::move(ipWStr);
}

CUser* CUser::Alloc()
{
	CUser* ret = s_pool.Alloc();
	if (ret == nullptr)
		return nullptr;

	ret->_lastRecvTime = timeGetTime();
	ret->_isAlive = true;
	ret->_accountNo = -1;
	_InterlockedIncrement((long*)&s_useSize);
	return ret;
}

int32_t CUser::Free(CUser* pUser)
{
	_InterlockedDecrement((long*)&s_useSize);
	return s_pool.Free(pUser);
}