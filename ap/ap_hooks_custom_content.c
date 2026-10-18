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
#include "ap_content_capability.h"
#include <platform/native_custom_content_verify.h>

// Arms the bundled Cortex Vortex pair for whichever of its two roles this seed
// uses: the Oxide 2 venue (schema 11) and the pad track (schema 15). The two
// refusals stay separate: a refused venue closes the Oxide garage exactly as
// before, and a refused pad track closes only the pads and cups leading to 110.
static void AP_OxideFinalContentPreflight(void)
{
	struct OxideFinalTrackDescriptor d;
	int venueOk, venueCortex, padOn, padOk;

	ap_oxide_final_content_required = 0;
	if (ap_content_plan_active())
	{
		// A content plan arms neither legacy role. Its Cortex Vortex entry
		// is scanned by AP_CustomContentPreflightSeed, which owns
		// ap_cortex_track_content_required for the plan.
		CustomTrack_ClearOxideFinalDescriptor();
		return;
	}
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

static int ap_content_package_index;
static int ap_content_package_ready[AP_CONTENT_MAX_PACKAGES];

// Open admission (#133 PR 2): a package outside the compiled registry is found
// by digest in assets/tracks/<track key>/original. Preflight records where its
// verified files are and what they measured; the loader hashes them again
// when it arms (CustomTrack_ApplySeedDescriptor) and retains the exact bytes.
struct AP_ContentFiles
{
	char levPath[CTR_CT_MANAGER_PATH_MAX];
	char vrmPath[CTR_CT_MANAGER_PATH_MAX];
	struct CustomContentMeasurements measured;
};
static struct AP_ContentFiles ap_content_files[AP_CONTENT_MAX_PACKAGES];

static const struct CustomTrackManagerPackage *AP_ContentPackage(int index)
{
	const ctr_content_package *p = ap_content_plan_package(index);
	return p ? CustomTrackManager_FindPackage(p->content_id, p->uuid, p->revision,
	                                         p->lev_sha256, p->vrm_sha256) : NULL;
}

// A race-start name the big font can draw: printable ASCII, cut until the
// width check passes, "CUSTOM TRACK" when nothing is left.
static void AP_ContentRaceName(const char *title, char *out, size_t size)
{
	size_t n = 0;
	const unsigned char *p = (const unsigned char *)title;
	if (!out || size == 0) return;
	for (; p && *p && n + 1 < size && n + 1 < CTR_CT_NAME_MAX; p++)
	{
		if ((*p & 0xC0) == 0x80) continue;
		out[n++] = (*p >= 0x20 && *p < 0x7F) ? (char)*p : '?';
	}
	out[n] = '\0';
	while (n > 0 && !CustomTrackPolicy_NameFits(out, NULL)) out[--n] = '\0';
	while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
	if (n == 0) snprintf(out, size, "CUSTOM TRACK");
}

// Locate one package's files (compiled profile folder, or the open track-key
// folder), measure the exact bytes and refuse a claimed capability they lack.
// Returns 1 Ready; otherwise status says why.
static int AP_ContentScanPackage(int index, int autoFinalize, struct CustomTrackManagerStatus *status)
{
	const ctr_content_package *pkg = ap_content_plan_package(index);
	const struct CustomTrackManagerPackage *compiled = AP_ContentPackage(index);
	struct CustomContentVerification report;
	char error[160], why[320], line[640];
	if (!pkg) return 0;
	memset(&ap_content_files[index], 0, sizeof ap_content_files[index]);
	if (compiled)
	{
		CustomTrackManager_ScanPackage(NativeAssets_GetAssetDir(), compiled, status);
		if (autoFinalize && (status->state == CTR_CT_MANAGER_MANIFEST_MISSING ||
		                     status->state == CTR_CT_MANAGER_MANIFEST_INVALID))
			CustomTrackManager_FinalizePackage(NativeAssets_GetAssetDir(), compiled, status);
	}
	else
		CustomTrackManager_LocateContentFiles(NativeAssets_GetAssetDir(), pkg->content_id, pkg->display_name,
		                                      pkg->revision, pkg->lev_sha256, pkg->lev_bytes,
		                                      pkg->vrm_sha256, pkg->vrm_bytes, status);
	if (status->state != CTR_CT_MANAGER_READY) return 0;
	error[0] = '\0';
	if (!CustomContentVerify_Files(status->levPath, status->vrmPath, &report, error, sizeof error) ||
	    strcmp(report.levSha256, pkg->lev_sha256) || strcmp(report.vrmSha256, pkg->vrm_sha256))
	{
		status->state = CTR_CT_MANAGER_UNSUPPORTED;
		snprintf(status->detail, sizeof status->detail, "%.48s %.24s failed the file check: %.120s",
		         pkg->display_name, pkg->revision, error[0] ? error : "the files changed while reading");
		snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, status->detail);
		AP_AppendLog(line);
		return 0;
	}
	if (!AP_ContentCapabilitiesHold(pkg->capabilities, &report, why, sizeof why) ||
	    !AP_ContentSuperTurboPadsHold(pkg->super_turbo_pads, &report, why, sizeof why))
	{
		status->state = CTR_CT_MANAGER_UNSUPPORTED;
		snprintf(status->detail, sizeof status->detail, "%.48s %.24s: %s", pkg->display_name, pkg->revision, why);
		snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, why);
		AP_AppendLog(line);
		return 0;
	}
	snprintf(ap_content_files[index].levPath, sizeof ap_content_files[index].levPath, "%s", status->levPath);
	snprintf(ap_content_files[index].vrmPath, sizeof ap_content_files[index].vrmPath, "%s", status->vrmPath);
	ap_content_files[index].measured = report.measured;
	return 1;
}

