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
// trusts, Project Saphi. A package that matches a compiled release profile
// exactly (CustomTrackManager_FindPackage: id, uuid, revision and both hashes)
// downloads as before. Any other package is looked up in the live track
// registry by the room's pinned hashes (row 10k, ap/ap_track_registry.h); the
// registry only says where on Saphi the files are, and the download installs
// nothing unless the bytes equal the room's pins. Every other package gets
// instructions.

#ifdef CTR_AP

#ifdef CTR_CUSTOM_TRACKS
#include "ap_seed_content.h"
#include "ap_track_registry.h"
#include <platform/native_track_sources.h>

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

// Row 10k: the registry lookup per package, kept until a new index or a new
// room. NO_INDEX is not kept: the refresh state says checking or offline.
static struct
{
	uint32_t generation;
	uint64_t epoch;
	int valid;
	unsigned char looked[AP_CONTENT_MAX_PACKAGES];
	int result[AP_CONTENT_MAX_PACKAGES];
	struct ApTrackRegistryMatch match[AP_CONTENT_MAX_PACKAGES];
} ap_seed_registry;
static int ap_seed_registry_asked, ap_seed_registry_last_state;
static uint64_t ap_seed_registry_asked_epoch;

// assets/track-registry-cache.json: next to tracks/, which is writable
// wherever tracks can be installed (the Deck's AppImage folder is not).
static int AP_SeedRegistryCachePath(char *out, size_t size)
{
	const char *assets = NativeAssets_GetAssetDir();
	size_t len;
	if (!assets || !assets[0]) return 0;
	len = strlen(assets);
	return snprintf(out, size, (assets[len - 1] == '/' || assets[len - 1] == '\\') ? "%strack-registry-cache.json"
	                                                                                 : "%s/track-registry-cache.json",
	                assets) < (int)size;
}

// Fetch the registry when the room has a missing package without a compiled
// profile: once per room, unless this session already has the live copy.
// `force` (Rescan) asks again unless a fetch is running.
static void AP_SeedRegistryEnsure(int force)
{
	char path[CTR_CT_MANAGER_PATH_MAX], line[640], detail[256];
	int i, needed = 0, source = AP_TRACK_REGISTRY_SOURCE_NONE;
	const int state = ap_track_registry_status(&source, detail, sizeof detail);
	// Say once in the log how a fetch ended: the screen only has room for
	// "unavailable" or "cached registry".
	if (state != ap_seed_registry_last_state && ap_seed_registry_last_state == AP_TRACK_REGISTRY_LOADING)
	{
		snprintf(line, sizeof line, "[CustomTracks] Current Seed: track registry %s%s%s\n",
		         source == AP_TRACK_REGISTRY_SOURCE_LIVE ? "fetched" :
		         source == AP_TRACK_REGISTRY_SOURCE_CACHED ? "not fetched, using the cached copy" : "unavailable, no cached copy",
		         detail[0] ? ": " : "", detail);
		AP_AppendLog(line);
	}
	ap_seed_registry_last_state = state;
	if (state == AP_TRACK_REGISTRY_LOADING) return;
	if (!force && ap_seed_registry_asked && ap_seed_registry_asked_epoch == ap_content_plan_epoch()) return;
	for (i = 0; i < AP_CONTENT_MAX_PACKAGES && !needed; ++i)
		if (ap_content_plan_package(i) && !AP_ContentPackage(i) &&
		    ap_content_package_scan[i] != CTR_CT_MANAGER_READY + 1) needed = 1;
	if (!needed) return;
	ap_seed_registry_asked = 1;
	ap_seed_registry_asked_epoch = ap_content_plan_epoch();
	if (!force && state == AP_TRACK_REGISTRY_DONE && source == AP_TRACK_REGISTRY_SOURCE_LIVE) return;
	if (!AP_SeedRegistryCachePath(path, sizeof path)) path[0] = '\0';
	if (ap_track_registry_refresh(path))
	{
		// Pending from here, so a fetch that ends before the next frame is
		// still logged.
		ap_seed_registry_last_state = AP_TRACK_REGISTRY_LOADING;
		snprintf(line, sizeof line, "[CustomTracks] Current Seed: checking the track registry (%s)\n",
		         AP_TRACK_REGISTRY_HOST AP_TRACK_REGISTRY_PATH);
		AP_AppendLog(line);
	}
}

