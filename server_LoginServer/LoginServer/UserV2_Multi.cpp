#include "UserV2_Multi.h"

//-------------------------------------------------------
// 세션 구조 초기화, maxCnt: 배열 확보
//-------------------------------------------------------
bool CUserManager::UserStructure::Init(int maxCnt)
{
	_maxUserCnt = maxCnt;
	_usersArray = new CUser[maxCnt];
	if (_usersArray == nullptr)
		return false;
	for (int i = maxCnt - 1; i >= 0; i--)
	{
		_indexStack.push(i);
	}
	return true;
}

//-------------------------------------------------------
// 세션 획득, 자리가 없으면 nullptr반환, 자리가 없으면 이상한 것
// index, session* 둘다 얻음, 
// 실패 시 index = -1, 반환은 nullptr
//-------------------------------------------------------
CUser* CUserManager::UserStructure::AcquireUser(int* index)
{
	if (_indexStack.pop(*index) == false)
	{
		*index = -1;
		return nullptr;
	}
	_InterlockedIncrement(&_usersCnt);
	return &_usersArray[*index];
}

//-------------------------------------------------------
// 세션 반환 (클리어 하고 반환하세요)
// index가 이상하면 false반환
//-------------------------------------------------------
bool CUserManager::UserStructure::ReleaseUser(CUser* pSession)
{
	int index = (int)(pSession - _usersArray);
	if (index < 0 || index >= _maxUserCnt)
		return false;
	_indexStack.push(index);
	_InterlockedDecrement(&_usersCnt);
	return true;
}