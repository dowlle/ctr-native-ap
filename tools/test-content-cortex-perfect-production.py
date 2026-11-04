"""Compile the production Cortex Vortex and Relic Race Perfect hooks of content-plan races (#133 PR 3c).

A content-plan Relic Race pays its own pad's Relic Race Perfect check through
AP_NotifyRelicPerfect and AP_StandalonePerfectCode: a retail track's frozen
code, Cortex Vortex's 35026005 or a custom track's own code, never the engine
host's, and only with every time crate broken. A content-plan Cortex Vortex
race runs on host 13 as a custom-bytes race: its route check, relic targets
(AP_CortexRelicTime, the real ap_cortex_track.h placeholders) and letter items
(35010200 to 35010202) are its own. Engine and network state are controlled
seams; this does not claim a Steam race.
"""
from pathlib import Path
import re
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


# The real Cortex Vortex relic targets and letter item rule, without the
# seed-config header ap_cortex_track.h pulls in.
cortex = (root / "ap/ap_cortex_track.h").read_text(encoding="utf-8")
targets = cortex[cortex.index("struct AP_CortexRelicTargets\n{"):cortex.index("// The relic tiers a finished run beat")]
letters = cortex[cortex.index("#define AP_CV_LETTER_ITEM_FIRST_INDEX"):cortex.index("#endif // AP_CORTEX_TRACK_H")]
seedcfg = (root / "ap/ap_seedcfg.h").read_text(encoding="utf-8")
pins = "\n".join(re.findall(r"^#define CTR_CFG_CORTEX_(?:LEV|VRM)_SHA256 .*$", seedcfg, re.M))

fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_race_context.h"
#include "ap/ap_class_check_policy.h"
#include "ap/ap_content_plan.h"
#include "ap/ap_content_modes.h"
#include "ap/ap_relic_perfect.h"
#include "ap/ap_lettersanity.h"
''' + pins + "\n" + targets + letters + r'''
#define ADVENTURE_MODE 1
#define ARCADE_MODE 2
#define ADVENTURE_CUP 4
#define ADVENTURE_BOSS 8
#define RELIC_RACE 16
#define CRYSTAL_CHALLENGE 32
#define TOKEN_RACE 1
#define STATIC_RELIC 0x61
#define NEW_RELIC 0x100
#define AP_ITEM_BASE 35010000
#define AP_RESULT_PRODUCER_RELIC_PERFECT 7
#define CTR_CFG_LETTER_TRACK_COUNT 18
#define CTR_CFG_LETTER_COUNT 3
struct Driver { int numTimeCrates; } driver;
struct GameTracker { int levelID,gameMode1,gameMode2,podiumRewardID,gameModeEnd,timeCratesInLEV; struct Driver *drivers[1]; struct { int cupID; } cup; } game;
static struct { struct GameTracker *gGT; int relicTime_1min,relicTime_10sec,relicTime_1sec,relicTime_10ms,relicTime_1ms; } sdataStore, *sdata=&sdataStore;
static struct { int RelicTime[18*3]; } data;
static struct { int relic_perfect_enabled, custom_tracks_ok; long relic_perfect[18]; struct { int slot; } custom_track; } ctr_cfg;
static struct CustomTrackRaceContext context;
static ctr_content_pad pad;
static ctr_content_package package;
static ctr_content_checks families;
static int active, planActive, haveContext, serving, blocked, cortexLatch, serveFault;
static uint64_t epoch;
static unsigned ap_state_gen;
static long sentCodes[8]; static int sentCount, feed;
static long checkedCodes[8]; static int checkedCount;
static unsigned char ap_letter_received[18][3], ap_custom_letter_received[AP_CUSTOM_LETTER_SLOT_COUNT][3];
static unsigned char ap_cortex_letter_received[3];
static unsigned char ap_named_letter_received[(AP_NAMED_BLOCK_LIMIT * 3 + 7) / 8];
int ctr_cfg_active(void) { return active; }
int ap_content_plan_active(void) { return planActive; }
uint64_t ap_content_plan_epoch(void) { return planActive ? epoch : 0; }
const ctr_content_pad *ap_content_plan_pad(int physical) { return planActive && pad.physical==physical ? &pad : NULL; }
const ctr_content_package *ap_content_plan_package(int index) { return planActive && index==0 && pad.package_index==0 ? &package : NULL; }
const ctr_content_checks *ap_content_plan_checks(void) { static const ctr_content_checks none; return planActive ? &families : &none; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) {
 return haveContext && level==context.hostLevelID ? &context : NULL;
}
int CustomTrack_ServingLoad(int level,int cup,int id) { (void)id; return level==context.hostLevelID && !cup && serving; }
const char *CustomTrack_ServeFaultReason(void) { return serveFault ? "fault" : NULL; }
int AP_CortexTrackActive(void) { return cortexLatch; }
int AP_RaceAttempt_ProducerBlocked(int kind) { assert(kind==AP_RESULT_PRODUCER_RELIC_PERFECT || kind==0); return blocked; }
int ap_net_location_exists(long code) { return code > 0; }
int ap_net_location_checked(long code) {
 for (int i=0;i<checkedCount;i++) if (checkedCodes[i]==code) return 1;
 return 0;
}
void ap_net_send_location(long code) { sentCodes[sentCount++]=code; checkedCodes[checkedCount++]=code; }
void AP_PerfNoteCheckSent(void) {}
void AP_AppendLog(const char *text) { (void)text; }
void AP_CeremonyLedgerAdd(long code,int bit,int tag) { (void)code; (void)bit; (void)tag; }
void AP_FeedOnLocationSent(long code) { (void)code; feed++; }
'''
for signature in ("static int AP_NamedLetterBit(int64_t code)",
                  "static int AP_NamedLetterReceived(int64_t code)",
                  "static int AP_ContentLetterItemReceived(int64_t code)",
                  "static int AP_EmitClassCheck(long code,",
                  "static long AP_RelicPerfectCustomCode(int slot)",
                  "static int AP_RelicPerfectServingCustom(struct GameTracker *gGT)",
                  "static const ctr_content_pad *AP_StandaloneRoutePad(",
                  "static const ctr_content_pad *AP_StandaloneRelicPad(void)",
                  "int AP_StandaloneRelicTarget(int tier)",
                  "static long AP_StandalonePerfectCode(int *outRoute)\n{",
                  "void AP_NotifyRelicPerfect(void)",
                  "int AP_NotifyStandaloneRelic(int raceTime)"):
    fixture += function(signature) + "\n"
fixture += r'''
#define CV_PAD 5
enum { RETAIL, CUSTOM, CORTEX };
static void reset(int kind) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context); memset(&pad,0,sizeof pad);
 memset(&package,0,sizeof package); memset(&families,0,sizeof families); memset(&ctr_cfg,0,sizeof ctr_cfg);
 memset(ap_cortex_letter_received,0,sizeof ap_cortex_letter_received);
 active=planActive=haveContext=serving=1; blocked=cortexLatch=serveFault=0; epoch=3; ap_state_gen=0;
 sentCount=feed=checkedCount=0;
 sdataStore.gGT=&game; game.drivers[0]=&driver; game.gameMode1=ADVENTURE_MODE|RELIC_RACE;
 game.timeCratesInLEV=kind==CORTEX ? 100 : 30; driver.numTimeCrates=game.timeCratesInLEV;
 context.physicalPad=CV_PAD; context.returnHub=27; context.seedEpoch=3; context.mode=AP_CONTENT_MODE_RELIC;
 context.retail=kind==RETAIL; context.cortex=kind==CORTEX; context.laps=3; context.slot=kind==CUSTOM ? 4 : 0;
 context.hostLevelID=kind==RETAIL ? 9 : kind==CORTEX ? 13 : 6; game.levelID=context.hostLevelID;
 strcpy(context.entryID,"entry/x"); strcpy(context.trackID,"x");
 pad.physical=CV_PAD; pad.hub=27; pad.occupied=1; pad.laps=3; pad.custom_slot=context.slot; pad.cortex=kind==CORTEX;
 pad.retail_id=kind==RETAIL ? 9 : -1; pad.package_index=kind==CUSTOM ? 0 : -1;
 pad.mode_count=2; pad.modes[0]=AP_CONTENT_MODE_TROPHY; pad.modes[1]=AP_CONTENT_MODE_RELIC; pad.stages[0]=pad.stages[1]=1;
 pad.trophy_location=kind==CORTEX ? 35026000 : 35011002;
 for (int t=0;t<3;t++) pad.relic_locations[t]=kind==CORTEX ? 35026001+t : kind==CUSTOM ? 35022009+t : 35012009+100*t;
 pad.perfect_location=kind==CORTEX ? 35026005 : kind==CUSTOM ? 35024003 : 35012402;
 context.trophyLocation=pad.relic_locations[0];
 strcpy(pad.entry_id,"entry/x"); strcpy(pad.track_id,"x");
 families.relic_perfect=1;
 package.has_relic_targets=1; package.relic_targets[0]=96000; package.relic_targets[1]=86400; package.relic_targets[2]=76800;
 for (int i=0;i<54;i++) data.RelicTime[i]=1;
 /* Legacy table rows a content plan must never read. */
 ctr_cfg.relic_perfect_enabled=1; for (int i=0;i<18;i++) ctr_cfg.relic_perfect[i]=AP_RelicPerfectExpectedCode(i);
}
static int sent(long code) { for (int i=0;i<sentCount;i++) if (sentCodes[i]==code) return 1; return 0; }
int main(void) {
 int cases=0;
 /* Every crate broken: the pad's own perfect, whatever the host. */
 for (int kind=RETAIL;kind<=CORTEX;kind++) {
  reset(kind); AP_NotifyRelicPerfect();
  assert(sentCount==1 && sent(pad.perfect_location) && feed==1); cases++;
  /* Already checked: nothing more. */
  AP_NotifyRelicPerfect(); assert(sentCount==1); cases++;
  /* The legacy option scalar is irrelevant to a content plan. */
  reset(kind); ctr_cfg.relic_perfect_enabled=0; AP_NotifyRelicPerfect(); assert(sentCount==1); cases++;
  /* A legacy Cortex latch or custom serve state does not redirect it. */
  reset(kind); cortexLatch=1; AP_NotifyRelicPerfect(); assert(sentCount==1 && sent(pad.perfect_location)); cases++;
 }
 /* Never the host's retail code: Cortex Vortex on 13 is not Oxide Station (35012415). */
 reset(CORTEX); AP_NotifyRelicPerfect(); assert(!sent(35012415) && sent(35026005)); cases++;
 for (int bad=0;bad<14;bad++) {
  reset(bad%3==0 ? CORTEX : bad%3==1 ? CUSTOM : RETAIL);
  switch (bad) {
   case 0: driver.numTimeCrates--; break;           /* one crate left */
   case 1: game.timeCratesInLEV=driver.numTimeCrates=0; break; /* no crates is no perfect */
   case 2: blocked=1; break;                         /* forced DeathLink loss */
   case 3: families.relic_perfect=0; pad.perfect_location=0; break; /* option off */
   case 4: pad.perfect_location=0; break;
   case 5: context.mode=AP_CONTENT_MODE_TROPHY; context.trophyLocation=pad.trophy_location; game.gameMode1=ADVENTURE_MODE; break;
   case 6: epoch=4; break;                            /* stale route */
   case 7: serving=0; break;                          /* custom bytes not served */
   case 8: haveContext=0; break;                      /* no content-plan race */
   case 9: pad.cortex=0; break;                       /* route says Cortex Vortex, pad does not */
   case 10: game.gameMode2=TOKEN_RACE; break;
   case 11: game.gameMode1|=ADVENTURE_CUP; break;
   case 12: context.hostLevelID=game.levelID=6; break; /* Cortex Vortex on the custom host */
   case 13: game.gameMode1&=~RELIC_RACE; break;
  }
  AP_NotifyRelicPerfect();
  assert(sentCount==0); cases++;
 }
 /* Outside a content plan the legacy table still answers. */
 reset(RETAIL); planActive=0; haveContext=0; serving=0; AP_NotifyRelicPerfect();
 assert(sentCount==1 && sent(AP_RelicPerfectExpectedCode(9))); cases++;
 /* Route identity: Cortex Vortex needs host 13 and the cortex flag on both sides. */
 reset(CORTEX); assert(AP_StandaloneRoutePad(&context)==&pad); cases++;
 reset(CORTEX); context.cortex=0; assert(AP_StandaloneRoutePad(&context)==NULL); cases++;
 reset(CORTEX); pad.cortex=0; assert(AP_StandaloneRoutePad(&context)==NULL); cases++;
 reset(CUSTOM); assert(AP_StandaloneRoutePad(&context)==&pad); cases++;
 reset(CUSTOM); pad.cortex=1; assert(AP_StandaloneRoutePad(&context)==NULL); cases++;
 /* Cortex Vortex relic targets are its own (the legacy pad track's), never the host table. */
 reset(CORTEX);
 for (int t=0;t<3;t++) assert(AP_StandaloneRelicTarget(t)==AP_CORTEX_RELIC_TARGETS.time[t] && AP_StandaloneRelicTarget(t)!=data.RelicTime[13*3+t]);
 assert(AP_NotifyStandaloneRelic(AP_CORTEX_RELIC_TARGETS.time[1])==2 && sent(35026001) && sent(35026002) && !sent(35026003)); cases++;
 reset(CORTEX); assert(AP_NotifyStandaloneRelic(AP_CORTEX_RELIC_TARGETS.time[0]+1)==0); cases++;
 reset(CUSTOM); assert(AP_StandaloneRelicTarget(2)==76800); cases++;
 /* Cortex Vortex letter items 35010200..202 read their own receipts. */
 reset(CORTEX);
 assert(!AP_ContentLetterItemReceived(35010201)); ap_cortex_letter_received[1]=1;
 assert(AP_ContentLetterItemReceived(35010201) && !AP_ContentLetterItemReceived(35010200) &&
        !AP_ContentLetterItemReceived(35010202) && !AP_ContentLetterItemReceived(35010203)); cases++;
 /* The pure rule: a content route answers only with its own code. */
 {
  AP_RelicPerfectFacts f; memset(&f,0,sizeof f);
  f.levelID=13; f.broken=f.total=100; f.retail=ctr_cfg.relic_perfect; f.contentRoute=1; f.contentCode=35026005;
  f.cortexActive=1; f.customServing=1; f.customCode=35024000;
  assert(AP_RelicPerfectResolvePure(&f)==35026005);
  f.contentCode=-1; assert(AP_RelicPerfectResolvePure(&f)==-1);
  f.contentCode=35026005; f.broken=99; assert(AP_RelicPerfectResolvePure(&f)==-1);
  f.broken=f.total=0; assert(AP_RelicPerfectResolvePure(&f)==-1);
  cases++;
 }
 printf("PASS: %d content-plan Cortex Vortex and Relic Race Perfect cases through production code\n",cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "cortex_perfect.c"
    binary = Path(temporary) / "cortex_perfect"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-I", str(root / "include"), "-I", str(root), "-I", str(root / "ap"),
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
