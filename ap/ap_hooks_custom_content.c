// ap_hooks_custom_content.c -- part of the ap_hooks unit (issue #305).
// Custom content (CTR_CUSTOM_TRACKS): Cortex Vortex preflight, the selected
// custom-track package, rescan / verify / download and the missing-content warning.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

#ifdef CTR_CUSTOM_TRACKS
// Arms the bundled Cortex Vortex pair for whichever of its two roles this seed
// uses: the Oxide 2 venue (schema 11) and the pad track (schema 15). The two
// refusals stay separate: a refused venue closes the Oxide garage exactly as
// before, and a refused pad track closes only the pads and cups leading to 110.
static void AP_OxideFinalContentPreflight(void)
{
	struct OxideFinalTrackDescriptor d;
	int venueOk, venueCortex, padOn, padOk;

	ap_oxide_final_content_required = 0;
	ap_cortex_track_content_required = 0;
	if (!ctr_cfg_active() || ctr_cfg.schema_version < 11)
	{
		CustomTrack_ClearOxideFinalDescriptor();
		return;
	}
	venueOk = ctr_cfg.oxide_final_venue.valid;
	venueCortex = venueOk && ctr_cfg.oxide_final_venue.track == CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX;
	padOn = ctr_cfg.cortex_track.option;
	padOk = padOn && ctr_cfg.cortex_track.valid;
	if (!venueOk)
		ap_oxide_final_content_required = 1;
	if (padOn && !padOk)
		ap_cortex_track_content_required = 1;
	if (!venueCortex && !padOk)
	{
		CustomTrack_ClearOxideFinalDescriptor();
		return;
	}
	memset(&d, 0, sizeof d);
	d.enabled = venueCortex;
	d.padTrackEnabled = padOk;
	d.hostLevelID = CTR_CFG_CORTEX_HOST_LEVEL;
	snprintf(d.levSha256, sizeof d.levSha256, "%s", CTR_CFG_CORTEX_LEV_SHA256);
	snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", CTR_CFG_CORTEX_VRM_SHA256);
	if (!CustomTrack_ApplyOxideFinalDescriptor(&d, NativeAssets_GetAssetDir()))
	{
		if (venueCortex)
			ap_oxide_final_content_required = 1;
		if (padOk)
			ap_cortex_track_content_required = 1;
	}
}

static void AP_CustomContentBuildRequirement(struct CustomTrackManagerRequirement *requirement)
{
	const ctr_custom_track *track = &ctr_cfg.custom_track;
	memset(requirement, 0, sizeof *requirement);
	requirement->id = track->id;
	requirement->packageUuid = track->package_uuid;
	requirement->packageVersion = track->package_version;
	requirement->minimumClientVersion = track->minimum_client_version;
	requirement->minimumApworldVersion = track->minimum_apworld_version;
	requirement->levSha256 = track->lev_sha256;
	requirement->vrmSha256 = track->vrm_sha256;
	requirement->navigationUuid = track->navigation_uuid;
	requirement->navigationRevision = track->navigation_revision;
	requirement->laps = track->laps;
	requirement->boxes = track->boxes;
	requirement->flagCrates = track->flags.crates;
	requirement->flagCtrLetters = track->flags.ctr_letters;
	requirement->flagRelicCrates = track->flags.relic_crates;
	requirement->flagAiNav = track->flags.ai_nav;
	requirement->flagMinimap = track->flags.minimap;
	requirement->flagGhosts = track->flags.ghosts;
	requirement->flagWumpaCollectible = track->flags.wumpa_collectible;
	requirement->flagSpawns = track->flags.spawns;
	requirement->flagCheckpoints = track->flags.checkpoints;
}

static const struct CustomTrackManagerPackage *AP_CustomContentSelectedPackage(void)
{
	struct CustomTrackManagerRequirement requirement;
	// No seed asks for a profile: offer the current Saphi release. A connected
	// seed still selects its exact profile below, so 1.0.0 seeds keep using
	// installed 1.0.0 files.
	if (!ctr_cfg_active() || !ctr_cfg.custom_tracks_seen)
		return CustomTrackManager_BabyTParkCurrent();
	if (!ctr_cfg.custom_tracks_ok) return NULL;
	AP_CustomContentBuildRequirement(&requirement);
	return CustomTrackManager_MatchingPackage(&requirement);
}

const struct CustomTrackManagerPackage *AP_CustomContentPackage(void)
{
	const struct CustomTrackManagerPackage *package = AP_CustomContentSelectedPackage();
	// An unusable seed descriptor has no profile. Its status already says
	// Incompatible, and export is refused because that status is not Ready.
	return package != NULL ? package : CustomTrackManager_BabyTParkCurrent();
}

