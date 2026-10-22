// ap_hooks_seed_content.c -- part of the ap_hooks unit (issue #305).
// Current Seed (#133 PR 7): the content-plan room's required tracks with
// Ready / Missing, and the one "Install missing content" action behind
// Options > Custom Content > Current Seed.
//
// Not a separate translation unit: game/game_unity.h includes it right after
// ap_hooks_custom_content.c, whose file-static package state it reads
// (ap_content_package_scan, ap_content_cortex_ready, AP_ContentPackage).
// The decisions are in ap/ap_seed_content_logic.h (tools/test-seed-content.c).
//
// Trust: the only download source is the one the player client already
// trusts, Project Saphi, and only for a package that matches a compiled
// release profile exactly (CustomTrackManager_FindPackage: id, uuid,
// revision and both hashes). Every other package gets instructions.

#ifdef CTR_AP

#ifdef CTR_CUSTOM_TRACKS
#include "ap_seed_content.h"

static int AP_SeedScanFromManager(int stored)
{
	switch (stored - 1)
	{
	case CTR_CT_MANAGER_READY: return AP_SEED_SCAN_READY;
	case CTR_CT_MANAGER_NOT_INSTALLED: return AP_SEED_SCAN_NOT_INSTALLED;
	case CTR_CT_MANAGER_MISSING_FILES: return AP_SEED_SCAN_MISSING_FILE;
	case CTR_CT_MANAGER_HASH_MISMATCH: return AP_SEED_SCAN_MISMATCH;
	case CTR_CT_MANAGER_MANIFEST_MISSING:
	case CTR_CT_MANAGER_MANIFEST_INVALID: return AP_SEED_SCAN_SETUP;
	case CTR_CT_MANAGER_UNSUPPORTED:
	case CTR_CT_MANAGER_INCOMPATIBLE: return AP_SEED_SCAN_REFUSED;
	case CTR_CT_MANAGER_IO_ERROR: return AP_SEED_SCAN_IO_ERROR;
	default: return AP_SEED_SCAN_UNKNOWN;
	}
}

int AP_SeedContentActive(void)
{
	int i;
	if (!ap_content_plan_active()) return 0;
	if (ap_content_plan_cortex()) return 1;
	for (i = 0; i < AP_CONTENT_MAX_PACKAGES; ++i)
		if (ap_content_plan_package(i)) return 1;
	return 0;
}

int AP_SeedContentRows(struct AP_SeedContentRow *rows, int max)
{
	int i, n = 0;
	if (!rows || max <= 0 || !ap_content_plan_active()) return 0;
	for (i = 0; i < AP_CONTENT_MAX_PACKAGES && n < max; ++i)
	{
		const ctr_content_package *pkg = ap_content_plan_package(i);
		const struct CustomTrackManagerPackage *compiled;
		struct AP_SeedContentRow *row;
		if (!pkg) continue;
		compiled = AP_ContentPackage(i);
		row = &rows[n++];
		memset(row, 0, sizeof *row);
		snprintf(row->title, sizeof row->title, "%s", pkg->display_name);
		snprintf(row->author, sizeof row->author, "%s", pkg->author);
		snprintf(row->revision, sizeof row->revision, "%s", pkg->revision);
		snprintf(row->folder, sizeof row->folder, "%s", pkg->content_id);
		row->lev_bytes = pkg->lev_bytes;
		row->vrm_bytes = pkg->vrm_bytes;
		row->scan = AP_SeedScanFromManager(ap_content_package_scan[i]);
		row->source = compiled && compiled->downloadApiUrl && compiled->downloadApiUrl[0]
		              ? AP_SEED_SOURCE_SAPHI : AP_SEED_SOURCE_MANUAL;
		row->index = i;
		// The room's package record has no patch locator yet: the field
		// stays empty.
		row->locator.bigfile_index = -1;
	}
	if (ap_content_plan_cortex() && n < max)
	{
		struct AP_SeedContentRow *row = &rows[n++];
		memset(row, 0, sizeof *row);
		snprintf(row->title, sizeof row->title, "Cortex Vortex");
		snprintf(row->author, sizeof row->author, "the client download");
		snprintf(row->folder, sizeof row->folder, "cortex-vortex");
		row->scan = ap_custom_content_scanned ? (ap_content_cortex_ready ? AP_SEED_SCAN_READY : AP_SEED_SCAN_NOT_INSTALLED)
		                                      : AP_SEED_SCAN_UNKNOWN;
		row->source = AP_SEED_SOURCE_BUNDLED;
		row->index = -1;
		row->locator.bigfile_index = -1;
	}
	return n;
}

