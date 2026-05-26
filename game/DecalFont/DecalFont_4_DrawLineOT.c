#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800228c4-0x80022930.
void DecalFont_DrawLineOT(char *str, int posX, int posY, s16 fontType, int flags, u_long *ot)
{
	struct GameTracker *gGT;
	u_long *backupOT;

	gGT = sdata->gGT;

	// backup
	backupOT = gGT->pushBuffer_UI.ptrOT;

	// alter
	gGT->pushBuffer_UI.ptrOT = ot;

	// draw
	DecalFont_DrawLine(str, (s16)posX, (s16)posY, fontType, (s16)flags);

	// reset
	gGT->pushBuffer_UI.ptrOT = backupOT;
}
