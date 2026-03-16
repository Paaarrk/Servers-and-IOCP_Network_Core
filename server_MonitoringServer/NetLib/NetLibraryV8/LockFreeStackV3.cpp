#include "LockFreeStackV3.h"

template<typename T>
CTlsObjectPool<typename CLockFreeStack<T>::stNode, LOCKFREE_STACK_POOL_NUM, TLS_OBJECTPOOL_USE_RAW> CLockFreeStack<T>::s_nodePools;


template class CLockFreeStack<int>;