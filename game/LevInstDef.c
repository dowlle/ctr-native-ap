#include <common.h>

#ifdef CTR_NATIVE
// NOTE(A4-39): retail gives every quadblock its own visInstSrc list, so the
// loops below convert each list exactly once. Some custom levels point many
// quadblocks at one list (Coco Team Racing's track 5 uses one list for all
// 11,934 quadblocks). Converting such a list again would treat an Instance as
// an InstDef and store a stray pointer, which the renderer then draws. An
// entry is an InstDef exactly when it lies on the level's InstDef array, so
// each direction only converts entries still in the other form. Retail lists
// see the same single conversion as before.
static int LevInstDef_IsLevelInstDef(const struct Level *level, const void *entry)
{
	uintptr_t base, at;
	if (level == NULL || level->ptrInstDefs == NULL)
		return 1; // No array to compare against: keep retail's behaviour.
	base = (uintptr_t)level->ptrInstDefs;
	at = (uintptr_t)entry;
	return at >= base && (at - base) / sizeof(struct InstDef) < level->numInstances &&
	       (at - base) % sizeof(struct InstDef) == 0;
}
#endif


void LevInstDef_UnPack(struct mesh_info *ptr_mesh_info)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003116c-0x80031268.
	int i;
	int numQuadBlock;
	struct QuadBlock *ptrQuadBlockArray;
	struct QuadBlock *qbCurr;
	struct InstDef **visInstSrc;
	struct Level *level1;

	numQuadBlock = ptr_mesh_info->numQuadBlock;
	ptrQuadBlockArray = ptr_mesh_info->ptrQuadBlockArray;

	// loop through all quadblocks
	for (i = 0; i < numQuadBlock; i++)
	{
		qbCurr = &ptrQuadBlockArray[i];

		if ((qbCurr->pvs != 0) && (qbCurr->pvs->visInstSrc != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = (struct InstDef **)qbCurr->pvs->visInstSrc; visInstSrc[0] != NULL; visInstSrc++)
			{
#ifdef CTR_NATIVE
				if (!LevInstDef_IsLevelInstDef(sdata->gGT->level1, visInstSrc[0]))
					continue; // A shared list this loop already converted.
#endif
				visInstSrc[0] = (struct InstDef *)visInstSrc[0]->ptrInstance;
			}
		}
	}

	level1 = sdata->gGT->level1;

	if (level1->ptrInstDefPtrArray != 0)
	{
		// loop through all instDef pointers in the LEV
		for (visInstSrc = level1->ptrInstDefPtrArray; visInstSrc[0] != 0; visInstSrc++)
		{
			visInstSrc[0] = (struct InstDef *)visInstSrc[0]->ptrInstance;
		}
	}
}


void LevInstDef_RePack(struct mesh_info *ptr_mesh_info, b32 boolAdvHub)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031268-0x800313c8.
	int i;
	int numQuadBlock;
	struct QuadBlock *ptrQuadBlockArray;
	struct QuadBlock *qbCurr;
	struct Instance **visInstSrc;
	struct Level *level1;
	struct Thread *th;

	numQuadBlock = ptr_mesh_info->numQuadBlock;
	ptrQuadBlockArray = ptr_mesh_info->ptrQuadBlockArray;

	// loop through all quadblocks
	for (i = 0; i < numQuadBlock; i++)
	{
		qbCurr = &ptrQuadBlockArray[i];

		if ((qbCurr->pvs != 0) && (qbCurr->pvs->visInstSrc != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = qbCurr->pvs->visInstSrc; visInstSrc[0] != NULL; visInstSrc++)
			{
#ifdef CTR_NATIVE
				if (LevInstDef_IsLevelInstDef(sdata->gGT->level1, visInstSrc[0]))
					continue; // A shared list this loop already converted back.
#endif
				visInstSrc[0] = (struct Instance *)visInstSrc[0]->instDef; // maybe `visInstSrc[0]->instDef->ptrInstance`?
			}
		}
	}

	level1 = sdata->gGT->level1;

	if (level1->ptrInstDefPtrArray != 0)
	{
		// loop through all instDef pointers in the LEV
		for (visInstSrc = (struct Instance **)level1->ptrInstDefPtrArray; visInstSrc[0] != NULL; visInstSrc++)
		{
			struct Instance *inst = visInstSrc[0];
			struct InstDef *instDef = inst->instDef;

			// if on adv hub
			if (boolAdvHub != 0)
			{
				th = inst->thread;
				if (th != 0)
				{
					th->flags |= THREAD_FLAG_DEAD;
				}

				// erase instance in pool
				LIST_AddFront(&sdata->gGT->JitPools.instance.free, (struct Item *)inst);
			}

			// go back to instDef
			visInstSrc[0] = (struct Instance *)instDef;
		}
	}

	PROC_CheckAllForDead();
}
