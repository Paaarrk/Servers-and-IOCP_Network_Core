#ifndef __PACKET_TLS_POOL_H__
#define __PACKET_TLS_POOL_H__
#include <stdlib.h>
#include <memory.h>
#include <stdint.h>
#include <windows.h>

#include "NetProtocol.h"
#include "TLSObjectPool_IntrusiveList.hpp"
//#include "TLSObjectPool_Array_V1.h"
//---------------------------------------------------------------------------------------------
// < V4.1: 일단 배열형 지원X >
// . 청크 풀의 Max에 도달하면 ~CPacket이 호출 될텐데, 배열같은 경우 128개가 전부 해제되지 않으면
//   그냥 그대로 밖을 떠돌아다님, 풀은 부족하고 더 생성하면 메모리가 불필요하게 많이 늘어난다.
// 
// < V3: 트레이스 기능 추가 >
// . 메모리 할당과 참조, 해제를 어디서 했는지 추적
// 
// < V2: 참조카운트 적용 >
// . 참조카운트 등장 (Alloc에서 +1, Free에서 -1)
//   => 복사, 대입 생성자 막음
// . 이를 외부에서 사용하기 위한 CPacketHandle 클래스가 생김
//   => 생성자에서 풀에서 하나가져옴 (참조 카운트 증가)
//   => 소멸자에서 Free함 (참조 카운트 감소)
// 
// . 완벽한 서버의 패킷 전용 직렬화 버퍼, 다른용도로 사용이 불가
//   => Alloc시 헤더 길이를 앞으로 땡겨둠
// 
// 
// < V1: TLS적용 >
//---------------------------------------------------------------------------------------------


//---------------------------------------------------------------------------------------------
// 전용 직렬화 패킷 풀, Clear시 이 길이만큼 땡김
//---------------------------------------------------------------------------------------------
#define CPACKET_HEADER_LEN			Net::NET_HEADER_LEN
#define COUNTER_CPACKETPOOLS		0x0001FFFE
#define COUNTER_CPACKETVIEWERPOOLS	0x0001FFFD

//#define CPACKET_TRACING

#ifdef CPACKET_TRACING
#include "TraceV1.h"

constexpr int CPACKET_TRACE_CNT = 2000;
	
#define CPACKET_ALLOC()					Net::CPacket::Alloc(__FILE__, __LINE__)

//-----------------------------------------------------------------------------------------
// CPacket 반환
// Parameter: CPacket 포인터 (사용 했던)
// Return: 0: 성공, 1: 언더플로, 2: 오버플로 3: 널ptr, 4: 더블프리, 5: 참조카운트 음수, Free호출이 많음
//-----------------------------------------------------------------------------------------
#define CPACKET_FREE(pPacket)			Net::CPacket::Free((pPacket), __FILE__, __LINE__)
#define CPACKET_ADDREF(pPacket)			(pPacket)->AddRef(__FILE__, __LINE__)
#define CPACKET_UPDATE_TRACE(pPacket)	(pPacket)->UpdateTrace(__FILE__, __LINE__)

#define CPACKET_CREATE(pkt)				Net::CPacketPtr (pkt)(__FILE__, __LINE__)
#else
#define CPACKET_ALLOC()					Net::CPacket::Alloc()

//-----------------------------------------------------------------------------------------
// CPacket 반환
// Parameter: CPacket 포인터 (사용 했던)
// Return: 0: 성공, 1: 언더플로, 2: 오버플로 3: 널ptr, 4: 더블프리, 5: 참조카운트 음수, Free호출이 많음
//-----------------------------------------------------------------------------------------
#define CPACKET_FREE(pPacket)			Net::CPacket::Free(pPacket)
#define CPACKET_ADDREF(pPacket)			(pPacket)->AddRef()
#define CPACKET_UPDATE_TRACE(pPacket)	

#define CPACKET_CREATE(pkt)				Net::CPacketPtr pkt
#endif

namespace Net
{
	//---------------------------------------------------------------------------------------------
	// 직렬화 버퍼, 패킷을 위해서
	// Tls 오브젝트풀을 사용한 버전. 버퍼는 배열 오브젝트풀
	//---------------------------------------------------------------------------------------------
	class CPacket
	{
		template <typename T, int __keyValue, int __whatFreeToUse>
		friend class CTlsObjectPool;
	public:
		//-----------------------------------------------------------------------------------------
		// PACKET ENUM
		//-----------------------------------------------------------------------------------------
		enum en_PACKET
		{
			eBUFFER_DEFAULT = 640
		};
		//-----------------------------------------------------------------------------------------
		// PACKET ENUM
		//-----------------------------------------------------------------------------------------
		enum en_PACKET_ERROR
		{
			PACKET_FINE,
			PACKET_UNDER_FLOW,
			PACKET_OVER_FLOW,
		};

