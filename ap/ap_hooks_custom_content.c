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
#include "ap_seed_content.h" // AP_SeedContentInstallBusy (#133 PR 7)
#include <platform/native_custom_content_verify.h>
#ifdef CTR_CUSTOM_PACKAGES
#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#endif

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
		// A content plan never arms the legacy pad track: its Cortex Vortex
		// entry is scanned by AP_CustomContentPreflightSeed, which owns
		// ap_cortex_track_content_required for the plan. With boss garages
		// (#133 PR 6) a Final Challenge on Cortex Vortex arms the Oxide 2
		// venue role, exactly as a normal room's oxide_final_venue does.
		const ctr_content_bosses *b = ap_content_plan_bosses();
		if (!b->present || b->oxide_goal == 3 || b->final_track != CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX)
		{
			CustomTrack_ClearOxideFinalDescriptor();
			return;
		}
		memset(&d, 0, sizeof d);
		d.enabled = 1;
		d.hostLevelID = CTR_CFG_CORTEX_HOST_LEVEL;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", CTR_CFG_CORTEX_LEV_SHA256);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", CTR_CFG_CORTEX_VRM_SHA256);
		if (!CustomTrack_ApplyOxideFinalDescriptor(&d, NativeAssets_GetAssetDir()))
			ap_oxide_final_content_required = 1;
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

// Resolve an exact verified pair from tracks/packages, falling back to older
// room folders. Preflight records the paths and measurements; the loader
// hashes the pair again when it arms the race.
struct AP_ContentFiles
{
	char levPath[CTR_CT_MANAGER_PATH_MAX];
	char vrmPath[CTR_CT_MANAGER_PATH_MAX];
	char packageUuid[37];
	struct CustomContentMeasurements measured;
	// Merged mode builds (`relic_files`): the verified relic pair, served only
	// for a Relic Race. Empty when the package has none.
	char relicLevPath[CTR_CT_MANAGER_PATH_MAX];
	char relicVrmPath[CTR_CT_MANAGER_PATH_MAX];
	struct CustomContentMeasurements relicMeasured;
};
static struct AP_ContentFiles ap_content_files[AP_CONTENT_MAX_PACKAGES];

static const struct CustomTrackManagerPackage *AP_ContentPackage(int index)
{
	const ctr_content_package *p = ap_content_plan_package(index);
	return p ? CustomTrackManager_FindPackage(p->content_id, p->uuid, p->revision,
	                                         p->lev_sha256, p->vrm_sha256) : NULL;
}

// Legacy loader name: printable ASCII, cut until the width check passes.
// The fly-in resolves the full display title from the active race context.
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

// Current Seed (#133 PR 7): what the last scan of each package found, as
// manager state + 1 (0 = not scanned).
static int ap_content_package_scan[AP_CONTENT_MAX_PACKAGES];
static int ap_content_descriptor_publication[AP_CONTENT_MAX_PACKAGES];

static const char ap_content_path_too_long[] = "The track path is too long. Shorten the install path, then Rescan.";

static int AP_ContentPathsTooLong(const char *levPath, const char *vrmPath)
{
	return strlen(levPath) >= CUSTOM_TRACK_PATH_MAX || strlen(vrmPath) >= CUSTOM_TRACK_PATH_MAX;
}

static int AP_ContentArcadeGridMissing(const struct CustomContentVerification *report)
{
	const struct CustomContentMeasurements *m = &report->measured;
	return m->checkpoints && (!m->navPaths || m->spawns < CTR_CT_FIELD_MAX);
}

static int AP_ContentScanPackageFiles(int index, int autoFinalize, struct CustomTrackManagerStatus *status);

// Locate one package's files (compiled profile folder, or the open track-key
// folder), measure the exact bytes and refuse a claimed capability they lack.
// Returns 1 Ready; otherwise status says why.
static int AP_ContentScanPackage(int index, int autoFinalize, struct CustomTrackManagerStatus *status)
{
	const int ready = AP_ContentScanPackageFiles(index, autoFinalize, status);
	if (index >= 0 && index < AP_CONTENT_MAX_PACKAGES)
		ap_content_package_scan[index] = (ready ? CTR_CT_MANAGER_READY : status->state) + 1;
	return ready;
}

// 1 when the verified main pair (relic 0) or relic pair (relic 1) of a package
// is installed, found the way the scan finds it. Used by the installer to
// fetch only what is missing.
static int AP_ContentPairInstalled(const ctr_content_package *pkg, int relic)
{
	struct CustomTrackManagerStatus found;
	const char *revision = relic && pkg->relic_revision[0] ? pkg->relic_revision : pkg->revision;
	const char *lev = relic ? pkg->relic_lev_sha256 : pkg->lev_sha256;
	const char *vrm = relic ? pkg->relic_vrm_sha256 : pkg->vrm_sha256;
	const unsigned long levBytes = relic ? pkg->relic_lev_bytes : pkg->lev_bytes;
	const unsigned long vrmBytes = relic ? pkg->relic_vrm_bytes : pkg->vrm_bytes;
#ifdef CTR_CUSTOM_PACKAGES
	char levPath[CTR_CT_MANAGER_PATH_MAX], vrmPath[CTR_CT_MANAGER_PATH_MAX], uuid[37];
	if (CustomPackage_FindPinnedFilesWithIdentity(NativeAssets_GetAssetDir(), revision, pkg->uuid, lev, levBytes, vrm, vrmBytes,
	                                              levPath, sizeof levPath, vrmPath, sizeof vrmPath, uuid))
		return 1;
#endif
	return CustomTrackManager_LocateContentFiles(NativeAssets_GetAssetDir(), pkg->content_id, pkg->display_name, revision,
	                                             lev, levBytes, vrm, vrmBytes, &found) == CTR_CT_MANAGER_READY;
}