static void AP_CustomContentBuildDescriptor(struct CustomTrackSeedDescriptor *descriptor)
{
	const ctr_custom_track *track = &ctr_cfg.custom_track;
	memset(descriptor, 0, sizeof *descriptor);
	descriptor->laps = track->laps;
	descriptor->hostLevelID = track->host_level_id;
	descriptor->replacesCupLevelID = track->replaces_cup_level_id;
	descriptor->boxes = track->boxes;
	snprintf(descriptor->levSha256, sizeof descriptor->levSha256, "%s", track->lev_sha256);
	snprintf(descriptor->vrmSha256, sizeof descriptor->vrmSha256, "%s", track->vrm_sha256);
	descriptor->flagCrates = track->flags.crates;
	descriptor->flagCtrLetters = track->flags.ctr_letters;
	descriptor->flagRelicCrates = track->flags.relic_crates;
	descriptor->flagAiNav = track->flags.ai_nav;
	descriptor->flagMinimap = track->flags.minimap;
	descriptor->flagGhosts = track->flags.ghosts;
	descriptor->flagWumpaCollectible = track->flags.wumpa_collectible;
	descriptor->flagSpawns = track->flags.spawns;
	descriptor->flagCheckpoints = track->flags.checkpoints;
}

static int AP_CustomContentActivateReady(void)
{
	struct CustomTrackSeedDescriptor descriptor;
	const struct CustomTrackManagerPackage *package = AP_CustomContentSelectedPackage();
	if (!ap_custom_content_seed_selected ||
	    ap_custom_content_status.state != CTR_CT_MANAGER_READY ||
	    package == NULL || !CustomTrack_UseManagedPackage(package, &ap_custom_content_status))
		return 0;
	AP_CustomContentBuildDescriptor(&descriptor);
	return CustomTrack_ApplySeedDescriptor(&descriptor);
}

static void AP_CustomContentLogStatus(const char *action)
{
	char line[768];
	const struct CustomTrackManagerStatus *status = &ap_custom_content_status;
	snprintf(line, sizeof line,
	         "[CustomTracks] manager %.32s: %.32s; %.240s; folder=\"%.360s\"\n",
	         action, CustomTrackManager_StateText(status->state),
	         status->detail[0] != '\0' ? status->detail : "no detail",
	         status->packageRoot[0] != '\0' ? status->packageRoot : "unresolved");
	AP_AppendLog(line);
}

static void AP_CustomContentPreflightSeed(int autoFinalize)
{
	struct CustomTrackManagerRequirement requirement;
	ap_custom_content_seed_selected = ctr_cfg_active() && ctr_cfg.custom_tracks_ok;
	ap_custom_content_gate_cached = 0;

	if (!ap_custom_content_seed_selected)
	{
		CustomTrack_ClearSeedDescriptor();
		ap_custom_content_required = ctr_cfg_active() && ctr_cfg.custom_tracks_seen;
		if (ap_custom_content_required)
		{
			memset(&ap_custom_content_status, 0, sizeof ap_custom_content_status);
			ap_custom_content_status.state = CTR_CT_MANAGER_INCOMPATIBLE;
			snprintf(ap_custom_content_status.detail, sizeof ap_custom_content_status.detail,
			         "This seed's custom-track descriptor is not usable by this client.");
			ap_custom_content_scanned = 1;
		}
		else
		{
			CustomTrackManager_ScanPackage(NativeAssets_GetAssetDir(),
			                               CustomTrackManager_BabyTParkCurrent(),
			                               &ap_custom_content_status);
			ap_custom_content_scanned = 1;
		}
		return;
	}

	AP_CustomContentBuildRequirement(&requirement);
	CustomTrackManager_Preflight(NativeAssets_GetAssetDir(), &requirement,
	                             autoFinalize, &ap_custom_content_status);
	ap_custom_content_scanned = 1;
	ap_custom_content_required = ap_custom_content_status.state != CTR_CT_MANAGER_READY ||
	                             !AP_CustomContentActivateReady();
	if (ap_custom_content_required)
		CustomTrack_ClearSeedDescriptor();
}

const struct CustomTrackManagerStatus *AP_CustomContentStatus(void)
{
	if (!ap_custom_content_scanned)
	{
		CustomTrackManager_ScanPackage(NativeAssets_GetAssetDir(),
		                               CustomTrackManager_BabyTParkCurrent(),
		                               &ap_custom_content_status);
		ap_custom_content_scanned = 1;
	}
	return &ap_custom_content_status;
}

