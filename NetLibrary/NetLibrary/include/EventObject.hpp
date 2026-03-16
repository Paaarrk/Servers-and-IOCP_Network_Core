#ifndef __EVENT_OBJECT_H__
#define __EVENT_OBJECT_H__
#include "LockFreeQueue.hpp"
#include "TlsPacket.hpp"
#include "TLSObjectPool_IntrusiveList.hpp"



//#define _EVENT_PROFILE
#ifdef _EVENT_PROFILE
#include <profileapi.h>
#endif

//////////////////////////////////////////////////////////////////////
// Event Object
// . 오로지 이벤트를 위한 오브젝트 입니다.
// 
// < V3 (25.11.19) : 핫패스 최적화 >
// . 구조체 조차 주지 않는 방법으로 구현
// . CPacket* 의 readptr을 바로 넣습니다.
// 
// < V2 (25.11.14) >
// . 구조체 방식에서 CPacketViewer* 를 그대로 주는 형식으로 변경
// 
// < V1 (25.11.06) > 
// . 시작점 
//////////////////////////////////////////////////////////////////////
#define EVENT_TLS_KEY	0xF000'0000

namespace Net
{
	class CPacketViewer;
	class CPacket;
	struct stEvent
	{
		enum enEventWhat
		{
			EVENT_MESSAGE = 0,
			EVENT_RELEASE,
			EVENT_CONNECT,
			EVENT_IDENTIFY_RESULT,
			EVENT_N2,
			EVENT_N3,
		};
		enum en_PACKET_ERROR
		{
			PACKET_FINE,
			PACKET_OVER_FLOW,
		};

		char* readptr;

		int				what;
		uint64_t		sessionId;

		CPacket* refPacket;

		int				datasize;
		int				packeterror;
#ifdef _EVENT_PROFILE
		LARGE_INTEGER liStartTime;
		LARGE_INTEGER liEndTime;
		double dProcTimeUs;
		inline static double dTotalProcUs;
		inline static double dMaxProcUs;
		inline static uint64_t iCount;

		void EventEnqueueStart()
		{
			QueryPerformanceCounter(&liStartTime);
		}
		void EventDequeueEnd()
		{
			QueryPerformanceCounter(&liEndTime);
			iCount++;
			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			dProcTimeUs = (double)(liEndTime.QuadPart - liStartTime.QuadPart) / freq.QuadPart * 1000 * 1000;
			if (dMaxProcUs < dProcTimeUs)
				dMaxProcUs = dProcTimeUs;
			dTotalProcUs += dProcTimeUs;
		}

		static double GetEventAvgUs()
		{
			if (iCount == 0)
				return 0.0;
			return (dTotalProcUs) / iCount;
		}
		static double GetEventMaxUs()
		{
			return dMaxProcUs;
		}
#endif


		//--------------------------------------------------------------
		// Info: 패킷을 바탕으로 stEventData구조체 세팅
		// ** pRefPacket의 참조카운트가 올라감
		// . pRefPacket을 안넣을거면 null, datalen은 0
		//--------------------------------------------------------------
		void SetEvent(uint64_t _sessionId, int _what, CPacket* pRefPacket, int iDatalen)
		{
			what = _what;
			sessionId = _sessionId;

			if (pRefPacket != nullptr)
			{
				CPACKET_ADDREF(pRefPacket);
				readptr = pRefPacket->GetReadPtr();
			}
			else
			{
				readptr = nullptr;
			}

			refPacket = pRefPacket;
			datasize = iDatalen;
			packeterror = en_PACKET_ERROR::PACKET_FINE;
		}

		static stEvent* Alloc()
		{
			_InterlockedIncrement(&s_useSize);
			return s_pool.Alloc();
		}

		//---------------------------------------------------------------
		// 성공 0
		// 실패 1 ~ 3(이 풀의 문제)
		//      4 ~ 6(CPacketViewer::Free의 문제)
		//---------------------------------------------------------------
		static int Free(stEvent* pEvent)
		{
			_InterlockedDecrement(&s_useSize);
			int ret;
			if (pEvent->refPacket != nullptr)
			{
				ret = CPACKET_FREE(pEvent->refPacket);
				if (ret)
				{
					s_pool.Free(pEvent);
					return ret + 3;
				}
				pEvent->refPacket = nullptr;
			}
			return s_pool.Free(pEvent);
		}

