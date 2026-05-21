#include <common.h>

static void CollMoved_GteRTV0(void)
{
	doCOP2(0x0486012);
}

static void CollMoved_GteGPF12(void)
{
	doCOP2(0x0198003d);
}

static void CollMoved_CopyVertexPos(void *dst, const void *src)
{
	CollFixed_WriteU32(dst, 0, CollFixed_ReadU32(src, 0));
	CollFixed_WriteS16(dst, 4, CollFixed_ReadS16(src, 4));
}

static void CollMoved_SelectProjection(s32 normalAxis, void *set1, void *v1, void **v2, void **v3, s32 *firstA, s32 *firstB, s32 *firstHit, s32 *secondA,
                                       s32 *secondB, s32 *secondHit)
{
	s32 origin;
	void *tmp;

	if (normalAxis == 3)
	{
		origin = CollFixed_ReadS16(v1, 4);
		*firstA = CollFixed_ReadS16(*v2, 4) - origin;
		*firstB = CollFixed_ReadS16(*v3, 4) - origin;
		*firstHit = CollFixed_ReadS16(set1, 0x14) - origin;

		if (((*firstA < 0) ? -*firstA : *firstA) < ((*firstB < 0) ? -*firstB : *firstB))
		{
			s32 tmpValue = *firstA;
			*firstA = *firstB;
			*firstB = tmpValue;
			tmp = *v2;
			*v2 = *v3;
			*v3 = tmp;
		}

		origin = CollFixed_ReadS16(v1, 0);
		*secondA = CollFixed_ReadS16(*v2, 0) - origin;
		*secondB = CollFixed_ReadS16(*v3, 0) - origin;
		*secondHit = CollFixed_ReadS16(set1, 0x10) - origin;
	}
	else if (normalAxis == 1)
	{
		origin = CollFixed_ReadS16(v1, 0);
		*firstA = CollFixed_ReadS16(*v2, 0) - origin;
		*firstB = CollFixed_ReadS16(*v3, 0) - origin;
		*firstHit = CollFixed_ReadS16(set1, 0x10) - origin;

		if (((*firstA < 0) ? -*firstA : *firstA) < ((*firstB < 0) ? -*firstB : *firstB))
		{
			s32 tmpValue = *firstA;
			*firstA = *firstB;
			*firstB = tmpValue;
			tmp = *v2;
			*v2 = *v3;
			*v3 = tmp;
		}

		origin = CollFixed_ReadS16(v1, 2);
		*secondA = CollFixed_ReadS16(*v2, 2) - origin;
		*secondB = CollFixed_ReadS16(*v3, 2) - origin;
		*secondHit = CollFixed_ReadS16(set1, 0x12) - origin;
	}
	else
	{
		origin = CollFixed_ReadS16(v1, 2);
		*firstA = CollFixed_ReadS16(*v2, 2) - origin;
		*firstB = CollFixed_ReadS16(*v3, 2) - origin;
		*firstHit = CollFixed_ReadS16(set1, 0x12) - origin;

		if (((*firstA < 0) ? -*firstA : *firstA) < ((*firstB < 0) ? -*firstB : *firstB))
		{
			s32 tmpValue = *firstA;
			*firstA = *firstB;
			*firstB = tmpValue;
			tmp = *v2;
			*v2 = *v3;
			*v3 = tmp;
		}

		origin = CollFixed_ReadS16(v1, 4);
		*secondA = CollFixed_ReadS16(*v2, 4) - origin;
		*secondB = CollFixed_ReadS16(*v3, 4) - origin;
		*secondHit = CollFixed_ReadS16(set1, 0x14) - origin;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001f928-0x8001fc40
s32 COLL_MOVED_TRIANGL_ReorderNormals(void *set1, void *v1, void *v2, void *v3)
{
	void *baryV2 = v2;
	void *baryV3 = v3;
	s32 firstA;
	s32 firstB;
	s32 firstHit;
	s32 secondA;
	s32 secondB;
	s32 secondHit;
	s32 baryA = -0x1000;
	s32 baryB = -0x1000;
	s32 sum;

	CollMoved_SelectProjection(CollFixed_ReadS16(set1, 6), set1, v1, &baryV2, &baryV3, &firstA, &firstB, &firstHit, &secondA, &secondB, &secondHit);

	if (firstA == 0)
	{
		if (firstB == 0)
			return -1;

		baryB = CollFixed_Sll32(firstHit, 12) / firstB;

		if (secondA != 0)
			baryA = (CollFixed_Sll32(secondHit, 12) - CollFixed_MulLo(baryB, secondB)) / secondA;
	}
	else
	{
		s32 denom = (CollFixed_MulLo(secondB, firstA) - CollFixed_MulLo(firstB, secondA)) >> 6;

		if (denom != 0)
		{
			baryB = CollFixed_MulLo(CollFixed_MulLo(secondHit, firstA) - CollFixed_MulLo(firstHit, secondA), 0x40) / denom;
			baryA = (CollFixed_Sll32(firstHit, 12) - CollFixed_MulLo(baryB, firstB)) / firstA;
		}
	}

	if (baryA == -0x1000)
		return -1;

	sum = baryA + baryB - 0x1000;

	if (baryA < 0)
	{
		if (baryB < 0)
		{
			CollMoved_CopyVertexPos(set1, v1);
			return 0;
		}

		if (sum >= 0)
		{
			CollMoved_CopyVertexPos(set1, baryV3);
			return 4;
		}

		COLL_FIXED_TRIANGL_Barycentrics((s16 *)set1, (s16 *)v1, (s16 *)baryV3, (s16 *)((u8 *)set1 + 0x10));
		return 5;
	}

	if (baryB >= 0)
	{
		if (sum <= 0)
		{
			CollMoved_CopyVertexPos(set1, (u8 *)set1 + 0x10);
			return 6;
		}

		COLL_FIXED_TRIANGL_Barycentrics((s16 *)set1, (s16 *)baryV2, (s16 *)baryV3, (s16 *)((u8 *)set1 + 0x10));
		return 3;
	}

	if (sum >= 0)
	{
		CollMoved_CopyVertexPos(set1, baryV2);
		return 2;
	}

	COLL_FIXED_TRIANGL_Barycentrics((s16 *)set1, (s16 *)v1, (s16 *)baryV2, (s16 *)baryV3);
	return 1;
}
