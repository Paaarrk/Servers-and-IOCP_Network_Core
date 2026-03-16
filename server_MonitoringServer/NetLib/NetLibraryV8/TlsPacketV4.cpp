#include "TlsPacketV4.h"

CTlsObjectPool<CPacket, COUNTER_CPACKETPOOLS, TLS_OBJECTPOOL_USE_CALLONCE> CPacket::_cPacketPools;
#ifdef CPACKET_TRACING
CTrace CPacket::m_tracePacket(2000000);

#endif