static struct
{
	int active, waiting, count, at, installed;
	int queue[AP_SEED_ROWS_MAX];      // package indices
	char title[AP_SEED_ROWS_MAX][48]; // plain titles for the messages
	uint64_t epoch;
} ap_seed_install;

int AP_SeedContentInstallBusy(void)
{
	return ap_seed_install.active;
}

static int AP_SeedDownloaderBusy(void)
{
	return ap_seed_install.active || ap_custom_track_download_status(NULL, 0) == AP_CT_DOWNLOAD_RUNNING;
}

int AP_SeedContentInstallPreview(int *plan, int *count, uint64_t *bytes, char *reason, size_t reasonSize)
{
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	const int n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	return ap_seed_install_plan(rows, n, AP_SeedDownloaderBusy(), plan, count, bytes, reason, reasonSize);
}

// Where package `index` installs, checked without touching the disk: the
// compiled profile's own folder, exactly one safe folder below assets/tracks.
static int AP_SeedInstallTarget(int index, const struct CustomTrackManagerPackage **profile,
                                struct CustomTrackManagerStatus *paths, char *tracks, size_t tracksSize)
{
	const ctr_content_package *pkg = ap_content_plan_package(index);
	const char *assets = NativeAssets_GetAssetDir();
	*profile = AP_ContentPackage(index);
	if (!pkg || !*profile || !assets || !assets[0]) return 0;
	if (snprintf(tracks, tracksSize, "%s/tracks", assets) >= (int)tracksSize) return 0;
	if (!CustomTrackManager_ResolvePaths(assets, *profile, paths)) return 0;
	return ap_seed_root_safe(tracks, paths->packageRoot);
}

static void AP_SeedInstallStop(void)
{
	ap_seed_install.active = 0;
	ap_seed_install.waiting = 0;
}

static int AP_SeedInstallBegin(int at, char *message, size_t messageSize)
{
	const int index = ap_seed_install.queue[at];
	const ctr_content_package *pkg = ap_content_plan_package(index);
	const struct CustomTrackManagerPackage *profile;
	struct CustomTrackManagerStatus paths;
	char tracks[CTR_CT_MANAGER_PATH_MAX];
	if (!pkg || !AP_SeedInstallTarget(index, &profile, &paths, tracks, sizeof tracks) ||
	    !ap_custom_track_download_start_pinned(tracks, paths.packageRoot, profile->downloadApiUrl, pkg->revision,
	                                           pkg->lev_sha256, pkg->lev_bytes, pkg->vrm_sha256, pkg->vrm_bytes))
	{
		snprintf(message, messageSize, "%s: could not start the download.%s", ap_seed_install.title[at],
		         ap_seed_install.installed ? "" : " Nothing was installed.");
		return 0;
	}
	ap_seed_install.waiting = 1;
	snprintf(message, messageSize, "Downloading %s (%d of %d)...", ap_seed_install.title[at], at + 1,
	         ap_seed_install.count);
	return 1;
}

int AP_SeedContentInstallStart(char *message, size_t messageSize)
{
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	int plan[AP_SEED_ROWS_MAX], count = 0, i, n;
	uint64_t bytes = 0;
	char line[320];
	if (!message || messageSize == 0) return 0;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	if (ap_seed_install_plan(rows, n, AP_SeedDownloaderBusy(), plan, &count, &bytes, message, messageSize) != AP_SEED_PLAN_OK)
		return 0;
	// Check every queued track before the first download: a refusal here
	// changes nothing on disk.
	for (i = 0; i < count; ++i)
	{
		const struct CustomTrackManagerPackage *profile;
		struct CustomTrackManagerStatus paths;
		char tracks[CTR_CT_MANAGER_PATH_MAX], title[48];
		ap_seed_plain_text(rows[plan[i]].title, title, sizeof title);
		if (!AP_SeedInstallTarget(rows[plan[i]].index, &profile, &paths, tracks, sizeof tracks))
		{
			snprintf(message, messageSize, "%s has no safe install folder. Nothing was installed.", title);
			return 0;
		}
	}
	memset(&ap_seed_install, 0, sizeof ap_seed_install);
	ap_seed_install.count = count;
	ap_seed_install.epoch = ap_content_plan_epoch();
	for (i = 0; i < count; ++i)
	{
		ap_seed_install.queue[i] = rows[plan[i]].index;
		ap_seed_plain_text(rows[plan[i]].title, ap_seed_install.title[i], sizeof ap_seed_install.title[i]);
	}
	ap_seed_install.active = 1;
	snprintf(line, sizeof line, "[CustomTracks] Current Seed: installing %d track(s) from Project Saphi\n", count);
	AP_AppendLog(line);
	if (!AP_SeedInstallBegin(0, message, messageSize))
	{
		AP_SeedInstallStop();
		return 0;
	}
	return 1;
}

