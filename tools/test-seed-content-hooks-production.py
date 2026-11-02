"""Current Seed rows and the install queue through the production hooks (#133 PR 7).

Compiles ap/ap_hooks_seed_content.c with the real track manager
(platform/native_custom_track_manager.c, for the compiled release profiles,
ResolvePaths and PrepareFolder) and the real decisions in
ap/ap_seed_content_logic.h. The content plan, the per-package scan results,
the downloader and the rescan are stubs that record what the hooks do.

Cases:
  1. no content plan: the page is off and has no rows,
  2. rows: compiled and open packages in plan order with their pinned sizes,
     author, folder and source, Cortex Vortex last,
  3. refusals before any state change: a download already running, only
     manual rows, a compiled profile whose folder is unsafe, a refused worker
     start; none starts a download or creates a folder,
  4. a clean queue: each download gets the room's exact pins and the profile's
     own folder under assets/tracks, nothing is created before the worker
     reports success, PrepareFolder runs after it, then the next track,
  5. a failure stops the queue: no further download, one rescan for what was
     installed, the failure in the message,
  6. a room change stops the queue after the running download,
  7. a second start while one runs is refused,
  8. the live track registry (row 10k, stubbed here): a missing package
     without a compiled profile is fetched for once per room, with the cache
     at assets/track-registry-cache.json; a registry match makes its row a
     Project Saphi download, labelled live or cached; offline, not listed or
     not on Saphi stay manual with the reason; the install calls the registry
     download with the room's pins and the track-key folder, never the
     compiled path; an unsafe key refuses; Rescan asks again.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
hooks_src = (root / "ap/ap_hooks_seed_content.c").read_text(encoding="utf-8")
hooks = hooks_src[hooks_src.index('#include "ap_seed_content.h"'):hooks_src.index("#endif // CTR_CUSTOM_TRACKS")]

prelude = r'''
#define _XOPEN_SOURCE 700
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
static char log_text[8192];
void AP_LogLine(const char *msg) { strncat(log_text, msg, sizeof log_text - strlen(log_text) - 1); }
static void AP_AppendLog(const char *msg) { AP_LogLine(msg); }
#include "platform/native_custom_content_verify.c"
#include "platform/native_custom_track_manager.c"
#include "ap/ap_content_plan.h"
#include "ap/ap_custom_track_download.h"
#include "ap/ap_track_registry.h"
'''

stubs = r'''
static char assets_root[256];
const char *NativeAssets_GetAssetDir(void) { return assets_root; }
static ctr_content_package packages[4];
static int package_count, plan_active = 1, plan_cortex;
static uint64_t plan_epoch = 7;
int ap_content_plan_active(void) { return plan_active; }
const ctr_content_package *ap_content_plan_package(int i) { return plan_active && i >= 0 && i < package_count ? &packages[i] : NULL; }
int ap_content_plan_cortex(void) { return plan_active && plan_cortex; }
uint64_t ap_content_plan_epoch(void) { return plan_active ? plan_epoch : 0; }
static int ap_content_package_scan[AP_CONTENT_MAX_PACKAGES];
static int ap_content_cortex_ready, ap_custom_content_scanned = 1;
static const struct CustomTrackManagerPackage *compiled_for[4];
static const struct CustomTrackManagerPackage *AP_ContentPackage(int i) { return i >= 0 && i < package_count ? compiled_for[i] : NULL; }
static int dl_state = AP_CT_DOWNLOAD_IDLE, starts, refuse_start;
static char dl_detail[200], start_tracks[512], start_root[512], start_version[80], start_lev[80], start_vrm[80];
static uint32_t start_lev_bytes, start_vrm_bytes;
int ap_custom_track_download_start_pinned(const char *tracks, const char *rootDir, const char *api, const char *version,
                                          const char *lev, uint32_t levBytes, const char *vrm, uint32_t vrmBytes)
{
	(void)api;
	starts++;
	snprintf(start_tracks, sizeof start_tracks, "%s", tracks); snprintf(start_root, sizeof start_root, "%s", rootDir);
	snprintf(start_version, sizeof start_version, "%s", version);
	snprintf(start_lev, sizeof start_lev, "%s", lev); snprintf(start_vrm, sizeof start_vrm, "%s", vrm);
	start_lev_bytes = levBytes; start_vrm_bytes = vrmBytes;
	if (refuse_start) return 0;
	dl_state = AP_CT_DOWNLOAD_RUNNING;
	snprintf(dl_detail, sizeof dl_detail, "Downloading Baby T Park LEV from Saphi...");
	return 1;
}
int ap_custom_track_download_status(char *detail, int bytes)
{
	if (detail && bytes > 0) snprintf(detail, (size_t)bytes, "%s", dl_detail);
	return dl_state;
}
static int rescans;
void AP_CustomContentRescan(void) { rescans++; }
/* Row 10k: the registry and its download, recorded. */
static int reg_state = AP_TRACK_REGISTRY_IDLE, reg_source = AP_TRACK_REGISTRY_SOURCE_NONE, reg_result = AP_TRACK_REGISTRY_NO_INDEX;
static int reg_refreshes, reg_starts;
static uint32_t reg_generation = 1;
static char reg_cache[512], reg_lev[80], reg_root[512], reg_pin_lev[80], reg_pin_vrm[80];
static int reg_track, reg_lev_id, reg_vrm_id;
static uint32_t reg_lev_bytes, reg_vrm_bytes;
int ap_track_registry_refresh(const char *cache) { reg_refreshes++; snprintf(reg_cache, sizeof reg_cache, "%s", cache); return 1; }
int ap_track_registry_status(int *source, char *detail, int n)
{
	if (source) *source = reg_source;
	if (detail && n > 0) detail[0] = 0;
	return reg_state;
}
uint32_t ap_track_registry_generation(void) { return reg_generation; }
/* The registry's one entry: LEV reg_lev, VRM REG_VRM, sizes 810876 and 458808. */
#define REG_VRM "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
int ap_track_registry_lookup(const char *lev, uint32_t lb, const char *vrm, uint32_t vb, struct ApTrackRegistryMatch *m)
{
	if (reg_result == AP_TRACK_REGISTRY_NO_INDEX) return reg_result;
	if (strcmp(lev, reg_lev) || strcmp(vrm, REG_VRM)) return AP_TRACK_REGISTRY_NOT_LISTED;
	if (lb != 810876 || vb != 458808) return AP_TRACK_REGISTRY_PIN_CONFLICT;
	if (reg_result == AP_TRACK_REGISTRY_MATCH && m)
	{
		memset(m, 0, sizeof *m);
		m->saphi_track_id = 121; m->lev_download_id = 872; m->vrm_download_id = 873;
	}
	return reg_result;
}
int ap_custom_track_download_start_registry(const char *tracks, const char *rootDir, int track, int levId, int vrmId,
                                            const char *lev, uint32_t levBytes, const char *vrm, uint32_t vrmBytes)
{
	(void)tracks;
	reg_starts++;
	snprintf(reg_root, sizeof reg_root, "%s", rootDir);
	reg_track = track; reg_lev_id = levId; reg_vrm_id = vrmId;
	snprintf(reg_pin_lev, sizeof reg_pin_lev, "%s", lev); snprintf(reg_pin_vrm, sizeof reg_pin_vrm, "%s", vrm);
	reg_lev_bytes = levBytes; reg_vrm_bytes = vrmBytes;
	dl_state = AP_CT_DOWNLOAD_RUNNING;
	return 1;
}
'''

tests = r'''
static int failures, checks;
static void expect(int ok, const char *what)
{
	checks++;
	if (!ok) { failures++; printf("FAIL: %s\n", what); }
}
static int entries(const char *dir)
{
	int n = 0; struct dirent *e; DIR *d = opendir(dir);
	if (!d) return -1;
	while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
	closedir(d);
	return n;
}
static void package(int i, const char *key, const char *rev, const struct CustomTrackManagerPackage *compiled,
                    unsigned lev, unsigned vrm, int scan)
{
	ctr_content_package *p = &packages[i];
	memset(p, 0, sizeof *p);
	p->locator_index = -1; /* the parser's value for a record without a locator */
	snprintf(p->id, sizeof p->id, "package/%s/%s", key, rev);
	snprintf(p->content_id, sizeof p->content_id, "%s", key);
	snprintf(p->revision, sizeof p->revision, "%s", rev);
	snprintf(p->display_name, sizeof p->display_name, "%s", compiled ? compiled->title : "Snowy Valley");
	snprintf(p->author, sizeof p->author, "%s", compiled ? compiled->author : "Pancho");
	snprintf(p->lev_sha256, sizeof p->lev_sha256, "%s", compiled ? compiled->levSha256 : "11");
	snprintf(p->vrm_sha256, sizeof p->vrm_sha256, "%s", compiled ? compiled->vrmSha256 : "22");
	p->lev_bytes = lev; p->vrm_bytes = vrm;
	compiled_for[i] = compiled;
	ap_content_package_scan[i] = scan + 1;
}
int main(void)
{
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	char message[256], tracks[300], path[512];
	int plan[AP_SEED_ROWS_MAX], count, n;
	uint64_t bytes;
	const struct CustomTrackManagerPackage *current = CustomTrackManager_BabyTParkCurrent();
	const struct CustomTrackManagerPackage *old = CustomTrackManager_BabyTPark();
	struct CustomTrackManagerPackage hostile = *current;

	snprintf(assets_root, sizeof assets_root, "/tmp/ctr-seed-hooks-XXXXXX");
	if (!mkdtemp(assets_root)) return 2;
	snprintf(tracks, sizeof tracks, "%s/tracks", assets_root); mkdir(tracks, 0755);

	/* 1. No plan. */
	plan_active = 0;
	expect(!AP_SeedContentActive() && AP_SeedContentRows(rows, AP_SEED_ROWS_MAX) == 0, "no content plan: page off, no rows");
	plan_active = 1;
	package_count = 0; plan_cortex = 0;
	expect(!AP_SeedContentActive(), "a plan without custom content keeps the old page");

	/* 2. Rows. */
	package(0, "baby-t-park", "1.0.2", current, 2470836, 115732, CTR_CT_MANAGER_NOT_INSTALLED);
	package(1, "snowy-valley", "1.0.0", NULL, 500000, 90000, CTR_CT_MANAGER_READY);
	package(2, "crash-cove-remix", "2.1.0", NULL, 300000, 70000, CTR_CT_MANAGER_HASH_MISMATCH);
	package_count = 3; plan_cortex = 1; ap_content_cortex_ready = 1;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(AP_SeedContentActive() && n == 4, "three packages and Cortex Vortex");
	expect(rows[0].source == AP_SEED_SOURCE_SAPHI && ap_seed_row_state(&rows[0]) == AP_SEED_STATE_MISSING &&
	       rows[0].lev_bytes == 2470836 && !strcmp(rows[0].author, "Lockheart") && !strcmp(rows[0].folder, "baby-t-park") &&
	       !strcmp(rows[0].revision, "1.0.2") && rows[0].index == 0 && rows[0].locator.bigfile_index == -1 &&
	       !rows[0].locator.patch_sha256[0], "compiled Baby T Park 1.0.2: Missing, from Saphi, with the room's pins");
	expect(rows[1].source == AP_SEED_SOURCE_MANUAL && ap_seed_row_state(&rows[1]) == AP_SEED_STATE_READY, "open package: Ready");
	/* xdelta importer: a record's locator reaches the row and its patch line. */
	packages[1].locator_index = 1;
	snprintf(packages[1].locator_sha256, sizeof packages[1].locator_sha256, "%s", "52a420e582fc44786210ae65817d7105c7d881c2f0b4387cebde2dce86064526");
	snprintf(packages[1].locator_name, sizeof packages[1].locator_name, "%s", "LRG2Pancho.xdelta");
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	{
		char locator[160];
		ap_seed_locator_line(&rows[1], locator, sizeof locator);
		expect(rows[1].locator.bigfile_index == 1 && !strcmp(locator, "Patch: LRG2Pancho.xdelta (52a420e5...) slot 1"),
		       "open package with a locator: the patch line names it");
	}
	packages[1].locator_index = -1; packages[1].locator_sha256[0] = packages[1].locator_name[0] = 0;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[2].source == AP_SEED_SOURCE_MANUAL && rows[2].scan == AP_SEED_SCAN_MISMATCH, "open package: other files");
	expect(rows[3].source == AP_SEED_SOURCE_BUNDLED && rows[3].index == -1 && ap_seed_row_state(&rows[3]) == AP_SEED_STATE_READY,
	       "Cortex Vortex last, bundled, Ready");
	expect(AP_SeedContentRows(rows, 2) == 2, "rows respect the caller's bound");
	expect(AP_SeedContentInstallPreview(plan, &count, &bytes, message, sizeof message) == AP_SEED_PLAN_OK &&
	       count == 1 && plan[0] == 0 && bytes == 2470836u + 115732u, "preview: one track, its pinned size");
	expect(starts == 0 && entries(tracks) == 0, "the preview changes nothing");

	/* 3. Refusals. */
	dl_state = AP_CT_DOWNLOAD_RUNNING; /* the package page's own download */
	expect(!AP_SeedContentInstallStart(message, sizeof message) && strstr(message, "already running") && starts == 0,
	       "a running download refuses the install");
	dl_state = AP_CT_DOWNLOAD_SUCCEEDED;
	ap_content_package_scan[0] = CTR_CT_MANAGER_READY + 1;
	expect(!AP_SeedContentInstallStart(message, sizeof message) && strstr(message, "instructions") && starts == 0,
	       "only a manual track missing: instructions, no download");
	ap_content_package_scan[0] = CTR_CT_MANAGER_NOT_INSTALLED + 1;
	hostile.id = "../../escape";
	compiled_for[0] = &hostile;
	expect(!AP_SeedContentInstallStart(message, sizeof message) && strstr(message, "no safe install folder") && starts == 0,
	       "a profile whose folder is unsafe refuses before any download");
	compiled_for[0] = current;
	refuse_start = 1;
	expect(!AP_SeedContentInstallStart(message, sizeof message) && strstr(message, "Nothing was installed") &&
	       !AP_SeedContentInstallBusy() && starts == 1, "a refused worker start leaves the queue idle");
	refuse_start = 0; starts = 0;
	expect(entries(tracks) == 0, "no folder after any refusal");

	/* 4. A clean queue of two compiled revisions. */
	package(3, "baby-t-park", "1.0.0", old, 2470000, 115000, CTR_CT_MANAGER_MISSING_FILES);
	package_count = 4;
	expect(AP_SeedContentInstallStart(message, sizeof message) && AP_SeedContentInstallBusy() && starts == 1,
	       "install starts the first download");
	snprintf(path, sizeof path, "%s/baby-t-park-1.0.2", tracks);
	expect(!strcmp(start_tracks, tracks) && !strcmp(start_root, path) && !strcmp(start_version, "1.0.2") &&
	       !strcmp(start_lev, current->levSha256) && !strcmp(start_vrm, current->vrmSha256) &&
	       start_lev_bytes == 2470836 && start_vrm_bytes == 115732, "first download: the room's exact pins, the 1.0.2 folder");
	expect(strstr(message, "(1 of 2)") != NULL, "progress names the position");
	expect(!AP_SeedContentInstallStart(message, sizeof message) && starts == 1, "a second start while one runs is refused");
	expect(AP_SeedContentInstallTick(message, sizeof message) == 1 && strstr(message, "1 of 2:") && entries(tracks) == 0,
	       "running: nothing created by the hooks");
	dl_state = AP_CT_DOWNLOAD_SUCCEEDED;
	expect(AP_SeedContentInstallTick(message, sizeof message) == 1 && starts == 2, "success starts the next track");
	snprintf(path, sizeof path, "%s/baby-t-park-1.0.2/extensions/navigation", tracks);
	expect(entries(path) == 0, "PrepareFolder ran for the installed revision after its download");
	snprintf(path, sizeof path, "%s/baby-t-park", tracks);
	expect(!strcmp(start_root, path) && !strcmp(start_version, "1.0.0") && start_lev_bytes == 2470000,
	       "second download: the 1.0.0 folder, its own pins; both revisions coexist");

	/* 5. Failure on the second stops the queue. */
	dl_state = AP_CT_DOWNLOAD_FAILED;
	snprintf(dl_detail, sizeof dl_detail, "Saphi lists another file size than this seed pins. Nothing was changed.");
	expect(AP_SeedContentInstallTick(message, sizeof message) == 0 && !AP_SeedContentInstallBusy() && starts == 2 && rescans == 1,
	       "a failure stops: no third start, one rescan for the installed track");
	expect(strstr(message, "Baby T Park: Saphi lists another file size") && strstr(message, "Installed 1 before it"),
	       "the message names the failure and what was installed");
	expect(AP_SeedContentInstallTick(message, sizeof message) == 0 && starts == 2, "an idle tick does nothing");

	/* A first-track failure installs nothing and does not rescan. */
	rescans = 0; starts = 0; dl_state = AP_CT_DOWNLOAD_IDLE;
	expect(AP_SeedContentInstallStart(message, sizeof message), "restart");
	dl_state = AP_CT_DOWNLOAD_FAILED;
	expect(AP_SeedContentInstallTick(message, sizeof message) == 0 && rescans == 0 && starts == 1 &&
	       !strstr(message, "Installed"), "first-track failure: no rescan, nothing claimed");

	/* 6. A room change stops the queue after the running download. */
	starts = 0; dl_state = AP_CT_DOWNLOAD_IDLE;
	expect(AP_SeedContentInstallStart(message, sizeof message), "restart for the room change");
	plan_epoch = 8;
	expect(AP_SeedContentInstallTick(message, sizeof message) == 1 && AP_SeedContentInstallBusy(),
	       "the running download is waited for");
	dl_state = AP_CT_DOWNLOAD_SUCCEEDED;
	expect(AP_SeedContentInstallTick(message, sizeof message) == 0 && !AP_SeedContentInstallBusy() && starts == 1 &&
	       strstr(message, "room changed"), "then the queue stops without another download");
	plan_epoch = 7;

	/* Unknown scan state is shown as needing a rescan, never as Ready. */
	ap_content_package_scan[1] = 0;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].scan == AP_SEED_SCAN_UNKNOWN && ap_seed_row_state(&rows[1]) == AP_SEED_STATE_ATTENTION, "unscanned is not Ready");
	ap_custom_content_scanned = 0;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[n - 1].scan == AP_SEED_SCAN_UNKNOWN, "Cortex Vortex before the first scan is not Ready");

	/* 8. The live track registry (row 10k). */
	ap_custom_content_scanned = 1;
	starts = 0; rescans = 0; dl_state = AP_CT_DOWNLOAD_IDLE;
	package(0, "baby-t-park", "1.0.2", current, 2470836, 115732, CTR_CT_MANAGER_READY);
	package(1, "acid-lake", "1.0.0", NULL, 810876, 458808, CTR_CT_MANAGER_NOT_INSTALLED);
	package_count = 2; plan_cortex = 0;
#define REG_LEV "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	snprintf(packages[1].lev_sha256, sizeof packages[1].lev_sha256, "%s", REG_LEV);
	snprintf(packages[1].vrm_sha256, sizeof packages[1].vrm_sha256, "%s", REG_VRM);
	snprintf(reg_lev, sizeof reg_lev, REG_LEV);
	plan_epoch = 20; reg_refreshes = 0;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	snprintf(path, sizeof path, "%s/track-registry-cache.json", assets_root);
	expect(reg_refreshes == 1 && !strcmp(reg_cache, path), "a missing open package fetches the registry, cache in assets");
	expect(rows[1].source == AP_SEED_SOURCE_MANUAL && rows[1].registry == AP_SEED_REGISTRY_CHECKING &&
	       strstr(ap_seed_source_text(&rows[1]), "checking"), "while it loads: no download yet, checking");
	reg_state = AP_TRACK_REGISTRY_LOADING;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(reg_refreshes == 1, "not asked again for the same room");
	/* Offline, no cache. */
	reg_state = AP_TRACK_REGISTRY_UNAVAILABLE;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].source == AP_SEED_SOURCE_MANUAL && rows[1].registry == AP_SEED_REGISTRY_OFFLINE &&
	       !strcmp(ap_seed_source_text(&rows[1]), "No download (registry unavailable)"), "offline without cache: manual, says why");
	expect(!AP_SeedContentInstallStart(message, sizeof message) && reg_starts == 0 && starts == 0,
	       "offline without cache: no download starts");
	/* Rescan asks again. */
	AP_SeedContentRegistryRetry();
	expect(reg_refreshes == 2, "Rescan fetches the registry again");
	/* A live match. */
	reg_state = AP_TRACK_REGISTRY_DONE; reg_source = AP_TRACK_REGISTRY_SOURCE_LIVE;
	reg_result = AP_TRACK_REGISTRY_MATCH; reg_generation++;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].source == AP_SEED_SOURCE_SAPHI && rows[1].registry == AP_SEED_REGISTRY_LIVE &&
	       !strcmp(ap_seed_source_text(&rows[1]), "Project Saphi (registry)"), "registry match: a Saphi download");
	expect(rows[0].registry == AP_SEED_REGISTRY_NONE && !strcmp(ap_seed_source_text(&rows[0]), "Project Saphi"),
	       "the compiled profile stays as it was");
	/* Cached copy: said on screen. */
	reg_source = AP_TRACK_REGISTRY_SOURCE_CACHED;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].registry == AP_SEED_REGISTRY_CACHED && strstr(ap_seed_source_text(&rows[1]), "cached registry"),
	       "a cached-copy match says so");
	/* Install: the registry download with the room's pins and the key folder. */
	expect(AP_SeedContentInstallPreview(plan, &count, &bytes, message, sizeof message) == AP_SEED_PLAN_OK && count == 1 &&
	       plan[0] == 1, "preview queues the registry track");
	expect(AP_SeedContentInstallStart(message, sizeof message) && reg_starts == 1 && starts == 0,
	       "the install starts the registry download, not the compiled one");
	snprintf(path, sizeof path, "%s/acid-lake", tracks);
	expect(!strcmp(reg_root, path) && reg_track == 121 && reg_lev_id == 872 && reg_vrm_id == 873 &&
	       !strcmp(reg_pin_lev, REG_LEV) && !strcmp(reg_pin_vrm, REG_VRM) && reg_lev_bytes == 810876 && reg_vrm_bytes == 458808,
	       "registry download: tracks/<key>, the located ids, the room's own pins");
	expect(strstr(log_text, "cached track registry") && strstr(log_text, "Saphi track 121"), "the log names the cached source");
	dl_state = AP_CT_DOWNLOAD_SUCCEEDED;
	expect(AP_SeedContentInstallTick(message, sizeof message) == 0 && rescans == 1 && entries(path) == -1,
	       "success rescans; the hooks create no folder for a registry track");
	/* Not listed, not on Saphi: manual with the reason. */
	dl_state = AP_CT_DOWNLOAD_IDLE;
	reg_result = AP_TRACK_REGISTRY_NOT_ON_SAPHI; reg_generation++;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].source == AP_SEED_SOURCE_MANUAL && rows[1].registry == AP_SEED_REGISTRY_NOT_ON_SAPHI, "not on Saphi: manual");
	snprintf(reg_lev, sizeof reg_lev, "other"); reg_result = AP_TRACK_REGISTRY_MATCH; reg_generation++;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].registry == AP_SEED_REGISTRY_NOT_LISTED && rows[1].source == AP_SEED_SOURCE_MANUAL &&
	       !strcmp(ap_seed_source_text(&rows[1]), "No download (not registered)"), "other hashes: not registered");
	/* An unsafe track key refuses before any download. */
	/* A size off the pin: the registry disagrees with the room, no download. */
	snprintf(reg_lev, sizeof reg_lev, REG_LEV); reg_generation++;
	packages[1].vrm_bytes = 458809;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(rows[1].registry == AP_SEED_REGISTRY_CONFLICT && rows[1].source == AP_SEED_SOURCE_MANUAL, "size off the pin: manual");
	packages[1].vrm_bytes = 458808; reg_generation++;
	snprintf(packages[1].content_id, sizeof packages[1].content_id, "%s", "../escape");
	reg_starts = 0;
	expect(!AP_SeedContentInstallStart(message, sizeof message) && reg_starts == 0, "an unsafe key refuses the install");
	snprintf(packages[1].content_id, sizeof packages[1].content_id, "%s", "acid-lake");
	/* Live this session and a new room: no new fetch; all open packages Ready: none either. */
	reg_source = AP_TRACK_REGISTRY_SOURCE_LIVE; plan_epoch = 21; reg_refreshes = 0;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(reg_refreshes == 0, "the live copy of this session serves the next room");
	reg_source = AP_TRACK_REGISTRY_SOURCE_CACHED; plan_epoch = 22;
	ap_content_package_scan[1] = CTR_CT_MANAGER_READY + 1;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(reg_refreshes == 0, "no fetch when every open package is Ready");
	ap_content_package_scan[1] = CTR_CT_MANAGER_NOT_INSTALLED + 1; plan_epoch = 23;
	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	expect(reg_refreshes == 1, "a cached copy is refreshed for a new room");

	snprintf(path, sizeof path, "rm -rf '%s'", assets_root);
	if (system(path) != 0) return 2;
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "seed_hooks.c", Path(tmp) / "seed_hooks"
    src.write_text(prelude + stubs + hooks + tests, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-format-truncation",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    "-I" + str(root), "-I" + str(root / "include"), "-I" + str(root / "ap"),
                    str(src), "-o", str(exe)], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
print("PASS: Current Seed hooks: rows and sources, refusals before any download or folder, exact pins per "
      "revision, folder setup only after success, failure and room-change stops, registry rows (live, cached, "
      "offline, not listed) and the registry download with the room's pins")
