"""Production pad preparation + parser + loader, using real installed packages.

Pass --assets to exercise the real Baby T Park package. The generated C unit
uses unmodified AP prepare/preflight functions and the actual manager/loader.
It does not claim rendering, player input or a Steam race.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

args = argparse.ArgumentParser()
args.add_argument("--assets", type=Path, required=True)
args.add_argument("--server-uri")
args.add_argument("--net-library", type=Path)
args = args.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks.c").read_text()
def function(signature):
    start = source.index(signature + "\n{")
    end = source.index("\n}\n", start) + 3
    return source[start:end]

fixture = r'''
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include "platform/native_custom_tracks.c"
#include "platform/native_custom_track_manager.c"
#include "ap/ap_content_plan.h"
#include "ap/ap_seedcfg.h"
#include "ap/ap_items.h"
#include "ap/ap_net.h"
#include "ap/ap_deathlink.h"
#include "ap/ap_class_check_policy.h"
struct sData sdata_static;
struct Data data;
NativeConfig g_config;
static struct GameTracker game;
static const char *assetRoot;
static int keys;
static int ap_content_package_index, ap_content_package_ready[2];
static int ap_custom_content_seed_selected, ap_custom_content_required, ap_custom_content_scanned;
static int ap_custom_content_gate_cached;
static long sentCode;
static int networkMode;
static unsigned ap_state_gen;
static struct CustomTrackManagerStatus ap_custom_content_status;
const char *NativeAssets_GetAssetDir(void) { return assetRoot; }
void AP_LogLine(const char *s) { (void)s; }
int AP_GateCount(int type) { assert(type==AP_IDX_KEY); return keys; }
static void AP_CustomContentBuildRequirement(struct CustomTrackManagerRequirement *r) { (void)r; assert(0); }
static int AP_CustomContentActivateReady(void) { assert(0); return 0; }
int AP_RaceAttempt_ProducerBlocked(int producer) { (void)producer; return 0; }
static void AP_AppendLog(const char *s) { fputs(s,stdout); }
static void AP_CeremonyLedgerAdd(long c,int b,int t) { (void)c;(void)b;(void)t;assert(0); }
static void AP_FeedOnLocationSent(long c) { sentCode=c; }
#ifndef ENTRY_NETWORK
int ap_net_location_checked(long long c) { (void)c; return 0; }
int ap_net_location_exists(long long c) { (void)c; return 1; }
void ap_net_send_location(long long c) { (void)c; }
#endif
'''
for signature in (
    "static const struct CustomTrackManagerPackage *AP_ContentPackage(int index)",
    "int AP_ContentPadReady(int physicalPad)",
    "int AP_ContentPreparePad(int physicalPad)",
    "static void AP_CustomContentPreflightSeed(int autoFinalize)",
):
    fixture += function(signature)
fixture += function("static int AP_EmitClassCheck(long code,\n                              int addToCeremonyLedger, int ledgerBit, int ledgerTag,\n                              int toastSentItem, const char *logFmt, ...)")
fixture += function("int AP_NotifyStandaloneRace(void)")
fixture += r'''
int exercise(const char *assets, int network) {
 struct Driver driver={0}; game.drivers[0]=&driver; game.gameMode1=ADVENTURE_MODE;
 networkMode=network;
 assetRoot=assets; sdata->gGT=&game; game.levelID=26; keys=4;
 AP_CustomContentPreflightSeed(1);
 assert(!ap_custom_content_required);
 int occupied=0,empty=0,custom=0,trials=0;
 for(int physical=0;physical<=104;physical++) {
  const ctr_content_pad *p=ap_content_plan_pad(physical);
  if(!p) continue;
  if(!p->occupied) { assert(AP_ContentPreparePad(physical)==-1); empty++; continue; }
  const ctr_content_pad saved=*p;
  if(p->keys) { keys=p->keys-1; assert(AP_ContentPreparePad(physical)==-1); }
  keys=4;
  int host=AP_ContentPreparePad(physical);
  assert(host==(p->retail_id<0?6:p->retail_id));
  assert(AP_ContentPreparePad(physical)==-1); /* pending load cannot be replaced */
  CustomTrack_OnRaceLoadRequested(host); game.levelID=host;
  const struct CustomTrackRaceContext *race=CustomTrack_StandaloneContext(host);
  assert(race && race->physicalPad==physical && race->returnHub==p->hub);
  assert(race->seedEpoch==ap_content_plan_epoch() && race->trophyLocation==p->trophy_location);
  assert(!strcmp(race->entryID,p->entry_id) && !strcmp(race->trackID,p->track_id));
  assert(race->laps==p->laps && race->retail==(p->retail_id>=0));
  assert(CustomTrack_EventFieldSize(host,0,0)==8);
  uint64_t generation=s_standaloneRace.generation;
  CustomTrack_OnRaceLoadRequested(host); /* restart */
  assert(s_standaloneRace.generation==generation && CustomTrack_StandaloneContext(host));
  if(p->retail_id<0) {
   custom++; assert(CustomTrack_ServingLoad(host,0,0));
   struct CustomTrackLoadContext ctx={0}; ctx.levelID=host;
   const char *path=NULL; u32 size=0;
   assert(CustomTrack_GetOverride(8*host+1,&ctx,&path,&size));
   assert(size==2558168 && !strcmp(path,"@ctr/standalone/lev"));
   unsigned char *bytes=malloc(size); assert(bytes);
   assert(CustomTrack_ReadFile(path,bytes,size,size));
   char digest[65]; unsigned char raw[32]; struct NativeSha256Ctx hash;
   NativeSha256_Init(&hash); NativeSha256_Update(&hash,bytes,size);
   NativeSha256_Final(&hash,raw); NativeSha256_ToHex(raw,digest); free(bytes);
   assert(!strcmp(digest,race->levSha256));
   AP_CustomContentPreflightSeed(1); /* reconnect preflight retains active bytes */
   assert(CustomTrack_ServingLoad(host,0,0));
  } else { assert(!CustomTrack_ServingLoad(host,0,0)); if(host>=16) trials++; }
  if(!networkMode || p->retail_id<0) {
   sentCode=-1; assert(AP_NotifyStandaloneRace()); assert(sentCode==p->trophy_location);
  }
  CustomTrack_OnRaceLoadRequested(saved.hub); game.levelID=saved.hub;
  assert(!CustomTrack_StandaloneContext(host));
  race=CustomTrack_PreviousStandaloneContext();
  assert(race && race->physicalPad==physical && race->returnHub==saved.hub);
  occupied++;
 }
 assert(occupied==19 && empty==8 && custom==1 && trials==2);
 printf("PASS actual plan/prepare/manager/loader: %d routes, %d empty pads, %d custom, %d trial Trophy routes; restart and exact return identity\n",occupied,empty,custom,trials);
 return 0;
}
'''
driver = r'''
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cassert>
#include <chrono>
#include <thread>
#include "ap_net.h"
extern "C" int exercise(const char *,int);
int main(int argc,char **argv) {
 assert(argc==3 || argc==4); nlohmann::json j; std::ifstream(argv[1])>>j;
 int legs[20]{}; ctr_cfg_set_vanilla_cup_legs(legs);
#ifdef ENTRY_NETWORK
 assert(argc==4 && ap_net_init("content-pad-probe","Crash Team Racing",argv[3])==0);
 ap_net_connect_slot("CustomPadTest","");
 for(int i=0;i<1000 && !ap_net_is_connected();++i) {
  ap_net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 if(!ap_net_is_connected()) { fprintf(stderr,"Connection failed: %s\n",ap_net_last_error()); return 1; }
 assert(ap_content_plan_active());
#else
 ap_seedcfg_parse_json(j); assert(!ap_seedcfg_rejected());
 std::vector<int64_t> ids; for(const auto &c:j["content_plan"]["checks"]) ids.push_back(c["location"]);
 assert(ap_content_plan_bind("entry-probe",0,1,ids.data(),ids.size()));
#endif
 int result=exercise(argv[2],argc==4);
#ifdef ENTRY_NETWORK
 for(int i=0;i<500 && !ap_net_location_checked(35016300);++i) {
  ap_net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 assert(ap_net_location_checked(35016300));
 assert(!ap_net_location_checked(35011001)); // Borrowed Roo's Tubes host never sent.
 ap_net_shutdown();
 puts("PASS real server acknowledged Custom Track 1 Trophy; borrowed retail host remains unchecked");
#endif
 return result;
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    c, cpp, obj, exe = (folder / n for n in ("entry.c", "driver.cpp", "entry.o", "probe"))
    c.write_text(fixture)
    cpp.write_text(driver)
    flags = ["-m32", "-DCTR_AP", "-DCTR_CUSTOM_TRACKS", "-DCTR_NATIVE", "-DBUILD=926",
             "-I" + str(root), "-I" + str(root / "include"), "-I" + str(root / "ap"),
             "-ffunction-sections", "-fdata-sections", "-fsanitize=undefined", "-fno-sanitize-recover=all"]
    if args.server_uri:
        assert args.net_library, "--server-uri requires the actual built --net-library"
        flags += ["-DENTRY_NETWORK"]
    subprocess.run(["cc", "-std=c11", *flags, "-c", str(c), "-o", str(obj)], check=True)
    units = [str(args.net_library.resolve()), "-lssl", "-lcrypto", "-lz", "-pthread"] if args.server_uri else [str(root / "ap/ap_seedcfg.cpp")]
    subprocess.run(["c++", "-std=c++17", *flags, "-I" + str(root / "ap/vendor/json/include"),
                    str(cpp), str(obj), *units, "-Wl,--gc-sections", "-o", str(exe)], check=True)
    command = [str(exe), str(root / "tools/fixtures/content-plan/baseline.json"), str(args.assets.resolve())]
    if args.server_uri: command.append(args.server_uri)
    subprocess.run(command, check=True)
