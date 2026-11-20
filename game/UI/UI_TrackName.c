#include <common.h>
#include <platform/native_custom_identity.h>
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_assets.h>
#include <platform/native_custom_package.h>
#include <platform/native_custom_offline.h>
#include <platform/native_track_credit.h>
#include <text_box.h>
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

#ifdef CTR_CUSTOM_PACKAGES
#ifdef CTR_AP
static const ctr_content_package *UI_TrackCreditPackage(int physicalPad, int *cortex)
{
	struct GameTracker *gGT = sdata->gGT;
	const ctr_content_pad *pad;
	if (physicalPad >= 0)
	{
		if (!ap_content_plan_active())
		{
			*cortex = ctr_cfg_active() && ctr_cfg.cortex_track.option && physicalPad == AP_CORTEX_DEST;
			return NULL;
		}
		pad = ap_content_plan_pad(physicalPad);
		if (!pad || !pad->occupied || pad->cup || pad->retail_id >= 0) return NULL;
		*cortex = pad->cortex;
		return ap_content_plan_package(pad->package_index);
	}

	const struct CustomTrackRaceContext *race = CustomTrack_StandaloneContext(gGT->levelID);
	if (race)
	{
		if (race->retail || !ap_content_plan_active() || race->seedEpoch != ap_content_plan_epoch())
			return NULL;
		pad = ap_content_plan_pad(race->physicalPad);
		if (!pad || !pad->occupied || strcmp(pad->entry_id, race->entryID)) return NULL;
		*cortex = race->cortex;
		if (race->cup)
		{
			if (!pad->cup || race->leg < 0 || race->leg >= AP_CONTENT_CUP_LEGS) return NULL;
			const ctr_content_leg *leg = &pad->legs[race->leg];
			return !strcmp(leg->track_id, race->trackID) ? ap_content_plan_package(leg->package_index) : NULL;
		}
		return !pad->cup && !strcmp(pad->track_id, race->trackID) ? ap_content_plan_package(pad->package_index) : NULL;
	}

	const int garage = CustomTrack_GarageVenueServing(gGT->levelID, gGT->bossID, gGT->gameMode1 & ADVENTURE_BOSS);
	if (garage >= 0 && ap_content_plan_active())
	{
		const ctr_content_bosses *bosses = ap_content_plan_bosses();
		*cortex = bosses->venue_cortex[garage];
		return ap_content_plan_package(bosses->venue_package[garage]);
	}
	*cortex = AP_CortexTrackActive() ||
		CustomTrack_OxideFinalServing(gGT->levelID, gGT->bossID, gGT->gameMode1 & ADVENTURE_BOSS);
	return NULL;
}
#endif

const char *UI_TrackCredit(int physicalPad)
{
	static char credit[CTR_TRACK_CREDIT_MAX];
#ifdef CTR_AP
	int cortex = 0;
	const ctr_content_package *pkg = UI_TrackCreditPackage(physicalPad, &cortex);
	if (cortex) return CTR_CORTEX_CREDIT;
	if (pkg)
	{
		CustomTrackCredit_EnsureRegistry(NativeAssets_GetAssetDir());
		CustomTrackCredit_Format(credit, sizeof credit, pkg->lev_sha256, pkg->vrm_sha256,
		                         0, pkg->author, CustomTrackCredit_PatchSource(pkg->locator_name, pkg->locator_sha256));
		return credit;
	}
	/* Older schema rooms can redirect a gem cup to a compiled package. */
	if (!ap_content_plan_active() && ctr_cfg_active() && ctr_cfg.custom_tracks_ok)
	{
		const struct GameTracker *gt = sdata->gGT;
		const int legacyCup = physicalPad >= 100 && physicalPad < 105
			? CustomTrack_CupDisplayName(physicalPad - 100, 1) != NULL
			: physicalPad < 0 && CustomTrack_ServingLoad(gt->levelID, gt->gameMode1 & ADVENTURE_CUP, gt->cup.cupID);
		const struct CustomTrackManagerPackage *legacy = legacyCup ? AP_CustomContentPackage() : NULL;
		if (legacy)
		{
			CustomTrackCredit_EnsureRegistry(NativeAssets_GetAssetDir());
			CustomTrackCredit_Format(credit, sizeof credit, legacy->levSha256, legacy->vrmSha256,
			                         0, legacy->author, "Project Saphi");
			return credit;
		}
	}
#endif
	if (physicalPad < 0 && MainRaceTrack_OfflineCustomLoad())
	{
		static uint64_t generation;
		static struct CustomPackageLocalInfo info;
		if (generation != CustomOffline_RuntimeGeneration())
		{
			CustomPackage_TrackCreditInfo(CustomOffline_RuntimePackage(), &info);
			generation = CustomOffline_RuntimeGeneration();
		}
		CustomTrackCredit_EnsureRegistry(NativeAssets_GetAssetDir());
		CustomPackage_TrackCredit(&info, NULL, credit, sizeof credit);
		return credit;
	}
	return NULL;
}

void UI_DrawTrackCredit(const char *credit, int x, int y, int width, int mode)
{
	char plain[CTR_TRACK_CREDIT_MAX];
	if (credit && CustomPackage_AsciiFold(credit, strlen(credit), plain, sizeof plain))
	{
		/* The byte font treats these glyphs as controls and controller icons. */
		for (char *c = plain; *c; ++c)
			if ((unsigned char)*c < 32 || strchr("~@[^*", *c)) *c = ' ';
		const int slot = mode == TEXT_BANNER_MARQUEE ? TEXT_SLOT_RACE_CREDIT : TEXT_SLOT_TRACK_CREDIT;
		DecalFont_DrawLineBox(plain, x, y, width, FONT_SMALL, WHITE | JUSTIFY_CENTER, mode, slot, NULL);
	}
}
#endif