// Content-plan Cortex Vortex (#133 PR 3c): the bundled pair at the path the
// legacy pad track and the Oxide 2 venue load it from, hashed against the
// compiled pins and measured like a package. It must hold every capability,
// since the plan may race all three of its modes. Nothing in the legacy
// Cortex Vortex serving state is armed: the race is a custom-bytes standalone
// race on host 13 (AP_ContentPreparePad), so no LevelID-13 site can read it
// as Oxide Station or as the legacy pad track.
static int ap_content_cortex_ready;
static struct AP_ContentFiles ap_content_cortex_files;

static int AP_ContentScanCortex(void)
{
	struct CustomContentVerification report;
	char error[160], why[320], line[640];
	const char *dir = NativeAssets_GetAssetDir();
	struct AP_ContentFiles *f = &ap_content_cortex_files;
	memset(f, 0, sizeof *f);
	if (dir == NULL ||
	    snprintf(f->levPath, sizeof f->levPath, "%s/tracks/cortex-vortex/CVortex Arcade All.lev", dir) >= (int)sizeof f->levPath ||
	    snprintf(f->vrmPath, sizeof f->vrmPath, "%s/tracks/cortex-vortex/CVortex Arcade All.vrm", dir) >= (int)sizeof f->vrmPath)
	{
		memset(f, 0, sizeof *f);
		AP_AppendLog("[CustomTracks] content plan refused Cortex Vortex: the bundled asset path is invalid\n");
		return 0;
	}
	error[0] = '\0';
	if (!CustomContentVerify_Files(f->levPath, f->vrmPath, &report, error, sizeof error) ||
	    strcmp(report.levSha256, CTR_CFG_CORTEX_LEV_SHA256) || strcmp(report.vrmSha256, CTR_CFG_CORTEX_VRM_SHA256))
	{
		snprintf(line, sizeof line, "[CustomTracks] content plan refused Cortex Vortex: the bundled files are missing "
		         "or not the pinned pair (%.160s)\n", error[0] ? error : "hash mismatch");
		AP_AppendLog(line);
		memset(f, 0, sizeof *f);
		return 0;
	}
	if (!AP_ContentCapabilitiesHold(AP_CONTENT_CAP_ALL, &report, why, sizeof why))
	{
		snprintf(line, sizeof line, "[CustomTracks] content plan refused Cortex Vortex: %s\n", why);
		AP_AppendLog(line);
		memset(f, 0, sizeof *f);
		return 0;
	}
	f->measured = report.measured;
	return 1;
}

