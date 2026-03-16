#ifndef __NET_HEADER_H__
#define __NET_HEADER_H__
#include "Type.h"

namespace Net
{
#pragma pack(push)
#pragma pack(1)
	struct stNetHeader
	{
		uint8 code;	//코드
		uint16 len;	//페이로드 길이
		uint8 randkey;	//랜덤키
		uint8 checksum;	//체크썸
	};
	static constexpr int NET_HEADER_LEN = sizeof(Net::stNetHeader);
#pragma pack(pop)
}

#endif