static int AP_ContentScanPackageFiles(int index, int autoFinalize, struct CustomTrackManagerStatus *status)
{
	const ctr_content_package *pkg = ap_content_plan_package(index);
	const struct CustomTrackManagerPackage *compiled = AP_ContentPackage(index);
	struct CustomContentVerification report;
	char error[160], why[320], line[640];
	int shared = 0;
	if (!pkg) return 0;
	memset(&ap_content_files[index], 0, sizeof ap_content_files[index]);
	memset(status, 0, sizeof *status);
	ap_content_descriptor_publication[index] = 0;
#ifdef CTR_CUSTOM_PACKAGES
	shared = CustomPackage_FindPinnedFilesWithIdentity(NativeAssets_GetAssetDir(), pkg->revision, pkg->uuid,
	                                      pkg->lev_sha256, pkg->lev_bytes, pkg->vrm_sha256, pkg->vrm_bytes,
	                                      status->levPath, sizeof status->levPath,
	                                      status->vrmPath, sizeof status->vrmPath, ap_content_files[index].packageUuid);
#endif
	if (shared)
	{
		status->state = CTR_CT_MANAGER_READY;
		status->levBytes = pkg->lev_bytes;
		status->vrmBytes = pkg->vrm_bytes;
		snprintf(status->actualLevSha256, sizeof status->actualLevSha256, "%s", pkg->lev_sha256);
		snprintf(status->actualVrmSha256, sizeof status->actualVrmSha256, "%s", pkg->vrm_sha256);
		snprintf(status->detail, sizeof status->detail, "%.48s %.24s files match the seed.",
		         pkg->display_name, pkg->revision);
	}
	else if (compiled)
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
	if (!shared && compiled)
		snprintf(ap_content_files[index].packageUuid, sizeof ap_content_files[index].packageUuid, "%s", compiled->packageUuid);
	if (AP_ContentPathsTooLong(status->levPath, status->vrmPath))
	{
		status->state = CTR_CT_MANAGER_PATH_TOO_LONG;
		snprintf(status->detail, sizeof status->detail, "%s", ap_content_path_too_long);
		snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, status->detail);
		AP_AppendLog(line);
		return 0;
	}
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
	// With `relic_files`, relic and time_trial are measured on the relic pair
	// and every other flag on the main pair.
	if (!AP_ContentCapabilitiesHoldMasked(pkg->capabilities,
	                                      pkg->has_relic_files ? AP_CONTENT_CAP_ALL & ~AP_CONTENT_RELIC_PAIR_CAPS : AP_CONTENT_CAP_ALL,
	                                      &report, why, sizeof why) ||
	    !AP_ContentSuperTurboPadsHold(pkg->super_turbo_pads, &report, why, sizeof why) ||
	    !AP_ContentWeaponCratesHold(pkg->weapon_crates, &report, why, sizeof why) ||
	    !AP_ContentSanityCountsHold(pkg, &report, why, sizeof why) ||
	    !AP_ContentBossVenueFactsHold(pkg->checkpoints, pkg->ai_nav, &report, why, sizeof why))
	{
		status->state = (pkg->capabilities & AP_CONTENT_CAP_ARCADE) && AP_ContentArcadeGridMissing(&report)
		                ? CTR_CT_MANAGER_RACE_GRID_UNSUPPORTED : CTR_CT_MANAGER_UNSUPPORTED;
		snprintf(status->detail, sizeof status->detail, "%.48s %.24s: %s", pkg->display_name, pkg->revision, why);
		snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, why);
		AP_AppendLog(line);
		return 0;
	}
	if (pkg->has_relic_files)
	{
		struct CustomContentVerification relicReport;
		struct CustomTrackManagerStatus relic;
		const char *relicRevision = pkg->relic_revision[0] ? pkg->relic_revision : pkg->revision;
		int relicShared = 0;
		memset(&relic, 0, sizeof relic);
#ifdef CTR_CUSTOM_PACKAGES
		char relicUuid[37];
		relicUuid[0] = '\0';
		relicShared = CustomPackage_FindPinnedFilesWithIdentity(NativeAssets_GetAssetDir(), relicRevision, pkg->uuid,
		                                      pkg->relic_lev_sha256, pkg->relic_lev_bytes,
		                                      pkg->relic_vrm_sha256, pkg->relic_vrm_bytes,
		                                      relic.levPath, sizeof relic.levPath,
		                                      relic.vrmPath, sizeof relic.vrmPath, relicUuid);
#endif
		if (relicShared) relic.state = CTR_CT_MANAGER_READY;
		else
			CustomTrackManager_LocateContentFiles(NativeAssets_GetAssetDir(), pkg->content_id, pkg->display_name,
			                                      relicRevision, pkg->relic_lev_sha256, pkg->relic_lev_bytes,
			                                      pkg->relic_vrm_sha256, pkg->relic_vrm_bytes, &relic);
		if (relic.state != CTR_CT_MANAGER_READY)
		{
			// A missing relic pair leaves the package not ready, like a missing main pair.
			*status = relic;
			snprintf(why, sizeof why, "%.48s %.24s: the Relic Race files (%.24s) are not installed or do not match the seed",
			         pkg->display_name, pkg->revision, relicRevision);
			snprintf(status->detail, sizeof status->detail, "%s", why);
			snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, why);
			AP_AppendLog(line);
			return 0;
		}
		error[0] = '\0';
		if (AP_ContentPathsTooLong(relic.levPath, relic.vrmPath))
		{
			status->state = CTR_CT_MANAGER_PATH_TOO_LONG;
			snprintf(status->detail, sizeof status->detail, "%s", ap_content_path_too_long);
			snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, status->detail);
			AP_AppendLog(line);
			return 0;
		}
		if (!CustomContentVerify_Files(relic.levPath, relic.vrmPath, &relicReport, error, sizeof error) ||
		    strcmp(relicReport.levSha256, pkg->relic_lev_sha256) || strcmp(relicReport.vrmSha256, pkg->relic_vrm_sha256))
		{
			status->state = CTR_CT_MANAGER_UNSUPPORTED;
			snprintf(status->detail, sizeof status->detail, "%.48s %.24s Relic Race files failed the file check: %.120s",
			         pkg->display_name, pkg->revision, error[0] ? error : "the files changed while reading");
			snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, status->detail);
			AP_AppendLog(line);
			return 0;
		}
		if (!AP_ContentCapabilitiesHoldMasked(pkg->capabilities, AP_CONTENT_RELIC_PAIR_CAPS, &relicReport, why, sizeof why))
		{
			status->state = CTR_CT_MANAGER_UNSUPPORTED;
			snprintf(status->detail, sizeof status->detail, "%.48s %.24s Relic Race files: %s", pkg->display_name, pkg->revision, why);
			snprintf(line, sizeof line, "[CustomTracks] content plan refused %s: %s\n", pkg->id, status->detail);
			AP_AppendLog(line);
			return 0;
		}
		snprintf(ap_content_files[index].relicLevPath, sizeof ap_content_files[index].relicLevPath, "%s", relic.levPath);
		snprintf(ap_content_files[index].relicVrmPath, sizeof ap_content_files[index].relicVrmPath, "%s", relic.vrmPath);
		ap_content_files[index].relicMeasured = relicReport.measured;
	}
	snprintf(ap_content_files[index].levPath, sizeof ap_content_files[index].levPath, "%s", status->levPath);
	snprintf(ap_content_files[index].vrmPath, sizeof ap_content_files[index].vrmPath, "%s", status->vrmPath);
	ap_content_files[index].measured = report.measured;