// AP_SEED_REGISTRY_* for package `index` without a compiled profile; match
// is filled for LIVE and CACHED.
static int AP_SeedRegistryRow(int index, const ctr_content_package *pkg, struct ApTrackRegistryMatch *match)
{
	const uint32_t generation = ap_track_registry_generation();
	const uint64_t epoch = ap_content_plan_epoch();
	int result, source = AP_TRACK_REGISTRY_SOURCE_NONE, state;
	if (index < 0 || index >= AP_CONTENT_MAX_PACKAGES) return AP_SEED_REGISTRY_NONE;
	if (!ap_seed_registry.valid || ap_seed_registry.generation != generation || ap_seed_registry.epoch != epoch)
	{
		memset(&ap_seed_registry, 0, sizeof ap_seed_registry);
		ap_seed_registry.valid = 1;
		ap_seed_registry.generation = generation;
		ap_seed_registry.epoch = epoch;
	}
	if (!ap_seed_registry.looked[index])
	{
		result = ap_track_registry_lookup(pkg->lev_sha256, pkg->lev_bytes, pkg->vrm_sha256, pkg->vrm_bytes,
		                                  &ap_seed_registry.match[index]);
		if (result != AP_TRACK_REGISTRY_NO_INDEX)
		{
			ap_seed_registry.looked[index] = 1;
			ap_seed_registry.result[index] = result;
		}
	}
	else result = ap_seed_registry.result[index];
	state = ap_track_registry_status(&source, NULL, 0);
	switch (result)
	{
	case AP_TRACK_REGISTRY_MATCH:
		if (match) *match = ap_seed_registry.match[index];
		return source == AP_TRACK_REGISTRY_SOURCE_CACHED ? AP_SEED_REGISTRY_CACHED : AP_SEED_REGISTRY_LIVE;
	case AP_TRACK_REGISTRY_NOT_ON_SAPHI: return AP_SEED_REGISTRY_NOT_ON_SAPHI;
	case AP_TRACK_REGISTRY_PIN_CONFLICT: return AP_SEED_REGISTRY_CONFLICT;
	case AP_TRACK_REGISTRY_NOT_LISTED: return AP_SEED_REGISTRY_NOT_LISTED;
	default:
		return state == AP_TRACK_REGISTRY_UNAVAILABLE ? AP_SEED_REGISTRY_OFFLINE : AP_SEED_REGISTRY_CHECKING;
	}
}

// A4-2: the built-in source table as a fallback for the live registry, only when
// the registry is unreachable or does not list the track (never when it lists the
// revision and says it is not on Saphi, or disagrees on size). A Saphi
// row whose LEV and VRM both equal the room's pins gives the same match the
// registry would; the download still installs nothing unless the bytes equal
// the pins.
static int AP_SeedTableMatch(const ctr_content_package *pkg, struct ApTrackRegistryMatch *match)
{
	struct TrackSource found;
	if (!TrackSources_Find(pkg->lev_sha256, pkg->vrm_sha256, &found) || found.kind != TRACK_SOURCE_SAPHI ||
	    !TrackSources_HexEqual(found.vrm, pkg->vrm_sha256) || found.saphiTrack <= 0 || found.levDownload <= 0 ||
	    found.vrmDownload <= 0) return 0;
	if (match)
	{
		memset(match, 0, sizeof *match);
		match->saphi_track_id = found.saphiTrack;
		match->lev_download_id = found.levDownload;
		match->vrm_download_id = found.vrmDownload;
		snprintf(match->track_key, sizeof match->track_key, "%s", pkg->content_id);
		snprintf(match->revision, sizeof match->revision, "%s", pkg->revision);
	}
	return 1;
}

