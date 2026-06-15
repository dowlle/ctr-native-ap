#include <common.h>

static struct InstDrawPerPlayer *RB_Burst_GetIDPP(struct Instance *inst, int playerIndex)
{
	return (struct InstDrawPerPlayer *)((char *)inst + sizeof(struct Instance) + (playerIndex * sizeof(struct InstDrawPerPlayer)));
}

static void RB_Burst_CopyDrawState(struct Instance *dstInst, struct Instance *srcInst, int playerIndex)
{
	struct InstDrawPerPlayer *src = RB_Burst_GetIDPP(srcInst, playerIndex);
	struct InstDrawPerPlayer *dst = RB_Burst_GetIDPP(dstInst, playerIndex);

	dst->instFlags &= src->instFlags | ~DRAW_SUCCESSFUL;
	dst->otRangeNormal = src->otRangeNormal;
	dst->depthOffset[0] = src->depthOffset[0];
	dst->depthOffset[1] = src->depthOffset[1];
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1bd8-0x800b1d2c
void RB_Burst_ProcessBucket(struct Thread *thread)
{
	struct GameTracker *gGT = sdata->gGT;

	for (; thread != NULL; thread = thread->siblingThread)
	{
		u32 *burst = thread->object;

		for (int i = 0; i < gGT->numPlyrCurrGame; i++)
		{
			struct Instance *shockwaveInst = (struct Instance *)(uintptr_t)burst[0];
			struct Instance *burstInst = (struct Instance *)(uintptr_t)burst[1];
			struct Instance *warpedBurstInst = (struct Instance *)(uintptr_t)burst[2];

			if (burstInst == NULL)
				continue;

			if (shockwaveInst != NULL)
				RB_Burst_CopyDrawState(shockwaveInst, burstInst, i);

			if (warpedBurstInst != NULL)
				RB_Burst_CopyDrawState(warpedBurstInst, burstInst, i);
		}
	}
}
