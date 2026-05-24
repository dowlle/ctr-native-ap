#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001cd20-0x8001cdb4
int DECOMP_CDSYS_XAGetTrackLength(int categoryID, int xaID)
{
	if (sdata->boolUseDisc == 0)
		return 0;

	if (sdata->bool_XnfLoaded == 0)
		return 0;

	if (categoryID >= CDSYS_XA_NUM_TYPES)
		return 0;

	if (xaID >= DECOMP_CDSYS_XAGetNumTracks(categoryID))
		return 0;

	int sizeIndex = sdata->ptrArray_firstSongIndex[categoryID] + xaID;

	return sdata->ptrArray_XaSize[sizeIndex].XaBytes;
}

int CDSYS_XAGetTrackLength(int categoryID, int xaID)
{
	return DECOMP_CDSYS_XAGetTrackLength(categoryID, xaID);
}
