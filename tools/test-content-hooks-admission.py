"""Open package admission through the production hooks (#133 PR 2 review).

Compiles AP_ContentScanPackage, AP_ContentPadReady and AP_ContentPreparePad
out of the ap_hooks unit together with the real structural verifier
(platform/native_custom_content_verify.c), the real track manager
(platform/native_custom_track_manager.c, which locates open packages by
digest), the real capability check (ap/ap_content_capability.h) and the real
loader and race stager. The seed side (content-plan records) uses stubs.
Fixture LEVs come from the build_lev helper of
tools/test-custom-content-verify.c; no third-party track asset is used.

Cases: an honest capability claim is Ready and enters with the measured flags;
a false claim is refused at connect and at pad entry and clears the ready flag;
a same-size file swap after admission is refused at pad entry; a swap between
locating and measuring is caught by the pin compare.

Cortex Vortex as a content-plan track (#133 PR 3c) uses the bundled pair the
repository ships under assets/tracks/cortex-vortex (the legacy pad track's
files, with their packaging permission): Ready only with the pinned bytes,
measured for all five capabilities, entered on host 13 as a custom-bytes
standalone race with no custom slot; missing or changed bytes are refused.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
unit = ap_hooks_unit.read()
start = unit.index("static int ap_content_package_index;")
hooks = unit[start:unit.index("static void AP_CustomContentBuildRequirement(", start)]

vehicle_birth = (root / "game/Vehicle/VehBirth.c").read_text(encoding="utf-8")
start = vehicle_birth.index("static void VehBirth_SetBottomFromPos(")
spawn_entry = vehicle_birth[start:vehicle_birth.index("static int VehBirth_ScaleTrig(", start)]

verify_test = (root / "tools/test-custom-content-verify.c").read_text(encoding="utf-8")
lev_start = verify_test.index("#define BODY_INST")
lev_helper = verify_test[lev_start:verify_test.index("/* The Arcade and Time Trial verdicts", lev_start)]

prelude = r'''
#define _XOPEN_SOURCE 700
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static char log_text[8192];
void AP_LogLine(const char *msg) { strncat(log_text, msg, sizeof log_text - strlen(log_text) - 1); }
static void AP_AppendLog(const char *msg) { AP_LogLine(msg); }
#include "platform/native_custom_tracks.c"
#include "platform/native_custom_content_verify.c"
#include "platform/native_custom_track_manager.c"
#include "ap/ap_content_capability.h"
#include <platform/native_custom_race_context.h>
#include <platform/native_custom_tracks_policy.h>
struct sData sdata_static;
NativeConfig g_config;
'''

stubs = r'''
static char assets_root[CTR_CT_MANAGER_PATH_MAX];
const char *NativeAssets_GetAssetDir(void) { return assets_root; }
static ctr_content_package plan_package;
static ctr_content_pad plan_pad;
static int plan_has_package = 1;
int AP_TrackFillPrepare(void) { return 1; }
static int retail_letters_ready = 1;
int AP_TrialLetters_Prepare(void) { return retail_letters_ready; }
void AP_FeedNoticeLine(const char *text) { AP_LogLine(text); }
const ctr_content_package *ap_content_plan_package(int index) { return index == 0 && plan_has_package ? &plan_package : NULL; }
const char *ap_content_plan_package_json(int index) { return index == 0 && plan_has_package ? "{}" : ""; }
/* Play Solo keeps the descriptor of a verified package (#89). */
static int solo_descriptors_kept;
static int solo_publish_ok = 1;
int AP_Solo_RecordTrackDescriptor(const char *contentId, const char *revision, const char *json)
{
	(void)contentId; (void)revision; (void)json;
	solo_descriptors_kept++;
	return solo_publish_ok;
}
const ctr_content_pad *ap_content_plan_pad(int physical) { return physical == plan_pad.physical ? &plan_pad : NULL; }
uint64_t ap_content_plan_epoch(void) { return 7; }
int AP_ContentPadOpen(const ctr_content_pad *pad) { return pad != NULL; }
int AP_ContentModeOpen(const ctr_content_pad *pad, int mode) { return pad != NULL && ap_content_mode_slot(pad, mode) >= 0; }
static struct CustomTrackManagerStatus ap_custom_content_status;
static int ap_custom_content_required;
static int ap_cortex_track_content_required;
#include "include/platform/native_cortex_asset_pins.h"
static unsigned ap_state_gen;
static int used_managed, used_files, applied, staged;
static int refuse_files;
static char used_lev[CTR_CT_MANAGER_PATH_MAX], used_vrm[CTR_CT_MANAGER_PATH_MAX], used_name[CTR_CT_NAME_MAX];
static struct CustomTrackSeedDescriptor applied_descriptor;
static struct CustomTrackRaceContext staged_race;
uint64_t ap_content_plan_room_identity(void) { return UINT64_C(0x123456789abcdef0); }
/* A compiled managed profile standing in for one whose pins the room matches
   (the real one, Baby T Park, has no relic_files): its main pair is the files
   fake_main_lev and fake_main_vrm, loaded through the manager. */
static int fake_profile_on;
static struct CustomTrackManagerPackage fake_profile;
static char fake_main_lev[CTR_CT_MANAGER_PATH_MAX], fake_main_vrm[CTR_CT_MANAGER_PATH_MAX];
static const struct CustomTrackManagerPackage *harness_find(const char *id, const char *uuid, const char *version,
                                                            const char *lev, const char *vrm)
{
	if (fake_profile_on) return &fake_profile;
	return CustomTrackManager_FindPackage(id, uuid, version, lev, vrm);
}
static int harness_scan(const char *assetsRoot, const struct CustomTrackManagerPackage *p, struct CustomTrackManagerStatus *s)
{
	if (!fake_profile_on) return CustomTrackManager_ScanPackage(assetsRoot, p, s);
	memset(s, 0, sizeof *s);
	s->state = CTR_CT_MANAGER_READY;
	snprintf(s->levPath, sizeof s->levPath, "%s", fake_main_lev);
	snprintf(s->vrmPath, sizeof s->vrmPath, "%s", fake_main_vrm);
	return s->state;
}
static int record_managed(const struct CustomTrackManagerPackage *p, struct CustomTrackManagerStatus *s)
{
	used_managed++;
	if (fake_profile_on) return 1;
	return CustomTrack_UseManagedPackage(p, s);
}
static int record_files(const char *lev, const char *vrm, const char *name)
{
	used_files++;
	snprintf(used_lev, sizeof used_lev, "%s", lev); snprintf(used_vrm, sizeof used_vrm, "%s", vrm);
	snprintf(used_name, sizeof used_name, "%s", name);
	if (refuse_files) return 0;
	return CustomTrack_UseContentFiles(lev, vrm, name);
}
static int record_descriptor(const struct CustomTrackSeedDescriptor *d)
{
	applied++;
	applied_descriptor = *d;
	return CustomTrack_ApplySeedDescriptor(d);
}
static int record_staging(const struct CustomTrackRaceContext *c)
{
	if (!CustomTrack_StageStandaloneRace(c)) return 0;
	staged++;
	staged_race = *c;
	return 1;
}
#define CustomTrack_UseManagedPackage record_managed
#define CustomTrackManager_FindPackage harness_find
#define CustomTrackManager_ScanPackage harness_scan
#define CustomTrack_UseContentFiles record_files
#define CustomTrack_ApplySeedDescriptor record_descriptor
#define CustomTrack_StageStandaloneRace record_staging
/* Between locating the files and measuring them: optionally replace the LEV. */
static const unsigned char *swap_after_locate;
static size_t swap_bytes;
static char swap_path[CTR_CT_MANAGER_PATH_MAX];
static int harness_locate(const char *assetsRoot, const char *trackKey, const char *title, const char *revision,
                          const char *levSha256, unsigned long levBytes, const char *vrmSha256,
                          unsigned long vrmBytes, struct CustomTrackManagerStatus *status)
{
	int state = CustomTrackManager_LocateContentFiles(assetsRoot, trackKey, title, revision, levSha256, levBytes,
	                                                  vrmSha256, vrmBytes, status);
	if (swap_after_locate) { write_bytes(swap_path, swap_after_locate, swap_bytes); swap_after_locate = NULL; }
	return state;
}
#define CustomTrackManager_LocateContentFiles harness_locate
'''

tests = r'''
#undef CustomTrackManager_LocateContentFiles
static void reset_calls(void)
{
	CustomTrack_OnRaceLoadRequested(-1);
	CustomTrack_ClearSeedDescriptor();
	used_managed = used_files = applied = staged = 0;
	log_text[0] = 0;
}
/* Use a longer spelling of the same real directory and its verified files. */
static void lengthen_assets_root(void)
{
	size_t n = strlen(assets_root);
	while (n < CUSTOM_TRACK_PATH_MAX)
	{
		assets_root[n++] = '/';
		assets_root[n++] = '.';
	}
	assets_root[n] = 0;
}
/* Connect and Rescan: the content-plan branch of AP_CustomContentPreflightSeed. */
static int preflight(void)
{
	struct CustomTrackManagerStatus status;
	ap_content_package_ready[0] = 0;
	ap_content_package_ready[0] = AP_ContentScanPackage(0, 1, &status);
	ap_custom_content_status = status;
	ap_custom_content_required = !ap_content_package_ready[0];
	return ap_content_package_ready[0];
}
int main(void)
{
	char dir[512], lev[600], vrm[600], error[256];
	char short_root[sizeof assets_root];
	unsigned char image[IMAGE_BYTES], changed[IMAGE_BYTES];
	unsigned char vrmBytes[22] = {0};
	struct CustomContentVerification report;
	unsigned measured;
	size_t levSize;
	snprintf(assets_root, sizeof assets_root, "/tmp/ctr-content-hooks-XXXXXX");
	if (!mkdtemp(assets_root)) return 2;
	strcpy(short_root, assets_root);
	snprintf(dir, sizeof dir, "%s/tracks", assets_root); mkdir(dir, 0700);
	snprintf(dir, sizeof dir, "%s/tracks/fixture-loop", assets_root); mkdir(dir, 0700);
	snprintf(dir, sizeof dir, "%s/tracks/fixture-loop/original", assets_root); mkdir(dir, 0700);
	snprintf(lev, sizeof lev, "%s/any-name.lev", dir);
	snprintf(vrm, sizeof vrm, "%s/any-name.vrm", dir);
	levSize = build_lev(image, 1);
	write_bytes(lev, image, levSize);
	vrmBytes[16] = 1; vrmBytes[18] = 1;
	write_bytes(vrm, vrmBytes, sizeof vrmBytes);
	expect(CustomContentVerify_Files(lev, vrm, &report, error, sizeof error), "fixture pair verifies");
	measured = AP_ContentMeasuredCapabilities(&report);
	expect(measured == (AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE | AP_CONTENT_CAP_RELIC | AP_CONTENT_CAP_CTR_TOKENS),
	       "fixture measures Time Trial, Arcade, relic crates and letters, no Wumpa");

	memset(&plan_package, 0, sizeof plan_package);
	snprintf(plan_package.id, sizeof plan_package.id, "package/fixture-loop/2.1");
	snprintf(plan_package.content_id, sizeof plan_package.content_id, "fixture-loop");
	snprintf(plan_package.uuid, sizeof plan_package.uuid, "0f0e0d0c-0b0a-4908-8706-050403020100");
	snprintf(plan_package.revision, sizeof plan_package.revision, "2.1");
	snprintf(plan_package.display_name, sizeof plan_package.display_name, "Fixture Loop");
	snprintf(plan_package.author, sizeof plan_package.author, "Fixture Author");
	snprintf(plan_package.lev_sha256, sizeof plan_package.lev_sha256, "%s", report.levSha256);
	snprintf(plan_package.vrm_sha256, sizeof plan_package.vrm_sha256, "%s", report.vrmSha256);
	plan_package.lev_bytes = (uint32_t)levSize; plan_package.vrm_bytes = sizeof vrmBytes;
	plan_package.laps = 3;
	plan_package.weapon_crates = AP_CONTENT_WEAPON_CRATES_UNSTATED; /* #133 PR 4e: unstated unless a case says */
	plan_package.checkpoints = AP_CONTENT_UNSTATED; /* #133 PR 6b: unstated unless a case says */
	plan_package.ai_nav = AP_CONTENT_UNSTATED;
	memset(&plan_pad, 0, sizeof plan_pad);
	plan_pad.physical = 3; plan_pad.hub = 26; plan_pad.occupied = 1; plan_pad.retail_id = -1;
	plan_pad.package_index = 0; plan_pad.custom_slot = 1; plan_pad.laps = 3; plan_pad.named_block = -1;
	plan_pad.trophy_location = 35016300;
	plan_pad.mode_count = 1; plan_pad.modes[0] = AP_CONTENT_MODE_TROPHY; plan_pad.stages[0] = 1;
	snprintf(plan_pad.entry_id, sizeof plan_pad.entry_id, "entry/custom/fixture-loop/2.1");
	snprintf(plan_pad.track_id, sizeof plan_pad.track_id, "custom/fixture-loop/2.1");

	/* Honest claim: less than measured is fine (no relic claim). */
	plan_package.capabilities = AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE | AP_CONTENT_CAP_CTR_TOKENS;
	reset_calls();
	expect(preflight() && AP_ContentPadReady(3) && !ap_custom_content_required, "honest claim is Ready at connect");
	expect(solo_descriptors_kept > 0, "a verified package's descriptor is kept for Play Solo");
	solo_publish_ok = 0;
	expect(AP_ContentScanPackage(0, 0, &ap_custom_content_status) &&
	       !ap_content_descriptor_publication[0] && ap_custom_content_status.state == CTR_CT_MANAGER_READY,
	       "descriptor publication failure is reported separately from race-ready bytes");
	solo_publish_ok = 1;
	expect(AP_ContentScanPackage(0, 0, &ap_custom_content_status) && ap_content_descriptor_publication[0],
	       "rescan recovers after publication succeeds");
	expect(strcmp(ap_content_files[0].levPath, lev) == 0 && strcmp(ap_content_files[0].vrmPath, vrm) == 0,
	       "open package located by digest under its track key");
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6, "honest claim enters on host level 6");
	expect(used_files == 1 && used_managed == 0 && strcmp(used_lev, lev) == 0 && strcmp(used_vrm, vrm) == 0 &&
	       strcmp(used_name, "Fixture Loop") == 0, "loader gets the located files and the race name");
	expect(applied == 1 && applied_descriptor.standalone && applied_descriptor.laps == 3 &&
	       applied_descriptor.flagRelicCrates == 1 && applied_descriptor.flagAiNav == 1 && applied_descriptor.flagCtrLetters == 1 &&
	       applied_descriptor.flagSpawns == 8 && applied_descriptor.flagCheckpoints == 35 &&
	       applied_descriptor.flagWumpaCollectible == 0 &&
	       strcmp(applied_descriptor.levSha256, plan_package.lev_sha256) == 0,
	       "loader descriptor carries the measured flags, not the claim");
	expect(staged == 1 && staged_race.laps == 3 && staged_race.trophyLocation == 35016300 && staged_race.physicalPad == 3 &&
	       staged_race.returnHub == 26 && staged_race.seedEpoch == 7 && !staged_race.retail &&
	       strcmp(staged_race.levSha256, plan_package.lev_sha256) == 0, "race context staged for the pad");
	expect(staged_race.roomIdentity == 0x123456789abcdef0ULL &&
	       strcmp(staged_race.packageRevision, plan_package.revision) == 0,
	       "staged race retains the room and package revision for quick saves");
	reset_calls();
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6 &&
	       staged_race.roomIdentity == 0x123456789abcdef0ULL,
	       "re-entering the same pad uses the admitted room without a socket dependency");

	reset_calls();
	refuse_files = 1;
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !AP_ContentPadReady(3) &&
	       ap_custom_content_required && ap_content_package_scan[0] == CTR_CT_MANAGER_LOAD_FAILED + 1 &&
	       used_files == 1 && applied == 0 && staged == 0, "loader refusal updates both pad and seed readiness");
	refuse_files = 0;
	expect(preflight() && AP_ContentPadReady(3), "a new verification can restore readiness");
	{
		lengthen_assets_root();
		expect(!preflight() && !AP_ContentPadReady(3) &&
		       ap_content_package_scan[0] == CTR_CT_MANAGER_PATH_TOO_LONG + 1,
		       "verified files at an overlong package path are refused at preflight");
		expect(!preflight() && ap_custom_content_status.state == CTR_CT_MANAGER_PATH_TOO_LONG,
		       "rescanning the same long path leaves the package refused");
		strcpy(assets_root, short_root);
		expect(preflight() && AP_ContentPadReady(3), "rescanning a shorter path restores package readiness");
		struct CustomTrackSeedDescriptor noGrid = applied_descriptor;
		noGrid.flagAiNav = 0;
		expect(AP_ContentDescriptorRefusal(&noGrid) == CTR_CT_MANAGER_RACE_GRID_UNSUPPORTED,
		       "a verified pair without AI paths gets a grid refusal");
		noGrid.soloRace = 1;
		expect(AP_ContentDescriptorRefusal(&noGrid) == CTR_CT_MANAGER_LOAD_FAILED,
		       "a solo race does not require an opponent grid");
	}

	/* A4-70: a pad whose Relic Race runs other laps than its Trophy Race and CTR
	   Challenge stages each mode's own laps in the race context. */
	{
		ctr_content_pad saved = plan_pad;
		plan_package.capabilities |= AP_CONTENT_CAP_RELIC;
		plan_pad.laps = 2; plan_pad.relic_laps = 3;
		plan_pad.mode_count = 3; plan_pad.modes[0] = AP_CONTENT_MODE_TROPHY; plan_pad.modes[1] = AP_CONTENT_MODE_CTR;
		plan_pad.modes[2] = AP_CONTENT_MODE_RELIC; plan_pad.stages[1] = plan_pad.stages[2] = 1;
		plan_pad.ctr_location = 35016301; plan_pad.relic_route = plan_pad.relic_locations[0] = 35016302;
		plan_pad.relic_locations[1] = 35016303; plan_pad.relic_locations[2] = 35016304;
		reset_calls();
		expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_RELIC) == 6 && staged == 1 &&
		       staged_race.mode == AP_CONTENT_MODE_RELIC && staged_race.laps == 3, "A4-70: the Relic Race stages the relic laps");
		reset_calls();
		expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6 && staged == 1 && staged_race.laps == 2,
		       "A4-70: the Trophy Race stays on the pad's laps");
		reset_calls();
		expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_CTR) == 6 && staged == 1 && staged_race.laps == 2,
		       "A4-70: the CTR Challenge stays on the pad's laps");
		plan_pad.placed_letters = 1;
		retail_letters_ready = 0;
		reset_calls();
		expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_CTR) == -1 && staged == 0,
		       "a placed letter challenge refuses entry without its models and collision boxes");
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6 && staged == 1,
		       "missing letter assets leave the Trophy Race available");
		retail_letters_ready = 1;
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_CTR) == 6 && staged == 1,
		       "a placed letter challenge can enter once the assets are ready");
		plan_pad.relic_laps = 0; /* an A4-13 pad: the Relic Race stays on the pad's two laps */
		reset_calls();
		expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_RELIC) == 6 && staged == 1 && staged_race.laps == 2,
		       "A4-70: without relic_laps the Relic Race keeps the pad's laps");
		plan_pad = saved;
		plan_package.capabilities &= ~AP_CONTENT_CAP_RELIC;
		reset_calls();
		expect(preflight(), "A4-70 case leaves the fixture as it was");
	}

	/* Super turbo pads (#133 PR 4c): the fixture LEV has no quad blocks, so
	   a stated true is refused at connect and at pad entry; false and
	   unstated are admitted. */
	plan_package.super_turbo_pads = 1;
	reset_calls();
	expect(!preflight() && !AP_ContentPadReady(3) && ap_custom_content_required &&
	       strstr(ap_custom_content_status.detail, "super_turbo_pads is true") &&
	       strstr(log_text, "content plan refused package/fixture-loop/2.1"), "super pads stated on a track without refused at connect");
	ap_content_package_ready[0] = 1;
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_package_ready[0] && used_files == 0,
	       "super pads stated on a track without refused at pad entry");
	plan_package.super_turbo_pads = AP_CONTENT_SUPER_PADS_UNSTATED;
	reset_calls();
	expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6, "unstated super pads admitted");
	plan_package.super_turbo_pads = 0;
	reset_calls();
	expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6, "super pads stated false and absent admitted");

	/* Weapon crates (#133 PR 4e): the fixture LEV holds one weapon crate. A
	   stated count other than 1 is refused at connect and at pad entry; 1 and
	   unstated are admitted. */
	plan_package.weapon_crates = 2;
	reset_calls();
	expect(!preflight() && !AP_ContentPadReady(3) && ap_custom_content_required &&
	       strstr(ap_custom_content_status.detail, "weapon_crates is 2") &&
	       strstr(log_text, "content plan refused package/fixture-loop/2.1"), "a wrong weapon crate count refused at connect");
	ap_content_package_ready[0] = 1;
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_package_ready[0] && used_files == 0,
	       "a wrong weapon crate count refused at pad entry");
	plan_package.weapon_crates = 0;
	reset_calls();
	expect(!preflight() && strstr(ap_custom_content_status.detail, "weapon_crates is 0"),
	       "no weapon crates stated on a track with one refused");
	plan_package.weapon_crates = 1;
	reset_calls();
	expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6, "the measured weapon crate count admitted");
	plan_package.weapon_crates = AP_CONTENT_WEAPON_CRATES_UNSTATED;

	/* Boss venue facts (#133 PR 6b): a stated restart point count or AI
	   navigation flag the fixture's bytes disagree with is refused at connect;
	   the measured values and unstated are admitted. */
	plan_package.checkpoints = (int)report.measured.checkpoints + 1;
	reset_calls();
	expect(!preflight() && strstr(ap_custom_content_status.detail, "checkpoints is"),
	       "a wrong restart point count refused at connect");
	plan_package.checkpoints = (int)report.measured.checkpoints;
	plan_package.ai_nav = report.measured.navPaths > 0 ? 0 : 1;
	reset_calls();
	expect(!preflight() && strstr(ap_custom_content_status.detail, "ai_nav is"),
	       "a wrong AI navigation flag refused at connect");
	plan_package.ai_nav = report.measured.navPaths > 0;
	reset_calls();
	expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6, "the measured boss venue facts admitted");
	plan_package.checkpoints = AP_CONTENT_UNSTATED;
	plan_package.ai_nav = AP_CONTENT_UNSTATED;

	/* False claim: Wumpa is not in the bytes. Refused at connect and at pad entry. */
	plan_package.capabilities |= AP_CONTENT_CAP_WUMPA;
	reset_calls();
	expect(!preflight() && !AP_ContentPadReady(3) && ap_custom_content_required, "false claim refused at connect");
	expect(ap_custom_content_status.state == CTR_CT_MANAGER_UNSUPPORTED && strstr(ap_custom_content_status.detail, "wumpa") &&
	       strstr(log_text, "content plan refused package/fixture-loop/2.1"), "refusal names the missing capability and is logged");
	expect(ap_content_files[0].levPath[0] == 0, "a refused package keeps no located paths");
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && used_files == 0 && applied == 0 && staged == 0, "false claim never reaches the loader");
	/* Ready at connect, then the claim check runs again at pad entry. */
	ap_content_package_ready[0] = 1; ap_state_gen = 0;
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_package_ready[0] && ap_custom_content_required && ap_state_gen == 1 &&
	       used_files == 0 && applied == 0, "pad entry refuses a false claim and clears the ready flag");

	/* File swap after admission: same size, different bytes. */
	plan_package.capabilities = AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE;
	reset_calls();
	expect(preflight() && AP_ContentPadReady(3), "admitted before the swap");
	memcpy(changed, image, levSize); changed[4 + 0xF00] ^= 0x01; /* padding: still a valid LEV */
	write_bytes(lev, changed, levSize);
	expect(CustomContentVerify_Files(lev, vrm, &report, error, sizeof error) &&
	       strcmp(report.levSha256, plan_package.lev_sha256) != 0, "swapped LEV is a valid LEV with another hash");
	ap_custom_content_required = 0; ap_state_gen = 0;
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_package_ready[0] && ap_custom_content_required && ap_state_gen == 1,
	       "same-size swap after admission refused at pad entry");
	expect(ap_custom_content_status.state != CTR_CT_MANAGER_READY && used_files == 0 && applied == 0 && staged == 0,
	       "swapped files never reach the loader");
	write_bytes(lev, image, levSize);
	expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6, "restoring the pinned bytes admits again");

	/* Swap between locating and measuring: the pin compare catches it. */
	reset_calls();
	ap_content_package_ready[0] = 1;
	swap_after_locate = changed; swap_bytes = levSize;
	snprintf(swap_path, sizeof swap_path, "%s", lev);
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_package_ready[0] && used_files == 0 && applied == 0,
	       "a swap between locate and measure is refused");
	expect(strstr(ap_custom_content_status.detail, "failed the file check") != NULL, "the pin mismatch is the reason");
	write_bytes(lev, image, levSize);

	/* Cortex Vortex (#133 PR 3c): the bundled pair, copied from the repository. */
	{
		ctr_content_pad saved = plan_pad;
		char cvDir[600], cvLev[700], cvVrm[700], cvRelicLev[700], cvRelicVrm[700], cmd[3072];
		snprintf(cvDir, sizeof cvDir, "%s/tracks/cortex-vortex", assets_root); mkdir(cvDir, 0700);
		snprintf(cvLev, sizeof cvLev, "%s/" CTR_CFG_CORTEX_LEV_FILE, cvDir);
		snprintf(cvVrm, sizeof cvVrm, "%s/" CTR_CFG_CORTEX_VRM_FILE, cvDir);
		snprintf(cvRelicLev, sizeof cvRelicLev, "%s/" CTR_CFG_CORTEX_RELIC_LEV_FILE, cvDir);
		snprintf(cvRelicVrm, sizeof cvRelicVrm, "%s/" CTR_CFG_CORTEX_RELIC_VRM_FILE, cvDir);
		memset(&plan_pad, 0, sizeof plan_pad);
		plan_pad.physical = 5; plan_pad.hub = 27; plan_pad.occupied = 1; plan_pad.retail_id = -1; plan_pad.cortex = 1;
		plan_pad.package_index = -1; plan_pad.custom_slot = 0; plan_pad.laps = 3; plan_pad.named_block = -1;
		plan_pad.mode_count = 3; plan_pad.modes[0] = AP_CONTENT_MODE_RELIC; plan_pad.modes[1] = AP_CONTENT_MODE_TROPHY;
		plan_pad.modes[2] = AP_CONTENT_MODE_CTR; plan_pad.stages[0] = plan_pad.stages[1] = plan_pad.stages[2] = 1;
		plan_pad.trophy_location = 35026000; plan_pad.ctr_location = 35026004;
		plan_pad.relic_locations[0] = 35026001; plan_pad.relic_locations[1] = 35026002; plan_pad.relic_locations[2] = 35026003;
		snprintf(plan_pad.entry_id, sizeof plan_pad.entry_id, "entry/cortex_vortex");
		snprintf(plan_pad.track_id, sizeof plan_pad.track_id, "cortex_vortex");
		reset_calls();
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(!ap_content_cortex_ready && !AP_ContentPadReady(5) && strstr(log_text, "refused Cortex Vortex"),
		       "missing bundled pair: Cortex Vortex is not Ready");
		ap_cortex_track_content_required = 0;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == -1 && used_files == 0 && staged == 0,
		       "missing bundled pair: the pad refuses entry");
		ap_content_cortex_ready = 1; /* stale flag from an earlier scan: entry scans again */
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == -1 && !ap_content_cortex_ready &&
		       ap_cortex_track_content_required && used_files == 0, "entry rescans and refuses missing files");
		snprintf(cmd, sizeof cmd, "cp '%s/assets/tracks/cortex-vortex/CVortex Arcade All.lev' '%s' && "
		         "cp '%s/assets/tracks/cortex-vortex/CVortex Arcade All.vrm' '%s'", CTR_REPO_ROOT, cvLev, CTR_REPO_ROOT, cvVrm);
		expect(system(cmd) == 0, "bundled main pair copied");
		/* CV1: the merged mode build needs its relic pair too. */
		reset_calls();
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(!ap_content_cortex_ready && !AP_ContentPadReady(5) && strstr(log_text, "Relic Race files"),
		       "bundled main pair without the relic pair is not Ready");
		snprintf(cmd, sizeof cmd, "cp '%s/assets/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_LEV_FILE "' '%s' && "
		         "cp '%s/assets/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_VRM_FILE "' '%s'",
		         CTR_REPO_ROOT, cvRelicLev, CTR_REPO_ROOT, cvRelicVrm);
		expect(system(cmd) == 0, "bundled relic pair copied");
		/* Swapped pairs: each pair is checked against its own pins. */
		snprintf(cmd, sizeof cmd, "cp '%s' '%s.main' && cp '%s' '%s'", cvLev, cvLev, cvRelicLev, cvLev);
		expect(system(cmd) == 0, "relic LEV put in the main LEV's place");
		reset_calls();
		expect(!AP_ContentScanCortex() && strstr(log_text, "not the pinned pair"), "a relic LEV in the main place is refused");
		snprintf(cmd, sizeof cmd, "mv '%s.main' '%s'", cvLev, cvLev);
		expect(system(cmd) == 0, "main LEV restored");
		reset_calls();
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(ap_content_cortex_ready && AP_ContentPadReady(5), "pinned bundled pairs are Ready");
		ap_cortex_track_content_required = 0;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == 13, "Cortex Vortex enters on host level 13");
		expect(used_files == 1 && used_managed == 0 && strcmp(used_lev, cvRelicLev) == 0 && strcmp(used_vrm, cvRelicVrm) == 0 &&
		       strcmp(used_name, "CORTEX VORTEX") == 0, "a Relic Race loads the bundled relic pair");
		expect(applied == 1 && applied_descriptor.standalone && applied_descriptor.hostLevelID == 13 && applied_descriptor.laps == 3 &&
		       applied_descriptor.flagRelicCrates == 1 && applied_descriptor.flagCrates == 0 &&
		       applied_descriptor.flagAiNav == 1 && applied_descriptor.flagCtrLetters == 1 &&
		       applied_descriptor.flagSpawns == 8 && applied_descriptor.flagCheckpoints == 255 &&
		       applied_descriptor.flagWumpaCollectible == 1 && applied_descriptor.replacesCupLevelID == -1 &&
		       strcmp(applied_descriptor.levSha256, CTR_CFG_CORTEX_RELIC_LEV_SHA256) == 0 &&
		       strcmp(applied_descriptor.vrmSha256, CTR_CFG_CORTEX_RELIC_VRM_SHA256) == 0,
		       "Relic Race descriptor carries the relic pair's measured flags and pins");
		expect(staged == 1 && staged_race.cortex == 1 && !staged_race.retail && staged_race.slot == 0 &&
		       staged_race.hostLevelID == 13 && staged_race.mode == AP_CONTENT_MODE_RELIC &&
		       staged_race.trophyLocation == 35026001 && staged_race.physicalPad == 5 && staged_race.returnHub == 27 &&
		       CustomRaceContext_Valid(&staged_race) &&
		       strcmp(staged_race.levSha256, CTR_CFG_CORTEX_RELIC_LEV_SHA256) == 0, "a valid Cortex Vortex Relic Race context is staged");
		for (int mode = AP_CONTENT_MODE_TROPHY; mode <= AP_CONTENT_MODE_CTR; ++mode)
		{
			reset_calls(); ap_cortex_track_content_required = 0;
			expect(AP_ContentPreparePad(5, mode) == 13 && used_files == 1 && strcmp(used_lev, cvLev) == 0 &&
			       strcmp(used_vrm, cvVrm) == 0 && applied == 1 && applied_descriptor.flagCrates == 1 &&
			       applied_descriptor.flagRelicCrates == 0 && applied_descriptor.flagCtrLetters == 1 &&
			       strcmp(applied_descriptor.levSha256, CTR_CFG_CORTEX_LEV_SHA256) == 0 &&
			       strcmp(applied_descriptor.vrmSha256, CTR_CFG_CORTEX_VRM_SHA256) == 0 &&
			       strcmp(staged_race.levSha256, CTR_CFG_CORTEX_LEV_SHA256) == 0,
			       mode == AP_CONTENT_MODE_TROPHY ? "a Trophy Race loads the bundled main pair"
			                                      : "a CTR Challenge loads the bundled main pair");
		}
		reset_calls(); ap_cortex_track_content_required = 0;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == 13 && strcmp(used_lev, cvRelicLev) == 0,
		       "back to the Relic Race after a Trophy Race: the relic pair again");
		reset_calls();
		refuse_files = 1;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == -1 && !AP_ContentPadReady(5) &&
		       ap_cortex_track_content_required && ap_content_cortex_load_error == CTR_CT_MANAGER_LOAD_FAILED + 1 && staged == 0,
		       "Cortex Vortex loader refusal also updates pad and seed readiness");
		refuse_files = 0;
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(AP_ContentPadReady(5) && !ap_content_cortex_load_error, "a scan clears the Cortex Vortex load failure");
		lengthen_assets_root();
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(!AP_ContentPadReady(5) && ap_content_cortex_load_error == CTR_CT_MANAGER_PATH_TOO_LONG + 1,
		       "bundled files at an overlong path are refused");
		expect(!AP_ContentScanCortex() && ap_content_cortex_load_error == CTR_CT_MANAGER_PATH_TOO_LONG + 1,
		       "rescanning the same long path leaves Cortex Vortex refused");
		strcpy(assets_root, short_root);
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(AP_ContentPadReady(5) && !ap_content_cortex_load_error, "the shorter path restores Cortex Vortex readiness");
		/* A changed byte after admission: same size, other hash. */
		snprintf(cmd, sizeof cmd, "printf 'X' | dd of='%s' bs=1 seek=100 conv=notrunc status=none", cvLev);
		expect(system(cmd) == 0, "bundled LEV changed in place");
		reset_calls(); ap_cortex_track_content_required = 0;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_cortex_ready &&
		       ap_cortex_track_content_required && used_files == 0 && applied == 0 && staged == 0,
		       "a changed bundled LEV is refused at pad entry");
		unlink(cvLev); unlink(cvVrm); unlink(cvRelicLev); unlink(cvRelicVrm); rmdir(cvDir);
		plan_pad = saved;
	}

	/* A6-23: the same package has relic crates and a lap graph, with one
	   measured spawn and no AI paths. Drive pad entry into the loader. */
	reset_calls();
	plan_package.capabilities = AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_RELIC;
	plan_pad.mode_count = 1;
	plan_pad.modes[0] = AP_CONTENT_MODE_RELIC;
	plan_pad.relic_route = plan_pad.relic_locations[0] = 35016302;
	plan_pad.relic_locations[1] = 35016303;
	plan_pad.relic_locations[2] = 35016304;
	enum { authoredSlot = 2 };
	const size_t spawnBytes = sizeof(struct SpawnPosRot);
	const size_t authoredSpawn = 4 + SPAWN_P + authoredSlot * spawnBytes;
	put32(image, 4 + NAV_P, 0);
	memset(image + 4 + SPAWN_P, 0, 8 * spawnBytes);
	put16(image, authoredSpawn, 22);
	put16(image, authoredSpawn + 2, 99);
	put16(image, authoredSpawn + 8, 0x60);
	write_bytes(lev, image, levSize);
	expect(!CustomContentVerify_Files(lev, vrm, &report, error, sizeof error) &&
	       strstr(error, "null navigation pointer in relocation map"),
	       "A6-23: an absent navigation table must not be relocated");
	/* Remove the navigation table pointer and its three unreachable entries
	   from the synthetic LEV's relocation map. */
	size_t map = 4 + BODY_PTRMAP, retained = map + 4;
	for (size_t i = retained; i < levSize; i += 4)
	{
		unsigned slot = rd32(image + i);
		if (slot != NAV_P && (slot < BODY_NAV || slot >= BODY_NAV + 12))
		{
			put32(image, retained, slot);
			retained += 4;
		}
	}
	levSize = retained;
	put32(image, map, levSize - map - 4);
	write_bytes(lev, image, levSize);
	expect(CustomContentVerify_Files(lev, vrm, &report, error, sizeof error) &&
	       report.measured.spawns == 1 && report.measured.navPaths == 0 &&
	       !(AP_ContentMeasuredCapabilities(&report) & AP_CONTENT_CAP_ARCADE),
	       "A6-23: fixture verifies with one spawn, no AI paths and no Arcade capability");
	snprintf(plan_package.lev_sha256, sizeof plan_package.lev_sha256, "%s", report.levSha256);
	plan_package.lev_bytes = (uint32_t)levSize;
	plan_package.capabilities |= AP_CONTENT_CAP_ARCADE;
	plan_pad.modes[0] = AP_CONTENT_MODE_TROPHY;
	expect(!preflight() && ap_content_package_scan[0] == CTR_CT_MANAGER_RACE_GRID_UNSUPPORTED + 1 &&
	       AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && used_files == 0,
	       "the package without AI paths is refused for an opponent race during preflight");
	plan_package.capabilities &= ~AP_CONTENT_CAP_ARCADE;
	plan_pad.modes[0] = AP_CONTENT_MODE_RELIC;
	expect(preflight() && AP_ContentPadReady(3), "A6-23: Relic-only package is Ready");
	expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_RELIC) == 6,
	       "A6-23: Relic-only pad arms the production loader");
	CustomTrack_OnRaceLoadRequested(6);
	const struct CustomTrackRaceContext *relic = CustomTrack_StandaloneContext(6);
	expect(relic && relic->mode == AP_CONTENT_MODE_RELIC && relic->trophyLocation == 35016302 &&
	       CustomTrack_ServingLoad(6, 0, 0) && CustomTrack_EventFieldSize(6, 0, 0) == 1,
	       "A6-23: solo Relic Race serves the verified custom package");
	struct GameTracker game = {0};
	struct Driver driver = {0};
	struct Level level = {0};
	SVec3 bottom;
	sdata->gGT = &game;
	game.level1 = &level;
	game.gameMode1 = ADVENTURE_MODE | RELIC_RACE;
	game.levelID = 6;
	sdata->kartSpawnOrderArray[0] = 1; /* P1's retail Relic Race spawn */
	memcpy(level.DriverSpawn, image + 4 + SPAWN_P, sizeof level.DriverSpawn);
	VehBirth_SetStartlinePosition(&driver, &level, &bottom);
	VehBirth_SetStartlineRotation(&driver, &level);
	expect(bottom.x == 22 && bottom.y == 99 + 0x80 && driver.rotCurr.y == 0x460,
	       "A6-23: P1 uses the package's only authored position and rotation");
	/* The fly-in changes both the usual slot and the selected live slot. */
	level.DriverSpawn[1].pos.y = 42;
	level.DriverSpawn[authoredSlot].pos.y = 97;
	CustomTrack_OnRaceLoadRequested(6);
	VehBirth_SetStartlinePosition(&driver, &level, &bottom);
	VehBirth_SetStartlineRotation(&driver, &level);
	expect(bottom.x == 22 && bottom.y == 99 + 0x80 && driver.rotCurr.y == 0x460,
	       "A6-23: retry uses the authored height after the fly-in moves the selected slot");
	game.levelID = 7;
	VehBirth_SetStartlinePosition(&driver, &level, &bottom);
	expect(bottom.x == 0 && bottom.y == 42 + 0x80,
	       "A6-23: a retail Relic Race keeps its live solo spawn");
	game.levelID = 6;
	for (int authoredSolo = 1; authoredSolo >= 0; --authoredSolo)
	{
		reset_calls();
		/* Keep slot 0 as an alternative in the authored-solo case. */
		memset(image + 4 + SPAWN_P, 0, sizeof level.DriverSpawn);
		if (authoredSolo)
		{
			image[4 + SPAWN_P] = 12;
			image[4 + SPAWN_P + sizeof level.DriverSpawn[0]] = 42;
		}
		write_bytes(lev, image, levSize);
		expect(CustomContentVerify_Files(lev, vrm, &report, error, sizeof error),
		       "A6-23: alternate Relic grid verifies");
		snprintf(plan_package.lev_sha256, sizeof plan_package.lev_sha256, "%s", report.levSha256);
		expect(preflight() && AP_ContentPreparePad(3, AP_CONTENT_MODE_RELIC) == 6,
		       "A6-23: alternate Relic grid starts through production pad entry");
		CustomTrack_OnRaceLoadRequested(6);
		memcpy(level.DriverSpawn, image + 4 + SPAWN_P, sizeof level.DriverSpawn);
		VehBirth_SetStartlinePosition(&driver, &level, &bottom);
		expect(bottom.x == (authoredSolo ? 42 : 0) && bottom.y == 0x80,
		       authoredSolo ? "A6-23: an authored solo slot keeps its priority"
		                    : "A6-23: an all-zero grid starts at the origin");
	}
	reset_calls();

	/* Merged mode builds (`relic_files`): the main pair is a crate build without
	   time crates, the relic pair the lap build. Relic and time_trial are
	   measured on the relic pair, everything else on the main pair; only a
	   Relic Race serves the relic pair; a missing relic pair is not ready. */
	{
		unsigned char crates[IMAGE_BYTES], lap[IMAGE_BYTES];
		unsigned char relicVrmBytes[22] = {0};
		char mainLev[600], relicLev[600], relicVrm[600];
		struct CustomContentVerification mainRep, relicRep;
		ctr_content_pad saved = plan_pad;
		const ctr_content_package keep = plan_package;
		size_t crateSize, lapSize;
		build_lev(crates, 1);
		/* The one weapon crate stands somewhere the relic build's does not. */
		put16(crates, 4 + BODY_INST + 2 * INST_SIZE + 0x30, 321); put16(crates, 4 + BODY_INST + 2 * INST_SIZE + 0x32, (unsigned)-45 & 0xffff);
		put16(crates, 4 + BODY_INST + 2 * INST_SIZE + 0x34, 987); put16(crates, 4 + BODY_INST + 2 * INST_SIZE + 0x38, 2048);
		/* The two time crate models stop being time crates. */
		put16(crates, 4 + BODY_MODELS + 3 * MODEL_STRIDE + MODEL_ID, CRYSTAL);
		put16(crates, 4 + BODY_MODELS + 4 * MODEL_STRIDE + MODEL_ID, CRYSTAL);
		lapSize = build_lev(lap, 1);
		crateSize = lapSize;
		snprintf(mainLev, sizeof mainLev, "%s/crates.lev", dir);
		snprintf(relicLev, sizeof relicLev, "%s/laps.lev", dir);
		snprintf(relicVrm, sizeof relicVrm, "%s/laps.vrm", dir);
		relicVrmBytes[16] = 1; relicVrmBytes[18] = 1; relicVrmBytes[20] = 9;
		write_bytes(mainLev, crates, crateSize);
		expect(CustomContentVerify_Files(mainLev, vrm, &mainRep, error, sizeof error), "merged: crate build verifies");
		expect(!(AP_ContentMeasuredCapabilities(&mainRep) & AP_CONTENT_CAP_RELIC), "merged: the crate build has no time crates");
		snprintf(plan_package.lev_sha256, sizeof plan_package.lev_sha256, "%s", mainRep.levSha256);
		snprintf(plan_package.vrm_sha256, sizeof plan_package.vrm_sha256, "%s", mainRep.vrmSha256);
		plan_package.lev_bytes = (uint32_t)crateSize; plan_package.vrm_bytes = sizeof vrmBytes;
		plan_package.capabilities = AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE | AP_CONTENT_CAP_RELIC | AP_CONTENT_CAP_CTR_TOKENS;
		plan_package.has_relic_targets = 1;
		/* Control: claiming relic on the crate build alone is refused. */
		reset_calls();
		expect(!preflight() && strstr(ap_custom_content_status.detail, "claims relic"), "merged: relic claim on the crate build alone is refused");
		/* The relic pair is not installed yet: not ready, like a missing main pair. */
		write_bytes(relicLev, lap, lapSize);
		write_bytes(relicVrm, relicVrmBytes, sizeof relicVrmBytes);
		expect(CustomContentVerify_Files(relicLev, relicVrm, &relicRep, error, sizeof error), "merged: lap build verifies");
		plan_package.has_relic_files = 1;
		snprintf(plan_package.relic_lev_sha256, sizeof plan_package.relic_lev_sha256, "%s", relicRep.levSha256);
		snprintf(plan_package.relic_vrm_sha256, sizeof plan_package.relic_vrm_sha256, "%s", relicRep.vrmSha256);
		plan_package.relic_lev_bytes = (uint32_t)lapSize; plan_package.relic_vrm_bytes = sizeof relicVrmBytes;
		unlink(relicLev); unlink(relicVrm);
		reset_calls();
		expect(!preflight() && !AP_ContentPadReady(3) && ap_custom_content_status.state != CTR_CT_MANAGER_READY &&
		       strstr(ap_custom_content_status.detail, "Relic Race files") && strstr(log_text, "content plan refused"),
		       "merged: a missing relic pair leaves the package not ready");
		write_bytes(relicLev, lap, lapSize);
		write_bytes(relicVrm, relicVrmBytes, sizeof relicVrmBytes);
		plan_pad.laps = 3; plan_pad.relic_laps = 3;
		plan_pad.mode_count = 2; plan_pad.modes[0] = AP_CONTENT_MODE_TROPHY; plan_pad.modes[1] = AP_CONTENT_MODE_RELIC;
		plan_pad.stages[0] = plan_pad.stages[1] = 1;
		plan_pad.relic_route = plan_pad.relic_locations[0] = 35016302;
		plan_pad.relic_locations[1] = 35016303; plan_pad.relic_locations[2] = 35016304;
		reset_calls();
		expect(preflight() && AP_ContentPadReady(3) && strcmp(ap_content_files[0].relicLevPath, relicLev) == 0 &&
		       strcmp(ap_content_files[0].levPath, mainLev) == 0,
		       "merged: both pairs verify and are located by their own pins");
		expect(ap_content_files[0].measured.relicCrates == 0 && ap_content_files[0].relicMeasured.relicCrates == 2,
		       "merged: each pair keeps its own measurements");
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6 && strcmp(used_lev, mainLev) == 0 &&
		       strcmp(applied_descriptor.levSha256, plan_package.lev_sha256) == 0 &&
		       strcmp(applied_descriptor.vrmSha256, plan_package.vrm_sha256) == 0 && applied_descriptor.flagRelicCrates == 0 &&
		       strcmp(staged_race.levSha256, plan_package.lev_sha256) == 0,
		       "merged: a Trophy Race serves the main pair and its identity");
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_RELIC) == 6 && strcmp(used_lev, relicLev) == 0 &&
		       strcmp(used_vrm, relicVrm) == 0 &&
		       strcmp(applied_descriptor.levSha256, plan_package.relic_lev_sha256) == 0 &&
		       strcmp(applied_descriptor.vrmSha256, plan_package.relic_vrm_sha256) == 0 && applied_descriptor.flagRelicCrates == 1 &&
		       strcmp(staged_race.levSha256, plan_package.relic_lev_sha256) == 0 &&
		       strcmp(staged_race.vrmSha256, plan_package.relic_vrm_sha256) == 0 &&
		       strcmp(staged_race.packageRevision, plan_package.revision) == 0 && staged_race.mode == AP_CONTENT_MODE_RELIC,
		       "merged: a Relic Race serves the relic pair, with flags and pins from it, identity on the main package");
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6 && strcmp(used_lev, mainLev) == 0,
		       "merged: the next Trophy Race serves the main pair again");
		/* The main pair's weapon crates, read from its own LEV (Item Boxes of a Relic Race). */
		{
			short (*rows)[4] = 0;
			long found = AP_ContentMainWeaponCrates(0, &rows);
			expect(found == 1 && rows && rows[0][0] == 321 && rows[0][1] == -45 && rows[0][2] == 987 && rows[0][3] == 2048,
			       "merged: the main pair's weapon crate is read from the main LEV, whatever pair a race serves");
			free(rows); rows = 0;
			write_bytes(mainLev, lap, lapSize);
			expect(AP_ContentMainWeaponCrates(0, &rows) == -1 && rows == 0, "merged: a main LEV that no longer matches its pin yields no crates");
			write_bytes(mainLev, crates, crateSize);
		}
		/* A package whose main pair matches a compiled managed profile still serves
		   its relic pair in a Relic Race; every other mode uses the manager. */
		fake_profile_on = 1;
		snprintf(fake_main_lev, sizeof fake_main_lev, "%s", mainLev); snprintf(fake_main_vrm, sizeof fake_main_vrm, "%s", vrm);
		reset_calls();
		expect(preflight() && AP_ContentPadReady(3), "profile: both pairs verify for a managed profile with relic_files");
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == 6 && used_managed == 1 && used_files == 0 &&
		       strcmp(applied_descriptor.levSha256, plan_package.lev_sha256) == 0,
		       "profile: a Trophy Race loads the main pair through the manager");
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_RELIC) == 6 && used_managed == 0 && used_files == 1 &&
		       strcmp(used_lev, relicLev) == 0 && strcmp(used_vrm, relicVrm) == 0 &&
		       strcmp(applied_descriptor.levSha256, plan_package.relic_lev_sha256) == 0 && applied_descriptor.flagRelicCrates == 1 &&
		       strcmp(staged_race.levSha256, plan_package.relic_lev_sha256) == 0,
		       "profile: a Relic Race loads the relic pair, with its pins and flags");
		fake_profile_on = 0;
		reset_calls();
		expect(preflight(), "profile: the fixture is back to open packages");
		/* A relic pair without time crates is refused as the relic pair. */
		write_bytes(relicLev, crates, crateSize);
		snprintf(plan_package.relic_lev_sha256, sizeof plan_package.relic_lev_sha256, "%s", mainRep.levSha256);
		reset_calls();
		expect(!preflight() && strstr(ap_custom_content_status.detail, "Relic Race files") &&
		       strstr(ap_custom_content_status.detail, "claims relic"), "merged: a relic pair without time crates is refused");
		/* A swapped relic pair is refused at entry, for every mode. */
		write_bytes(relicLev, lap, lapSize);
		snprintf(plan_package.relic_lev_sha256, sizeof plan_package.relic_lev_sha256, "%s", relicRep.levSha256);
		reset_calls();
		expect(preflight(), "merged: ready again with the right relic pair");
		unlink(relicLev);
		reset_calls();
		expect(AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1 && !AP_ContentPadReady(3) && used_files == 0,
		       "merged: removing the relic pair after admission refuses even a Trophy Race");
		unlink(relicVrm); unlink(mainLev);
		plan_package = keep; plan_pad = saved;
		reset_calls();
		expect(preflight(), "merged case leaves the fixture as it was");
	}

	/* No package record: nothing to scan or enter. */
	plan_has_package = 0;
	expect(!AP_ContentScanPackage(0, 0, &ap_custom_content_status) && AP_ContentPreparePad(3, AP_CONTENT_MODE_TROPHY) == -1, "missing record refused");

	unlink(lev); unlink(vrm); rmdir(dir);
	snprintf(dir, sizeof dir, "%s/tracks/fixture-loop", assets_root); rmdir(dir);
	snprintf(dir, sizeof dir, "%s/tracks", assets_root); rmdir(dir); rmdir(assets_root);
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "content_hooks.c", Path(tmp) / "content_hooks"
    src.write_text(prelude + lev_helper + stubs + hooks + spawn_entry + tests, encoding="utf-8")
    subprocess.run(["cc", "-m32", "-DCTR_NATIVE", "-DBUILD=926", "-std=gnu99",
                    "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-format-truncation",
                    "-DCTR_REPO_ROOT=\"" + str(root) + "\"",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    "-I" + str(root), "-I" + str(root / "include"), "-I" + str(root / "ap"),
                    str(src), "-o", str(exe)], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
print("PASS: open package hooks: honest claim, false claim at connect and pad entry, swap after admission and mid-scan; "
      "bundled Cortex Vortex pair ready, entered on host 13, refused when missing or changed")
