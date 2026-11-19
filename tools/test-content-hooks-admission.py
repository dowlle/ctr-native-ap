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
static char assets_root[256];
const char *NativeAssets_GetAssetDir(void) { return assets_root; }
static ctr_content_package plan_package;
static ctr_content_pad plan_pad;
static int plan_has_package = 1;
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
#define CTR_CFG_CORTEX_LEV_SHA256 "4e3a2daf56c67be3ac645d3bb5375e516c828a0bca24c35ac69b3366c466fe13"
#define CTR_CFG_CORTEX_VRM_SHA256 "4131444b9d1d53971befcfd11349efceaf887c20b795c8890fdcb2c36bdff07d"
static unsigned ap_state_gen;
static int used_managed, used_files, applied, staged;
static int refuse_files;
static char used_lev[CTR_CT_MANAGER_PATH_MAX], used_vrm[CTR_CT_MANAGER_PATH_MAX], used_name[CTR_CT_NAME_MAX];
static struct CustomTrackSeedDescriptor applied_descriptor;
static struct CustomTrackRaceContext staged_race;
uint64_t ap_content_plan_room_identity(void) { return UINT64_C(0x123456789abcdef0); }
static int record_managed(const struct CustomTrackManagerPackage *p, struct CustomTrackManagerStatus *s)
{
	used_managed++;
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
	unsigned char image[IMAGE_BYTES], changed[IMAGE_BYTES];
	unsigned char vrmBytes[22] = {0};
	struct CustomContentVerification report;
	unsigned measured;
	size_t levSize;
	snprintf(assets_root, sizeof assets_root, "/tmp/ctr-content-hooks-XXXXXX");
	if (!mkdtemp(assets_root)) return 2;
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
		struct AP_ContentFiles longPath = {0};
		memset(longPath.levPath, 'a', CUSTOM_TRACK_PATH_MAX - 1);
		expect(AP_ContentFileRefusal(&longPath) == CTR_CT_MANAGER_LOAD_FAILED, "a path that fits gets the generic refusal");
		longPath.levPath[CUSTOM_TRACK_PATH_MAX - 1] = 'a';
		expect(AP_ContentFileRefusal(&longPath) == CTR_CT_MANAGER_PATH_TOO_LONG, "a path beyond the loader limit gets specific help");
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
		char cvDir[600], cvLev[700], cvVrm[700], cmd[2048];
		snprintf(cvDir, sizeof cvDir, "%s/tracks/cortex-vortex", assets_root); mkdir(cvDir, 0700);
		snprintf(cvLev, sizeof cvLev, "%s/CVortex Arcade All.lev", cvDir);
		snprintf(cvVrm, sizeof cvVrm, "%s/CVortex Arcade All.vrm", cvDir);
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
		expect(system(cmd) == 0, "bundled pair copied");
		reset_calls();
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(ap_content_cortex_ready && AP_ContentPadReady(5), "pinned bundled pair is Ready");
		ap_cortex_track_content_required = 0;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == 13, "Cortex Vortex enters on host level 13");
		expect(used_files == 1 && used_managed == 0 && strcmp(used_lev, cvLev) == 0 && strcmp(used_vrm, cvVrm) == 0 &&
		       strcmp(used_name, "CORTEX VORTEX") == 0, "loader gets the bundled files");
		expect(applied == 1 && applied_descriptor.standalone && applied_descriptor.hostLevelID == 13 && applied_descriptor.laps == 3 &&
		       applied_descriptor.flagRelicCrates == 1 && applied_descriptor.flagAiNav == 1 && applied_descriptor.flagCtrLetters == 1 &&
		       applied_descriptor.flagSpawns == 8 && applied_descriptor.flagCheckpoints == 255 &&
		       applied_descriptor.flagWumpaCollectible == 1 && applied_descriptor.replacesCupLevelID == -1 &&
		       strcmp(applied_descriptor.levSha256, CTR_CFG_CORTEX_LEV_SHA256) == 0 &&
		       strcmp(applied_descriptor.vrmSha256, CTR_CFG_CORTEX_VRM_SHA256) == 0,
		       "loader descriptor carries the bundled pair's measured flags and pins");
		expect(staged == 1 && staged_race.cortex == 1 && !staged_race.retail && staged_race.slot == 0 &&
		       staged_race.hostLevelID == 13 && staged_race.mode == AP_CONTENT_MODE_RELIC &&
		       staged_race.trophyLocation == 35026001 && staged_race.physicalPad == 5 && staged_race.returnHub == 27 &&
		       CustomRaceContext_Valid(&staged_race) &&
		       strcmp(staged_race.levSha256, CTR_CFG_CORTEX_LEV_SHA256) == 0, "a valid Cortex Vortex race context is staged");
		reset_calls();
		refuse_files = 1;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_RELIC) == -1 && !AP_ContentPadReady(5) &&
		       ap_cortex_track_content_required && ap_content_cortex_load_error == CTR_CT_MANAGER_LOAD_FAILED + 1 && staged == 0,
		       "Cortex Vortex loader refusal also updates pad and seed readiness");
		refuse_files = 0;
		ap_content_cortex_ready = AP_ContentScanCortex();
		expect(AP_ContentPadReady(5) && !ap_content_cortex_load_error, "a scan clears the Cortex Vortex load failure");
		/* A changed byte after admission: same size, other hash. */
		snprintf(cmd, sizeof cmd, "printf 'X' | dd of='%s' bs=1 seek=100 conv=notrunc status=none", cvLev);
		expect(system(cmd) == 0, "bundled LEV changed in place");
		reset_calls(); ap_cortex_track_content_required = 0;
		expect(AP_ContentPreparePad(5, AP_CONTENT_MODE_TROPHY) == -1 && !ap_content_cortex_ready &&
		       ap_cortex_track_content_required && used_files == 0 && applied == 0 && staged == 0,
		       "a changed bundled LEV is refused at pad entry");
		unlink(cvLev); unlink(cvVrm); rmdir(cvDir);
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
