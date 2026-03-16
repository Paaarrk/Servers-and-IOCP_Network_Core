#ifndef __ZONE_TYPE_H__
#define __ZONE_TYPE_H__
#include <new>
#include "Type.h"


namespace Net
{
	struct stZoneType
	{
		int32	 contentsId;
		int32	 minimumTick;
		int32	 maxUsers;
		bool	 usePinned;
		int32	 zoneWeight;
		int32	 zoneSize;
		int32	 zoneAlign;
		void	(*constructer)(void*);
		void	(*destructer)(void*);
	};

	template<class T>
	stZoneType* MakeZoneType(int32 contentsID, int32 minimumTick, int32 maxUsers, bool usePinned = false, int32 zoneWeight = 0)
	{
		return new stZoneType{
			contentsID,
			minimumTick,
			maxUsers,
			usePinned, 
			zoneWeight,
			(int32)sizeof(T),
			(int32)alignof(T),
			[](void* pBuf) { new (pBuf) T(); },
			[](void* pBuf) { static_cast<T*>(pBuf)->~T(); }
		};
	}
}


#endif