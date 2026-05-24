#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8001cdb4-0x8001cf98
int DECOMP_CDSYS_XAPlay(int categoryID, int xaID)
{
	char buf1[8];
	char buf2[8];

	if (sdata->boolUseDisc == 0)
		return 1;

	if (sdata->bool_XnfLoaded == 0)
		return 0;

	if (categoryID >= CDSYS_XA_NUM_TYPES)
		return 0;

	if (xaID >= DECOMP_CDSYS_XAGetNumTracks(categoryID))
		return 0;

	if (sdata->load_inProgress != 0)
	{
		DECOMP_OtherFX_Play(5, 1);
		return 0;
	}

	if (sdata->discMode != DM_AUDIO)
		DECOMP_CDSYS_SetMode_StreamAudio();

	sdata->XA_State = 2;

	int vol = sdata->vol_Voice;
	if (categoryID == CDSYS_XA_TYPE_MUSIC)
		vol = sdata->vol_Music;

	sdata->XA_VolumeBitshift = vol << 7;
	SpuSetCommonCDVolume((s16)sdata->XA_VolumeBitshift, (s16)sdata->XA_VolumeBitshift);

	sdata->XA_Playing_Index = xaID;
	sdata->XA_Playing_Category = categoryID;

	struct XaSize *xas = &sdata->ptrArray_XaSize[sdata->ptrArray_firstSongIndex[categoryID] + xaID];
	int sum = sdata->ptrArray_XaCdPos[sdata->ptrArray_firstXaIndex[categoryID] + xas->XaPrefix];

	buf1[0] = 1;
	buf1[1] = xas->XaIndex;
	CdControl(CdlSetfilter, &buf1[0], 0);

	CdIntToPos(sum, (CdlLOC *)&buf2[0]);

	sdata->XA_StartPos = sum;
	sdata->XA_EndPos = sum + xas->XaBytes;
	sdata->XA_MaxSampleVal = 0;
	sdata->XA_MaxSampleValInArr = 0;
	sdata->unused_8008d700 = 0;

	sdata->countPass_CdReadyCallback = 0;
	sdata->countFail_CdReadyCallback = 0;
	sdata->XA_CurrOffset = 0; // ND bug? Variable resuse?
	sdata->countPass_CdTransferCallback = 0;

	if (CdControl(CdlReadS, &buf2[0], 0) == 1)
	{
		// As of now, XA plays indefinitely, until CdReadyCallback
		// determines the current CD position is past the end position,
		// and then when IRQ determines the SPU is done playing the last
		// of the XA, CDSYS_XAPauseForce is called to stop playing XA.

		// Emulators with no IRQ support will keep playing random
		// XA audio on the disc infinitely, and never reach ND Box

		DECOMP_CDSYS_SpuEnableIRQ();
		return 1;
	}

	sdata->XA_State = 0;
	return 0;
}

int CDSYS_XAPlay(int categoryID, int xaID)
{
	return DECOMP_CDSYS_XAPlay(categoryID, xaID);
}