#if !defined(CTR_AP_AUTHORING)
	// Verified on disk: Play Solo's custom-track step may now offer it (#89).
	if (!AP_Solo_RecordTrackDescriptor(pkg->content_id, pkg->revision, ap_content_plan_package_json(index)))
	{
		snprintf(line, sizeof line, "[CustomTracks] %s files verified, but Play Solo descriptor publication failed.\n", pkg->id);
		AP_AppendLog(line);
	}
	else ap_content_descriptor_publication[index] = 1;
#endif
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
static int ap_content_cortex_load_error;
static struct AP_ContentFiles ap_content_cortex_files;

static int AP_ContentScanCortex(void)
{
	struct CustomContentVerification report, relicReport;
	char error[160], why[320], line[640];
	const char *dir = NativeAssets_GetAssetDir();
	struct AP_ContentFiles *f = &ap_content_cortex_files;
	ap_content_cortex_load_error = 0;
	memset(f, 0, sizeof *f);
	if (!dir) return 0;
	if (snprintf(f->levPath, sizeof f->levPath, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_LEV_FILE, dir) >= (int)sizeof f->levPath ||
	    snprintf(f->vrmPath, sizeof f->vrmPath, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_VRM_FILE, dir) >= (int)sizeof f->vrmPath ||
	    snprintf(f->relicLevPath, sizeof f->relicLevPath, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_LEV_FILE, dir) >= (int)sizeof f->relicLevPath ||
	    snprintf(f->relicVrmPath, sizeof f->relicVrmPath, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_VRM_FILE, dir) >= (int)sizeof f->relicVrmPath ||
	    AP_ContentPathsTooLong(f->levPath, f->vrmPath) || AP_ContentPathsTooLong(f->relicLevPath, f->relicVrmPath))
	{
		memset(f, 0, sizeof *f);
		ap_content_cortex_load_error = CTR_CT_MANAGER_PATH_TOO_LONG + 1;
		AP_AppendLog("[CustomTracks] content plan refused Cortex Vortex: the track path is too long\n");
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
	// A merged mode build (CV1), like a package with `relic_files`: the main
	// pair holds every mode but the Relic Race, the relic pair holds that one.
	if (!AP_ContentCapabilitiesHoldMasked(AP_CONTENT_CAP_ALL, AP_CONTENT_CAP_ALL & ~AP_CONTENT_RELIC_PAIR_CAPS,
	                                      &report, why, sizeof why))
	{
		if (AP_ContentArcadeGridMissing(&report))
			ap_content_cortex_load_error = CTR_CT_MANAGER_RACE_GRID_UNSUPPORTED + 1;
		snprintf(line, sizeof line, "[CustomTracks] content plan refused Cortex Vortex: %s\n", why);
		AP_AppendLog(line);
		memset(f, 0, sizeof *f);
		return 0;
	}
	error[0] = '\0';
	if (!CustomContentVerify_Files(f->relicLevPath, f->relicVrmPath, &relicReport, error, sizeof error) ||
	    strcmp(relicReport.levSha256, CTR_CFG_CORTEX_RELIC_LEV_SHA256) ||
	    strcmp(relicReport.vrmSha256, CTR_CFG_CORTEX_RELIC_VRM_SHA256))
	{
		snprintf(line, sizeof line, "[CustomTracks] content plan refused Cortex Vortex: the bundled Relic Race files are "
		         "missing or not the pinned pair (%.160s)\n", error[0] ? error : "hash mismatch");
		AP_AppendLog(line);
		memset(f, 0, sizeof *f);
		return 0;
	}
	if (!AP_ContentCapabilitiesHoldMasked(AP_CONTENT_CAP_ALL, AP_CONTENT_RELIC_PAIR_CAPS, &relicReport, why, sizeof why))
	{
		snprintf(line, sizeof line, "[CustomTracks] content plan refused Cortex Vortex Relic Race files: %s\n", why);
		AP_AppendLog(line);
		memset(f, 0, sizeof *f);
		return 0;
	}
	f->measured = report.measured;
	f->relicMeasured = relicReport.measured;
	return 1;
}

// Whether one track's bytes are ready: retail always, Cortex Vortex and a
// custom package once measured (#133 PR 5 shares this with cup legs).
static int AP_ContentTrackReady(int retail, int cortex, int packageIndex)
{
	if (retail) return 1;
	if (cortex) return ap_content_cortex_ready;
	return packageIndex >= 0 && packageIndex < AP_CONTENT_MAX_PACKAGES && ap_content_package_ready[packageIndex];
}

long AP_ContentMainWeaponCrates(int packageIndex, short (**out)[4])
{
	const ctr_content_package *pkg = ap_content_plan_package(packageIndex);
	struct CustomContentOwnedPair pair;
	char error[160];
	short (*rows)[4];
	long found, kept;
	*out = 0;
	if (!pkg || packageIndex < 0 || packageIndex >= AP_CONTENT_MAX_PACKAGES || !ap_content_package_ready[packageIndex] ||
	    !ap_content_files[packageIndex].levPath[0] || !ap_content_files[packageIndex].vrmPath[0])
		return -1;
	// The bytes are read and hashed again against the seed's main pins.
	if (!CustomContentVerify_ReadPair(ap_content_files[packageIndex].levPath, ap_content_files[packageIndex].vrmPath,
	                                  &pair, error, sizeof error))
		return -1;
	if (strcmp(pair.report.levSha256, pkg->lev_sha256) || strcmp(pair.report.vrmSha256, pkg->vrm_sha256))
	{
		CustomContentVerify_FreePair(&pair);
		return -1;
	}
	found = CustomContentVerify_WeaponCrates(&pair, 0, 0);
	if (found < 0 || found > 65536)
	{
		CustomContentVerify_FreePair(&pair);
		return -1;
	}
	rows = (short (*)[4])malloc((size_t)(found ? found : 1) * sizeof *rows);
	if (!rows)
	{
		CustomContentVerify_FreePair(&pair);
		return -1;
	}
	kept = CustomContentVerify_WeaponCrates(&pair, rows, found);
	CustomContentVerify_FreePair(&pair);
	if (kept != found)
	{
		free(rows);
		return -1;
	}
	*out = rows;
	return found;
}

int AP_ContentPadReady(int physicalPad)
{
	const ctr_content_pad *pad = ap_content_plan_pad(physicalPad);
	int k;
	if (!pad || !pad->occupied) return 0;
	// A cup (#133 PR 5) is ready when every leg's track is.
	if (pad->cup)
	{
		for (k = 0; k < AP_CONTENT_CUP_LEGS; ++k)
			if (!AP_ContentTrackReady(pad->legs[k].retail_id >= 0, pad->legs[k].cortex, pad->legs[k].package_index))
				return 0;
		return 1;
	}
	return AP_ContentTrackReady(pad->retail_id >= 0, pad->cortex, pad->package_index);
}

static enum CustomTrackManagerState AP_ContentDescriptorRefusal(const struct CustomTrackSeedDescriptor *descriptor)
{
	if (!descriptor->soloRace &&
	    !CustomTrackPolicy_FlagsSupportRace(descriptor->flagAiNav, descriptor->flagSpawns, 0, NULL))
		return CTR_CT_MANAGER_RACE_GRID_UNSUPPORTED;
	return CTR_CT_MANAGER_LOAD_FAILED;
}

static int AP_ContentLoadRefused(int cortex, int packageIndex, enum CustomTrackManagerState reason)
{
	if (cortex)
	{
		ap_content_cortex_ready = 0;
		ap_content_cortex_load_error = reason + 1;
		ap_cortex_track_content_required = 1;
	}
	else
	{
		const char *detail = "The race could not load the verified files. Check the client log.";
		if (reason == CTR_CT_MANAGER_RACE_GRID_UNSUPPORTED)
			detail = "The track cannot support this race's starting grid. Check the client log.";
		ap_content_package_ready[packageIndex] = 0;
		ap_content_package_scan[packageIndex] = reason + 1;
		ap_custom_content_required = 1;
		ap_custom_content_status.state = reason;
		snprintf(ap_custom_content_status.detail, sizeof ap_custom_content_status.detail, "%s", detail);
	}
	ap_state_gen++;
	return 0;
}

// Locate, hash and measure a Cortex Vortex or custom track again, arm the
// loader with it for `race` (host, laps) and copy its pins into the race. A
// retail race needs nothing. Returns 0 on refusal (the content is marked
// missing).
static int AP_ContentArmTrack(int cortex, int packageIndex, struct CustomTrackRaceContext *race)
{
	race->roomIdentity = ap_content_plan_room_identity();
	if (race->retail) return 1;
	if (cortex)
	{
		const struct CustomContentMeasurements *m;
		struct CustomTrackSeedDescriptor d;
		int serveRelic;
		const char *servedLev, *servedVrm;
		// Hash and measure again: a file swapped since connect is refused here.
		if (!(ap_content_cortex_ready = AP_ContentScanCortex()))
		{
			ap_cortex_track_content_required = 1;
			ap_state_gen++;
			return 0;
		}
		// Merged mode build (CV1): a Relic Race serves the relic pair, every
		// other mode the main pair. The scan above verified both.
		serveRelic = race->mode == AP_CONTENT_MODE_RELIC;
		m = serveRelic ? &ap_content_cortex_files.relicMeasured : &ap_content_cortex_files.measured;
		servedLev = serveRelic ? CTR_CFG_CORTEX_RELIC_LEV_SHA256 : CTR_CFG_CORTEX_LEV_SHA256;
		servedVrm = serveRelic ? CTR_CFG_CORTEX_RELIC_VRM_SHA256 : CTR_CFG_CORTEX_VRM_SHA256;
		if (!CustomTrack_UseContentFiles(serveRelic ? ap_content_cortex_files.relicLevPath : ap_content_cortex_files.levPath,
		                                 serveRelic ? ap_content_cortex_files.relicVrmPath : ap_content_cortex_files.vrmPath,
		                                 "CORTEX VORTEX"))
			return AP_ContentLoadRefused(1, -1, CTR_CT_MANAGER_LOAD_FAILED);
		memset(&d, 0, sizeof d);
		d.standalone = 1; d.laps = race->laps; d.hostLevelID = race->hostLevelID;
		d.replacesCupLevelID = -1; d.boxes = 0;
		d.flagCrates = m->ordinaryCrates > 0; d.flagCtrLetters = m->letterC && m->letterT && m->letterR;
		d.flagRelicCrates = m->relicCrates > 0; d.flagAiNav = m->navPaths > 0;
		d.flagWumpaCollectible = 1; // measured above (AP_CONTENT_CAP_WUMPA)
		d.flagSpawns = m->spawns > 8 ? 8 : (int)m->spawns;
		d.flagCheckpoints = m->checkpoints > 255 ? 255 : (int)m->checkpoints;
		// A Relic Race runs alone: the loader's opponent rule does not apply
		// (A6-23 F1). Trophy, CTR and cup legs keep it.
		d.soloRace = race->mode == AP_CONTENT_MODE_RELIC;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", servedLev);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", servedVrm);
		snprintf(race->levSha256, sizeof race->levSha256, "%s", servedLev);
		snprintf(race->vrmSha256, sizeof race->vrmSha256, "%s", servedVrm);
		if (!CustomTrack_ApplySeedDescriptor(&d))
			return AP_ContentLoadRefused(1, -1, AP_ContentDescriptorRefusal(&d));
		return 1;
	}
	{
		const ctr_content_package *pkg = ap_content_plan_package(packageIndex);
		const struct CustomTrackManagerPackage *p = AP_ContentPackage(packageIndex);
		const struct CustomContentMeasurements *m;
		struct CustomTrackSeedDescriptor d;
		char raceName[CTR_CT_NAME_MAX];
		int used;
		int serveRelic;
		const char *servedLev, *servedVrm;
		if (!pkg) return 0;
		ap_content_package_index = packageIndex;
		// Locate and measure again: a file swapped since connect is refused here.
		if (!AP_ContentScanPackage(packageIndex, 0, &ap_custom_content_status))
		{
			ap_content_package_ready[packageIndex] = 0;
			ap_custom_content_required = 1;
			ap_state_gen++;
			return 0;
		}
		// Merged mode builds: a Relic Race serves the relic pair; every other
		// mode serves the main pair. The scan above verified both.
		serveRelic = pkg->has_relic_files && race->mode == AP_CONTENT_MODE_RELIC &&
		             ap_content_files[packageIndex].relicLevPath[0];
		m = serveRelic ? &ap_content_files[packageIndex].relicMeasured : &ap_content_files[packageIndex].measured;
		servedLev = serveRelic ? pkg->relic_lev_sha256 : pkg->lev_sha256;
		servedVrm = serveRelic ? pkg->relic_vrm_sha256 : pkg->vrm_sha256;
		AP_ContentRaceName(pkg->display_name, raceName, sizeof raceName);
		// A compiled profile's main pair loads through the manager; its relic pair
		// is a plain verified file pair.
		used = (p && !serveRelic) ? CustomTrack_UseManagedPackage(p, &ap_custom_content_status)
		         : CustomTrack_UseContentFiles(serveRelic ? ap_content_files[packageIndex].relicLevPath
		                                                  : ap_content_files[packageIndex].levPath,
		                                       serveRelic ? ap_content_files[packageIndex].relicVrmPath
		                                                  : ap_content_files[packageIndex].vrmPath, raceName);
		if (!used)
			return AP_ContentLoadRefused(0, packageIndex, CTR_CT_MANAGER_LOAD_FAILED);
#ifdef CTR_CUSTOM_PACKAGES
		// The track's own music, when the shared package this seed resolved pins
		// a Saphi .sca. Prefer manifest-pinned music, and the seed's UUID
		// within each tier of verified matches. Optional: no file, no pin or a
		// refused file plays the host slot's music (native_custom_tracks.c
		// verifies the digest and forgets any pin of an earlier package).
		{
			char scaPath[512], scaSha[65];
			unsigned long scaBytes = 0;
			if (CustomPackage_FindPinnedMusic(NativeAssets_GetAssetDir(), pkg->revision, pkg->uuid, 0,
			                                  pkg->lev_sha256, pkg->lev_bytes, pkg->vrm_sha256, pkg->vrm_bytes,
			                                  scaPath, sizeof scaPath, scaSha, &scaBytes))
				// The music stays the main pair's; it binds to the served LEV.
				CustomTrack_UseContentMusic(scaPath, scaSha, scaBytes, servedLev);
		}
#endif
		memset(&d, 0, sizeof d);
		d.standalone = 1; d.laps = race->laps; d.hostLevelID = race->hostLevelID;
		d.replacesCupLevelID = -1; d.boxes = 0;
		// The flags the loader acts on are the measured ones, from the bytes it
		// is about to hash, never the descriptor's claims.
		d.flagCrates = m->ordinaryCrates > 0; d.flagCtrLetters = m->letterC && m->letterT && m->letterR;
		d.flagRelicCrates = m->relicCrates > 0; d.flagAiNav = m->navPaths > 0;
		d.flagMinimap = p ? p->flagMinimap : 0; d.flagGhosts = p ? p->flagGhosts : 0;
		d.flagWumpaCollectible = (pkg->capabilities & AP_CONTENT_CAP_WUMPA) != 0;
		d.flagSpawns = m->spawns > 8 ? 8 : (int)m->spawns;
		d.flagCheckpoints = m->checkpoints > 255 ? 255 : (int)m->checkpoints;
		// A Relic Race runs alone: the loader's opponent rule does not apply
		// (A6-23 F1). Trophy, CTR and cup legs keep it.
		d.soloRace = race->mode == AP_CONTENT_MODE_RELIC;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", servedLev);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", servedVrm);
		snprintf(race->packageRevision, sizeof race->packageRevision, "%s", pkg->revision);
		snprintf(race->packageUuid, sizeof race->packageUuid, "%s", ap_content_files[packageIndex].packageUuid);
		snprintf(race->levSha256, sizeof race->levSha256, "%s", servedLev);
		snprintf(race->vrmSha256, sizeof race->vrmSha256, "%s", servedVrm);
		if (!CustomTrack_ApplySeedDescriptor(&d))
			return AP_ContentLoadRefused(0, packageIndex, AP_ContentDescriptorRefusal(&d));
		return 1;
	}
}