		bool GetData(char* buffer, int len)
		{
			if (len > datasize)
			{
				packeterror = PACKET_OVER_FLOW;
				return false;
			}
			memcpy(buffer, readptr, len);
			readptr += len;
			datasize -= len;
			return true;
		}

#pragma region 연산자 오버로딩 >>
		// get bool, 사용 후 GetOperatorError()체크해주세요.
		stEvent & operator >> (bool& val)
		{
			if (datasize < sizeof(bool))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((bool*)readptr);
			readptr += sizeof(bool);
			datasize -= sizeof(bool);
			return *this;
		}
		// get uint8_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (uint8_t& val)
		{
			if (datasize < sizeof(uint8_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((uint8_t*)readptr);
			readptr += sizeof(uint8_t);
			datasize -= sizeof(uint8_t);
			return *this;
		}
		// get int8_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (int8_t& val)
		{
			if (datasize < sizeof(int8_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((int8_t*)readptr);
			readptr += sizeof(int8_t);
			datasize -= sizeof(int8_t);
			return *this;
		}

		// get uint16_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (uint16_t& val)
		{
			if (datasize < sizeof(uint16_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((uint16_t*)readptr);
			readptr += sizeof(uint16_t);
			datasize -= sizeof(uint16_t);
			return *this;
		}
		// get int16_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (int16_t& val)
		{
			if (datasize < sizeof(int16_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((int16_t*)readptr);
			readptr += sizeof(int16_t);
			datasize -= sizeof(int16_t);
			return *this;
		}

		// get uint32_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (uint32_t& val)
		{
			if (datasize < sizeof(uint32_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((uint32_t*)readptr);
			readptr += sizeof(uint32_t);
			datasize -= sizeof(uint32_t);
			return *this;
		}
		// get int32_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (int32_t& val)
		{
			if (datasize < sizeof(int32_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((int32_t*)readptr);
			readptr += sizeof(int32_t);
			datasize -= sizeof(int32_t);
			return *this;
		}

		// get uint64_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (uint64_t& val)
		{
			if (datasize < sizeof(uint64_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((uint64_t*)readptr);
			readptr += sizeof(uint64_t);
			datasize -= sizeof(uint64_t);
			return *this;
		}
		// get int64_t, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (int64_t& val)
		{
			if (datasize < sizeof(int64_t))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((int64_t*)readptr);
			readptr += sizeof(int64_t);
			datasize -= sizeof(int64_t);
			return *this;
		}

		// get float, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (float& val)
		{
			if (datasize < sizeof(float))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((float*)readptr);
			readptr += sizeof(float);
			datasize -= sizeof(float);
			return *this;
		}

		// get double, 사용 후 GetOperatorError()체크해주세요.
		stEvent& operator >> (double& val)
		{
			if (datasize < sizeof(double))
			{
				packeterror = en_PACKET_ERROR::PACKET_OVER_FLOW;
				return *this;
			}
			val = *((double*)readptr);
			readptr += sizeof(double);
			datasize -= sizeof(double);
			return *this;
		}

#pragma endregion

		static int GetUseSize() { return (int)s_useSize; }
		static int GetPoolCreateChunkNum() { return s_pool.GetAllocChunkPoolCreateNum(); }
		static int GetPoolLeftChunkNum() { return s_pool.GetAllocChunkPoolSize(); }

		static int GetPoolCreateRChunkNum() { return s_pool.GetReleaseChunkPoolCreateNum(); }
		static int GetPoolLeftRChunkNum() { return s_pool.GetReleaseChunkPoolSize(); }

	private:
		stEvent() = delete;
		inline static long s_useSize = 0;
		inline static CTlsObjectPool<stEvent, EVENT_TLS_KEY, TLS_OBJECTPOOL_USE_RAW> s_pool;

	};
}



#endif