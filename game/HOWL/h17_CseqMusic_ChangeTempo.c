#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029008-0x800290cc
void DECOMP_CseqMusic_ChangeTempo(u16 songID, int p2)
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
		return;
	if (sdata->ptrCseqHeader == 0)
		return;
	if (sdata->ptrCseqHeader->numSongs <= songID)
		return;

	DECOMP_Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if (((song->flags & 1) != 0) && (song->id == songID))
		{
			DECOMP_SongPool_ChangeTempo(song, p2);
		}
	}

	DECOMP_Smart_ExitCriticalSection();
}
