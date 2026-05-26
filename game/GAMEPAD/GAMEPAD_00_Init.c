#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800251ac-0x80025208.
void GAMEPAD_Init(struct GamepadSystem *gGamepads)
{
	int i;
	struct GamepadBuffer *pad;

	PadInitMtap(&gGamepads->slotBuffer[0], &gGamepads->slotBuffer[1]);
	PadStartCom();

	for (i = 0; i < 8; i++)
	{
		pad = &gGamepads->gamepad[i];

		// no analog sticks detected
		pad->gamepadType = 0;
		pad->unk44 = 0;
	}

	gGamepads->gamepadsConnectedByFlag = 0xffffffff;

	return;
}