// The race context of one leg of a cup pad (#133 PR 5), not yet armed.
static void AP_ContentLegContext(const ctr_content_pad *pad, int leg, struct CustomTrackRaceContext *race)
{
	const ctr_content_leg *l = &pad->legs[leg];
	memset(race, 0, sizeof *race);
	race->physicalPad = pad->physical; race->returnHub = pad->hub;
	race->laps = l->laps; race->trophyLocation = pad->trophy_location;
	race->mode = AP_CONTENT_MODE_TROPHY; race->cup = 1; race->leg = leg;
	race->seedEpoch = ap_content_plan_epoch(); race->retail = l->retail_id >= 0;
	race->cortex = l->cortex;
	race->hostLevelID = ap_content_leg_host(l);
	race->slot = race->retail || race->cortex ? 0 : l->custom_slot;
	snprintf(race->entryID, sizeof race->entryID, "%s", pad->entry_id);
	snprintf(race->trackID, sizeof race->trackID, "%s", l->track_id);
}

int AP_ContentPreparePad(int physicalPad, int mode)
{
	const ctr_content_pad *pad = ap_content_plan_pad(physicalPad);
	struct CustomTrackRaceContext race;
	int64_t codes[3] = {0, 0, 0};
	if (!pad || !pad->occupied || CustomTrack_StandaloneBusy() || !AP_ContentPadReady(physicalPad) ||
	    !AP_ContentPadOpen(pad)) return -1;
	// A cup (#133 PR 5) starts at its first leg, whatever the selector said.
	if (pad->cup)
	{
		AP_ContentLegContext(pad, 0, &race);
		if (!AP_ContentArmTrack(pad->legs[0].cortex, pad->legs[0].package_index, &race) ||
		    !CustomTrack_StageStandaloneRace(&race)) return -1;
		CustomTrack_CortexTrackSelectNextLoad(0);
		return race.hostLevelID;
	}
	// A track entry races one of its modes, through the same gate the
	// selector used; a Crystal Challenge has no modes and races mode 0.
	if (pad->crystal ? mode != AP_CONTENT_MODE_TROPHY
	                 : (!AP_ContentModeOpen(pad, mode) || ap_content_mode_codes(pad, mode, codes) < 1)) return -1;
	memset(&race, 0, sizeof race);
	race.physicalPad = physicalPad; race.returnHub = pad->hub;
	race.laps = ap_content_mode_laps(pad, mode); race.trophyLocation = pad->crystal ? pad->trophy_location : ap_content_mode_route(pad, mode);
	race.mode = mode;
	race.crystal = pad->crystal; race.timeLimitTicks = pad->time_limit_ticks;
	race.seedEpoch = ap_content_plan_epoch(); race.retail = pad->retail_id >= 0;
	race.cortex = pad->cortex;
	race.hostLevelID = ap_content_pad_host(pad);
	race.slot = pad->custom_slot;
	snprintf(race.entryID, sizeof race.entryID, "%s", pad->entry_id);
	snprintf(race.trackID, sizeof race.trackID, "%s", pad->track_id);
	if (!AP_ContentArmTrack(pad->cortex, pad->package_index, &race)) return -1;
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

// Content-plan garage venues on custom bytes (#133 PR 6b): arm each ordinary
// garage whose Boss Track Shuffle venue is a custom package or Cortex Vortex
// with the scanned files, served on the garage's home LevelID. The measured
// restart point count must be the certified one; a garage whose files are
// missing or differ stays shut (AP_BossGarageVenue) and its door says why.
static void AP_GarageVenuePreflight(void)
{
	const ctr_content_bosses *b = ap_content_plan_bosses();
	int g;
	CustomTrack_ClearGarageVenueDescriptors();
	if (!b->present || !b->track_shuffle) return;
	for (g = 0; g < 4; ++g)
	{
		struct GarageVenueDescriptor d;
		const struct AP_ContentFiles *f = NULL;
		const ctr_content_package *pkg = NULL;
		const char *lev = NULL, *vrm = NULL;
		char line[200];
		memset(&d, 0, sizeof d);
		if (b->venue_package[g] >= 0)
		{
			pkg = ap_content_plan_package(b->venue_package[g]);
			if (pkg && ap_content_package_ready[b->venue_package[g]])
			{
				f = &ap_content_files[b->venue_package[g]];
				lev = pkg->lev_sha256; vrm = pkg->vrm_sha256;
				snprintf(d.navPackageUuid, sizeof d.navPackageUuid, "%s", f->packageUuid);
#ifdef CTR_CUSTOM_PACKAGES
				snprintf(d.packageUuid, sizeof d.packageUuid, "%s", pkg->uuid);
				snprintf(d.musicPackageRevision, sizeof d.musicPackageRevision, "%s", pkg->revision);
				d.levBytes = pkg->lev_bytes;
				d.vrmBytes = pkg->vrm_bytes;
#endif
			}
		}
		else if (b->venue_cortex[g] && ap_content_cortex_ready)
		{
			f = &ap_content_cortex_files;
			lev = CTR_CFG_CORTEX_LEV_SHA256; vrm = CTR_CFG_CORTEX_VRM_SHA256;
#ifdef CTR_CUSTOM_PACKAGES
			d.cortex = 1;
#endif
		}
		else if (!b->venue_cortex[g])
			continue; // a retail venue
		if (f == NULL || (int)f->measured.checkpoints != b->venue_points[g] || f->measured.navPaths == 0)
		{
			snprintf(line, sizeof line, "[AP BOSS] garage %d custom venue not armed: files missing, or %lu restart "
			         "points where %d were certified\n", g, f ? (unsigned long)f->measured.checkpoints : 0ul,
			         b->venue_points[g]);
			AP_AppendLog(line);
			ap_custom_content_required = 1;
			continue;
		}
		d.enabled = 1;
		d.hostLevelID = b->venues[g];
		d.roomIdentity = ap_content_plan_room_identity();
		snprintf(d.trackID, sizeof d.trackID, "%s", pkg ? pkg->id : "cortex_vortex");
		if (pkg)
			snprintf(d.packageRevision, sizeof d.packageRevision, "%s", pkg->revision);
		snprintf(d.levPath, sizeof d.levPath, "%s", f->levPath);
		snprintf(d.vrmPath, sizeof d.vrmPath, "%s", f->vrmPath);
		snprintf(d.levSha256, sizeof d.levSha256, "%s", lev);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", vrm);
		if (!CustomTrack_ApplyGarageVenueDescriptor(g, &d))
			ap_custom_content_required = 1;
	}
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
		AP_GarageVenuePreflight();
		ap_custom_content_scanned = 1;
		ap_state_gen++;
		// A reconnect cannot replace the buffers of a race already in flight.
		if (!CustomTrack_StandaloneBusy())
			CustomTrack_ClearSeedDescriptor();
		return;
	}
	struct CustomTrackManagerRequirement requirement;
	CustomTrack_ClearGarageVenueDescriptors(); // garage venues are content-plan only (#133 PR 6b)
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
#ifdef CTR_AP_AUTHORING
	return !ctr_cfg_active();
#else
	// A pad race owns the loader until it ends. A room is no refusal here: its
	// pins stay on their own path, and AP_ArcadeHoldsRoom keeps the race apart.
	return !CustomTrack_StandaloneBusy();
#endif
}

