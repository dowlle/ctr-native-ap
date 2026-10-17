"""Compile the production per-track check hooks of content-plan races (#133 PR 3b).

The podium rung routing (logical track, finish ladder, live held rungs and the
won-Trophy backstop), the Reach 10 Wumpa code of a race, the Lettersanity
letter hooks and the CTR Challenge award, all with the pad's own codes and the
received letter items. Engine and network state are controlled seams; this does
not claim a Steam race.
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
#define CTR_CUSTOM_TRACKS 1
#include "platform/native_custom_race_context.h"
#include "ap/ap_class_check_policy.h"
#include "ap/ap_content_plan.h"
#include "ap/ap_content_modes.h"
#include "ap/ap_lettersanity.h"
#define ADVENTURE_MODE 1
#define ARCADE_MODE 2
#define ADVENTURE_CUP 4
#define ADVENTURE_BOSS 8
#define RELIC_RACE 16
#define CRYSTAL_CHALLENGE 32
#define TOKEN_RACE 1
#define AP_ITEM_BASE 35010000
#define AP_RESULT_PRODUCER_CUSTOM_TROPHY 1
#define AP_RESULT_PRODUCER_PODIUM 2
#define CTR_CFG_PODIUM_TRACK_COUNT 16
#define CTR_CFG_TRIAL_TRACK_COUNT 2
#define CTR_CFG_LETTER_TRACK_COUNT 18
#define CTR_CFG_LETTER_COUNT 3
#define AP_TRIAL_PODIUM_LOGICAL_BASE 48
#define AP_CV_PODIUM_LOGICAL_TRACK 50
#define ADV_REWARD_FIRST_TROPHY 6
#define CTR_CFG_TRIAL_TROPHY 0
#define AP_CV_SLOT_TROPHY 0
#define AP_CV_SLOT_LETTER0 0
#define AP_CHECKDIAG_ABSENT 0
#define AP_CHECKDIAG_MISMATCH 1
enum { AP_RUNG_HELD_1ST, AP_RUNG_HELD_3RD, AP_RUNG_HELD_5TH, AP_RUNG_FINISH_PODIUM, AP_RUNG_FINISH_ANY };
typedef struct { long held_1st, held_3rd, held_5th, finish_podium, finish_any; } ctr_podium_rungs;
struct Driver { int driverRank; struct { int numCollected; } PickupLetterHUD; } driver;
struct GameTracker { int levelID,gameMode1,gameMode2; struct Driver *drivers[1]; struct { int cupID; } cup; } game;
static struct { struct GameTracker *gGT; } sdataStore, *sdata=&sdataStore;
static struct {
 int podium_enabled, custom_tracks_ok, lettersanity_mode, custom_lettersanity_mode;
 ctr_podium_rungs podium[18];
 struct { int valid; ctr_podium_rungs podium; } cortex_track;
 struct { int slot, host_level_id; ctr_podium_rungs podium; } custom_track;
 long lettersanity_locations[18][3], custom_letter_locations[3];
} ctr_cfg;
static struct CustomTrackRaceContext context;
static ctr_content_pad pad;
static ctr_content_checks families;
static int active, planActive, haveContext, serving, blocked;
static uint64_t epoch;
static long sentCodes[16]; static int sentCount;
static long checkedCodes[32]; static int checkedCount;
static int ap_checkdiag_once;
static unsigned char ap_letter_received[18][3], ap_custom_letter_received[AP_CUSTOM_LETTER_SLOT_COUNT][3];
static unsigned char ap_cortex_letter_received[3];
static unsigned char ap_named_letter_received[(AP_NAMED_BLOCK_LIMIT * 3 + 7) / 8];
int ctr_cfg_active(void) { return active; }
int ap_content_plan_active(void) { return planActive; }
uint64_t ap_content_plan_epoch(void) { return planActive ? epoch : 0; }
const ctr_content_pad *ap_content_plan_pad(int physical) { return planActive && pad.physical==physical ? &pad : NULL; }
const ctr_content_checks *ap_content_plan_checks(void) { static const ctr_content_checks none; return planActive ? &families : &none; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) {
 return haveContext && level==context.hostLevelID ? &context : NULL;
}
int CustomTrack_ServingLoad(int level,int cup,int id) { (void)cup; (void)id; return level==context.hostLevelID && serving; }
int CustomTrack_RetailPodiumLevelID(int level,int cup,int id) { (void)cup; (void)id; return level; }
int AP_CortexTrackActive(void) { return 0; }
int AP_CortexTrackChecked(int slot) { (void)slot; return 0; }
long AP_CortexTrackCode(int slot) { (void)slot; return -1; }
int AP_CustomTrackTrophyChecked(void) { return 0; }
int AP_LocationCheckedByBit(int bit) { (void)bit; return 0; }
int AP_TrialTrackLocationChecked(int level,int challenge) { (void)level; (void)challenge; return 0; }
int AP_RaceAttempt_ProducerBlocked(int kind) { (void)kind; return blocked; }
int AP_CheckDiagOnce(int *s,int a,int b,int c,int d) { (void)s; (void)a; (void)b; (void)c; (void)d; return 1; }
int ap_net_is_connected(void) { return 1; }
int ap_net_location_exists(long code) { return code > 0; }
int ap_net_location_checked(long code) {
 for (int i=0;i<checkedCount;i++) if (checkedCodes[i]==code) return 1;
 return 0;
}
void ap_net_send_location(long code) { sentCodes[sentCount++]=code; checkedCodes[checkedCount++]=code; }
void AP_PerfNoteCheckSent(void) {}
void AP_AppendLog(const char *text) { assert(!strstr(text,"refused") && !strstr(text,"mismatch")); }
void AP_CeremonyLedgerAdd(long code,int bit,int tag) { (void)code; (void)bit; (void)tag; }
void AP_FeedOnLocationSent(long code) { (void)code; }
void AP_FeedOnRungSent(long code,int tag) { (void)code; (void)tag; }
/* Legacy letter paths stay out of a content-plan race. */
static int AP_CustomLetterSlot(int track) { (void)track; return 0; }
static int AP_CortexLetterTrack(int track) { (void)track; return 0; }
static void AP_CortexLetterCodes(long codes[3]) { codes[0]=codes[1]=codes[2]=-1; }
'''
for signature in ("static int AP_NamedLetterBit(int64_t code)",
                  "static void AP_NamedLetterReceive(int64_t code)",
                  "static int AP_NamedLetterReceived(int64_t code)",
                  "static int AP_ContentLetterItemReceived(int64_t code)",
                  "static int AP_EmitClassCheck(long code,",
                  "static const ctr_content_pad *AP_StandaloneRoutePad(",
                  "int AP_StandalonePodiumTrack(void)",
                  "static int AP_ContentPodiumRungs(int track, ctr_podium_rungs *out)",
                  "static long AP_StandaloneWumpaCode(void)",
                  "static const ctr_content_pad *AP_StandaloneLetterPad(int track)",
                  "static void AP_ContentLetterState(",
                  "int AP_LetterAvailable(int track, int letter)",
                  "long AP_LetterLocation(int track, int letter)",
                  "int AP_LettersRequiredMet(int track)",
                  "int AP_LettersRequiredCount(int track)",
                  "int AP_LetterTokenEarned(int track, int didWin, int collected)",
                  "int AP_NotifyStandaloneRace(void)",
                  "static void AP_EmitRung(int track, long code, int rungTag, int position,",
                  "static void AP_SendPodiumChecks(int track, int placement)\n{",
                  "static void AP_SendHeldChecks(int track, int position)\n{",
                  "static void AP_ReconcilePodiumFromTrophies(void)\n{"):
    fixture += function(signature) + "\n"
fixture += r'''
#define CC 3      /* Crash Cove's physical pad */
static void reset(int mode, int retail) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context); memset(&pad,0,sizeof pad);
 memset(&families,0,sizeof families); memset(&ctr_cfg,0,sizeof ctr_cfg);
 memset(ap_letter_received,0,sizeof ap_letter_received); memset(ap_custom_letter_received,0,sizeof ap_custom_letter_received);
 memset(ap_named_letter_received,0,sizeof ap_named_letter_received);
 active=planActive=haveContext=serving=1; blocked=0; epoch=4; sentCount=checkedCount=0;
 sdataStore.gGT=&game; game.drivers[0]=&driver; driver.driverRank=0; driver.PickupLetterHUD.numCollected=0;
 game.gameMode1=ADVENTURE_MODE; game.gameMode2=mode==AP_CONTENT_MODE_CTR ? TOKEN_RACE : 0;
 context.physicalPad=CC; context.returnHub=26; context.seedEpoch=4; context.mode=mode; context.retail=retail;
 context.laps=3; context.slot=retail?0:2; context.hostLevelID=retail?3:6; game.levelID=context.hostLevelID;
 strcpy(context.entryID,"entry/x"); strcpy(context.trackID,"x");
 pad.physical=CC; pad.hub=26; pad.occupied=1; pad.laps=3; pad.custom_slot=context.slot;
 pad.retail_id=retail?3:-1; pad.package_index=retail?-1:0;
 pad.mode_count=3; pad.modes[0]=AP_CONTENT_MODE_TROPHY; pad.modes[1]=AP_CONTENT_MODE_CTR; pad.modes[2]=AP_CONTENT_MODE_RELIC;
 pad.stages[0]=pad.stages[1]=pad.stages[2]=1;
 pad.trophy_location=35011000; pad.ctr_location=35012300;
 pad.relic_locations[0]=35012000; pad.relic_locations[1]=35012100; pad.relic_locations[2]=35012200;
 pad.rung_locations[0]=35015100; pad.rung_locations[1]=35015101; pad.rung_locations[3]=35015103; pad.rung_locations[4]=35015002;
 pad.wumpa_location=35016104; pad.wumpa_modes=3u;
 context.trophyLocation=mode==AP_CONTENT_MODE_TROPHY ? pad.trophy_location : mode==AP_CONTENT_MODE_CTR ? pad.ctr_location : pad.relic_locations[0];
 strcpy(pad.entry_id,"entry/x"); strcpy(pad.track_id,"x");
 families.podium=27; families.wumpa=2;
}
static int sent(long code) { for (int i=0;i<sentCount;i++) if (sentCodes[i]==code) return 1; return 0; }
int main(void) {
 int cases=0;
 /* Podium: the logical track is the pad's, only for its Trophy Race. */
 reset(AP_CONTENT_MODE_TROPHY,1);
 assert(AP_StandalonePodiumTrack()==AP_CONTENT_PODIUM_LOGICAL_BASE+CC); cases++;
 for (int bad=0;bad<5;bad++) {
  reset(bad==0?AP_CONTENT_MODE_CTR:AP_CONTENT_MODE_TROPHY,bad==3?0:1);
  if (bad==1) epoch=5;
  if (bad==2) haveContext=0;
  if (bad==3) serving=0;           /* a custom track whose bytes are not served */
  if (bad==4) pad.hub=27;
  assert(AP_StandalonePodiumTrack()==-1); cases++;
 }
 /* Finish ladder with the pad's rungs; held 5th is not in this seed. */
 int places[]={1,2,4,9}; int want[]={4,3,1,1};
 for (int i=0;i<4;i++) {
  reset(AP_CONTENT_MODE_TROPHY,1);
  AP_SendPodiumChecks(AP_CONTENT_PODIUM_LOGICAL_BASE+CC, places[i]);
  assert(sentCount==want[i] && sent(35015002)); cases++;
  if (places[i]==1) assert(sent(35015100) && sent(35015101) && sent(35015103));
  if (places[i]==2) assert(!sent(35015100) && sent(35015101) && sent(35015103));
 }
 reset(AP_CONTENT_MODE_TROPHY,1); AP_SendHeldChecks(AP_CONTENT_PODIUM_LOGICAL_BASE+CC,1);
 assert(sentCount==2 && sent(35015100) && sent(35015101)); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); AP_SendHeldChecks(AP_CONTENT_PODIUM_LOGICAL_BASE+CC,3);
 assert(sentCount==1 && sent(35015101)); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); blocked=1; AP_SendPodiumChecks(AP_CONTENT_PODIUM_LOGICAL_BASE+CC,1);
 assert(sentCount==0); cases++;  /* a forced loss owns no rung */
 reset(AP_CONTENT_MODE_TROPHY,1); memset(pad.rung_locations,0,sizeof pad.rung_locations);
 AP_SendPodiumChecks(AP_CONTENT_PODIUM_LOGICAL_BASE+CC,1); assert(sentCount==0); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); AP_SendPodiumChecks(AP_CONTENT_PODIUM_LOGICAL_BASE+CC+1,1);
 assert(sentCount==0); cases++;  /* another pad's identity resolves nothing */
 reset(AP_CONTENT_MODE_TROPHY,1); ctr_cfg.podium_enabled=1; ctr_cfg.podium[3].held_1st=35015012;
 AP_SendPodiumChecks(3,1); assert(sentCount==0); cases++; /* legacy rows are not read in a content plan */
 /* Backstop: a checked Trophy Race earns every rung, once. */
 reset(AP_CONTENT_MODE_TROPHY,1); AP_ReconcilePodiumFromTrophies(); assert(sentCount==0);
 checkedCodes[checkedCount++]=pad.trophy_location; AP_ReconcilePodiumFromTrophies();
 assert(sentCount==4); AP_ReconcilePodiumFromTrophies(); assert(sentCount==4); cases++;
 /* Wumpa: the pad's own check from Trophy and CTR, global, never Relic. */
 reset(AP_CONTENT_MODE_TROPHY,1); assert(AP_StandaloneWumpaCode()==35016104); cases++;
 reset(AP_CONTENT_MODE_CTR,0); assert(AP_StandaloneWumpaCode()==35016104); cases++;
 reset(AP_CONTENT_MODE_RELIC,1); assert(AP_StandaloneWumpaCode()==-1); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); families.wumpa=1; families.wumpa_global=35016100; pad.wumpa_location=0;
 assert(AP_StandaloneWumpaCode()==35016100); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); families.wumpa=0; assert(AP_StandaloneWumpaCode()==-1); cases++;
 reset(AP_CONTENT_MODE_TROPHY,0); serving=0; assert(AP_StandaloneWumpaCode()==-1); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); epoch=9; assert(AP_StandaloneWumpaCode()==-1); cases++;
 /* Letters, Lettersanity off: the retail rule on the host level. */
 reset(AP_CONTENT_MODE_CTR,1);
 assert(AP_LetterAvailable(3,1) && AP_LetterLocation(3,0)==-1 && AP_LettersRequiredCount(3)==3 &&
        AP_LetterTokenEarned(3,1,3) && !AP_LetterTokenEarned(3,1,2)); cases++;
 /* Mode 1: letter checks, letters stay vanilla, token needs all three. */
 reset(AP_CONTENT_MODE_CTR,1); families.lettersanity=1; families.letters_per_track=2;
 pad.letter_locations[0]=35012500; pad.letter_locations[2]=35012502;
 assert(AP_LetterAvailable(3,1) && AP_LetterLocation(3,0)==35012500 && AP_LetterLocation(3,1)==-1 &&
        AP_LetterTokenEarned(3,1,3) && !AP_LetterTokenEarned(3,1,2)); cases++;
 /* Mode 2 on a retail track: drawn letters need their items, T is transparent. */
 reset(AP_CONTENT_MODE_CTR,1); families.lettersanity=2; families.letters_per_track=2;
 pad.letter_locations[0]=35012500; pad.letter_locations[2]=35012502;
 pad.letter_items[0]=35010139; pad.letter_items[2]=35010141;
 assert(!AP_LetterAvailable(3,0) && !AP_LetterAvailable(3,1) && AP_LettersRequiredCount(3)==2 && !AP_LettersRequiredMet(3));
 { int level,letter; assert(AP_LetterItemIndexToIdentityPure(139,&level,&letter)); ap_letter_received[level][letter]=1; }
 assert(AP_LetterAvailable(3,0) && !AP_LetterAvailable(3,2) && !AP_LetterTokenEarned(3,1,2));
 { int level,letter; assert(AP_LetterItemIndexToIdentityPure(141,&level,&letter)); ap_letter_received[level][letter]=1; }
 assert(AP_LettersRequiredMet(3) && AP_LetterTokenEarned(3,1,2) && !AP_LetterTokenEarned(3,0,2)); cases++;
 /* The CTR award follows the same rule through the production finish sender. */
 driver.PickupLetterHUD.numCollected=2; assert(AP_NotifyStandaloneRace()==1 && sent(35012300)); cases++;
 reset(AP_CONTENT_MODE_CTR,1); families.lettersanity=2; families.letters_per_track=2;
 pad.letter_locations[0]=35012500; pad.letter_locations[2]=35012502;
 pad.letter_items[0]=35010139; pad.letter_items[2]=35010141; driver.PickupLetterHUD.numCollected=2;
 assert(AP_NotifyStandaloneRace()==0 && sentCount==0); cases++; /* letters without their items */
 /* Mode 3 on a named custom track: block letter items. */
 reset(AP_CONTENT_MODE_CTR,0); families.lettersanity=3;
 for (int l=0;l<3;l++) pad.letter_items[l]=35100000+5*8+l;
 assert(!AP_LetterAvailable(6,1) && AP_LetterLocation(6,1)==-1 && AP_LettersRequiredCount(6)==3);
 AP_NamedLetterReceive(35100041); assert(AP_LetterAvailable(6,1) && !AP_LetterAvailable(6,0));
 AP_NamedLetterReceive(35100040); AP_NamedLetterReceive(35100042); AP_NamedLetterReceive(35100043);
 assert(!AP_NamedLetterReceived(35100043) && AP_LettersRequiredMet(6) && AP_LetterTokenEarned(6,1,3)); cases++;
 /* Mode 2 on a generic slot: Custom Track 2's letter items. */
 reset(AP_CONTENT_MODE_CTR,0); families.lettersanity=2; families.letters_per_track=1;
 pad.letter_locations[1]=35020004; pad.letter_items[1]=35021004;
 assert(!AP_LetterAvailable(6,1)); ap_custom_letter_received[1][1]=1;
 assert(AP_LetterAvailable(6,1) && !AP_LetterAvailable(6,0) && AP_LetterTokenEarned(6,1,1)); cases++;
 /* Outside a content-plan CTR Challenge the letter hooks fall back. */
 reset(AP_CONTENT_MODE_TROPHY,1); families.lettersanity=3; pad.letter_items[0]=35010139;
 assert(AP_LetterAvailable(3,0) && AP_LetterLocation(3,0)!=35012500); cases++;
 reset(AP_CONTENT_MODE_CTR,1); families.lettersanity=3; pad.letter_items[0]=35010139; epoch=7;
 assert(AP_LetterAvailable(3,0)); cases++;  /* a stale route reads no content pad */
 printf("PASS: %d content-plan podium, Wumpa and Lettersanity hook cases through production code\n",cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "families.c"
    binary = Path(temporary) / "families"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-I", str(root / "include"), "-I", str(root), "-I", str(root / "ap"),
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
