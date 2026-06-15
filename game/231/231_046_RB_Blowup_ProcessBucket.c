#include <common.h>

static struct InstDrawPerPlayer *RB_Blowup_GetIDPP(struct Instance *inst, int playerIndex)
{
	return (struct InstDrawPerPlayer *)((char *)inst + sizeof(struct Instance) + (playerIndex * sizeof(struct InstDrawPerPlayer)));
}

static void RB_Blowup_CopyDrawState(struct Instance *dstInst, struct Instance *srcInst, int playerIndex)
{
	struct InstDrawPerPlayer *src = RB_Blowup_GetIDPP(srcInst, playerIndex);
	struct InstDrawPerPlayer *dst = RB_Blowup_GetIDPP(dstInst, playerIndex);

	dst->instFlags &= src->instFlags | ~DRAW_SUCCESSFUL;
	dst->otRangeNormal = src->otRangeNormal;
	dst->depthOffset[0] = src->depthOffset[0];
	dst->depthOffset[1] = src->depthOffset[1];
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1714-0x800b17f0
void RB_Blowup_ProcessBucket(struct Thread *thread)
{
	struct GameTracker *gGT = sdata->gGT;

	for (; thread != NULL; thread = thread->siblingThread)
	{
		u32 *blowup = thread->object;

		for (int i = 0; i < gGT->numPlyrCurrGame; i++)
		{
			struct Instance *shockwaveInst = (struct Instance *)(uintptr_t)blowup[0];
			struct Instance *explosionInst = (struct Instance *)(uintptr_t)blowup[1];

			if (shockwaveInst == NULL || explosionInst == NULL)
				continue;

			RB_Blowup_CopyDrawState(explosionInst, shockwaveInst, i);
		}
	}
}