const char *AP_CustomOfflineRoomNote(void)
{
	return ctr_cfg_active() ? "Room: no checks here, items wait" : "";
}

int AP_ArcadeHoldsRoom(void)
{
	// Retail Arcade and Time Trial need the same isolation as custom pages.
	// Menus and hubs still drain receipts; a non-Adventure race holds them
	// and cannot send checks or apply queued item effects.
	return CustomOffline_RuntimeActive() ||
	       (sdata->gGT && LOAD_IsOpen_RacingOrBattle() && !AP_CapabilityRoomStatsRace());
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
	// A running download (this page's or the Current Seed install, #133 PR 7)
	// refuses before the folder is prepared, so a refusal changes nothing.
	if (ap_custom_track_download_status(NULL, 0) == AP_CT_DOWNLOAD_RUNNING || AP_SeedContentInstallBusy())
		return 0;
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

static int AP_CustomContentCortexMissing(int planActive)
{
	return planActive ? ap_content_plan_cortex() && !ap_content_cortex_ready
	                  : ap_cortex_track_content_required;
}

static int AP_CustomContentWarningVisible(void)
{
	return ap_custom_content_required || ap_oxide_final_content_required ||
	       AP_CustomContentCortexMissing(ap_content_plan_active());
}

void AP_DrawCustomContentWarning(void)
{
	const int planActive = ap_content_plan_active();
	const int cortexMissing = AP_CustomContentCortexMissing(planActive);
#if defined(CTR_CUSTOM_PACKAGES) && !defined(CTR_AP_AUTHORING)
	char *planPath = "OPTIONS > Custom Tracks > This Seed";
	char *verifyPath = "OPTIONS > Custom Tracks: verify.";
#else
	char *planPath = "OPTIONS > Custom Content";
	char *verifyPath = "OPTIONS > Custom Content: verify.";
#endif
	if (planActive && (ap_custom_content_required || cortexMissing))
	{
		int missing = cortexMissing;
		for (int i = 0; i < AP_CONTENT_MAX_PACKAGES; ++i)
			if (ap_content_plan_package(i) && !ap_content_package_ready[i])
				missing++;
		if (missing > 0)
		{
			char line[48];
			snprintf(line, sizeof line, ap_oxide_final_content_required
			         ? "%d TRACK%s MISSING. OXIDE 2 LOCKED"
			         : "!! %d CUSTOM TRACK%s MISSING !!",
			         missing, missing == 1 ? "" : "S");
			DecalFont_DrawLine(line, AP_FEED_X, AP_FEED_CONTENT_WARNING_Y, FONT_SMALL, RED);
			DecalFont_DrawLine(planPath, AP_FEED_X, AP_FEED_CONTENT_WARNING_Y + AP_FEED_LINE_H, FONT_SMALL, RED);
			return;
		}
	}
	if (cortexMissing)
	{
		DecalFont_DrawLine("!! CORTEX VORTEX REQUIRED !!", AP_FEED_X, AP_FEED_CONTENT_WARNING_Y, FONT_SMALL, RED);
		DecalFont_DrawLine(ap_oxide_final_content_required ? "CORTEX PADS AND OXIDE 2 LOCKED"
		                                                 : "Install Cortex Vortex: pad locked.",
		                   AP_FEED_X, AP_FEED_CONTENT_WARNING_Y + AP_FEED_LINE_H, FONT_SMALL, RED);
		return;
	}
	if (ap_oxide_final_content_required)
	{
		DecalFont_DrawLine("!! CORTEX VORTEX REQUIRED !!", AP_FEED_X, AP_FEED_CONTENT_WARNING_Y, FONT_SMALL, RED);
		DecalFont_DrawLine("Verify Cortex Vortex: Oxide 2 locked.",
		                   AP_FEED_X, AP_FEED_CONTENT_WARNING_Y + AP_FEED_LINE_H, FONT_SMALL, RED);
		return;
	}
	if (!ap_custom_content_required)
		return;
	DecalFont_DrawLine("!! CUSTOM CONTENT REQUIRED !!", AP_FEED_X, AP_FEED_CONTENT_WARNING_Y, FONT_SMALL, RED);
	DecalFont_DrawLine(planActive ? planPath : verifyPath,
	                   AP_FEED_X, AP_FEED_CONTENT_WARNING_Y + AP_FEED_LINE_H, FONT_SMALL, RED);
}
#endif

#endif // CTR_AP
