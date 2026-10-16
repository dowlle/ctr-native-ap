"""Open package admission through the production hooks (#133 PR 2 review).

Compiles AP_ContentScanPackage, AP_ContentPadReady and AP_ContentPreparePad
out of the ap_hooks unit together with the real structural verifier
(platform/native_custom_content_verify.c), the real track manager
(platform/native_custom_track_manager.c, which locates open packages by
digest) and the real capability check (ap/ap_content_capability.h). The seed
side (content-plan records), the loader and the race stager are stubs that
record what the hooks hand them. Fixture LEVs come from the build_lev helper of
tools/test-custom-content-verify.c; no third-party track asset is used.

Cases: an honest capability claim is Ready and enters with the measured flags;
a false claim is refused at connect and at pad entry and clears the ready flag;
a same-size file swap after admission is refused at pad entry; a swap between
locating and measuring is caught by the pin compare.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
unit = ap_hooks_unit.read()
start = unit.index("static int ap_content_package_index;")
hooks = unit[start:unit.index("static void AP_CustomContentBuildRequirement(", start)]

verify_test = (root / "tools/test-custom-content-verify.c").read_text(encoding="utf-8")
lev_start = verify_test.index("#define BODY_INST")
lev_helper = verify_test[lev_start:verify_test.index("/* The Arcade and Time Trial verdicts", lev_start)]

tracks_header = (root / "include/platform/native_custom_tracks.h").read_text(encoding="utf-8")
seed_start = tracks_header.index("struct CustomTrackSeedDescriptor\n{")
seed_struct = tracks_header[seed_start:tracks_header.index("};", seed_start) + 2]

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
#include "platform/native_custom_content_verify.c"
#include "platform/native_custom_track_manager.c"
#include "ap/ap_content_capability.h"
#include <platform/native_custom_race_context.h>
#include <platform/native_custom_tracks_policy.h>
'''

stubs = r'''
static char assets_root[256];
const char *NativeAssets_GetAssetDir(void) { return assets_root; }
static ctr_content_package plan_package;
static ctr_content_pad plan_pad;
static int plan_has_package = 1;
const ctr_content_package *ap_content_plan_package(int index) { return index == 0 && plan_has_package ? &plan_package : NULL; }
const ctr_content_pad *ap_content_plan_pad(int physical) { return physical == plan_pad.physical ? &plan_pad : NULL; }
uint64_t ap_content_plan_epoch(void) { return 7; }
int AP_ContentPadOpen(const ctr_content_pad *pad) { return pad != NULL; }
static struct CustomTrackManagerStatus ap_custom_content_status;
static int ap_custom_content_required;
static unsigned ap_state_gen;
int CustomTrack_StandaloneBusy(void) { return 0; }
static int used_managed, used_files, applied, staged;
static char used_lev[CTR_CT_MANAGER_PATH_MAX], used_vrm[CTR_CT_MANAGER_PATH_MAX], used_name[CTR_CT_NAME_MAX];
static struct CustomTrackSeedDescriptor applied_descriptor;
static struct CustomTrackRaceContext staged_race;
int CustomTrack_UseManagedPackage(const struct CustomTrackManagerPackage *p, struct CustomTrackManagerStatus *s) { (void)p; (void)s; used_managed++; return 1; }
int CustomTrack_UseContentFiles(const char *lev, const char *vrm, const char *name)
{
	used_files++;
	snprintf(used_lev, sizeof used_lev, "%s", lev); snprintf(used_vrm, sizeof used_vrm, "%s", vrm);
	snprintf(used_name, sizeof used_name, "%s", name);
	return 1;
}
int CustomTrack_ApplySeedDescriptor(const struct CustomTrackSeedDescriptor *d) { applied++; applied_descriptor = *d; return 1; }
int CustomTrack_StageStandaloneRace(const struct CustomTrackRaceContext *c) { staged++; staged_race = *c; return 1; }
void CustomTrack_CortexTrackSelectNextLoad(int cortex) { (void)cortex; }
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
static void reset_calls(void) { used_managed = used_files = applied = staged = 0; log_text[0] = 0; }
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
	memset(&plan_pad, 0, sizeof plan_pad);
	plan_pad.physical = 3; plan_pad.hub = 26; plan_pad.occupied = 1; plan_pad.retail_id = -1;
	plan_pad.package_index = 0; plan_pad.custom_slot = 1; plan_pad.laps = 3; plan_pad.named_block = -1;
	plan_pad.trophy_location = 35016300;
	snprintf(plan_pad.entry_id, sizeof plan_pad.entry_id, "entry/custom/fixture-loop/2.1");
	snprintf(plan_pad.track_id, sizeof plan_pad.track_id, "custom/fixture-loop/2.1");

	/* Honest claim: less than measured is fine (no relic claim). */
	plan_package.capabilities = AP_CONTENT_CAP_TIME_TRIAL | AP_CONTENT_CAP_ARCADE | AP_CONTENT_CAP_CTR_TOKENS;
	reset_calls();
	expect(preflight() && AP_ContentPadReady(3) && !ap_custom_content_required, "honest claim is Ready at connect");
	expect(strcmp(ap_content_files[0].levPath, lev) == 0 && strcmp(ap_content_files[0].vrmPath, vrm) == 0,
	       "open package located by digest under its track key");
	expect(AP_ContentPreparePad(3) == 6, "honest claim enters on host level 6");
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

	/* False claim: Wumpa is not in the bytes. Refused at connect and at pad entry. */
	plan_package.capabilities |= AP_CONTENT_CAP_WUMPA;
	reset_calls();
	expect(!preflight() && !AP_ContentPadReady(3) && ap_custom_content_required, "false claim refused at connect");
	expect(ap_custom_content_status.state == CTR_CT_MANAGER_UNSUPPORTED && strstr(ap_custom_content_status.detail, "wumpa") &&
	       strstr(log_text, "content plan refused package/fixture-loop/2.1"), "refusal names the missing capability and is logged");
	expect(ap_content_files[0].levPath[0] == 0, "a refused package keeps no located paths");
	expect(AP_ContentPreparePad(3) == -1 && used_files == 0 && applied == 0 && staged == 0, "false claim never reaches the loader");
	/* Ready at connect, then the claim check runs again at pad entry. */
	ap_content_package_ready[0] = 1; ap_state_gen = 0;
	expect(AP_ContentPreparePad(3) == -1 && !ap_content_package_ready[0] && ap_custom_content_required && ap_state_gen == 1 &&
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
	expect(AP_ContentPreparePad(3) == -1 && !ap_content_package_ready[0] && ap_custom_content_required && ap_state_gen == 1,
	       "same-size swap after admission refused at pad entry");
	expect(ap_custom_content_status.state != CTR_CT_MANAGER_READY && used_files == 0 && applied == 0 && staged == 0,
	       "swapped files never reach the loader");
	write_bytes(lev, image, levSize);
	expect(preflight() && AP_ContentPreparePad(3) == 6, "restoring the pinned bytes admits again");

	/* Swap between locating and measuring: the pin compare catches it. */
	reset_calls();
	ap_content_package_ready[0] = 1;
	swap_after_locate = changed; swap_bytes = levSize;
	snprintf(swap_path, sizeof swap_path, "%s", lev);
	expect(AP_ContentPreparePad(3) == -1 && !ap_content_package_ready[0] && used_files == 0 && applied == 0,
	       "a swap between locate and measure is refused");
	expect(strstr(ap_custom_content_status.detail, "failed the file check") != NULL, "the pin mismatch is the reason");
	write_bytes(lev, image, levSize);

	/* No package record: nothing to scan or enter. */
	plan_has_package = 0;
	expect(!AP_ContentScanPackage(0, 0, &ap_custom_content_status) && AP_ContentPreparePad(3) == -1, "missing record refused");

	unlink(lev); unlink(vrm); rmdir(dir);
	snprintf(dir, sizeof dir, "%s/tracks/fixture-loop", assets_root); rmdir(dir);
	snprintf(dir, sizeof dir, "%s/tracks", assets_root); rmdir(dir); rmdir(assets_root);
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "content_hooks.c", Path(tmp) / "content_hooks"
    src.write_text(prelude + seed_struct + "\n" + lev_helper + stubs + hooks + tests, encoding="utf-8")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-format-truncation",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    "-I" + str(root), "-I" + str(root / "include"), "-I" + str(root / "ap"),
                    str(src), "-o", str(exe)], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
print("PASS: open package hooks: honest claim, false claim at connect and pad entry, swap after admission and mid-scan")
