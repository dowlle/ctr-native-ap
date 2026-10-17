"""Compile the production content-plan Relic Race result and CTR display bit.

#133 PR 3. AP_NotifyStandaloneRelic pays the tiers a time reaches against the
track's own targets (a custom package's, or the retail track's), with the pad's
own codes, through the common check emitter. Engine and network state are
controlled seams; this does not claim a Steam race.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "platform/native_custom_race_context.h"
#include "ap/ap_class_check_policy.h"
#include "ap/ap_content_plan.h"
#include "ap/ap_content_modes.h"
#define ADVENTURE_MODE 1
#define ARCADE_MODE 2
#define ADVENTURE_CUP 4
#define ADVENTURE_BOSS 8
#define RELIC_RACE 16
#define CRYSTAL_CHALLENGE 32
#define TOKEN_RACE 1
#define STATIC_RELIC 0x61
#define NEW_RELIC 0x100
struct GameTracker { int levelID,gameMode1,gameMode2,podiumRewardID,gameModeEnd; } game;
static struct { struct GameTracker *gGT; int relicTime_1min,relicTime_10sec,relicTime_1sec,relicTime_10ms,relicTime_1ms; } sdataStore, *sdata=&sdataStore;
static struct { int RelicTime[18*3]; } data;
static struct CustomTrackRaceContext context;
static ctr_content_pad pad;
static ctr_content_package package;
static int active, planActive, haveContext, serving, havePackage;
static uint64_t epoch;
static unsigned ap_state_gen;
static long sentCodes[8]; static int sentCount, ledgerBits[8], ledgerCount, feed;
static long checkedCodes[8]; static int checkedCount;
static long existsMissing;
int ctr_cfg_active(void) { return active; }
int ap_content_plan_active(void) { return planActive; }
uint64_t ap_content_plan_epoch(void) { return planActive ? epoch : 0; }
const ctr_content_pad *ap_content_plan_pad(int physical) { return planActive && pad.physical==physical ? &pad : NULL; }
const ctr_content_package *ap_content_plan_package(int index) { return havePackage && index==0 ? &package : NULL; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) {
 return haveContext && level==context.hostLevelID ? &context : NULL;
}
int AP_CortexRelicTime(int tier) { return 200000 - tier; } /* covered in test-content-cortex-perfect-production.py */
int CustomTrack_ServingLoad(int level,int cup,int id) { assert(level==context.hostLevelID && !cup && !id); return serving; }
int ap_net_location_exists(long code) { return code != existsMissing; }
int ap_net_location_checked(long code) {
 for (int i=0;i<checkedCount;i++) if (checkedCodes[i]==code) return 1;
 return 0;
}
void ap_net_send_location(long code) { sentCodes[sentCount++]=code; checkedCodes[checkedCount++]=code; }
void AP_PerfNoteCheckSent(void) {}
void AP_AppendLog(const char *text) { assert(strstr(text,"standalone relic")); }
void AP_CeremonyLedgerAdd(long code,int bit,int tag) { (void)code; (void)tag; ledgerBits[ledgerCount++]=bit; }
void AP_FeedOnLocationSent(long code) { (void)code; feed++; }
'''
for signature in ("static int AP_EmitClassCheck(long code,",
                  "static const ctr_content_pad *AP_StandaloneRoutePad(",
                  "int AP_StandaloneCtrBit(void)",
                  "static const ctr_content_pad *AP_StandaloneRelicPad(void)",
                  "int AP_StandaloneRelicTarget(int tier)",
                  "long AP_StandaloneRelicCode(int tier)",
                  "int AP_NotifyStandaloneRelic(int raceTime)"):
    fixture += function(signature) + "\n"
fixture += r'''
static void reset(int retail) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context); memset(&pad,0,sizeof pad);
 memset(&package,0,sizeof package);
 active=planActive=haveContext=1; serving=1; havePackage=1; epoch=5; ap_state_gen=0;
 sentCount=ledgerCount=feed=checkedCount=0; existsMissing=-1;
 sdataStore.gGT=&game; game.gameMode1=ADVENTURE_MODE|RELIC_RACE;
 context.physicalPad=12; context.returnHub=28; context.seedEpoch=5; context.mode=AP_CONTENT_MODE_RELIC;
 context.retail=retail; context.laps=retail?3:5; context.slot=retail?0:2;
 context.hostLevelID=retail?9:6; game.levelID=context.hostLevelID;
 strcpy(context.entryID,"entry/x"); strcpy(context.trackID,"x");
 pad.physical=12; pad.hub=28; pad.occupied=1; pad.laps=context.laps; pad.custom_slot=context.slot;
 pad.retail_id=retail?9:-1; pad.package_index=retail?-1:0;
 pad.mode_count=2; pad.modes[0]=AP_CONTENT_MODE_TROPHY; pad.modes[1]=AP_CONTENT_MODE_RELIC;
 pad.stages[0]=1; pad.stages[1]=2; pad.trophy_location=35011002;
 pad.relic_locations[0]=35012009; pad.relic_locations[1]=35012109; pad.relic_locations[2]=35012209;
 context.trophyLocation=pad.relic_locations[0];
 strcpy(pad.entry_id,"entry/x"); strcpy(pad.track_id,"x");
 for (int i=0;i<54;i++) data.RelicTime[i]=100000-i;
 data.RelicTime[9*3+0]=120000; data.RelicTime[9*3+1]=110000; data.RelicTime[9*3+2]=100000;
 package.has_relic_targets=1; package.relic_targets[0]=96000; package.relic_targets[1]=86400; package.relic_targets[2]=76800;
}
int main(void) {
 int cases=0;
 /* Custom package targets, never the host slot's retail times. */
 int times[] = {96001, 96000, 90000, 86400, 80000, 76800, 1};
 int expectMask[] = {0, 1, 1, 3, 3, 7, 7};
 for (int i=0;i<7;i++) {
  reset(0);
  assert(AP_StandaloneRelicTarget(0)==96000 && AP_StandaloneRelicTarget(2)==76800);
  int sent=AP_NotifyStandaloneRelic(times[i]);
  int count=__builtin_popcount(expectMask[i]);
  assert(sent==count && sentCount==count && feed==count && ledgerCount==count);
  for (int t=0,n=0;t<3;t++) if (expectMask[i]&(1<<t)) {
   assert(sentCodes[n]==pad.relic_locations[t]);
   assert(ledgerBits[n]==AP_CONTENT_BIT_RELIC+12*3+t);
   n++;
  }
  assert((game.podiumRewardID==STATIC_RELIC)==(count>0));
  assert(((game.gameModeEnd&NEW_RELIC)!=0)==(count>0));
  cases++;
 }
 /* Retail tracks keep their retail targets. */
 reset(1);
 assert(AP_StandaloneRelicTarget(1)==110000);
 assert(AP_NotifyStandaloneRelic(110000)==2 && sentCodes[0]==35012009 && sentCodes[1]==35012109); cases++;
 /* Already checked tiers are skipped, the rest still pay. */
 reset(0); checkedCodes[checkedCount++]=35012009;
 assert(AP_NotifyStandaloneRelic(1)==2 && sentCodes[0]==35012109 && sentCodes[1]==35012209); cases++;
 /* A tier the room lacks is skipped. */
 reset(0); existsMissing=35012109;
 assert(AP_NotifyStandaloneRelic(1)==2); cases++;
 /* Refusals: no relic route, wrong context, stale epoch, wrong code, no bytes. */
 for (int bad=0;bad<14;bad++) {
  reset(0);
  switch (bad) {
   case 0: active=0; break;
   case 1: haveContext=0; break;
   case 2: context.mode=AP_CONTENT_MODE_TROPHY; context.trophyLocation=pad.trophy_location; break;
   case 3: game.gameMode1=ADVENTURE_MODE; break;
   case 4: game.gameMode2=TOKEN_RACE; break;
   case 5: game.gameMode1|=ADVENTURE_CUP; break;
   case 6: epoch=6; break;
   case 7: context.trophyLocation=35012109; break;
   case 8: serving=0; break;
   case 9: pad.hub=27; break;
   case 10: pad.mode_count=1; break; /* the pad lost its Relic mode */
   case 11: game.gameMode1&=~ADVENTURE_MODE; break;
   case 12: sdataStore.gGT=NULL; break;
   case 13: strcpy(pad.track_id,"y"); break;
  }
  assert(AP_NotifyStandaloneRelic(1)==0 && sentCount==0); cases++;
 }
 /* A package without targets earns nothing (the parser refuses it anyway). */
 reset(0); package.has_relic_targets=0;
 assert(AP_StandaloneRelicTarget(0)==-1 && AP_NotifyStandaloneRelic(1)==0); cases++;
 /* No relic route: no target and no code. */
 reset(0); context.mode=AP_CONTENT_MODE_CTR;
 assert(AP_StandaloneRelicTarget(0)==-1 && AP_StandaloneRelicCode(0)==-2); cases++;
 reset(0); assert(AP_StandaloneRelicCode(1)==35012109 && AP_StandaloneRelicCode(3)==-1); cases++;
 /* CTR display bit only for a CTR route. */
 reset(0); assert(AP_StandaloneCtrBit()==-1);
 context.mode=AP_CONTENT_MODE_CTR; assert(AP_StandaloneCtrBit()==AP_CONTENT_BIT_CTR+12);
 haveContext=0; assert(AP_StandaloneCtrBit()==-1); cases++;
 printf("PASS: %d content-plan Relic Race result and CTR display cases through the production emitter\n",cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "relic.c"
    binary = Path(temporary) / "relic"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    "-I", str(root / "include"), "-I", str(root), str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
