#ifndef __NET_CRYPTO_H__
#define __NET_CRYPTO_H__
#include "NetProtocol.h"

namespace Net
{
	class CPacket;

	struct stPacketCrypto
	{
		uint8 static_key;
		uint8 code;
	};

	class CCryptoUtils
	{
	public:
		//--------------------------------------------------------
		// 체크썸을 구합니다. (센드 용)
		//--------------------------------------------------------
		static uint8 GetCheckSum(Net::CPacket* pPacket);

		//--------------------------------------------------------
		// 체크썸을 구합니다.
		//--------------------------------------------------------
		static uint8 GetCheckSum(unsigned char* pRead, uint16 payloadLen);

		//--------------------------------------------------------
		// 패킷을 인코드합니다.
		//--------------------------------------------------------
		static void Encode(Net::CPacket* pPacket, stPacketCrypto& rCrypto);

		//--------------------------------------------------------
		// 패킷을 디코드합니다.
		//--------------------------------------------------------
		static void Decode(unsigned char* pRead, int32 payloadLen, stPacketCrypto& rCrypto);

		//--------------------------------------------------------
		// 헤더를 체크합니다.
		// . read포인터, 페이로드 길이, 디코딩 할것인지여부 (o: 1)
		//--------------------------------------------------------
		static bool CheckHeader(unsigned char* pRead, uint16 payloadLen, bool bNeedDecode, stPacketCrypto& rCrypto);
	private:
		CCryptoUtils() = delete;
	};
}


#endif