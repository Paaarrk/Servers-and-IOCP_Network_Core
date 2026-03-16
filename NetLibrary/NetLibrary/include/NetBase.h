#ifndef __NET_BASE_H__
#define __NET_BASE_H__

#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include "TlsPacket.hpp"

#define SESSION_RELEASE_FLAG		0x8000'0000
#define SESSION_INDEX_MASK			0x0000'0000'000F'FFFF
namespace Net
{
	constexpr const wchar_t* TAG_NET = L"Net";
	constexpr const int32_t NET_MAX_THREADS_CNT = 64;
	constexpr const int32_t NET_RECVQ_SIZE = Net::CPacket::en_PACKET::eBUFFER_DEFAULT - Net::NET_HEADER_LEN;
	constexpr const int32_t NET_RECVQ_MIN_LEFT_SIZE = 50;

	constexpr const wchar_t* NET_STRANGE_PACKET = L"NetStrangePacket";
	// 헤더 받은 이후 메시지 완s성까지 최대 기다려주는 recv횟수
	constexpr const int32_t NET_MAX_PACKET_COMPLETE = 5;
	constexpr const int		SERVER_MONITORING_TICK = 1000;

	constexpr const DWORD	SERVER_MSG_EXIT = 0;
	constexpr const DWORD	SERVER_MSG_RELEASE = 1;
	constexpr const DWORD	SERVER_MSG_DELAYSEND = 2;
	constexpr const DWORD	SERVER_MSG_USER_EVENT = 3;
	constexpr const	DWORD	SERVER_MSG_ZONE_UPDATE = 4;


	constexpr const int32	CLIENT_MONITORING_TICK = 1000;

	constexpr const DWORD	CLIENT_MSG_EXIT = 0;
	constexpr const DWORD	CLIENT_MSG_RELEASE = 1;
	constexpr const DWORD	CLIENT_MSG_DELAYSEND = 2;
	constexpr const DWORD	CLIENT_MSG_CONNECT = 3;


	// wsa 전역 1회 초기화 위해 존재, cleanup은 시점 애매해서 하지 않음
	class CWSAStart
	{
	public:
		CWSAStart();
	private:
		inline static SRWLOCK _lock = SRWLOCK_INIT;
		inline static int32* _callonce = nullptr;
	};
}

#endif