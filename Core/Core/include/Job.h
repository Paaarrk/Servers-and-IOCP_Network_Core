#ifndef __JOB_H__
#define __JOB_H__
#include "TLSObjectPool_IntrusiveList.hpp"

namespace Core
{
	constexpr int JOB_BLOCK_SIZE = 128;

	struct stJobBlock;

	class IJob
	{
	public:
		virtual void Excute() = 0;
		virtual ~IJob() {};
	};

	template <typename F>
	class CJobLambda : public IJob
	{
	public:
		CJobLambda(F&& f) : func(std::forward<F>(f)) 
		{
			
		}
		void Excute() { func(); }

		void* operator new(size_t size)
		{
			return stJobBlock::Alloc();
		}

		void operator delete(void* ptr)
		{
			stJobBlock::Free((stJobBlock*)ptr);
		}
		~CJobLambda() {}
	private:
		F func;
	};

	struct stJobBlock
	{
		alignas(8) char buffer[JOB_BLOCK_SIZE];

		static stJobBlock* Alloc()
		{
			_InterlockedIncrement(&useCount);
			return s_pool.Alloc();
		}
		static void Free(stJobBlock* pBlock)
		{
			_InterlockedDecrement(&useCount);
			int ret = s_pool.Free(pBlock);
			if (ret)
				__debugbreak();
		}
		static int GetSize() { return s_pool.GetAllocChunkPoolSize(); }
		static int GetCapacity() { return s_pool.GetAllocChunkPoolCreateNum(); }
		static int GetUseSize() { return useCount; }
	private:
		stJobBlock() = delete;
		stJobBlock(const stJobBlock& ref) = delete;
		stJobBlock& operator=(const stJobBlock& ref) = delete;
		inline static long useCount = 0;
		inline static CTlsObjectPool<stJobBlock, 0x2348'3453, TLS_OBJECTPOOL_USE_RAW> s_pool;
	};
}

#endif