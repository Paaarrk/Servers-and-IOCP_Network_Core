#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__
#include <stdlib.h>
#include <memory.h>
#include <windows.h>

//------------------------------------------------------------------------------
// V3 멀티스레드 안전성 추가
// V4 IsEmpty() 함수 추가
//------------------------------------------------------------------------------
#ifndef RB_SETSIZE
#define RB_SETSIZE		1024
#endif

#ifndef RB_MAXSIZE
	#define RB_MAXSIZE		8192
#endif
/*
	한칸을 비우는 링버퍼
	(가용공간 = _size - 1)
	* _read > _write 일 때 
	wrap_around 발생 하니 주의! *
	RB_MAXSIZE 설정해주세요!
*/
namespace Core
{
	class RingBuffer
	{
		friend struct stNetSession;
		friend class CLanServer;
	public:
		RingBuffer(int size = RB_SETSIZE) :_size(size), _lock(SRWLOCK_INIT)
		{
			_buff = (char*)malloc(sizeof(char) * (_size));
			_end = _buff + _size;
			_read = _buff;
			_write = _buff;
		}
		~RingBuffer()
		{
			if (_buff != nullptr)
				free(_buff);
		}
		int GetBufferSize() { return _size; } const

		//---------------------------------------------------------------
		// cchar*data, int _count
		// data를 넣고 넣은 만큼 반환. 반환은 size or 0 (실패)
		// 
		//---------------------------------------------------------------
		int Enqueue(const char* data, int size)
		{
			if (size <= 0)
				return 0;
			int freesize;
			const char* pdata;
			int firstcopysize;
			int leftcopysize;
			// GetFreeSize
			char* read = _read;
			char* write = _write;
			if (read > write)
			{
				freesize = (int)(read - write - 1);
				// 공간 부족
				if (freesize < size)
					return 0;

				// 맘 편하게 복사 (read가 크니까)
				memcpy(write, data, (size_t)size);

				// _write 포인터 이동
				_write = write + size;
			}
			else
			{
				freesize = (int)(read - write - 1 + _size);
				// 공간 부족
				if (freesize < size)
					return 0;

				// 불편하게 복사
				pdata = data;
				firstcopysize = (int)(_end - write);
				if (firstcopysize < size)
				{
					// 1. 먼저 read의 앞으로 복사
					memcpy(write, pdata, (size_t)(firstcopysize));
					pdata += firstcopysize;
					leftcopysize = size - firstcopysize;
					// 2. 남은거 버퍼의 앞에 복사
					memcpy(_buff, pdata, (size_t)leftcopysize);

					// _write 포인터 이동
					_write = _buff + leftcopysize;
				}
				else
				{	// 사이즈가 firstcopysize보다 작거나 같으면 한방에
					memcpy(write, pdata, (size_t)size);
					write += size;
					if (write == _end)
						_write = _buff;
					else
						_write = write;
				}
			}
			return size;
		}

		//---------------------------------------------------------------
		// char* buff, int _count
		// buff에 size만큼 받는다. 반환은 size or 0 (실패)
		//---------------------------------------------------------------
		int Dequeue(char* buff, int size)
		{
			if (size <= 0)
				return 0;
			int usesize;
			char* pbuff;
			int firstcopysize;
			int leftcopysize;
			char* write = _write;
			char* read = _read;
			// GetUseSize
			if (read > write)
			{
				usesize = (int)(write - read + _size);
				if (usesize < size)
					return 0;

				// 불편하게 복사
				// 1. write의 앞 먼저 
				pbuff = buff;
				firstcopysize = (int)(_end - read);
				if (firstcopysize < size)
				{	// 사이즈가 더 크면
					memcpy(pbuff, read, (size_t)firstcopysize);
					pbuff += firstcopysize;
					leftcopysize = size - firstcopysize;
					// 2. 버퍼 앞부터 write 이전 까지
					memcpy(pbuff, _buff, (size_t)leftcopysize);

					// 포인터 이동
					_read = _buff + leftcopysize;
				}
				else
				{	// 사이즈가 더 작거나 같으면 한방에
					memcpy(pbuff, read, (size_t)size);
					read += size;
					if (read == _end)
						_read = _buff;
					else
						_read = read;
				}
			}
			else
			{
				usesize = (int)(write - read);
				if (usesize < size)
					return 0;

				// 맘편히 복사
				memcpy(buff, read, (size_t)size);

				// 포인터 이동
				_read = read + size;
			}
			return size;
		}

		//---------------------------------------------------------------
		// 현재 버퍼에 남은 용량 얻기 (실 용량 - 1)
		//---------------------------------------------------------------
		int GetFreeSize()
		{
			char* read = _read;
			char* write = _write;
			if (read > write)
				return (int)(read - write - 1);
			return (int)(read - write - 1 + _size);
		}

		//---------------------------------------------------------------
		// 현재 버퍼에 저장된 양 얻기
		//---------------------------------------------------------------
		int GetUseSize()
		{
			char* read = _read;
			char* write = _write;
			if (read > write)
				return (int)(write - read + _size);
			return (int)(write - read);
		}

		//---------------------------------------------------------------
		// 비었으면 true, 뭔가 있으면 false
		//---------------------------------------------------------------
		bool IsEmpty() { return (_read == _write); }

		//---------------------------------------------------------------
		// 버퍼를 비웁니다.
		//---------------------------------------------------------------
		void ClearBuffer()
		{
			_read = _buff;
			_write = _buff;
		}

