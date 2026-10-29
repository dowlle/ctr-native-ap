"""Compile the production ctr_first hooks of content-plan races (J11, decision 14).

A content-plan track entry that runs ctr_first has no Trophy Race: its modes
are the CTR Challenge, then the Relic Race. Through the production code of the
ap_hooks unit this pins that a qualifying CTR win (first place with the letters
the CTR Challenge needs, Lettersanity letter items included) sends the CTR code
and the entry's Trophy Race code once each, skipping checked codes; that the
podium rungs are routed from that CTR Challenge by finishing place, letters or
not (finish fan-out, live held listener and the won-Trophy backstop); that the
plain Trophy rung route stays closed on such a pad; and that a classic entry
(ctr_first 0) and a forged flag on an entry that still has a Trophy mode keep
the classic behaviour. The pure pad decisions (pending checks, pad state, glow
families and the selector rows) come from ap_content_modes.h; the race-loss
stakes are in test-content-win-stakes-production.py. Engine and network state
are controlled seams; this does not claim a Steam race.
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
#include "ap/ap_race_structure_logic.h"
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
 struct { int slot, host_level_id; long trophy_location; ctr_podium_rungs podium; } custom_track;
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
/* The legacy ctr_first answer (ctr_cfg.adventure_structure + award pairs):
   a content plan must never consult it. */
static int legacySuppress, legacyAsked;
long AP_LookupLocationCode(int bit) { return bit; }
long AP_TrialTrackLocation(int level,int challenge) { (void)level; (void)challenge; return -1; }
int AP_RaceStructureSuppressTrophy(long code) { (void)code; legacyAsked++; return legacySuppress; }
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
static char lastLog[256];
void AP_AppendLog(const char *text) { assert(!strstr(text,"refused") && !strstr(text,"mismatch")); snprintf(lastLog,sizeof lastLog,"%s",text); }
void AP_CeremonyLedgerAdd(long code,int bit,int tag) { (void)code; (void)bit; (void)tag; }
void AP_FeedOnLocationSent(long code) { (void)code; }
void AP_FeedOnRungSent(long code,int tag) { (void)code; (void)tag; }
/* Cortex Vortex letter items (ap_cortex_track.h), read by AP_ContentLetterItemReceived (#133 PR 3c). */
static inline int AP_CortexLetterItemIndexPure(long long idx) { return idx >= 200 && idx < 203 ? (int)(idx - 200) : -1; }
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
                  "static const ctr_content_pad *AP_StandaloneLetterPad(int track)",
                  "static void AP_ContentLetterState(",
                  "int AP_LetterAvailable(int track, int letter)",
                  "long AP_LetterLocation(int track, int letter)",
                  "int AP_LettersRequiredMet(int track)",
                  "int AP_LettersRequiredCount(int track)",
                  "int AP_LetterTokenEarned(int track, int didWin, int collected)",
                  "int AP_NotifyStandaloneRace(void)",
                  "int AP_RaceStructurePodiumCtrFirst(int podiumTrack)",
                  "static void AP_EmitRung(int track, long code, int rungTag, int position,",
                  "static void AP_SendPodiumChecks(int track, int placement)\n{",
                  "static void AP_SendHeldChecks(int track, int position)\n{",
                  "static void AP_ReconcilePodiumFromTrophies(void)\n{"):
    fixture += function(signature) + "\n"
fixture += r'''
#define CC 3      /* Crash Cove's physical pad */
#define TROPHY 35011000L
#define CTR 35012300L
static void reset(int mode, int ctrFirst) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context); memset(&pad,0,sizeof pad);
 memset(&families,0,sizeof families); memset(&ctr_cfg,0,sizeof ctr_cfg);
 memset(ap_letter_received,0,sizeof ap_letter_received); memset(ap_custom_letter_received,0,sizeof ap_custom_letter_received);
 memset(ap_named_letter_received,0,sizeof ap_named_letter_received);
 active=planActive=haveContext=serving=1; blocked=0; epoch=4; sentCount=checkedCount=0; legacySuppress=legacyAsked=0;
 sdataStore.gGT=&game; game.drivers[0]=&driver; driver.driverRank=0; driver.PickupLetterHUD.numCollected=3;
 game.gameMode1=ADVENTURE_MODE; game.gameMode2=mode==AP_CONTENT_MODE_CTR ? TOKEN_RACE : 0;
 context.physicalPad=CC; context.returnHub=26; context.seedEpoch=4; context.mode=mode; context.retail=1;
 context.laps=3; context.slot=0; context.hostLevelID=3; game.levelID=3;
 strcpy(context.entryID,"entry/x"); strcpy(context.trackID,"x");
 pad.physical=CC; pad.hub=26; pad.occupied=1; pad.laps=3; pad.retail_id=3; pad.package_index=-1;
 if (ctrFirst) {
  pad.ctr_first=1; pad.mode_count=2; pad.modes[0]=AP_CONTENT_MODE_CTR; pad.modes[1]=AP_CONTENT_MODE_RELIC;
  pad.stages[0]=1; pad.stages[1]=2;
 } else {
  pad.mode_count=3; pad.modes[0]=AP_CONTENT_MODE_TROPHY; pad.modes[1]=AP_CONTENT_MODE_CTR; pad.modes[2]=AP_CONTENT_MODE_RELIC;
  pad.stages[0]=pad.stages[1]=pad.stages[2]=1;
 }
 pad.trophy_location=TROPHY; pad.ctr_location=CTR;
 pad.relic_locations[0]=35012000; pad.relic_locations[1]=35012100; pad.relic_locations[2]=35012200;
 pad.rung_locations[0]=35015100; pad.rung_locations[1]=35015101; pad.rung_locations[3]=35015103; pad.rung_locations[4]=35015002;
 context.trophyLocation=mode==AP_CONTENT_MODE_TROPHY ? pad.trophy_location : mode==AP_CONTENT_MODE_CTR ? pad.ctr_location : pad.relic_locations[0];
 strcpy(pad.entry_id,"entry/x"); strcpy(pad.track_id,"x");
 families.podium=27;
}
static int sent(long code) { for (int i=0;i<sentCount;i++) if (sentCodes[i]==code) return 1; return 0; }
static int sentTimes(long code) { int n=0; for (int i=0;i<sentCount;i++) n+=sentCodes[i]==code; return n; }
static int checkedFn(int64_t code) { return ap_net_location_checked((long)code); }
/* The listener's decision (ap_hooks_race_checks.c): AP_RetailPodiumTrack is
   AP_StandalonePodiumTrack for a content-plan race. */
static int listenerEarns(void) {
 int podium=AP_StandalonePodiumTrack();
 return podium>=0 && AP_RaceStructureRaceEarnsRungs(context.mode==AP_CONTENT_MODE_TROPHY, context.mode==AP_CONTENT_MODE_CTR,
                                                    AP_RaceStructurePodiumCtrFirst(podium));
}
int main(void) {
 int cases=0;
 const int logical=AP_CONTENT_PODIUM_LOGICAL_BASE+CC;
 /* ── rungs come from the CTR Challenge by finishing place ── */
 reset(AP_CONTENT_MODE_CTR,1);
 assert(AP_StandalonePodiumTrack()==logical && AP_RaceStructurePodiumCtrFirst(logical)==1 && listenerEarns()); cases++;
 assert(legacyAsked==0); cases++;                                   /* the seed scalar is never read */
 { int places[]={1,2,4,9}; int want[]={4,3,1,1};
   for (int i=0;i<4;i++) {
    reset(AP_CONTENT_MODE_CTR,1); driver.PickupLetterHUD.numCollected=0;  /* letters or not */
    AP_SendPodiumChecks(logical, places[i]);
    assert(sentCount==want[i] && sent(35015002) && !sent(TROPHY) && !sent(CTR)); cases++;
   } }
 reset(AP_CONTENT_MODE_CTR,1); AP_SendHeldChecks(logical,1);
 assert(sentCount==2 && sent(35015100) && sent(35015101)); cases++;   /* the live held listener */
 reset(AP_CONTENT_MODE_CTR,1); blocked=1; AP_SendPodiumChecks(logical,1); assert(sentCount==0); cases++; /* forced loss */
 /* The Relic Race of a ctr_first pad earns no rung. */
 reset(AP_CONTENT_MODE_RELIC,1); assert(AP_StandalonePodiumTrack()==-1 && !listenerEarns()); cases++;
 /* A stale route or another pad: no identity. */
 reset(AP_CONTENT_MODE_CTR,1); epoch=5; assert(AP_StandalonePodiumTrack()==-1); cases++;
 reset(AP_CONTENT_MODE_CTR,1); pad.hub=27; assert(AP_StandalonePodiumTrack()==-1); cases++;
 /* Backstop: a checked Trophy (paid by the CTR win) earns every rung, once. */
 reset(AP_CONTENT_MODE_CTR,1); AP_ReconcilePodiumFromTrophies(); assert(sentCount==0);
 checkedCodes[checkedCount++]=TROPHY; AP_ReconcilePodiumFromTrophies(); assert(sentCount==4);
 AP_ReconcilePodiumFromTrophies(); assert(sentCount==4); cases++;
 /* ── the qualifying CTR win pays the CTR and the Trophy, once each ── */
 reset(AP_CONTENT_MODE_CTR,1);
 assert(AP_NotifyStandaloneRace()==1 && sentTimes(CTR)==1 && sentTimes(TROPHY)==1 && sentCount==2); cases++;
 assert(strstr(lastLog,"ctr_first") && strstr(lastLog,"35011000")); cases++;
 assert(AP_NotifyStandaloneRace()==0 && sentCount==2); cases++;      /* a re-win sends nothing */
 reset(AP_CONTENT_MODE_CTR,1); checkedCodes[checkedCount++]=CTR;      /* CTR collected, Trophy open */
 assert(AP_NotifyStandaloneRace()==1 && sentCount==1 && sent(TROPHY)); cases++;
 reset(AP_CONTENT_MODE_CTR,1); checkedCodes[checkedCount++]=TROPHY;   /* Trophy collected, CTR open */
 assert(AP_NotifyStandaloneRace()==1 && sentCount==1 && sent(CTR)); cases++;
 reset(AP_CONTENT_MODE_CTR,1); driver.driverRank=1; assert(AP_NotifyStandaloneRace()==0 && sentCount==0); cases++; /* second */
 reset(AP_CONTENT_MODE_CTR,1); driver.PickupLetterHUD.numCollected=2; assert(AP_NotifyStandaloneRace()==0 && sentCount==0); cases++;
 reset(AP_CONTENT_MODE_CTR,1); blocked=1; assert(AP_NotifyStandaloneRace()==0 && sentCount==0); cases++; /* forced loss */
 /* Lettersanity items: the win waits for the pooled letters, then pays both. */
 reset(AP_CONTENT_MODE_CTR,1); families.lettersanity=2; families.letters_per_track=2;
 pad.letter_locations[0]=35012500; pad.letter_locations[2]=35012502; pad.letter_items[0]=35010139; pad.letter_items[2]=35010141;
 driver.PickupLetterHUD.numCollected=2; assert(AP_NotifyStandaloneRace()==0 && sentCount==0); cases++;
 { int level,letter; assert(AP_LetterItemIndexToIdentityPure(139,&level,&letter)); ap_letter_received[level][letter]=1;
   assert(AP_LetterItemIndexToIdentityPure(141,&level,&letter)); ap_letter_received[level][letter]=1; }
 assert(AP_NotifyStandaloneRace()==1 && sent(CTR) && sent(TROPHY) && sentCount==2); cases++;
 /* ── classic content pads are unchanged ── */
 reset(AP_CONTENT_MODE_CTR,0);
 assert(AP_StandalonePodiumTrack()==-1 && AP_RaceStructurePodiumCtrFirst(logical)==0 && !listenerEarns()); cases++;
 assert(AP_NotifyStandaloneRace()==1 && sentCount==1 && sent(CTR) && !sent(TROPHY)); cases++;
 reset(AP_CONTENT_MODE_TROPHY,0);
 assert(AP_StandalonePodiumTrack()==logical && AP_RaceStructurePodiumCtrFirst(logical)==0 && listenerEarns()); cases++;
 driver.PickupLetterHUD.numCollected=0; assert(AP_NotifyStandaloneRace()==1 && sentCount==1 && sent(TROPHY)); cases++;
 /* The legacy answer is ignored on a content plan, whatever it says. */
 reset(AP_CONTENT_MODE_CTR,0); legacySuppress=1; assert(AP_RaceStructurePodiumCtrFirst(logical)==0 && legacyAsked==0); cases++;
 assert(AP_RaceStructurePodiumCtrFirst(3)==0 && AP_RaceStructurePodiumCtrFirst(-1)==0); cases++;
 /* A forged flag on an entry that still has a Trophy mode: classic. */
 reset(AP_CONTENT_MODE_CTR,0); pad.ctr_first=1;
 assert(!ap_content_pad_ctr_first(&pad) && AP_StandalonePodiumTrack()==-1); cases++;
 assert(AP_NotifyStandaloneRace()==1 && sentCount==1 && !sent(TROPHY)); cases++;
 /* ── pure pad decisions (ap_content_modes.h) ── */
 reset(AP_CONTENT_MODE_CTR,1);
 { int64_t codes[AP_CONTENT_FAMILY_SLOTS]; int slots[AP_CONTENT_FAMILY_SLOTS];
   assert(ap_content_rung_mode(&pad)==AP_CONTENT_MODE_CTR); cases++;
   assert(ap_content_mode_family_codes(&pad,AP_CONTENT_MODE_CTR,codes,slots)==4 && slots[0]==0 && codes[3]==35015002); cases++;
   assert(ap_content_mode_family_codes(&pad,AP_CONTENT_MODE_TROPHY,codes,slots)==0); cases++;
   assert(ap_content_mode_family_codes(&pad,AP_CONTENT_MODE_RELIC,codes,slots)==0); cases++;
   assert(ap_content_mode_paired_trophy(&pad,AP_CONTENT_MODE_CTR)==TROPHY &&
          ap_content_mode_paired_trophy(&pad,AP_CONTENT_MODE_RELIC)==0 &&
          ap_content_mode_paired_trophy(&pad,AP_CONTENT_MODE_TROPHY)==0); cases++; }
 { int counts[15]={0};
   /* CTR, Trophy and four rungs count under the CTR mode; three relic tiers under Relic. */
   assert(ap_content_mode_pending(&pad,AP_CONTENT_MODE_CTR,checkedFn)==6); cases++;
   assert(ap_content_mode_pending(&pad,AP_CONTENT_MODE_RELIC,checkedFn)==3); cases++;
   assert(ap_content_track_pad_state(&pad,counts,checkedFn)==2); cases++;
   checkedCodes[checkedCount++]=CTR; assert(ap_content_mode_pending(&pad,AP_CONTENT_MODE_CTR,checkedFn)==5); cases++;
   checkedCodes[checkedCount++]=35015100; checkedCodes[checkedCount++]=35015101; checkedCodes[checkedCount++]=35015103;
   checkedCodes[checkedCount++]=35015002;
   /* Only the Trophy is left at stage 1: the pad keeps its open look. */
   assert(ap_content_mode_pending(&pad,AP_CONTENT_MODE_CTR,checkedFn)==1 && ap_content_track_pad_state(&pad,counts,checkedFn)==2); cases++;
   checkedCodes[checkedCount++]=TROPHY;
   /* Stage-1 work done, the Relic Race waits behind stage 2. */
   assert(ap_content_track_pad_state(&pad,counts,checkedFn)==3); cases++;
   /* The selector: one row per entry mode, CTR then Relic, no Trophy row. */
   { ap_content_selector sel; ap_content_selector_build(&pad,counts,checkedFn,&sel);
     assert(sel.count==2 && sel.mode[0]==AP_CONTENT_MODE_CTR && sel.mode[1]==AP_CONTENT_MODE_RELIC &&
            sel.open[0] && !sel.open[1] && sel.cursor==0); cases++; } }
 /* A classic entry keeps its rungs on the Trophy Race and pays no paired Trophy. */
 reset(AP_CONTENT_MODE_TROPHY,0);
 { int64_t codes[AP_CONTENT_FAMILY_SLOTS]; int slots[AP_CONTENT_FAMILY_SLOTS];
   assert(ap_content_rung_mode(&pad)==AP_CONTENT_MODE_TROPHY &&
          ap_content_mode_family_codes(&pad,AP_CONTENT_MODE_TROPHY,codes,slots)==4 &&
          ap_content_mode_family_codes(&pad,AP_CONTENT_MODE_CTR,codes,slots)==0 &&
          ap_content_mode_paired_trophy(&pad,AP_CONTENT_MODE_CTR)==0 &&
          ap_content_mode_pending(&pad,AP_CONTENT_MODE_CTR,checkedFn)==1); cases++; }
 /* Crystal and cup entries never run ctr_first. */
 reset(AP_CONTENT_MODE_CTR,1); pad.crystal=1; assert(!ap_content_pad_ctr_first(&pad) && ap_content_rung_mode(&pad)==-1); cases++;
 reset(AP_CONTENT_MODE_CTR,1); pad.cup=1; assert(!ap_content_pad_ctr_first(&pad) && ap_content_rung_mode(&pad)==-1); cases++;
 printf("PASS: %d content-plan ctr_first cases through production code\n",cases);
 return 0;
}
'''

# Production wiring the compiled cases do not reach on their own.
hooks = (root / "ap" / "ap_hooks.c").read_text(encoding="utf-8")
pads = (root / "ap" / "ap_hooks_pads.c").read_text(encoding="utf-8")
checks = (root / "ap" / "ap_hooks_race_checks.c").read_text(encoding="utf-8")
game222 = (root / "game" / "222.c").read_text(encoding="utf-8")
warp = (root / "game" / "232" / "AH_WarpPad.c").read_text(encoding="utf-8")
# The pad display bit of a ctr_first Trophy resolves to its code (AP_LookupLocationCode).
assert "if (contentMode == AP_CONTENT_MODE_TROPHY && ap_content_pad_ctr_first(pad))" in hooks
# The glow lists the paired Trophy under the CTR mode.
glow = pads[pads.index("int AP_PadUncollectedGlowBits("):]
glow = glow[:glow.index("\n}\n")]
assert "ap_content_mode_paired_trophy(pad,pad->modes[n])" in glow and "AP_CONTENT_BIT_TROPHY+pad->physical" in glow
# Both rung producers ask the same per-podium question.
assert checks.count("AP_RaceStructurePodiumCtrFirst(podiumTrack)") == 2
# The content-plan result path reports through AP_NotifyStandaloneRace.
assert "AP_NotifyStandaloneRace();" in game222
# The content-plan mode menu is built from the entry's own modes.
assert re.search(r"AP_ContentSelectorBuild\(pad, &sel\);\s*AH_WarpPad_ContentModeRows\(&sel\);", warp)

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "ctrfirst.c"
    binary = Path(temporary) / "ctrfirst"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-I", str(root / "include"), "-I", str(root), "-I", str(root / "ap"),
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