void AP_SeedContentRegistryRetry(void)
{
	AP_SeedRegistryEnsure(1);
}

int AP_SeedContentRows(struct AP_SeedContentRow *rows, int max)
{
	int i, n = 0;
	if (!rows || max <= 0 || !ap_content_plan_active()) return 0;
	AP_SeedRegistryEnsure(0);
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
		if (!compiled)
		{
			row->registry = AP_SeedRegistryRow(i, pkg, NULL);
			if (row->registry == AP_SEED_REGISTRY_LIVE || row->registry == AP_SEED_REGISTRY_CACHED)
				row->source = AP_SEED_SOURCE_SAPHI;
			else if ((row->registry == AP_SEED_REGISTRY_OFFLINE || row->registry == AP_SEED_REGISTRY_NOT_LISTED) &&
			         AP_SeedTableMatch(pkg, NULL))
			{
				row->registry = AP_SEED_REGISTRY_TABLE;
				row->source = AP_SEED_SOURCE_SAPHI;
			}
			else if (row->scan != AP_SEED_SCAN_READY)
			{
				struct TrackSource found;
				if (TrackSources_Find(pkg->lev_sha256, pkg->vrm_sha256, &found) && found.kind == TRACK_SOURCE_PATCH)
					TrackSources_Hint(pkg->lev_sha256, pkg->vrm_sha256, row->source_hint, sizeof row->source_hint);
			}
		}
		row->index = i;
		// The xdelta patch an imported track comes from, when the room's
		// package record names one.
		row->locator.bigfile_index = pkg->locator_index;
		snprintf(row->locator.patch_sha256, sizeof row->locator.patch_sha256, "%s", pkg->locator_sha256);
		snprintf(row->locator.patch_name, sizeof row->locator.patch_name, "%s", pkg->locator_name);
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

_Static_assert(AP_SEED_ROWS_MAX == AP_CONTENT_MAX_PACKAGES + 1, "one row per package, plus Cortex Vortex");

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

// A safe track key and source lookup for package `index`, checked before
// download. The worker publishes into tracks/packages; these legacy paths
// only supply the bounded track key and profile metadata.
static int AP_SeedInstallTarget(int index, const struct CustomTrackManagerPackage **profile,
                                struct ApTrackRegistryMatch *match,
                                struct CustomTrackManagerStatus *paths, char *tracks, size_t tracksSize)
{
	const ctr_content_package *pkg = ap_content_plan_package(index);
	const char *assets = NativeAssets_GetAssetDir();
	*profile = AP_ContentPackage(index);
	if (!pkg || !assets || !assets[0]) return 0;
	// The same join the track manager uses, so its package root matches.
	{
		const size_t len = strlen(assets);
		const int sep = assets[len - 1] == '/' || assets[len - 1] == '\\';
		if (snprintf(tracks, tracksSize, sep ? "%stracks" : "%s/tracks", assets) >= (int)tracksSize) return 0;
	}
	if (*profile)
	{
		if (!CustomTrackManager_ResolvePaths(assets, *profile, paths)) return 0;
	}
	else
	{
		const int registry = AP_SeedRegistryRow(index, pkg, match);
		if (registry != AP_SEED_REGISTRY_LIVE && registry != AP_SEED_REGISTRY_CACHED &&
		    !((registry == AP_SEED_REGISTRY_OFFLINE || registry == AP_SEED_REGISTRY_NOT_LISTED) && AP_SeedTableMatch(pkg, match)))
			return 0;
		if (!ap_seed_folder_safe(pkg->content_id)) return 0;
		memset(paths, 0, sizeof *paths);
		if (snprintf(paths->packageRoot, sizeof paths->packageRoot, "%s/%s", tracks, pkg->content_id) >=
		    (int)sizeof paths->packageRoot) return 0;
	}
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
	struct ApTrackRegistryMatch match;
	struct CustomTrackManagerStatus paths;
	char tracks[CTR_CT_MANAGER_PATH_MAX], line[400];
	int started = 0;
	memset(&match, 0, sizeof match);
	if (pkg && AP_SeedInstallTarget(index, &profile, &match, &paths, tracks, sizeof tracks))
	{
		if (profile)
			started = ap_custom_track_download_start_shared(tracks, paths.packageRoot, profile->downloadApiUrl,
			                                                pkg->revision, pkg->uuid, pkg->display_name, pkg->author, pkg->laps,
			                                                pkg->has_relic_targets ? pkg->relic_targets[0] : 0,
			                                                pkg->has_relic_targets ? pkg->relic_targets[1] : 0,
			                                                pkg->has_relic_targets ? pkg->relic_targets[2] : 0,
			                                                pkg->lev_sha256, pkg->lev_bytes,
			                                                pkg->vrm_sha256, pkg->vrm_bytes, 0, 0, 0);
		else
		{
			int source = AP_TRACK_REGISTRY_SOURCE_NONE;
			ap_track_registry_status(&source, NULL, 0);
			snprintf(line, sizeof line,
			         "[CustomTracks] Current Seed: %s located by the %s track registry: Saphi track %d, "
			         "files %d and %d, installed only if they equal the room's pins\n",
			         ap_seed_install.title[at], source == AP_TRACK_REGISTRY_SOURCE_CACHED ? "cached" : "live",
			         match.saphi_track_id, match.lev_download_id, match.vrm_download_id);
			AP_AppendLog(line);
			started = ap_custom_track_download_start_shared(tracks, paths.packageRoot, NULL,
			                                                pkg->revision, pkg->uuid, pkg->display_name, pkg->author, pkg->laps,
			                                                pkg->has_relic_targets ? pkg->relic_targets[0] : 0,
			                                                pkg->has_relic_targets ? pkg->relic_targets[1] : 0,
			                                                pkg->has_relic_targets ? pkg->relic_targets[2] : 0,
			                                                pkg->lev_sha256, pkg->lev_bytes,
			                                                pkg->vrm_sha256, pkg->vrm_bytes, match.saphi_track_id,
			                                                match.lev_download_id, match.vrm_download_id);
		}
	}
	if (!started)
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
		struct ApTrackRegistryMatch match;
		struct CustomTrackManagerStatus paths;
		char tracks[CTR_CT_MANAGER_PATH_MAX], title[48];
		ap_seed_plain_text(rows[plan[i]].title, title, sizeof title);
		if (!AP_SeedInstallTarget(rows[plan[i]].index, &profile, &match, &paths, tracks, sizeof tracks))
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
	int n, ready, publicationFailed = 0, i;
	char line[512];
	const int installed = ap_seed_install.installed;
	AP_SeedInstallStop();
	if (installed) AP_CustomContentRescan();
	for (i = 0; i < installed; ++i)
		if (ap_content_package_scan[ap_seed_install.queue[i]] != CTR_CT_MANAGER_READY + 1 ||
		    !ap_content_descriptor_publication[ap_seed_install.queue[i]])
			publicationFailed = 1;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	ready = ap_seed_ready_count(rows, n);
	if (failure && installed && publicationFailed)
		snprintf(message, messageSize, "%.100s Installed %d before it; verification or Play Solo publication also failed. Ready: %d / %d.", failure, installed, ready, n);
	else if (failure && installed)
		snprintf(message, messageSize, "%s Installed %d before it. Ready: %d / %d.", failure, installed, ready, n);
	else if (failure)
		snprintf(message, messageSize, "%s Ready: %d / %d.", failure, ready, n);
	else if (publicationFailed)
		snprintf(message, messageSize, "Files installed, but verification or Play Solo publication failed. Ready: %d / %d.", ready, n);
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
