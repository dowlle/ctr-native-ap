#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800263fc-0x80026440.
void GAMEPAD_JogCon2(struct Driver *d, char val, s16 timeMS)
{
	// if AI
	if ((d->actionsFlagSet & 0x100000) != 0)
		return;

	struct GamepadBuffer *gb = &sdata->gGamepads->gamepad[d->driverID];

	gb->unk42 = val;
	gb->unk48 = timeMS;
}
