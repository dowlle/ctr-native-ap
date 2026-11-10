#include <common.h>
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

// -1 names the race on screen. Other values name a physical Adventure pad,
// never the active race sharing its retail host. Retail selection/record menus
// intentionally keep using their own retail metadata.
const char *UI_TrackName(int physicalPad)
{
	struct GameTracker *gGT = sdata->gGT;
	const char *title = NULL;
	int level = physicalPad;
	if (physicalPad < 0)
	{
		level = gGT->levelID;
#ifdef CTR_AP
#ifdef CTR_CUSTOM_TRACKS
		title = AP_ContentRaceDisplayName();
#endif
		if (!title) title = AP_CortexTrackDisplayName();
#endif
#ifdef CTR_CUSTOM_PACKAGES
		if (!title) title = MainRaceTrack_OfflineCustomTitle();
#endif
#ifdef CTR_CUSTOM_TRACKS
		if (!title && (gGT->gameMode1 & ADVENTURE_CUP))
			title = CustomTrack_CupDisplayName(gGT->cup.cupID, 1);
#endif
	}
#ifdef CTR_AP
	else if (ap_content_plan_active())
	{
		const ctr_content_pad *pad = ap_content_plan_pad(physicalPad);
		return pad && pad->occupied ? (pad->room_title[0] ? pad->room_title : pad->display_name)
		                            : "No entry selected";
	}
#endif
	if (title) return title;
	if (level >= 100 && level < 105)
	{
#ifdef CTR_CUSTOM_TRACKS
		title = CustomTrack_CupDisplayName(level - 100, 1);
		if (title) return title;
#endif
		return sdata->lngStrings[data.AdvCups[level - 100].lngIndex_CupName];
	}
	return level >= 0 && level < 65 ? sdata->lngStrings[data.metaDataLEV[level].name_LNG] : "?";
}
