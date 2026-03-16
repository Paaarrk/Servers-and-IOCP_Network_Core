#include "NetCrypto.h"
#include "TlsPacket.hpp"

//--------------------------------------------------------
// 체크썸을 구합니다.
//--------------------------------------------------------
uint8 Net::CCryptoUtils::GetCheckSum(Net::CPacket* pPacket)
{
	unsigned char* pCur = (unsigned char*)pPacket->GetBufferPtr() + Net::NET_HEADER_LEN;
	unsigned int sum = 0;
	unsigned char* pEnd = (unsigned char*)pPacket->GetWritePtr();
	while (pCur != pEnd)
	{
		sum += (*pCur);
		pCur++;
	}
	return (unsigned char)(sum % 256);
}	

//--------------------------------------------------------
// 체크썸을 구합니다.
//--------------------------------------------------------
uint8 Net::CCryptoUtils::GetCheckSum(unsigned char* pRead, uint16 payloadLen)
{
	unsigned char* pCur = pRead + Net::NET_HEADER_LEN;
	unsigned int sum = 0;
	unsigned char* pEnd = pCur + payloadLen;
	while (pCur != pEnd)
	{
		sum += (*pCur);
		pCur++;
	}
	return (unsigned char)(sum % 256);
}

//--------------------------------------------------------
// 패킷을 인코드합니다.
//--------------------------------------------------------
void Net::CCryptoUtils::Encode(Net::CPacket* pPacket, Net::stPacketCrypto& rCrypto)
{
	//-----------------------------------------
	// 체크섬 부터 인코드 시작
	//-----------------------------------------
	unsigned char randKey = *(unsigned char*)(pPacket->GetBufferPtr() + 3);
	unsigned char* pEncryptStart = (unsigned char*)pPacket->GetBufferPtr() + 4;
	unsigned char* pEncryptEnd = (unsigned char*)pPacket->GetWritePtr();

	unsigned char i = 1;
	unsigned char Pn = 0;
	unsigned char En = 0;
	while (pEncryptStart != pEncryptEnd)
	{
		Pn = (*pEncryptStart) ^ (Pn + randKey + i);

		En = (Pn) ^ (En + rCrypto.static_key + i);

		*pEncryptStart = En;

		i++;
		pEncryptStart++;
	}
}


//--------------------------------------------------------
// 패킷을 디코드합니다.
//--------------------------------------------------------
void Net::CCryptoUtils::Decode(unsigned char* pRead, int32 payloadLen, Net::stPacketCrypto& rCrypto)
{
	unsigned char randKey = *(pRead + 3);
	unsigned char checkSum = *(pRead + 4);
	unsigned char* pDecryptStart = pRead + 4;
	unsigned char* pDecryptEnd = pRead + 5 + payloadLen;

	unsigned char i = 1;
	unsigned char Dn;
	unsigned char Pn;
	unsigned char En_1 = 0;
	unsigned char Pn_1 = 0;

	while (pDecryptStart != pDecryptEnd)
	{
		Pn = (*pDecryptStart) ^ (En_1 + rCrypto.static_key + i);
		En_1 = *pDecryptStart;

		Dn = Pn ^ (Pn_1 + randKey + i);
		Pn_1 = Pn;

		*pDecryptStart = Dn;

		i++;
		pDecryptStart++;
	}
}

//--------------------------------------------------------
// 헤더를 체크합니다.
//--------------------------------------------------------
bool Net::CCryptoUtils::CheckHeader(unsigned char* pRead, uint16 payloadLen, bool bNeedDecode, Net::stPacketCrypto& rCrypto)
{
	//----------------------------------------------------
	// 헤더 코드가 불일치 한다면
	//----------------------------------------------------
	if (*pRead != rCrypto.code)
		return false;

	if (bNeedDecode)
	{
		Decode(pRead, payloadLen, rCrypto);
	}
	//----------------------------------------------------
	// 체크썸이 불일치 한다면
	//----------------------------------------------------

	uint8 calculateChecksum = GetCheckSum(pRead, payloadLen);
	if (calculateChecksum != *(pRead + 4))
		return false;

	return true;
}