		//-----------------------------------------------------------------------------------------
		// 연산자 사용 시 언더플로, 오버플로 체크
		// Parameter: -
		// Return: PACKET_FINE(0), PACKET_UNDER_FLOW(1), PACKET_OVER_FLOW(2)
		//-----------------------------------------------------------------------------------------
		unsigned int GetOperatorError()
		{
			return m_error;
		}

		//-----------------------------------------------------------------------------------------
		// 청소하는 목적, 데이터사이즈를 CPACKET_HEADER_LEN 으로 만든다.
		// . 이유: 나주엥 SendPacket에서 헤더 심어주려고
		// Parameter: -
		// Return: -
		//-----------------------------------------------------------------------------------------
		void Clear(void)
		{
			m_datasize = CPACKET_HEADER_LEN;
			m_readptr = m_chrbuffer;
			m_writeptr = m_chrbuffer + CPACKET_HEADER_LEN;
			m_buffersize = eBUFFER_DEFAULT;
		}

		void SetRecvBuffer()
		{
			m_datasize = 0;
			m_readptr = m_chrbuffer + CPACKET_HEADER_LEN;
			m_buffersize = eBUFFER_DEFAULT - CPACKET_HEADER_LEN;
		}

		//-----------------------------------------------------------------------------------------
		// 버퍼 사이즈 얻기
		// Prameter: -
		// Return: 버퍼의 크기(int)
		//-----------------------------------------------------------------------------------------
		int GetBufferSize() const
		{
			return m_buffersize;
		}
		//-----------------------------------------------------------------------------------------
		// 버퍼 내 사용중인 사이즈 얻기
		// ** 혹시 외부에서 이걸 확인하면 CPACKET_HEADER_LEN만큼 빼줘야 자신이 넣은 데이터 크기
		// Prameter: -
		// Return: 버퍼 내 데이터 크기(int)
		//-----------------------------------------------------------------------------------------
		int GetDataSize() const
		{
			return m_datasize;
		}
		//-----------------------------------------------------------------------------------------
		// 버퍼 내 미사용 사이즈 얻기
		// Prameter: -
		// Return: 버퍼 내 free한(쓸 수 있는) 공간(int)
		//-----------------------------------------------------------------------------------------
		int GetFreeSize() const
		{
			return (int)(m_chrbufend - m_writeptr);
		}
		//-----------------------------------------------------------------------------------------
		// 데이터 얻기
		// size와 리턴이 같으면 성공, 0일 시 실패
		// Parameter: 데이터를 뺄 메모리공간(char*),  빼낼 크기 (int) [0이상]
		// Return: (int)빼낸 크기
		//-----------------------------------------------------------------------------------------
		int GetData(char* dest, int size)
		{
			if (size <= 0)	//이건 0인상태로 memcpy가 들어가지 않도록
				return 0;
			int newdatasize = m_datasize - size;
			if (newdatasize < 0)
				return 0;

			memcpy(dest, m_readptr, size);
			m_readptr += size;
			m_datasize = newdatasize;

			return size;
		}

		//-----------------------------------------------------------------------------------------
		// 데이터 삽입
		// size와 리턴이 같으면 성공, 0일 시 실패
		// Parameter: 넣을 데이터(char*),  넣을 크기 (int) [0이상]
		// Return: 넣은 크기 (int)
		//-----------------------------------------------------------------------------------------
		int PushData(const char* src, int size)
		{
			if (size <= 0)	//이건 0인상태로 memcpy가 들어가지 않도록
				return 0;

			int newdatasize = m_datasize + size;
			if (newdatasize > m_buffersize)
				return 0;

			memcpy(m_writeptr, src, size);
			m_writeptr += size;
			m_datasize = newdatasize;

			return size;
		}

