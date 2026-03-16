#include "TraceV1.h"

using namespace Core;
#include "logclassV1.h"
void CTrace::stTraceNode::Init(void* _ptr, const char* ___file__, int ___line__)
{
	ptr = _ptr;
	use__file__ = ___file__;
	use__line__ = ___line__;
	alloc__file__ = ___file__;
	alloc__line__ = ___line__;
}

void CTrace::stTraceNode::Clear()
{
	memset(this, 0, sizeof(stTraceNode));
}



CTrace::CTrace(int maxTraceNum) :_maxTraceNum(maxTraceNum)
{
	_ptrArray = (stTraceNode*)malloc(sizeof(stTraceNode) * maxTraceNum);
#pragma warning(push)
#pragma warning(disable: 6387)
	// null 이면 뻑
	memset(_ptrArray, 0, sizeof(stTraceNode) * maxTraceNum);
	for (int i = maxTraceNum - 1; i >= 0; i--)
	{
		_freeIndexStack.push(i);
	}
#pragma warning(pop)
}

CTrace::~CTrace()
{
	_freeIndexStack.Clear();
	if (_ptrArray != nullptr)
		free(_ptrArray);
}

int CTrace::GetLeftIndexNum()
{
	return _freeIndexStack.GetSize();
}

int CTrace::RegisterTrace(void* ptr, const char* _file_, int _line_)
{
	int index;
	if (_freeIndexStack.pop(index))
	{
		_ptrArray[index].Init(ptr, _file_, _line_);
		return index;
	}
	return -1;
}

bool CTrace::UpdateTrace(int index, const char* _file_, int _line_)
{
	if (index < 0 || index >= _maxTraceNum)
		return false;
	_InterlockedExchangePointer((volatile PVOID*)&_ptrArray[index].use__file__, (PVOID)_file_);
	_InterlockedExchange((long*)&_ptrArray[index].use__line__, _line_);
	return true;
}

bool CTrace::CancelTrace(int index)
{
	if (index < 0 || index >= _maxTraceNum)
		return false;
	_ptrArray[index].Clear();
	_freeIndexStack.push(index);
	return true;
}

bool CTrace::CheckTrace()
{
	bool success = true;
	for (int i = 0; i < _maxTraceNum; i++)
	{
		if (_ptrArray[i].ptr != nullptr)
		{
			c_syslog::logging().Log(L"TRACING", c_syslog::en_ERROR, L"미반환된 포인터 입니다: %p / 트레이스 인덱스: %d", _ptrArray[i].ptr, i);
			wchar_t __wFile__[256];

			const char* file = _ptrArray[i].alloc__file__;
			MultiByteToWideChar(CP_UTF8, 0, file, -1, __wFile__, _countof(__wFile__));
			c_syslog::logging().Log(L"TRACING", c_syslog::en_ERROR, L"[인덱스: %d] 할당 위치: %d / %s", i, _ptrArray[i].alloc__line__, __wFile__);

			file = _ptrArray[i].use__file__;
			MultiByteToWideChar(CP_UTF8, 0, file, -1, __wFile__, _countof(__wFile__));
			c_syslog::logging().Log(L"TRACING", c_syslog::en_ERROR, L"[인덱스: %d] 마지막 참조: %d / %s", i, _ptrArray[i].use__line__, __wFile__);
			success = false;
		}
	}
	return success;
}