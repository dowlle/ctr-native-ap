#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001fc40-0x80020064
void COLL_MOVED_TRIANGL_TestPoint(struct ScratchpadStruct *sps, struct BspSearchVertex *v1, struct BspSearchVertex *v2, struct BspSearchVertex *v3)
{
	u8 *spsBytes = (u8 *)sps;
	u8 *v1Bytes = (u8 *)v1;
	struct QuadBlock *quad;
	u16 quadFlags;
	s32 planeNear;
	s32 planeFar;
	s32 normalW;
	s32 projectedFromInput;
	s32 distanceSq;
	s32 distance;
	s32 hitX;
	s32 hitY;
	s32 hitZ;
	s32 reorderResult;

	CollFixed_WriteS16(spsBytes, 0x3c, CollFixed_ReadS16(spsBytes, 0x3c) + 1);
	CollFixed_WriteS16(spsBytes, 0x52, CollFixed_ReadS16(v1Bytes, 6));
	CollFixed_WriteU32(spsBytes, 0x54, CollFixed_ReadU32(v1Bytes, 0xc));
	CollFixed_WriteU32(spsBytes, 0x58, CollFixed_ReadU32(v1Bytes, 0x10));

	quad = *(struct QuadBlock **)(spsBytes + 0x64);

	if (((quad->quadFlags & 0x400) != 0) && (((s32)(s8)quad->terrain_type & sdata->doorAccessFlags) != 0))
		return;

	CollFixed_GteLoadR11R12(CollFixed_ReadU32(spsBytes, 0));
	CollFixed_GteLoadR13R21(CollFixed_ReadU32(spsBytes, 0x10) << 16 | CollFixed_ReadU16(spsBytes, 4));
	CollFixed_GteLoadR22R23((CollFixed_ReadU32(spsBytes, 0x10) >> 16) | ((u32)CollFixed_ReadU16(spsBytes, 0x14) << 16));
	CollFixed_GteLoadVXY0(CollFixed_ReadU32(spsBytes, 0x54));
	CollFixed_GteLoadVZ0(CollFixed_ReadS32(spsBytes, 0x58));
	CollMoved_GteRTV0();

	normalW = CollFixed_ReadS16(spsBytes, 0x5a);
	planeNear = CollFixed_GteReadMAC1() + normalW * -2;
	planeFar = CollFixed_GteReadMAC2() + normalW * -2;

	if (planeFar < 0)
	{
		if (((quad->quadFlags & 0x40) == 0) && ((s32)quad->draw_order_low >= 0))
			goto KeepNormal;

		planeNear = -planeNear;
		planeFar = -planeFar;
		CollFixed_WriteS16(spsBytes, 0x54, -CollFixed_ReadS16(spsBytes, 0x54));
		CollFixed_WriteS16(spsBytes, 0x56, -CollFixed_ReadS16(spsBytes, 0x56));
		CollFixed_WriteS16(spsBytes, 0x58, -CollFixed_ReadS16(spsBytes, 0x58));
		CollFixed_WriteS16(spsBytes, 0x5a, -CollFixed_ReadS16(spsBytes, 0x5a));
	}

KeepNormal:
	quadFlags = quad->quadFlags;
	CollFixed_WriteS16(spsBytes, 0x3c, CollFixed_ReadS16(spsBytes, 0x3c) + 1);

	if ((planeNear - CollFixed_ReadS16(spsBytes, 6)) >= 0)
		return;

	if (planeFar < 0)
		return;

	if (((quadFlags & 0x40) == 0) && ((planeNear - planeFar) > 0))
		return;

	if (planeNear >= 0)
	{
		CollFixed_GteLoadIR0(planeNear);
		CollFixed_GteLoadIR(CollFixed_ReadS16(spsBytes, 0x54), CollFixed_ReadS16(spsBytes, 0x56), CollFixed_ReadS16(spsBytes, 0x58));
		projectedFromInput = 0;
	}
	else
	{
		CollFixed_GteLoadIR(CollFixed_ReadS16(spsBytes, 0) - CollFixed_ReadS16(spsBytes, 0x10),
		                    CollFixed_ReadS16(spsBytes, 2) - CollFixed_ReadS16(spsBytes, 0x12),
		                    CollFixed_ReadS16(spsBytes, 4) - CollFixed_ReadS16(spsBytes, 0x14));
		CollFixed_GteLoadIR0((CollFixed_MulLo(planeNear, -0x1000)) / (planeFar - planeNear));
		projectedFromInput = 1;
	}

	CollMoved_GteGPF12();
	hitX = CollFixed_GteReadMAC1();
	hitY = CollFixed_GteReadMAC2();
	hitZ = CollFixed_GteReadMAC3();

	CollFixed_WriteS16(spsBytes, 0x5c, CollFixed_ReadS16(spsBytes, 0) - hitX);
	CollFixed_WriteS16(spsBytes, 0x5e, CollFixed_ReadS16(spsBytes, 2) - hitY);
	CollFixed_WriteS16(spsBytes, 0x60, CollFixed_ReadS16(spsBytes, 4) - hitZ);

	*(struct BspSearchVertex **)(spsBytes + 0xd8) = v1;
	*(struct BspSearchVertex **)(spsBytes + 0xdc) = v2;
	*(struct BspSearchVertex **)(spsBytes + 0xe0) = v3;

	reorderResult = COLL_MOVED_TRIANGL_ReorderNormals(spsBytes + 0x4c, v1, v2, v3);
	if (reorderResult < 0)
		return;

	if (projectedFromInput != 0)
	{
		CollFixed_WriteS16(spsBytes, 0xe4, CollFixed_ReadS16(spsBytes, 0x5c) - CollFixed_ReadS16(spsBytes, 0x4c));
		CollFixed_WriteS16(spsBytes, 0xe6, CollFixed_ReadS16(spsBytes, 0x5e) - CollFixed_ReadS16(spsBytes, 0x4e));
		CollFixed_WriteS16(spsBytes, 0xe8, CollFixed_ReadS16(spsBytes, 0x60) - CollFixed_ReadS16(spsBytes, 0x50));
	}
	else
	{
		CollFixed_WriteS16(spsBytes, 0xe4, CollFixed_ReadS16(spsBytes, 0) - CollFixed_ReadS16(spsBytes, 0x4c));
		CollFixed_WriteS16(spsBytes, 0xe6, CollFixed_ReadS16(spsBytes, 2) - CollFixed_ReadS16(spsBytes, 0x4e));
		CollFixed_WriteS16(spsBytes, 0xe8, CollFixed_ReadS16(spsBytes, 4) - CollFixed_ReadS16(spsBytes, 0x50));
	}

	CollFixed_GteLoadR11R12(CollFixed_ReadU32(spsBytes, 0xe4));
	CollFixed_GteLoadR13R21(CollFixed_ReadS16(spsBytes, 0xe8));
	CollFixed_GteLoadVXY0(CollFixed_ReadU32(spsBytes, 0xe4));
	CollFixed_GteLoadVZ0(CollFixed_ReadS16(spsBytes, 0xe8));
	CollFixed_GteMVMVA();
	distanceSq = CollFixed_GteReadMAC1();

	if ((distanceSq - CollFixed_ReadS32(spsBytes, 8)) > 0)
		return;

	if ((quadFlags & 0x40) != 0)
	{
		if ((planeNear < 0) || (((planeNear - CollFixed_ReadS16(spsBytes, 6)) | (planeFar - CollFixed_ReadS16(spsBytes, 6))) < 0))
		{
			CollFixed_WriteU32(spsBytes, 0x1a4, CollFixed_ReadU32(spsBytes, 0x1a4) | (u8)quad->terrain_type);
			return;
		}
	}

	distance = planeFar - planeNear;
	if (distance != 0)
		distance = 0x1000 - (CollFixed_MulLo(CollFixed_ReadS16(spsBytes, 6) - planeNear, 0x1000) / distance);

	if ((distance - CollFixed_ReadS32(spsBytes, 0x84)) >= 0)
		return;

	if ((quadFlags & 0x10) != 0)
	{
		if ((quadFlags & 0x200) != 0)
			CollFixed_WriteU32(spsBytes, 0x1a4, CollFixed_ReadU32(spsBytes, 0x1a4) | 0x4000);

		return;
	}

	CollFixed_WriteS32(spsBytes, 0x84, distance);
	sps->levVertHit[0] = v1->pLevelVertex;
	sps->levVertHit[1] = v2->pLevelVertex;
	sps->levVertHit[2] = v3->pLevelVertex;

	CollFixed_WriteU32(spsBytes, 0x68, CollFixed_ReadU32(spsBytes, 0x4c));
	CollFixed_WriteU32(spsBytes, 0x6c, CollFixed_ReadU32(spsBytes, 0x50));
	CollFixed_WriteU32(spsBytes, 0x70, CollFixed_ReadU32(spsBytes, 0x54));
	CollFixed_WriteU32(spsBytes, 0x74, CollFixed_ReadU32(spsBytes, 0x58));
	CollFixed_WriteU32(spsBytes, 0x78, CollFixed_ReadU32(spsBytes, 0x5c));
	CollFixed_WriteU32(spsBytes, 0x7c, CollFixed_ReadU32(spsBytes, 0x60));
	*(struct QuadBlock **)(spsBytes + 0x80) = quad;

	*(u8 *)(spsBytes + 0x7f) = *(u8 *)(spsBytes + 0x63);
	*(s8 *)(spsBytes + 0x7e) = (s8)reorderResult;

	if (distance <= 0)
	{
		CollFixed_WriteU32(spsBytes, 0x1c, CollFixed_ReadU32(spsBytes, 0x10));
		CollFixed_WriteS16(spsBytes, 0x20, CollFixed_ReadS16(spsBytes, 0x14));
	}
	else
	{
		CollFixed_GteLoadIR0(distance);
		CollFixed_GteLoadIR(CollFixed_ReadS16(spsBytes, 0) - CollFixed_ReadS16(spsBytes, 0x10),
		                    CollFixed_ReadS16(spsBytes, 2) - CollFixed_ReadS16(spsBytes, 0x12),
		                    CollFixed_ReadS16(spsBytes, 4) - CollFixed_ReadS16(spsBytes, 0x14));
		CollMoved_GteGPF12();

		CollFixed_WriteS16(spsBytes, 0x1c, CollFixed_ReadS16(spsBytes, 0x10) + CollFixed_GteReadMAC1());
		CollFixed_WriteS16(spsBytes, 0x1e, CollFixed_ReadS16(spsBytes, 0x12) + CollFixed_GteReadMAC2());
		CollFixed_WriteS16(spsBytes, 0x20, CollFixed_ReadS16(spsBytes, 0x14) + CollFixed_GteReadMAC3());
	}

	CollFixed_WriteS16(spsBytes, 0x3e, CollFixed_ReadS16(spsBytes, 0x3e) + 1);
}
