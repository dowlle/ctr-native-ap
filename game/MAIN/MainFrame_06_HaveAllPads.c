#include <common.h>

int DECOMP_MainFrame_HaveAllPads(short numPlyrNextGame)
{
	// if game is not loading
	if (sdata->Loading.stage == -1)
	{
		struct GamepadBuffer *gb = &sdata->gGamepads->gamepad[0];

		for (int i = 0; i < numPlyrNextGame; i++)
		{
			struct ControllerPacket *packet = gb->ptrControllerPacket;

			if (packet == NULL)
				return 0;
			if (packet->plugged != PLUGGED)
				return 0;

			gb++;
		}
	}

	return 1;
}
