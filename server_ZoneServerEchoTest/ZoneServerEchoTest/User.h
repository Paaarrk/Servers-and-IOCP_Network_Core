#ifndef __USER_H__
#define __USER_H__
#include "TLSObjectPool_IntrusiveList.hpp"
#include <string>

class CUser
{
public:
	enum enUser
	{
		POOL_KEY = 0x1212'F0F5
	};
	void			MessageRecved(DWORD dwRecvTime) { _lastRecvTime = dwRecvTime; }
	unsigned long	GetLastRecvTime() const { return _lastRecvTime; }
	int64_t			GetAccountNo() const { return _accountNo; }
	void			SetAccountNo(int64_t accno) { _accountNo = accno; }
	void			SetIp(std::wstring& ipWstr);
	void			SetSessionKey(const char* key)
	{
		memcpy(_sessionKey, key, 64);
	}

	static CUser* Alloc();
	static int32_t Free(CUser* pUser);
	void SetDead() { _isAlive = false; }
	bool IsAlive() const { return _isAlive; }

	static int32_t UserPoolCreateChunk()
	{
		return s_pool.GetAllocChunkPoolCreateNum();
	}
	static int32_t UserPoolLeftChunk()
	{
		return s_pool.GetAllocChunkPoolSize();
	}
	static int32_t PoolUseSize()
	{
		return s_useSize;
	}
	bool IsIpSame(const wchar_t* IP) const
	{
		std::wstring_view view(IP);
		return (_ipWstr == IP);
	}
private:
	DWORD			_lastRecvTime = 0;
	bool			_isAlive = false;
	int64_t			_accountNo = 0;
	std::wstring	_ipWstr;
	char			_sessionKey[64] = {};

	static int32_t s_useSize;
	static CTlsObjectPool<CUser, POOL_KEY, TLS_OBJECTPOOL_USE_CALLONCE> s_pool;
};

#endif