int AP_ContentPadReady(int physicalPad)
{
	const ctr_content_pad *pad = ap_content_plan_pad(physicalPad);
	if (pad && pad->occupied && pad->cortex)
		return ap_content_cortex_ready;
	return pad && pad->occupied && (pad->retail_id >= 0 ||
	       (pad->package_index >= 0 && pad->package_index < AP_CONTENT_MAX_PACKAGES &&
	        ap_content_package_ready[pad->package_index]));
}

int AP_ContentPreparePad(int physicalPad, int mode)
{
	const ctr_content_pad *pad = ap_content_plan_pad(physicalPad);
	struct CustomTrackRaceContext race;
	int64_t codes[3] = {0, 0, 0};
	if (!pad || !pad->occupied || CustomTrack_StandaloneBusy() || !AP_ContentPadReady(physicalPad) ||
	    !AP_ContentPadOpen(pad)) return -1;
	// A track entry races one of its modes, through the same gate the
	// selector used; a Crystal Challenge has no modes and races mode 0.
	if (pad->crystal ? mode != AP_CONTENT_MODE_TROPHY
	                 : (!AP_ContentModeOpen(pad, mode) || ap_content_mode_codes(pad, mode, codes) < 1)) return -1;
	memset(&race, 0, sizeof race);
	race.physicalPad = physicalPad; race.returnHub = pad->hub;
	race.laps = pad->laps; race.trophyLocation = pad->crystal ? pad->trophy_location : codes[0];
	race.mode = mode;
	race.crystal = pad->crystal; race.timeLimitTicks = pad->time_limit_ticks;
	race.seedEpoch = ap_content_plan_epoch(); race.retail = pad->retail_id >= 0;
	race.cortex = pad->cortex;
	race.hostLevelID = ap_content_pad_host(pad);
	race.slot = pad->custom_slot;
	snprintf(race.entryID, sizeof race.entryID, "%s", pad->entry_id);
	snprintf(race.trackID, sizeof race.trackID, "%s", pad->track_id);
	if (race.cortex)
	{
		const struct CustomContentMeasurements *m;
		struct CustomTrackSeedDescriptor d;
		// Hash and measure again: a file swapped since connect is refused here.
		if (!(ap_content_cortex_ready = AP_ContentScanCortex()))
		{
			ap_cortex_track_content_required = 1;
			ap_state_gen++;
			return -1;
		}
		m = &ap_content_cortex_files.measured;
		if (!CustomTrack_UseContentFiles(ap_content_cortex_files.levPath, ap_content_cortex_files.vrmPath, "CORTEX VORTEX"))
		{
			ap_content_cortex_ready = 0;
			ap_state_gen++;
			return -1;
		}
		memset(&d, 0, sizeof d);
		d.standalone = 1; d.laps = race.laps; d.hostLevelID = race.hostLevelID;
		d.replacesCupLevelID = -1; d.boxes = 0;
		d.flagCrates = m->ordinaryCrates > 0; d.flagCtrLetters = m->letterC && m->letterT && m->letterR;
		d.flagRelicCrates = m->relicCrates > 0; d.flagAiNav = m->navPaths > 0;
		d.flagWumpaCollectible = 1; // measured above (AP_CONTENT_CAP_WUMPA)
		d.flagSpawns = m->spawns > 8 ? 8 : (int)m->spawns;
		d.flagCheckpoints = m->checkpoints > 255 ? 255 : (int)m->checkpoints;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", CTR_CFG_CORTEX_LEV_SHA256);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", CTR_CFG_CORTEX_VRM_SHA256);
		snprintf(race.levSha256, sizeof race.levSha256, "%s", CTR_CFG_CORTEX_LEV_SHA256);
		snprintf(race.vrmSha256, sizeof race.vrmSha256, "%s", CTR_CFG_CORTEX_VRM_SHA256);
		if (!CustomTrack_ApplySeedDescriptor(&d)) return -1;
	}
	else if (!race.retail)
	{
		const ctr_content_package *pkg = ap_content_plan_package(pad->package_index);
		const struct CustomTrackManagerPackage *p = AP_ContentPackage(pad->package_index);
		const struct CustomContentMeasurements *m;
		struct CustomTrackSeedDescriptor d;
		char raceName[CTR_CT_NAME_MAX];
		int used;
		if (!pkg) return -1;
		ap_content_package_index = pad->package_index;
		// Locate and measure again: a file swapped since connect is refused here.
		if (!AP_ContentScanPackage(pad->package_index, 0, &ap_custom_content_status))
		{
			ap_content_package_ready[pad->package_index] = 0;
			ap_custom_content_required = 1;
			ap_state_gen++;
			return -1;
		}
		m = &ap_content_files[pad->package_index].measured;
		AP_ContentRaceName(pkg->display_name, raceName, sizeof raceName);
		used = p ? CustomTrack_UseManagedPackage(p, &ap_custom_content_status)
		         : CustomTrack_UseContentFiles(ap_content_files[pad->package_index].levPath,
		                                       ap_content_files[pad->package_index].vrmPath, raceName);
		if (!used)
		{
			ap_content_package_ready[pad->package_index] = 0;
			ap_state_gen++;
			return -1;
		}
		memset(&d, 0, sizeof d);
		d.standalone = 1; d.laps = race.laps; d.hostLevelID = race.hostLevelID;
		d.replacesCupLevelID = -1; d.boxes = 0;
		// The flags the loader acts on are the measured ones, from the bytes it
		// is about to hash, never the descriptor's claims.
		d.flagCrates = m->ordinaryCrates > 0; d.flagCtrLetters = m->letterC && m->letterT && m->letterR;
		d.flagRelicCrates = m->relicCrates > 0; d.flagAiNav = m->navPaths > 0;
		d.flagMinimap = p ? p->flagMinimap : 0; d.flagGhosts = p ? p->flagGhosts : 0;
		d.flagWumpaCollectible = (pkg->capabilities & AP_CONTENT_CAP_WUMPA) != 0;
		d.flagSpawns = m->spawns > 8 ? 8 : (int)m->spawns;
		d.flagCheckpoints = m->checkpoints > 255 ? 255 : (int)m->checkpoints;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", pkg->lev_sha256);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", pkg->vrm_sha256);
		snprintf(race.levSha256, sizeof race.levSha256, "%s", pkg->lev_sha256);
		snprintf(race.vrmSha256, sizeof race.vrmSha256, "%s", pkg->vrm_sha256);
		if (!CustomTrack_ApplySeedDescriptor(&d)) return -1;
	}
	if (!CustomTrack_StageStandaloneRace(&race)) return -1;
	CustomTrack_CortexTrackSelectNextLoad(0);
	return race.hostLevelID;
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
	if (ap_content_plan_active() && ap_content_plan_package(0))
		return AP_ContentPackage(ap_content_package_index);
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
	if (ap_content_plan_active())
	{
		int i, firstMissing = -1;
		struct CustomTrackManagerStatus status;
		ap_custom_content_seed_selected = ap_content_plan_package(0) != NULL;
		ap_custom_content_required = 0;
		ap_content_package_index = 0;
		for (i = 0; i < AP_CONTENT_MAX_PACKAGES; ++i)
		{
			ap_content_package_ready[i] = 0;
			if (!ap_content_plan_package(i)) continue;
			ap_content_package_ready[i] = AP_ContentScanPackage(i, autoFinalize, &status);
			if (i == 0) ap_custom_content_status = status;
			if (!ap_content_package_ready[i])
			{
				ap_custom_content_required = 1;
				if (firstMissing < 0) { firstMissing = i; ap_content_package_index = i; ap_custom_content_status = status; }
			}
		}
		// Cortex Vortex (#133 PR 3c): the bundled pair, when the plan races it.
		ap_content_cortex_ready = ap_content_plan_cortex() ? AP_ContentScanCortex() : 0;
		ap_cortex_track_content_required = ap_content_plan_cortex() && !ap_content_cortex_ready;
		ap_custom_content_scanned = 1;
		ap_state_gen++;
		// A reconnect cannot replace the buffers of a race already in flight.
		if (!CustomTrack_StandaloneBusy())
			CustomTrack_ClearSeedDescriptor();
		return;
	}
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
	if (ap_content_plan_active() || (ctr_cfg_active() && ctr_cfg.custom_tracks_seen))
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