		//-----------------------------------------------------------------------------------------
		// CPacket 획득
		// Parameter: -
		// Return: -
		//-----------------------------------------------------------------------------------------
#ifndef CPACKET_TRACING
		static CPacket* Alloc()
		{
			_InterlockedIncrement(&CPacket::s_useNodeCount);
			CPacket* ret = _cPacketPools.Alloc();
			ret->Clear();
			ret->m_refcount = 1;
			ret->m_isEncoded = 0;
			return ret;
		}
#else
		static CPacket* Alloc(const char* _file_, int _line_)
		{
			_InterlockedIncrement(&CPacket::s_useNodeCount);
			CPacket* ret = _cPacketPools.Alloc();
			ret->Clear();
			ret->m_refcount = 1;
			ret->m_isEncoded = 0;

			ret->m_myindex = m_tracePacket.RegisterTrace(ret, _file_, _line_);
			//if (ret->m_myindex == -1)
			//	__debugbreak();
			return ret;
		}
#endif
		//-----------------------------------------------------------------------------------------
		// CPacket 반환
		// Parameter: CPacket 포인터 (사용 했던)
		// Return: 0: 성공, 1: 언더플로, 2: 오버플로 3: 널ptr, 4: 더블프리, 5: 참조카운트 음수, Free호출이 많음
		//-----------------------------------------------------------------------------------------
#ifndef CPACKET_TRACING
		static int Free(CPacket* pPacket)
		{
			long refCount = _InterlockedDecrement(&pPacket->m_refcount);
			if (refCount == 0)
			{
				_InterlockedDecrement(&CPacket::s_useNodeCount);
				return _cPacketPools.Free(pPacket);
			}
			else if (refCount < 0)
				return 5;
			return 0;
		}
#else
		static int Free(CPacket* pPacket, const char* _file_, int _line_)
		{
			pPacket->UpdateTrace(_file_, _line_);
			long refCount = _InterlockedDecrement(&pPacket->m_refcount);
			if (refCount == 0)
			{
				_InterlockedDecrement(&CPacket::s_useNodeCount);
				m_tracePacket.CancelTrace(pPacket->m_myindex);
				return _cPacketPools.Free(pPacket);
			}
			else if (refCount < 0)
				return 5;
			return 0;
		}
#endif

		//-----------------------------------------------------------------------------------------
		// 사용중인 패킷 수
		//-----------------------------------------------------------------------------------------
		static int GetUsePacketCnt()
		{
			return CPacket::s_useNodeCount;
		}

		//-----------------------------------------------------------------------------------------
		// CPacket 사용 끝나면 호출할 릴리즈 함수
		//-----------------------------------------------------------------------------------------
		static void ThreadRelease()
		{
			_cPacketPools.ThreadRelease();
			//_bufferPools.ThreadRelease();
		}

		//-----------------------------------------------------------------------------------------
		// 만들어진 청크 수 (배열버퍼는 X, Alloc)
		//-----------------------------------------------------------------------------------------
		static int GetCreateChunkNum()
		{
			return _cPacketPools.GetAllocChunkPoolCreateNum();
		}
		static int GetCreateRChunkNum()
		{
			return _cPacketPools.GetReleaseChunkPoolCreateNum();
		}
		//-----------------------------------------------------------------------------------------
		// 배열 버퍼 만들어진 청크 수 
		//-----------------------------------------------------------------------------------------
		//static int GetCreateBufferChunkNum()
		//{
		//	return _bufferPools.GetChunkUsingNum();
		//}
		//-----------------------------------------------------------------------------------------
		// 풀에 남은 청크 수 (Alloc)
		//-----------------------------------------------------------------------------------------
		static int GetLeftChunkNum()
		{
			return _cPacketPools.GetAllocChunkPoolSize();
		}
		static int GetLeftRChunkNum()
		{
			return _cPacketPools.GetReleaseChunkPoolSize();
		}
		//-----------------------------------------------------------------------------------------
		// 배열 버퍼 만들어진 청크 수 
		//-----------------------------------------------------------------------------------------
		//static int GetLeftBufferChunkNum()
		//{
		//	return _bufferPools.GetSize();
		//}

