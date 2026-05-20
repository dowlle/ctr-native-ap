#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003f434-0x8003f48c
void Particle_UpdateAllParticles(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if ((gGT->gameMode1 & DEBUG_MENU) != 0)
		return;

	Particle_UpdateList((struct Particle **)&gGT->particleList_ordinary, gGT->particleList_ordinary);
	Particle_UpdateList((struct Particle **)&gGT->particleList_heatWarp, gGT->particleList_heatWarp);
}
