#include <common.h>

static u32 CAM_MapRange_PackS16Pair(s32 lo, s32 hi)
{
	return (u16)lo | ((u32)(u16)hi << 16);
}

int CAM_MapRange_PosPoints(s16 *pos1, s16 *pos2, s16 *currPos)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001b254-0x8001b334.
	SVec3 pathDelta;
	pathDelta.x = pos1[0] - pos2[0];
	pathDelta.y = pos1[1] - pos2[1];
	pathDelta.z = pos1[2] - pos2[2];

	MATH_VectorNormalize(&pathDelta);

	SVec3 currDelta;
	currDelta.x = currPos[0] - pos1[0];
	currDelta.y = currPos[1] - pos1[1];
	currDelta.z = currPos[2] - pos1[2];

	CTC2(CAM_MapRange_PackS16Pair(pathDelta.x, pathDelta.y), 0);
	CTC2((s32)pathDelta.z, 1);
	gte_ldv0(&currDelta);
	gte_mvmva(0, 0, 0, 3, 0);

	return MFC2_S(25) >> 12;
}
