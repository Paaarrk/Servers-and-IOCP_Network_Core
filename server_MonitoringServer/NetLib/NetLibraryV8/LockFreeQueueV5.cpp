#include "LockFreeQueueV5.h"

template <typename T>
unsigned long CLockFreeQueue<T>::s_counter = 0;
template <typename T>
CTlsObjectPool<typename CLockFreeQueue<T>::stNode, LOCKFREE_QUEUE_POOL_NUM, TLS_OBJECTPOOL_USE_RAW> CLockFreeQueue<T>::s_nodePool;


class CPacket;
template class CLockFreeQueue<CPacket*>;

struct stEvent;
template class CLockFreeQueue<stEvent*>;

struct stServerMonitorInfo;
template class CLockFreeQueue<stServerMonitorInfo*>;

