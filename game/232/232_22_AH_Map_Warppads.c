#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1a18-0x800b1c90.
void AH_Map_Warppads(s16 *ptrMap, struct Thread *warppadThread, s16 *param_3)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Instance *warppadInst;
	struct Instance *closestWarppadInst;
	int distX;
	int distY;
	int distZ;
	int color;
	int currDistance;
	int minDistance;
	int posX;
	int posY;

	// find minDistance, set to max
	minDistance = 0x7fffffff;
	closestWarppadInst = NULL;

	MATRIX *dMat = &gGT->drivers[0]->instSelf->matrix;

	for (
	    /**/; warppadThread != NULL; warppadThread = warppadThread->siblingThread)
	{
		int index = warppadThread->modelIndex;
		int isTrophy = 0;
		int skipDistance = 0;

		warppadInst = warppadThread->inst;

		switch ((u32)index)
		{
		case 0:
			color = 0x17;
			skipDistance = 1;
			break;
		case 1:
			color = 5;
			if ((gGT->timer & 2) != 0)
				color = 4;
			isTrophy = 1;
			break;
		case 2:
			color = 3;
			break;
		case 3:
			color = 0xe;
			break;
		case 4:
			// Each Slide Coliseum/Turbo Track color lasts two frames.
			color = ((gGT->timer >> 1) & 7) + 5;
			break;
		default:
			color = 0x15;
			skipDistance = 1;
			break;
		}

		if (isTrophy)
		{
			// get posZ in 3D, turns into posY in 2D
			posX = warppadInst->matrix.t[0];
			posY = warppadInst->matrix.t[2];

			D232.unkModeHubItems = 1;

			// Get Icon Dimensions
			UI_Map_GetIconPos(ptrMap, &posX, &posY);

			AH_Map_HubArrowOutter(ptrMap, (int)*param_3, posX, posY, 0, 0);

			*param_3 = *param_3 + 1;
		}

		UI_Map_DrawRawIcon((int)ptrMap, (int *)&warppadInst->matrix.t[0], 0x31, color, 0, 0x1000);

		if (skipDistance)
		{
			// skip distance check
			continue;
		}

		distX = warppadInst->matrix.t[0] - dMat->t[0];
		distY = warppadInst->matrix.t[1] - dMat->t[1];
		distZ = warppadInst->matrix.t[2] - dMat->t[2];

		currDistance = SquareRoot0_stub(distX * distX + distY * distY + distZ * distZ);

		if (minDistance > currDistance)
		{
			minDistance = currDistance;
			closestWarppadInst = warppadInst;
		}
	}

	// play sound from closest unlocked warppad
	if (closestWarppadInst != NULL)
		PlayWarppadSound(minDistance << 1);

	return;
}
