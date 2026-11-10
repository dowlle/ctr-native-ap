#include <common.h>
#include <platform/native_custom_identity.h>
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_package.h>
#endif
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
	static char plain[641];
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
		if (!pad || !pad->occupied) return "No entry selected";
		title = pad->room_title[0] ? pad->room_title : pad->display_name;
	}
#endif
	if (title)
	{
#ifdef CTR_CUSTOM_PACKAGES
		// The game font consumes bytes, while room/package titles are UTF-8.
		// Fold before uppercasing so accents do not become garbage or '?'.
		if (CustomPackage_AsciiFold(title, strlen(title), plain, sizeof plain))
		{
			size_t n = strlen(plain);
			const char *first = plain;
			while (n && plain[n - 1] == ' ') plain[--n] = 0;
			while (*first == ' ') ++first;
			CustomIdentity_BannerName(plain, sizeof plain, first);
			return plain;
		}
#endif
		CustomIdentity_BannerName(plain, sizeof plain, title);
		return plain;
	}
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
