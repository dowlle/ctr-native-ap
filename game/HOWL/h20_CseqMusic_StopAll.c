#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029258-0x800292e0
void DECOMP_CseqMusic_StopAll()
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
			DECOMP_SongPool_StopAllCseq(song);
		}
	}

	DECOMP_Smart_ExitCriticalSection();
}
