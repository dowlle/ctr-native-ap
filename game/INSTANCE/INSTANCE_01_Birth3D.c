#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003086c-0x800308e4.
struct Instance *INSTANCE_Birth3D(struct Model *model, const char *name, struct Thread *th)
{
	struct Instance *inst = (struct Instance *)JitPool_Add(&sdata->gGT->JitPools.instance);

	if (inst != 0)
	{
		INSTANCE_Birth(inst, model, name, th, 0xf);
	}

	return inst;
}
