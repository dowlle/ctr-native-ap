"""Compile the production standalone finish sender and common check emitter.

Engine/network state are controlled seams; this does not claim a Steam race.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks.c").read_text()


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
#define ADVENTURE_MODE 1
#define ARCADE_MODE 2
#define ADVENTURE_CUP 4
#define ADVENTURE_BOSS 8
#define RELIC_RACE 16
#define TOKEN_RACE 1
#define AP_RESULT_PRODUCER_CUSTOM_TROPHY 1
struct Driver { int driverRank; } driver;
struct GameTracker { int levelID,gameMode1,gameMode2; struct Driver *drivers[1]; } game;
static struct {struct GameTracker *gGT;} data, *sdata=&data;
static struct CustomTrackRaceContext context;
static int active,blocked,serving,exists,checked,haveContext,sent,feed;
static long sentCode;
static int planActive;
static uint64_t epoch;
static ctr_content_pad pad;
int ap_content_plan_active(void) { return planActive; }
uint64_t ap_content_plan_epoch(void) { return planActive ? epoch : 0; }
const ctr_content_pad *ap_content_plan_pad(int physical) {
 return planActive && pad.physical==physical ? &pad : NULL;
}
int ctr_cfg_active(void) { return active; }
int AP_RaceAttempt_ProducerBlocked(int kind) { assert(kind==1); return blocked; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) {
 return haveContext && level==context.hostLevelID ? &context : NULL;
}
int CustomTrack_ServingLoad(int level,int cup,int id) {
 assert(level==context.hostLevelID && cup==0 && id==0); return serving;
}
int ap_net_location_exists(long code) { assert(code==context.trophyLocation); return exists; }
int ap_net_location_checked(long code) { assert(code==context.trophyLocation); return checked; }
void ap_net_send_location(long code) { sent++; sentCode=code; }
void AP_AppendLog(const char *text) { assert(strstr(text,"standalone")); }
void AP_CeremonyLedgerAdd(long code,int bit,int tag) { (void)code; (void)bit; (void)tag; assert(0); }
void AP_FeedOnLocationSent(long code) { assert(code==sentCode); feed++; }
'''
fixture += function("static int AP_EmitClassCheck(long code,")
fixture += function("int AP_NotifyStandaloneRace(void)")
fixture += r'''
static void reset(void) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context);
 context.hostLevelID=6; context.trophyLocation=35016300; context.slot=1;
 strcpy(context.entryID,"entry-1"); strcpy(context.trackID,"custom/baby-t-park");
 game.levelID=6; game.gameMode1=ADVENTURE_MODE; game.drivers[0]=&driver;
 driver.driverRank=0; data.gGT=&game;
 active=serving=exists=haveContext=1; checked=blocked=sent=feed=0; sentCode=-1;
 planActive=0; epoch=0; memset(&pad,0,sizeof pad);
}
int main(void) {
 int cases=0;
 for(int mode=0;mode<32;mode++) for(int token=0;token<2;token++)
 for(int rank=0;rank<8;rank++) for(int loss=0;loss<2;loss++) {
  reset(); game.gameMode1=mode; game.gameMode2=token;
  driver.driverRank=rank; blocked=loss;
  int expected=mode==ADVENTURE_MODE && !token && rank==0 && !loss;
  assert(AP_NotifyStandaloneRace()==expected);
  assert(sent==expected && feed==expected);
  if(sent) assert(sentCode==35016300);
  cases++;
 }
 for(int state=0;state<9;state++) {
  reset();
  switch(state) {
   case 0: data.gGT=NULL; break;
   case 1: game.drivers[0]=NULL; break;
   case 2: active=0; break;
   case 3: haveContext=0; break;
   case 4: serving=0; break;
   case 5: exists=0; break;
   case 6: checked=1; break;
   case 7: game.levelID=3; break;
   case 8: context.trophyLocation=INT64_C(4294967296)+35016300; break;
  }
  assert(!AP_NotifyStandaloneRace() && !sent && !feed); cases++;
 }
 reset(); assert(AP_NotifyStandaloneRace());
 checked=1; assert(!AP_NotifyStandaloneRace() && sent==1);
 for(int retail=0;retail<2;retail++) for(int bad=0;bad<10;bad++) {
  reset(); planActive=1; epoch=7; context.seedEpoch=7;
  context.retail=retail; context.slot=retail?0:1; context.laps=retail?3:7;
  context.returnHub=26; context.physicalPad=3;
  pad.physical=3; pad.hub=26; pad.occupied=1; pad.laps=context.laps;
  pad.retail_id=retail?6:-1; pad.custom_slot=context.slot;
  pad.trophy_location=context.trophyLocation;
  strcpy(pad.entry_id,context.entryID); strcpy(pad.track_id,context.trackID);
  if(retail) serving=0; /* retail needs a retained route, not custom bytes */
  switch(bad) {
   case 1: epoch++; break;
   case 2: planActive=0; break;
   case 3: pad.physical++; break;
   case 4: pad.trophy_location++; break;
   case 5: pad.hub++; break;
   case 6: pad.laps++; break;
   case 7: pad.custom_slot++; break;
   case 8: strcpy(pad.entry_id,"wrong-entry"); break;
   case 9: strcpy(pad.track_id,"wrong-track"); break;
  }
  assert(AP_NotifyStandaloneRace()==(bad==0)); assert(sent==(bad==0)); cases++;
 }
 printf("PASS: %d standalone finish/refusal cases and checked-state replay through production emitter\n",cases);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "result.c"
    binary = Path(temporary) / "result"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                    "-I", str(root / "include"), "-I", str(root), str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
