"""Compile production profile selection, AP activation and navigation handoff.

Loader IO and descriptor hash application are controlled seams, not gameplay.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


fixture = r'''
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_track_manager.c"
#include <platform/native_custom_tracks_policy.h>
#include <platform/native_custom_race_context.h>
static struct CustomTrackRaceLatch s_standaloneRace;
#define CTR_CT_NAV_UUID_BYTES 16
struct CustomTrackSeedDescriptor { int value; } s_descriptor;
static struct { char path[1024]; } s_customTrackLev, s_customTrackVrm;
static struct { char raceName[64]; unsigned char navTrackUuid[16];
 unsigned int navRevision; int navIdentityValid; } s_customTrackConfig;
static int s_customTracksLoaded, s_haveDescriptor, resets, applied;
static int active=1, ap_custom_content_seed_selected=1;
static struct { int custom_tracks_seen, custom_tracks_ok; } ctr_cfg={1,1};
static struct CustomTrackManagerStatus ap_custom_content_status;
static const struct CustomTrackManagerPackage *chosen;
static int ap_content_package_index;
int ap_content_plan_active(void) { return 0; }
const void *ap_content_plan_package(int i) { (void)i; return NULL; }
const struct CustomTrackManagerPackage *AP_ContentPackage(int i) { (void)i; return NULL; }
int ctr_cfg_active(void) { return active; }
void CustomTrack_Load(void) { s_customTracksLoaded=1; }
#define CustomTrack_Log(...) ((void)0)
void CustomTrack_CopyField(char *dst, size_t n, const char *src) { snprintf(dst,n,"%s",src); }
void CustomTrack_ResetArmedState(void) { resets++; }
void AP_CustomContentBuildRequirement(struct CustomTrackManagerRequirement *r) {
 *r=(struct CustomTrackManagerRequirement){chosen->id, chosen->packageUuid, chosen->version,
 chosen->minimumClientVersion, chosen->minimumApworldVersion, chosen->levSha256,
 chosen->vrmSha256, chosen->navigationUuid, chosen->navigationRevision, chosen->laps,
 0, chosen->flagCrates, chosen->flagCtrLetters, chosen->flagRelicCrates, chosen->flagAiNav,
 chosen->flagMinimap, chosen->flagGhosts, chosen->flagWumpaCollectible,
 chosen->flagSpawns, chosen->flagCheckpoints};
}
void AP_CustomContentBuildDescriptor(struct CustomTrackSeedDescriptor *d) { d->value=7; }
int CustomTrack_ApplySeedDescriptor(const struct CustomTrackSeedDescriptor *d) {
 assert(d->value==7); applied++; return 1;
}
'''
fixture += function("ap/ap_hooks.c", "const struct CustomTrackManagerPackage *AP_CustomContentSelectedPackage(void)")
fixture += function("platform/native_custom_tracks.c", "int CustomTrack_UseManagedPackage(")
fixture += function("ap/ap_hooks.c", "static int AP_CustomContentActivateReady(void)")
fixture += r'''
int main(void) {
 const struct CustomTrackManagerPackage *profiles[]={CustomTrackManager_BabyTPark(),&s_babyTParkCurrent};
 for(int i=0;i<2;i++) {
  char yaml[4096];
  unsigned char expected[16]; chosen=profiles[i];
  assert(AP_CustomContentSelectedPackage()==chosen);
  assert(CustomTrackPolicy_ParseNavUuid(chosen->navigationUuid,expected));
  ap_custom_content_status.state=CTR_CT_MANAGER_READY;
  snprintf(ap_custom_content_status.levPath,sizeof ap_custom_content_status.levPath,"profile%d.lev",i);
  snprintf(ap_custom_content_status.vrmPath,sizeof ap_custom_content_status.vrmPath,"profile%d.vrm",i);
  // Model a completed scan, then exercise the same selected handle that the
  // menu passes to its details/export actions. Actual file scans are covered
  // by test-custom-track-manager.c.
  assert(Manager_PackageReceipt(chosen,&ap_custom_content_status,
         ap_custom_content_status.verifiedPackageSha256));
  assert(CustomTrackManager_RenderYaml(AP_CustomContentSelectedPackage(),
         &ap_custom_content_status,yaml,sizeof yaml));
  assert(strstr(yaml,chosen->levSha256));
  assert(!CustomTrackManager_RenderYaml(profiles[1-i],&ap_custom_content_status,yaml,sizeof yaml));
  s_haveDescriptor=1; resets=applied=0;
  assert(AP_CustomContentActivateReady());
  assert(memcmp(s_customTrackConfig.navTrackUuid,expected,16)==0);
  assert(s_customTrackConfig.navRevision==chosen->navigationRevision && s_customTrackConfig.navIdentityValid);
  assert(strcmp(s_customTrackLev.path,ap_custom_content_status.levPath)==0);
  assert(strcmp(s_customTrackVrm.path,ap_custom_content_status.vrmPath)==0);
  assert(s_haveDescriptor==0 && resets==1 && applied==1);
 }
 ctr_cfg.custom_tracks_ok=0; resets=applied=0;
 assert(AP_CustomContentSelectedPackage()==NULL);
 assert(!AP_CustomContentActivateReady() && resets==0 && applied==0);
 ctr_cfg.custom_tracks_ok=1; ap_custom_content_status.state=CTR_CT_MANAGER_HASH_MISMATCH;
 assert(!AP_CustomContentActivateReady() && resets==0 && applied==0);
 ap_custom_content_status.state=CTR_CT_MANAGER_READY; ap_custom_content_seed_selected=0;
 assert(!AP_CustomContentActivateReady() && resets==0 && applied==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    source = Path(temporary) / "activation.c"
    source.write_text(fixture)
    binary = Path(temporary) / "activation"
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined",
                    "-I", str(root / "include"), "-I", str(root), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: production legacy/current activation navigation identity and refusal isolation, UBSan")
