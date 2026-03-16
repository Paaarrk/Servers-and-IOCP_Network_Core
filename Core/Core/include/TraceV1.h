#ifndef __TRACE_H__
#define __TRACE_H__
///////////////////////////////////////////////////////
// 객체를 추적하는 클래스 입니다.
// 락프리 큐를 활용해 인덱스를 관리합니다.
///////////////////////////////////////////////////////

#include "LockFreeStack.hpp"
namespace Core
{
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
			void Init(void* _ptr, const char* ___file__, int ___line__);
			void Clear();
		};

		CTrace(int maxTraceNum);
		~CTrace();
		int GetLeftIndexNum();
		//------------------------------------------------------------------
		// 등록 성공시 받은 인덱스, 실패시(인덱스 부족) -1
		//------------------------------------------------------------------
		int RegisterTrace(void* ptr, const char* _file_, int _line_);
		//------------------------------------------------------------------
		// 업데이트 트레이스
		//------------------------------------------------------------------
		bool UpdateTrace(int index, const char* _file_, int _line_);

		//------------------------------------------------------------------
		// 해제 성공시 true, 실패시 false
		//------------------------------------------------------------------
		bool CancelTrace(int index);

		bool CheckTrace();

	private:
		Core::CLockFreeStack<int> _freeIndexStack;
		stTraceNode* _ptrArray;
		int _maxTraceNum;
	};
}

#endif