		//---------------------------------------------------------------
		// ReadPos에서 데이터만 읽음. ReadPos고정
		// 
		// 매개변수: (char*)데이터 받을 버퍼, (int)읽을 크기
		// Return: 가져온 크기, 읽을크기보다 usesize가 작으면 0반환
		//---------------------------------------------------------------
		int Peek(char* buff, int size)
		{
			int usesize;
			char* pbuff;
			int firstcopysize;
			int leftcopysize;
			char* read = _read;
			char* write = _write;
			// GetUseSize
			if (read > write)
			{
				usesize = (int)(write - read + _size);
				if (usesize < size)
					return 0;

				// 불편하게 복사
				// 1. write의 앞 먼저 
				pbuff = buff;
				firstcopysize = (int)(_end - read);
				if (firstcopysize < size)
				{	// 사이즈가 더 크면
					memcpy(pbuff, read, (size_t)firstcopysize);
					pbuff += firstcopysize;
					leftcopysize = size - firstcopysize;
					// 2. 버퍼 앞부터 write 이전 까지
					memcpy(pbuff, _buff, (size_t)leftcopysize);

					// 포인터 이동 안함
				}
				else
				{	// 사이즈가 더 작거나 같으면 한방에
					memcpy(pbuff, read, (size_t)size);

					// 포인터 이동 안함
				}
			}
			else
			{
				usesize = (int)(write - read);
				if (usesize < size)
					return 0;

				// 맘편히 복사
				memcpy(buff, read, (size_t)size);

				// 포인터 이동 안함
			}
			return size;
		}
		//---------------------------------------------------------------
		// 익스클루지브 락
		//---------------------------------------------------------------
		void exclusive_lock()
		{
			AcquireSRWLockExclusive(&_lock);
		}
		//---------------------------------------------------------------
		// 셰어드 락
		//---------------------------------------------------------------
		void shared_lock()
		{
			AcquireSRWLockShared(&_lock);
		}
#pragma warning(push)
#pragma warning(disable: 26110)
		//---------------------------------------------------------------
		// 익스클루지브 언락
		//---------------------------------------------------------------
		void exclusive_unlock()
		{
			ReleaseSRWLockExclusive(&_lock);
		}
		//---------------------------------------------------------------
		// 셰어드 언락
		//---------------------------------------------------------------
		void shared_unlock()
		{
			ReleaseSRWLockShared(&_lock);
		}
#pragma warning(pop)

		//---------------------------------------------------------------
		// 버퍼의 _write포인터 획득
		//---------------------------------------------------------------
		char* GetWritePtr() { return _write; }

		//---------------------------------------------------------------
		// 버퍼의 _read포인터 획득
		//---------------------------------------------------------------
		char* GetReadPtr() { return _read; }

		//---------------------------------------------------------------
		// 원하는 길이만큼 _write포인터 이동
		// 성공시 _count, 실패시 0 반환
		//---------------------------------------------------------------
		int MoveWrite(int size)
		{
			if (size <= 0)	//뒤로 가면 안됨, 0도 이유 없음
				return 0;
			int freesize;
			int firstmovesize;
			char* read = _read;
			char* write = _write;
			if (read > write)
			{
				freesize = (int)(read - write - 1);
				// 공간 부족
				if (freesize < size)
					return 0;

				// _write 포인터 이동
				write += size;
			}
			else
			{
				freesize = (int)(read - write - 1 + _size);
				// 공간 부족
				if (freesize < size)
					return 0;

				firstmovesize = (int)(_end - write);
				if (firstmovesize < size)
				{
					// _write 포인터 이동 (_buff + leftmovesize)
					write = _buff + (size - firstmovesize);
				}
				else
				{
					write += size;
					if (write == _end)
						write = _buff;
				}
			}
			_write = write;

			return size;
		}

		//---------------------------------------------------------------
		// 원하는 길이만큼 _read포인터 이동
		// 성공시 _count, 실패시 0 반환
		//---------------------------------------------------------------
		int MoveRead(int size)
		{
			if (size <= 0)
				return 0;
			int usesize;
			int firstmovesize;
			char* read = _read;
			char* write = _write;

			if (read > write)
			{
				usesize = (int)(write - read + _size);
				if (usesize < size)
					return 0;

				firstmovesize = (int)(_end - read);
				if (firstmovesize < size)
				{
					// 포인터 이동 (_buff + leftsize)
					read = (_buff + size - firstmovesize);
				}
				else
				{	// 사이즈가 더 작거나 같으면 한방에
					read += size;
					if (read == _end)
						read = _buff;
				}
			}
			else
			{
				usesize = (int)(write - read);
				if (usesize < size)
					return 0;

				// 포인터 이동
				read += size;
			}

			_read = read;
			return size;
		}

		//---------------------------------------------------------------
		// 끊기지 않는 길이
		// return: 외부에서 한방에 쓸 수 있는 길이 반환
		//---------------------------------------------------------------
		int DirectEnqueueSize()
		{
			char* read = _read;
			char* write = _write;
			if (read > write)
				return (int)(read - write - 1);
			else
			{
				if (read == _buff)
					return (int)(_end - write - 1);
				else
					return (int)(_end - write);
			}
		}

		//---------------------------------------------------------------
		// 끊기지 않는 길이
		// return: 외부에서 한방에 쓸 수 있는 길이 반환
		//---------------------------------------------------------------
		int DirectDequeueSize()
		{
			char* read = _read;
			char* write = _write;
			if (read > write)
				return (int)(_end - read);
			else
				return (int)(write - read);
		}

	private:
		int		_size;
		char*	_buff;
		char*	_end;
		char*	_read;
		char*	_write;
		SRWLOCK _lock;
	};
}

#endif