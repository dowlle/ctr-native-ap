#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80028de0-0x80028e5c
// resume all songs
void DECOMP_CseqMusic_Resume()
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
		return;
	if (sdata->ptrCseqHeader == 0)
		return;

	DECOMP_Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if ((song->flags & 1) != 0)
		{
			// unpause song
			song->flags &= ~(2);
		}
	}

	DECOMP_Smart_ExitCriticalSection();
}
