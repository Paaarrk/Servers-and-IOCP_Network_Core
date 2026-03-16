#include "TlsPacketV4.h"

CTlsObjectPool<CPacket, COUNTER_CPACKETPOOLS, TLS_OBJECTPOOL_USE_CALLONCE> CPacket::_cPacketPools;
//CTlsObjectPool_Array<CPacket::_buffer, COUNTER_CPACKETPOOLS, TLS_OBJECTPOOL_USE_RAW> CPacket::_bufferPools;
#ifdef CPACKET_TRACING
CTrace CPacket::m_tracePacket(2000000);

#endif


// CTlsObjectPool<CPacketViewer, COUNTER_CPACKETVIEWERPOOLS, TLS_OBJECTPOOL_USE_RAW> CPacketViewer::s_pool;