int AP_CustomContentSeedSelected(void)
{
	return ap_custom_content_seed_selected;
}

int AP_CustomContentRequired(void)
{
	return ap_custom_content_required;
}

#ifdef CTR_CUSTOM_PACKAGES
int AP_CustomOfflineLaunchAllowed(void)
{
	return !ctr_cfg_active();
}
#endif

void AP_CustomContentRescan(void)
{
	if (ctr_cfg_active() && ctr_cfg.custom_tracks_seen)
	{
		AP_CustomContentPreflightSeed(1);
		AP_CustomContentLogStatus("Rescan");
		return;
	}
	CustomTrackManager_ScanPackage(NativeAssets_GetAssetDir(),
	                               CustomTrackManager_BabyTParkCurrent(),
	                               &ap_custom_content_status);
	if (ap_custom_content_status.state == CTR_CT_MANAGER_MANIFEST_MISSING ||
	    ap_custom_content_status.state == CTR_CT_MANAGER_MANIFEST_INVALID)
		CustomTrackManager_FinalizePackage(NativeAssets_GetAssetDir(),
		                                   CustomTrackManager_BabyTParkCurrent(),
		                                   &ap_custom_content_status);
	ap_custom_content_scanned = 1;
	AP_CustomContentLogStatus("Rescan");
}

void AP_CustomContentVerify(void)
{
	const struct CustomTrackManagerPackage *package = AP_CustomContentSelectedPackage();
	if (package == NULL) { AP_CustomContentRescan(); return; }
	CustomTrackManager_PrepareFolder(NativeAssets_GetAssetDir(),
	                                 package,
	                                 &ap_custom_content_status);
	ap_custom_content_scanned = 1;
	AP_CustomContentRescan();
}

int AP_CustomContentDownloadStart(void)
{
	const struct CustomTrackManagerPackage *package = AP_CustomContentSelectedPackage();
	if (package == NULL) return 0;
	if (!CustomTrackManager_PrepareFolder(NativeAssets_GetAssetDir(), package,
	                                    &ap_custom_content_status)) return 0;
	ap_custom_content_scanned = 1;
	return ap_custom_track_download_start(ap_custom_content_status.packageRoot,
	                                      package->downloadApiUrl, package->version,
	                                      package->levSha256, package->vrmSha256);
}

int AP_CustomContentDownloadStatus(char *detail, int detailBytes)
{
	return ap_custom_track_download_status(detail, detailBytes);
}

int AP_CustomContentGateEventEntry(int forceVerify)
{
	unsigned int now = (unsigned int)sdata->gGT->timer;
	if (!ap_custom_content_seed_selected)
		return !ap_custom_content_required;
	if (!forceVerify && ap_custom_content_gate_cached && now - ap_custom_content_gate_timer < 120)
		return 1;

	AP_CustomContentPreflightSeed(0);
	if (ap_custom_content_required || !CustomTrack_ReverifyArmedContent())
	{
		ap_custom_content_required = 1;
		ap_custom_content_gate_cached = 0;
		return 0;
	}
	ap_custom_content_gate_timer = now;
	ap_custom_content_gate_cached = 1;
	return 1;
}

void AP_DrawCustomContentWarning(void)
{
	if (ap_cortex_track_content_required)
	{
		DecalFont_DrawLine("!! CORTEX VORTEX REQUIRED !!", AP_FEED_X, 0x30, FONT_SMALL, RED);
		DecalFont_DrawLine("Cortex Vortex pad track unavailable. Its pad refuses entry.",
		                   AP_FEED_X, 0x30 + AP_FEED_LINE_H, FONT_SMALL, RED);
		return;
	}
	if (ap_oxide_final_content_required)
	{
		DecalFont_DrawLine("!! CORTEX VORTEX REQUIRED !!", AP_FEED_X, 0x30, FONT_SMALL, RED);
		DecalFont_DrawLine("Exact bundled files missing or mismatched. Oxide 2 is locked.",
		                   AP_FEED_X, 0x30 + AP_FEED_LINE_H, FONT_SMALL, RED);
		return;
	}
	if (!ap_custom_content_required)
		return;
	DecalFont_DrawLine("!! CUSTOM CONTENT REQUIRED !!", AP_FEED_X, 0x30, FONT_SMALL, RED);
	DecalFont_DrawLine("Open OPTIONS > Custom Content to download or verify the track.",
	                   AP_FEED_X, 0x30 + AP_FEED_LINE_H, FONT_SMALL, RED);
}
#endif

#endif // CTR_AP
