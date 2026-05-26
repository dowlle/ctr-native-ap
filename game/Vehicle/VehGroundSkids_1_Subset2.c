#include "common.h"

#define VEH_GROUND_SKIDS_SCRATCH_X 0xb8
#define VEH_GROUND_SKIDS_SCRATCH_Y 0xbc
#define VEH_GROUND_SKIDS_SCRATCH_Z 0xc0

static s16 VehGroundSkids_ScaleRelative(u16 value, u16 origin)
{
	return (s16)((value - origin) << 2);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8005c278-0x8005c354.
void VehGroundSkids_Subset2(SVECTOR *scratch, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3)
{
	u8 *scratchBytes = (u8 *)scratch;
	u16 originX = *(u16 *)(scratchBytes + VEH_GROUND_SKIDS_SCRATCH_X);
	u16 originY = *(u16 *)(scratchBytes + VEH_GROUND_SKIDS_SCRATCH_Y);
	u16 originZ = *(u16 *)(scratchBytes + VEH_GROUND_SKIDS_SCRATCH_Z);

	scratch[0].vx = VehGroundSkids_ScaleRelative((u16)v1->vx, originX);
	scratch[0].vy = VehGroundSkids_ScaleRelative((u16)v1->vy, originY);
	scratch[0].vz = VehGroundSkids_ScaleRelative((u16)v1->vz, originZ);

	scratch[1].vx = VehGroundSkids_ScaleRelative((u16)v2->vx, originX);
	scratch[1].vy = VehGroundSkids_ScaleRelative((u16)v2->vy, originY);
	scratch[1].vz = VehGroundSkids_ScaleRelative((u16)v2->vz, originZ);

	scratch[2].vx = VehGroundSkids_ScaleRelative((u16)v3->vx, originX);
	scratch[2].vy = VehGroundSkids_ScaleRelative((u16)v3->vy, originY);
	scratch[2].vz = VehGroundSkids_ScaleRelative((u16)v3->vz, originZ);
}
