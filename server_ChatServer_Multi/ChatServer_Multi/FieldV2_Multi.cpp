#include "FieldV2_Multi.h"
#include "UserV2_Multi.h"


// CTlsObjectPool<CAroundSessionId, CAroundSessionId::KEY, TLS_OBJECTPOOL_USE_CALLONCE> CAroundSessionId::s_pool;
long CAroundSessionId::s_resizeCnt = 0;

CField g_field;
void CField::GetAround(stSectorPos* pSector, stSectorAround* pAround)
{
	pAround->iCount = 0;

	int newX;
	int newY;
	int ix = pSector->iX;
	int iy = pSector->iY;
	stSectorPos* pArounds = pAround->around;

	//------------------------------------------------------
	//   x
	// y 0 1 2
	//   3 4 5
	//   6 7 8
	// < 락 규칙: y가 작은것 우선 -> x가 작은 것 우선 >
	//------------------------------------------------------

	/* 0 */
	newX = ix - 1;
	newY = iy - 1;
	if (newX >= 0 && newY >= 0)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 1 */
	newX = ix;
	newY = iy - 1;
	if (newY >= 0)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 2 */
	newX = ix + 1;
	newY = iy - 1;
	if (newX < CField::FIELD_WIDTH && newY >= 0)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 3 */
	newX = ix - 1;
	newY = iy;
	if (newX >= 0)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 4 : 자기자신 */
	newX = ix;
	newY = iy;
	pArounds[pAround->iCount].iX = newX;
	pArounds[pAround->iCount++].iY = newY;

	/* 5 */
	newX = ix + 1;
	newY = iy;
	if (newX < CField::FIELD_WIDTH)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 6 */
	newX = ix - 1;
	newY = iy + 1;
	if (newX >= 0 && newY < CField::FIELD_HEIGHT)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 7 */
	newX = ix;
	newY = iy + 1;
	if (newY < CField::FIELD_HEIGHT)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}

	/* 8 */
	newX = ix + 1;
	newY = iy + 1;
	if (newX < CField::FIELD_WIDTH && newY < CField::FIELD_HEIGHT)
	{
		pArounds[pAround->iCount].iX = newX;
		pArounds[pAround->iCount++].iY = newY;
	}
}