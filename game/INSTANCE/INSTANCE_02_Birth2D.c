#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800308e4-0x800309a4.
struct Instance *INSTANCE_Birth2D(struct Model *model, const char *name, struct Thread *th)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Instance *inst;
	struct InstDrawPerPlayer *idpp;
	int i;

	inst = (struct Instance *)JitPool_Add(&gGT->JitPools.instance);

	if (inst != NULL)
	{
		INSTANCE_Birth(inst, model, name, th, 0x40f);
	}

	idpp = INST_GETIDPP(inst);
	idpp[0].pushBuffer = &gGT->pushBuffer_UI;

	for (i = 1; i < gGT->numPlyrCurrGame; i++)
	{
		idpp[i].pushBuffer = 0;
	}

	return inst;
}