static void AP_SeedInstallFinish(char *message, size_t messageSize, const char *failure)
{
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	int n, ready;
	char line[512];
	const int installed = ap_seed_install.installed;
	AP_SeedInstallStop();
	if (installed) AP_CustomContentRescan();
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	ready = ap_seed_ready_count(rows, n);
	if (failure && installed)
		snprintf(message, messageSize, "%s Installed %d before it. Ready: %d / %d.", failure, installed, ready, n);
	else if (failure)
		snprintf(message, messageSize, "%s Ready: %d / %d.", failure, ready, n);
	else
		snprintf(message, messageSize, "Installed %d. Files verified. Ready: %d / %d.", installed, ready, n);
	snprintf(line, sizeof line, "[CustomTracks] Current Seed: %s\n", message);
	AP_AppendLog(line);
}

int AP_SeedContentInstallTick(char *message, size_t messageSize)
{
	char detail[200], failure[320];
	int state;
	if (!ap_seed_install.active || !message || messageSize == 0) return 0;
	// A different room has other requirements: stop after what already ran.
	if (!ap_content_plan_active() || ap_content_plan_epoch() != ap_seed_install.epoch)
	{
		if (ap_seed_install.waiting &&
		    ap_custom_track_download_status(NULL, 0) == AP_CT_DOWNLOAD_RUNNING) return 1;
		AP_SeedInstallStop();
		snprintf(message, messageSize, "The room changed. Install stopped.");
		return 0;
	}
	if (!ap_seed_install.waiting)
	{
		if (!AP_SeedInstallBegin(ap_seed_install.at, failure, sizeof failure))
		{
			AP_SeedInstallFinish(message, messageSize, failure);
			return 0;
		}
		snprintf(message, messageSize, "%s", failure);
		return 1;
	}
	state = ap_custom_track_download_status(detail, sizeof detail);
	if (state == AP_CT_DOWNLOAD_RUNNING)
	{
		snprintf(message, messageSize, "%d of %d: %s", ap_seed_install.at + 1, ap_seed_install.count, detail);
		return 1;
	}
	ap_seed_install.waiting = 0;
	if (state != AP_CT_DOWNLOAD_SUCCEEDED)
	{
		snprintf(failure, sizeof failure, "%s: %s", ap_seed_install.title[ap_seed_install.at], detail);
		AP_SeedInstallFinish(message, messageSize, failure);
		return 0;
	}
	{
		// The verified files are in place: add the profile's folder skeleton
		// (PrepareFolder never replaces a file) before the rescan finalizes.
		const struct CustomTrackManagerPackage *profile;
		struct CustomTrackManagerStatus paths;
		char tracks[CTR_CT_MANAGER_PATH_MAX];
		if (AP_SeedInstallTarget(ap_seed_install.queue[ap_seed_install.at], &profile, &paths, tracks, sizeof tracks))
			CustomTrackManager_PrepareFolder(NativeAssets_GetAssetDir(), profile, &paths);
	}
	ap_seed_install.installed++;
	ap_seed_install.at++;
	if (ap_seed_install.at >= ap_seed_install.count)
	{
		AP_SeedInstallFinish(message, messageSize, NULL);
		return 0;
	}
	if (!AP_SeedInstallBegin(ap_seed_install.at, failure, sizeof failure))
	{
		AP_SeedInstallFinish(message, messageSize, failure);
		return 0;
	}
	snprintf(message, messageSize, "%s", failure);
	return 1;
}

#endif // CTR_CUSTOM_TRACKS

#endif // CTR_AP