		//-----------------------------------------------------------------------------------------
		// 연산자 오버로딩!! 넣기
		//-----------------------------------------------------------------------------------------
#pragma region 연산자 오버로딩 <<
	// push bool, 사용 후 GetOperatorError()체크해주세요.
		CPacket & operator << (bool val)
		{
			int newdatasize = m_datasize + sizeof(bool);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((bool*)m_writeptr) = val;
			m_writeptr += sizeof(bool);
			m_datasize = newdatasize;
			return *this;
		}
		// push uint8_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (uint8_t val)
		{
			int newdatasize = m_datasize + sizeof(uint8_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((uint8_t*)m_writeptr) = val;
			m_writeptr += sizeof(uint8_t);
			m_datasize = newdatasize;
			return *this;
		}
		// push int8_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (int8_t val)
		{
			int newdatasize = m_datasize + sizeof(int8_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((int8_t*)m_writeptr) = val;
			m_writeptr += sizeof(int8_t);
			m_datasize = newdatasize;
			return *this;
		}

		// push uint16_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (uint16_t val)
		{
			int newdatasize = m_datasize + sizeof(uint16_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((uint16_t*)m_writeptr) = val;
			m_writeptr += sizeof(uint16_t);
			m_datasize = newdatasize;
			return *this;
		}
		// push int16_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (int16_t val)
		{
			int newdatasize = m_datasize + sizeof(int16_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((int16_t*)m_writeptr) = val;
			m_writeptr += sizeof(int16_t);
			m_datasize = newdatasize;
			return *this;
		}

		// push uint32_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (uint32_t val)
		{
			int newdatasize = m_datasize + sizeof(uint32_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((uint32_t*)m_writeptr) = val;
			m_writeptr += sizeof(uint32_t);
			m_datasize = newdatasize;
			return *this;
		}
		// push int32_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (int32_t val)
		{
			int newdatasize = m_datasize + sizeof(int32_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((int32_t*)m_writeptr) = val;
			m_writeptr += sizeof(int32_t);
			m_datasize = newdatasize;
			return *this;
		}

		// push uint64_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (uint64_t val)
		{
			int newdatasize = m_datasize + sizeof(uint64_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((uint64_t*)m_writeptr) = val;
			m_writeptr += sizeof(uint64_t);
			m_datasize = newdatasize;
			return *this;
		}
		// push int64_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (int64_t val)
		{
			int newdatasize = m_datasize + sizeof(int64_t);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((int64_t*)m_writeptr) = val;
			m_writeptr += sizeof(int64_t);
			m_datasize = newdatasize;
			return *this;
		}

		// push float, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (float val)
		{
			int newdatasize = m_datasize + sizeof(float);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((float*)m_writeptr) = val;
			m_writeptr += sizeof(float);
			m_datasize = newdatasize;
			return *this;
		}

		// push double, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator << (double val)
		{
			int newdatasize = m_datasize + sizeof(double);
			if (newdatasize > m_buffersize)
			{
				m_error = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			*((double*)m_writeptr) = val;
			m_writeptr += sizeof(double);
			m_datasize = newdatasize;
			return *this;
		}
#pragma endregion
		//-----------------------------------------------------------------------------------------
		// 연산자 오버로딩!! 빼기
		//-----------------------------------------------------------------------------------------
#pragma region 연산자 오버로딩 >>
	// get bool, 사용 후 GetOperatorError()체크해주세요.
		CPacket & operator >> (bool& val)
		{
			int newdatasize = m_datasize - sizeof(bool);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((bool*)m_readptr);
			m_readptr += sizeof(bool);
			m_datasize = newdatasize;
			return *this;
		}
		// get uint8_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (uint8_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint8_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint8_t*)m_readptr);
			m_readptr += sizeof(uint8_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int8_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (int8_t& val)
		{
			int newdatasize = m_datasize - sizeof(int8_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int8_t*)m_readptr);
			m_readptr += sizeof(int8_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get uint16_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (uint16_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint16_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint16_t*)m_readptr);
			m_readptr += sizeof(uint16_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int16_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (int16_t& val)
		{
			int newdatasize = m_datasize - sizeof(int16_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int16_t*)m_readptr);
			m_readptr += sizeof(int16_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get uint32_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (uint32_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint32_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint32_t*)m_readptr);
			m_readptr += sizeof(uint32_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int32_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (int32_t& val)
		{
			int newdatasize = m_datasize - sizeof(int32_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int32_t*)m_readptr);
			m_readptr += sizeof(int32_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get uint64_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (uint64_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint64_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint64_t*)m_readptr);
			m_readptr += sizeof(uint64_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int64_t, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (int64_t& val)
		{
			int newdatasize = m_datasize - sizeof(int64_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int64_t*)m_readptr);
			m_readptr += sizeof(int64_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get float, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (float& val)
		{
			int newdatasize = m_datasize - sizeof(float);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((float*)m_readptr);
			m_readptr += sizeof(float);
			m_datasize = newdatasize;
			return *this;
		}

		// get double, 사용 후 GetOperatorError()체크해주세요.
		CPacket& operator >> (double& val)
		{
			int newdatasize = m_datasize - sizeof(double);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((double*)m_readptr);
			m_readptr += sizeof(double);
			m_datasize = newdatasize;
			return *this;
		}
#pragma endregion


#pragma region Raw

		long isEncoded() { return m_isEncoded; }
		void SetEncoded() { m_isEncoded = 1; }

		//-----------------------------------------------------------------------------------------
		// 버퍼 포인터 얻기
		// Prameter: -
		// Return: 버퍼의 시작점 (char*)
		//-----------------------------------------------------------------------------------------
		char* GetBufferPtr()
		{
			return m_chrbuffer;
		}

		//-----------------------------------------------------------------------------------------
		// Read포인터 얻기
		// Prameter: -
		// Return: Read포인터 (char*)
		//-----------------------------------------------------------------------------------------
		char* GetReadPtr()
		{
			return m_readptr;
		}

		//-----------------------------------------------------------------------------------------
		// Write 포인터 얻기
		// Prameter: -
		// Return: Write포인터 (char*)
		//-----------------------------------------------------------------------------------------
		char* GetWritePtr()
		{
			return m_writeptr;
		}

		//-----------------------------------------------------------------------------------------
		// Read포인터 이동, Parameter에 넣은값이 반환되면 성공, 실패시 0반환
		// 데이터 사이즈도 같이 변경됨
		// 음수, 0 이동 불가
		// Prameter: 이동하고싶은 size (int)
		// Return: 실제 이동된 size (int)
		//-----------------------------------------------------------------------------------------
		int MoveReadPtr(int size)
		{
			if (size <= 0)
				return 0;
			int newdatasize = m_datasize - size;
			if (newdatasize < 0)
				return 0;
			m_datasize = newdatasize;
			m_readptr += size;
			return size;
		}
		//-----------------------------------------------------------------------------------------
		// Write포인터 이동, Parameter에 넣은값이 반환되면 성공, 실패시 0반환
		// 데이터 사이즈도 같이 변경됨
		// 음수, 0 이동 불가
		// Prameter: 이동하고싶은 size (int)
		// Return: 실제 이동된 size (int)
		//-----------------------------------------------------------------------------------------
		int MoveWritePtr(int size)
		{
			if (size <= 0)
				return 0;
			int newdatasize = m_datasize + size;
			if (newdatasize > m_buffersize)
				return 0;
			m_datasize = newdatasize;
			m_writeptr += size;
			return size;
		}


		//-----------------------------------------------------------------------------------------
		// Info: AddRef(), 참조 카운트 올림 (내리는건 Free호출), SendPacket에서 쓰려고 만듬
		// Prameter: 이동하고싶은 size (int)
		// Return: 실제 이동된 size (int)
		//-----------------------------------------------------------------------------------------
#ifndef CPACKET_TRACING
		void AddRef()
		{
			_InterlockedIncrement(&m_refcount);
		}
#else
		void AddRef(const char* _file_, int _line_)
		{
			//if (m_tracePacket.UpdateTrace(m_myindex, _file_, _line_) == false)
			//	__debugbreak();
			m_tracePacket.UpdateTrace(m_myindex, _file_, _line_);
			_InterlockedIncrement(&m_refcount);
		}
		void UpdateTrace(const char* _file_, int _line_) const
		{
			//if (m_tracePacket.UpdateTrace(m_myindex, _file_, _line_) == false)
			//	__debugbreak();
			m_tracePacket.UpdateTrace(m_myindex, _file_, _line_);
		}
#endif


#pragma endregion

	private:
		//-----------------------------------------------------------------------------------------
		// 생성자
		// Return: -
		//-----------------------------------------------------------------------------------------
		CPacket(int size = eBUFFER_DEFAULT) :m_buffersize(size), m_datasize(0), m_error(PACKET_FINE), m_refcount(0), m_isEncoded(0)
		{
			m_chrbuffer = (char*)malloc(CPacket::eBUFFER_DEFAULT);
			m_readptr = m_chrbuffer;
			m_writeptr = m_chrbuffer;
			m_chrbufend = m_chrbuffer + size;
#ifdef CPACKET_TRACING
			m_myindex = -1;
#endif
		}
		//-----------------------------------------------------------------------------------------
		// 파괴자
		// Return: -
		//-----------------------------------------------------------------------------------------
		~CPacket()
		{
			free(m_chrbuffer);
		}
		// 편리한 참조카운트 관리를 위해 막음, 대입할 일도 없다
		CPacket& operator=(const CPacket& ref) = delete;
		// 편리한 참조카운트 관리를 위해 막음, 대입할 일도 없다
		CPacket(const CPacket& ref) = delete;

		// 버퍼 (char 배열 m_buffersize만큼 동적할당)
		char* m_chrbuffer;
		// 버퍼의 끝
		char* m_chrbufend;
		// 버퍼의 크기(int)
		int m_buffersize;
		// 현재 버퍼 내 데이터의 크기 (int)
		int m_datasize;
		// read포인터
		char* m_readptr;
		// write포인터
		char* m_writeptr;
		// 에러 처리 (<<, >>)
		en_PACKET_ERROR m_error;

		// V2: 참조 카운트
		long m_refcount;
		// V2: 인코딩 여부
		long m_isEncoded;

		// V1: TLS기반의 풀
		inline static CTlsObjectPool<CPacket, COUNTER_CPACKETPOOLS, TLS_OBJECTPOOL_USE_CALLONCE> _cPacketPools;
		inline static long s_useNodeCount = 0;
		//static CTlsObjectPool_Array<CPacket::_buffer, COUNTER_CPACKETPOOLS, TLS_OBJECTPOOL_USE_RAW> _bufferPools;

#ifdef CPACKET_TRACING
		int m_myindex;
	public: static bool CheckTrace() { return m_tracePacket.CheckTrace(); }
		  inline static Core::CTrace m_tracePacket{ CPACKET_TRACE_CNT };
#endif
	};

	//---------------------------------------------------------------------------------------------
	// CPacketPtr : 외부에서 패킷을 보내고 싶을 때 이것으로 CPacket을 생성
	// . CPacket을 위한 래퍼
	// . 사용법: 
	//    => CPacketHandle pkt;	//내부적으로 Alloc()수행
	//    => *pkt << (uint32_t)1;
	//    => pkt->PushData(_buffer, sizeof(_buffer));
	//	  // 기존과 사용법 동일합니다.
	// 
	// *pkt << data; // * 오버로딩이 되어있어서 CPacket&을 반환함
	// 
	// ** 센드 패킷 활용
	//   => SendPacket(sessionId, pkt.GetCPacketPtr());
	//---------------------------------------------------------------------------------------------
	class CPacketPtr
	{
	public:
		//-----------------------------------------------------------------------------------------
		// 생성자, 내부적으로 CPacket::Alloc()을 수행함
		//-----------------------------------------------------------------------------------------
#ifndef CPACKET_TRACING
		CPacketPtr()
		{
			pkt = CPacket::Alloc();
		}
		~CPacketPtr()
		{
			int ret = CPACKET_FREE(pkt);
			if (ret)
			{
				__debugbreak();
			}
		}
#else
		CPacketPtr(const char* _file_, int _line_)
		{
			pkt = CPacket::Alloc(_file_, _line_);
		}
		~CPacketPtr()
		{
			int ret = CPACKET_FREE(pkt);
			if (ret)
			{
				__debugbreak();
			}
		}
#endif
		CPacket& operator*()
		{
			return (*pkt);
		}
		CPacket* operator->()
		{
			return pkt;
		}
		CPacket* GetCPacketPtr()
		{
			return pkt;
		}
		// 소유권 전달, 큐에서꺼냇을 때
		CPacketPtr(CPacket* packet)
		{
			pkt = packet;
		}
	private:
		CPacket* pkt;
	};


	//---------------------------------------------------------------------------------------------
	// 뷰어 클래스
	// 오로지 Recv버퍼의 일부를 OnMessage에 보여주기 위해서 만들었다.
	// 이건 그대로 다른곳에 넘겨주면 안되고, 그 스레드에서 해결되야함
	// 여기 데이터를 바탕으로 CPacket을 만들면 됨
	//---------------------------------------------------------------------------------------------
	class CPacketViewer
	{
	public:
		//-----------------------------------------------------------------------------------------
		// PACKET ENUM
		//-----------------------------------------------------------------------------------------
		enum en_PACKET_ERROR
		{
			PACKET_FINE,
			PACKET_UNDER_FLOW,
			PACKET_OVER_FLOW,
		};

		//-----------------------------------------------------------------------------------------
		// 세팅 (지역변수 사용 전용)
		//-----------------------------------------------------------------------------------------
		void SetView(CPacket* cPacket, int len)
		{
			m_readptr = cPacket->GetReadPtr();
			m_datasize = len;
			m_error = PACKET_FINE;
		}

		int MoveReadPtr(int len)
		{
			if (m_datasize - len < 0)
				return 0;
			m_readptr += len;
			m_datasize -= len;
			return len;
		}
		//-----------------------------------------------------------------------------------------
		// 길이
		//-----------------------------------------------------------------------------------------
		int GetDataSize()
		{
			return m_datasize;
		}
		//-----------------------------------------------------------------------------------------
		// 데이터 획득 (len이 내부 데이터 크기보다 작으면 잘릴 수 있음)
		//-----------------------------------------------------------------------------------------
		bool GetData(char* buffer, int len)
		{
			if (len > m_datasize)
			{
				m_error = PACKET_OVER_FLOW;
				return false;
			}
			memcpy(buffer, m_readptr, len);
			m_readptr += len;
			m_datasize -= len;
			return true;
		}

		const char* GetReadPtr() { return m_readptr; }

		int GetPacketError() { return (int)m_error; }

		//-----------------------------------------------------------------------------------------
		// 연산자 오버로딩!! 빼기
		//-----------------------------------------------------------------------------------------
#pragma region 연산자 오버로딩 >>
	// get bool, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer & operator >> (bool& val)
		{
			int newdatasize = m_datasize - sizeof(bool);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((bool*)m_readptr);
			m_readptr += sizeof(bool);
			m_datasize = newdatasize;
			return *this;
		}
		// get uint8_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (uint8_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint8_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint8_t*)m_readptr);
			m_readptr += sizeof(uint8_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int8_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (int8_t& val)
		{
			int newdatasize = m_datasize - sizeof(int8_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int8_t*)m_readptr);
			m_readptr += sizeof(int8_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get uint16_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (uint16_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint16_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint16_t*)m_readptr);
			m_readptr += sizeof(uint16_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int16_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (int16_t& val)
		{
			int newdatasize = m_datasize - sizeof(int16_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int16_t*)m_readptr);
			m_readptr += sizeof(int16_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get uint32_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (uint32_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint32_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint32_t*)m_readptr);
			m_readptr += sizeof(uint32_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int32_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (int32_t& val)
		{
			int newdatasize = m_datasize - sizeof(int32_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int32_t*)m_readptr);
			m_readptr += sizeof(int32_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get uint64_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (uint64_t& val)
		{
			int newdatasize = m_datasize - sizeof(uint64_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((uint64_t*)m_readptr);
			m_readptr += sizeof(uint64_t);
			m_datasize = newdatasize;
			return *this;
		}
		// get int64_t, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (int64_t& val)
		{
			int newdatasize = m_datasize - sizeof(int64_t);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((int64_t*)m_readptr);
			m_readptr += sizeof(int64_t);
			m_datasize = newdatasize;
			return *this;
		}

		// get float, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (float& val)
		{
			int newdatasize = m_datasize - sizeof(float);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((float*)m_readptr);
			m_readptr += sizeof(float);
			m_datasize = newdatasize;
			return *this;
		}

		// get double, 사용 후 GetOperatorError()체크해주세요.
		CPacketViewer& operator >> (double& val)
		{
			int newdatasize = m_datasize - sizeof(double);
			if (newdatasize < 0)
			{
				m_error = en_PACKET_ERROR::PACKET_UNDER_FLOW;
				return *this;
			}
			val = *((double*)m_readptr);
			m_readptr += sizeof(double);
			m_datasize = newdatasize;
			return *this;
		}
#pragma endregion

		CPacketViewer() : m_datasize(0), m_readptr(nullptr), m_error(PACKET_FINE)
		{

		}
	private:
		// 현재 버퍼 내 데이터의 크기 (int)
		int m_datasize;
		// read포인터
		char* m_readptr;
		// 에러 처리 (<<, >>)
		en_PACKET_ERROR m_error;
	};
}

#endif