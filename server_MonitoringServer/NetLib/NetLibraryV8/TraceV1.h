#ifndef __TRACE_H__
#define __TRACE_H__
#include "LockFreeStackV3.h"
#include "logclassV1.h"
///////////////////////////////////////////////////////
// 객체를 추적하는 클래스 입니다.
// 락프리 큐를 활용해 인덱스를 관리합니다.
///////////////////////////////////////////////////////


class CTrace
{
public:
	struct stTraceNode
	{
		void* ptr;
		const char* use__file__;
		int use__line__;
		const char* alloc__file__;
		int alloc__line__;
		void Init(void* _ptr, const char* ___file__, int ___line__)
		{
			ptr = _ptr;
			use__file__ = ___file__;
			use__line__ = ___line__;
			alloc__file__ = ___file__;
			alloc__line__ = ___line__;
		}
		void Clear()
		{
			memset(this, 0, sizeof(stTraceNode));
		}
	};

	CTrace(int maxTraceNum):_maxTraceNum(maxTraceNum)
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
	~CTrace()
	{
		_freeIndexStack.Clear();
		if (_ptrArray != nullptr)
			free(_ptrArray);
	}
	int GetLeftIndexNum()
	{
		return _freeIndexStack.GetSize();
	}
	//------------------------------------------------------------------
	// 등록 성공시 받은 인덱스, 실패시(인덱스 부족) -1
	//------------------------------------------------------------------
	int RegisterTrace(void* ptr, const char* _file_, int _line_)
	{
		int index;
		if (_freeIndexStack.pop(index))
		{
			_ptrArray[index].Init(ptr, _file_, _line_);
			return index;
		}
		return -1;
	}
	//------------------------------------------------------------------
	// 업데이트 트레이스
	//------------------------------------------------------------------
	bool UpdateTrace(int index, const char* _file_, int _line_)
	{
		if (index < 0 || index >= _maxTraceNum)
			return false;
		_InterlockedExchangePointer((volatile PVOID*)&_ptrArray[index].use__file__, (PVOID)_file_);
		_InterlockedExchange((long*)&_ptrArray[index].use__line__, _line_);
		return true;
	}

	//------------------------------------------------------------------
	// 해제 성공시 true, 실패시 false
	//------------------------------------------------------------------
	bool CancelTrace(int index)
	{
		if (index < 0 || index >= _maxTraceNum)
			return false;
		_ptrArray[index].Clear();
		_freeIndexStack.push(index);
		return true;
	}

	bool CheckTrace(c_syslog* pLog)
	{
		bool success = true;
		for (int i = 0; i < _maxTraceNum; i++)
		{
			if (_ptrArray[i].ptr != nullptr)
			{
				pLog->Log(L"TRACING", c_syslog::en_ERROR, L"미반환된 포인터 입니다: %p / 트레이스 인덱스: %d", _ptrArray[i].ptr, i);
				wchar_t __wFile__[256];

				const char* file = _ptrArray[i].alloc__file__;
				MultiByteToWideChar(CP_UTF8, 0, file, -1, __wFile__, _countof(__wFile__));
				pLog->Log(L"TRACING", c_syslog::en_ERROR, L"[인덱스: %d] 할당 위치: %d / %s", i, _ptrArray[i].alloc__line__, __wFile__);
				
				file = _ptrArray[i].use__file__;
				MultiByteToWideChar(CP_UTF8, 0, file, -1, __wFile__, _countof(__wFile__));
				pLog->Log(L"TRACING", c_syslog::en_ERROR, L"[인덱스: %d] 마지막 참조: %d / %s", i, _ptrArray[i].use__line__, __wFile__);
				success = false;
			}
		}
		return success;
	}

private:
	CLockFreeStack<int> _freeIndexStack;
	stTraceNode* _ptrArray;
	int _maxTraceNum;
};

#endif