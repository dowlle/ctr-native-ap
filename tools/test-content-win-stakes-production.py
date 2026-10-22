"""Compile the production race-loss stakes of content-plan races (#133 PR 4a).

A content-plan room has no win_logic block, so before PR 4a every lost race
sent a race-loss DeathLink (the 0.2.3 fallback). The production
AP_ContentWinFacts (ap_hooks_race.c), AP_WinDecideContent (ap_win_logic.c)
and AP_WinStakesContentDecide (ap_win_logic.h) now decide from the race's own
pad: a loss sends while its win check is open and does not wait for letter
items. PR 4c adds the boost term: a win whose plan check carries a boost rank
is out of logic, so exempt, until the pad's racer (or any driveable racer)
has that many Progressive Boosts, counted from progression-flagged receipts
plus the plan's starting copies (AP_WinEnvContentCount, AP_WinContentCapMet).
Engine and network state are controlled seams; this does not claim a Steam
race.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()
win_logic = (root / "ap" / "ap_win_logic.c").read_text(encoding="utf-8")


def function(signature, text=source):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CTR_CUSTOM_TRACKS 1
#define AP_WIN_LOGIC_FREESTANDING_ONLY 1
#include "platform/native_custom_race_context.h"
#include "ap/ap_content_plan.h"
#include "ap/ap_content_modes.h"
#include "ap/ap_win_logic.h"
#define ADVENTURE_CUP 0x10000000
#define ADVENTURE_BOSS 0x80000000
#define AP_BOSS_VENUE_LOCATION_BASE 35011100L
struct GameTracker { int levelID; unsigned gameMode1; short bossID; } game;
/* Boss garages (#133 PR 6): the plan's `bosses` record and its check ranks. */
static ctr_content_bosses bossesRecord;
static long long boostCode; static int boostRank;
static long long roomCodes[8]; static int roomCount;
const ctr_content_bosses *ap_content_plan_bosses(void) { return &bossesRecord; }
int ap_content_plan_check_boost(int64_t code) { return code==boostCode ? boostRank : 0; }
int ap_net_location_exists(long long code) {
 for (int i=0;i<roomCount;i++) if (roomCodes[i]==code) return 1;
 return 0;
}
static struct { struct GameTracker *gGT; } sdataStore, *sdata=&sdataStore;
static struct CustomTrackRaceContext context;
static ctr_content_pad pad, crystal;
static int planActive, haveContext, crystalRunning;
static uint64_t epoch;
static long long checked[8]; static int checkedCount;
static long long received[8]; static int receivedCount;
static char lastLog[256];
int ap_content_plan_active(void) { return planActive; }
uint64_t ap_content_plan_epoch(void) { return planActive ? epoch : 0; }
const ctr_content_pad *ap_content_plan_pad(int physical) { return planActive && pad.physical==physical ? &pad : NULL; }
const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int level) {
 return haveContext && level==context.hostLevelID ? &context : NULL;
}
const ctr_content_pad *AP_ContentCrystalPad(void) { return crystalRunning ? &crystal : NULL; }
/* Content-plan cups (#133 PR 5): the running cup's pad, validated in production by AP_ContentCupPad. */
static ctr_content_pad cupPad; static int cupRunning;
const ctr_content_pad *AP_ContentCupPad(void) { return cupRunning ? &cupPad : NULL; }
static int AP_ContentLetterItemReceived(int64_t code) {
 for (int i=0;i<receivedCount;i++) if (received[i]==code) return 1;
 return 0;
}
int ap_net_location_checked(long long code) {
 for (int i=0;i<checkedCount;i++) if (checked[i]==code) return 1;
 return 0;
}
void AP_LogLine(const char *text) { snprintf(lastLog,sizeof lastLog,"%s",text); }
static AP_ItemIdTally g_wl_tally;
static int envBoostMode, envStart, envUnlocks;
static long long startId; static int startN;
int ap_content_plan_start_count(int64_t item) { return item==startId ? startN : 0; }
static AP_WinLogicEnv AP_WinEnvNow(void) {
 AP_WinLogicEnv env; memset(&env,0,sizeof env);
 env.boost_mode=envBoostMode; env.starting_character=envStart; env.character_unlocks=envUnlocks;
 env.unreliable=g_wl_tally.overflow; return env;
}
static void got(long long id, unsigned flags) { AP_ItemIdTallyReceive(&g_wl_tally,id,7,flags); }
'''
for signature in ("static const ctr_content_pad *AP_StandaloneRoutePad(",
                  "int AP_ContentWinFacts(long *outCode, int *outNeedsItems, int *outBoost, int *outRacer)\n{"):
    fixture += function(signature) + "\n"
for signature in ("static int AP_WinEnvContentCount(", "static int AP_WinContentCapMet("):
    fixture += function(signature, win_logic) + "\n"
fixture += function("static int AP_WinDecideContent(", win_logic) + "\n"
fixture += r'''
#define CC 3
static void reset(int mode, int retail) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context); memset(&pad,0,sizeof pad); memset(&crystal,0,sizeof crystal);
 planActive=haveContext=1; crystalRunning=0; cupRunning=0; memset(&cupPad,0,sizeof cupPad); epoch=4; checkedCount=receivedCount=0; lastLog[0]=0;
 AP_ItemIdTallyReset(&g_wl_tally); envBoostMode=0; envStart=0; envUnlocks=0; startId=0; startN=0;
 memset(&bossesRecord,0,sizeof bossesRecord); boostCode=0; boostRank=0; roomCount=0;
 sdataStore.gGT=&game;
 context.physicalPad=CC; context.returnHub=26; context.seedEpoch=4; context.mode=mode; context.retail=retail;
 context.laps=3; context.slot=retail?0:2; context.hostLevelID=retail?3:6; game.levelID=context.hostLevelID;
 strcpy(context.entryID,"entry/x"); strcpy(context.trackID,"x");
 pad.physical=CC; pad.hub=26; pad.occupied=1; pad.laps=3; pad.custom_slot=context.slot;
 pad.retail_id=retail?3:-1; pad.package_index=retail?-1:0;
 pad.mode_count=3; pad.modes[0]=AP_CONTENT_MODE_TROPHY; pad.modes[1]=AP_CONTENT_MODE_CTR; pad.modes[2]=AP_CONTENT_MODE_RELIC;
 pad.stages[0]=pad.stages[1]=pad.stages[2]=1;
 pad.trophy_location=35011000; pad.ctr_location=35012300;
 pad.relic_locations[0]=35012000; pad.relic_locations[1]=35012100; pad.relic_locations[2]=35012200;
 context.trophyLocation=mode==AP_CONTENT_MODE_TROPHY ? pad.trophy_location : mode==AP_CONTENT_MODE_CTR ? pad.ctr_location : pad.relic_locations[0];
 strcpy(pad.entry_id,"entry/x"); strcpy(pad.track_id,"x");
 pad.racer_lock=-1;
}
static AP_WinStakes decide(int *handled) { AP_WinStakes s; memset(&s,0,sizeof s); *handled=AP_WinDecideContent(&game,"at race start",&s); return s; }
int main(void) {
 int cases=0, handled;
 AP_WinStakes s;
 /* Trophy Race: open win -> a loss sends; collected -> exempt. */
 reset(AP_CONTENT_MODE_TROPHY,1); s=decide(&handled);
 assert(handled && s.stakes && s.why==AP_WS_WHY_STAKES && s.code==35011000 && strstr(lastLog,"content-plan")); cases++;
 checked[checkedCount++]=35011000; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 /* Custom track: same rule, the pad's own code, never the host's. */
 reset(AP_CONTENT_MODE_TROPHY,0); pad.trophy_location=context.trophyLocation=35201600; s=decide(&handled);
 assert(handled && s.stakes && s.code==35201600); cases++;
 /* CTR Challenge: the token check; Lettersanity items gate it. */
 reset(AP_CONTENT_MODE_CTR,1); s=decide(&handled);
 assert(handled && s.stakes && s.code==35012300); cases++;
 reset(AP_CONTENT_MODE_CTR,1); pad.letter_items[0]=35010139; pad.letter_items[2]=35010141;
 received[receivedCount++]=35010139; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 received[receivedCount++]=35010141; s=decide(&handled);
 assert(handled && s.stakes && s.code==35012300); cases++;
 checked[checkedCount++]=35012300; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 /* Relic Race: no win check (a Relic Race is never a loss send anyway). */
 reset(AP_CONTENT_MODE_RELIC,1); s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_NO_ENTRY); cases++;
 /* Crystal Challenge: its completion code. */
 reset(AP_CONTENT_MODE_TROPHY,1); haveContext=0; crystalRunning=1; crystal.trophy_location=35013018; s=decide(&handled);
 assert(handled && s.stakes && s.code==35013018); cases++;
 checked[checkedCount++]=35013018; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 /* Not a content-plan race on its route: the legacy path decides. */
 reset(AP_CONTENT_MODE_TROPHY,1); planActive=0; decide(&handled); assert(!handled); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); haveContext=0; decide(&handled); assert(!handled); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); epoch=5; decide(&handled); assert(!handled); cases++;   /* stale route */
 reset(AP_CONTENT_MODE_TROPHY,1); pad.hub=27; decide(&handled); assert(!handled); cases++; /* another pad */
 /* Boost terms (#133 PR 4c). Shared chain: the Trophy Race needs USF (2). */
 reset(AP_CONTENT_MODE_TROPHY,1); envBoostMode=1; pad.trophy_boost=2; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 got(35010027,1); s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 got(35010027,0); s=decide(&handled); assert(handled && !s.stakes); cases++;          /* not progression: not counted */
 got(35010027,1); s=decide(&handled); assert(handled && s.stakes && s.code==35011000); cases++;
 /* A starting copy from the plan's ledger counts. */
 reset(AP_CONTENT_MODE_TROPHY,1); envBoostMode=1; pad.trophy_boost=1; startId=35010027; startN=1; s=decide(&handled);
 assert(handled && s.stakes); cases++;
 /* Boost mode off: the rank is vacuous, as the cap leaf says. */
 reset(AP_CONTENT_MODE_TROPHY,1); envBoostMode=0; pad.trophy_boost=2; s=decide(&handled); assert(handled && s.stakes); cases++;
 /* CTR Challenge: its own rank, on top of the letter items. */
 reset(AP_CONTENT_MODE_CTR,1); envBoostMode=1; pad.ctr_boost=1; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 got(35010027,1); s=decide(&handled); assert(handled && s.stakes && s.code==35012300); cases++;
 /* Per racer: a locked pad reads its racer's chain (Coco, engine 3, item 35010035). */
 reset(AP_CONTENT_MODE_TROPHY,1); envBoostMode=2; envUnlocks=1; pad.trophy_boost=1; pad.racer_lock=3;
 got(35010031,1); s=decide(&handled); assert(handled && !s.stakes); cases++;          /* Crash's chain is not Coco's */
 got(35010035,1); s=decide(&handled); assert(handled && !s.stakes); cases++;          /* Coco not unlocked */
 got(35010124,1); s=decide(&handled); assert(handled && s.stakes); cases++;
 /* Per racer, unlocked pad: any driveable racer with the rank. */
 reset(AP_CONTENT_MODE_TROPHY,1); envBoostMode=2; envUnlocks=1; pad.trophy_boost=1;
 got(35010035,1); s=decide(&handled); assert(handled && !s.stakes); cases++;          /* Coco has it but is locked */
 got(35010031,1); s=decide(&handled); assert(handled && s.stakes); cases++;           /* the starting racer (Crash) has it */
 /* A collected win is exempt whatever the rank. */
 reset(AP_CONTENT_MODE_TROPHY,1); envBoostMode=1; pad.trophy_boost=2; checked[checkedCount++]=35011000; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 /* A content-plan cup (#133 PR 5): its Win and its rank, the pad's racer. */
 reset(AP_CONTENT_MODE_TROPHY,1); cupRunning=1; cupPad.cup=1; cupPad.trophy_location=35027004; cupPad.racer_lock=-1; s=decide(&handled);
 assert(handled && s.stakes && s.code==35027004); cases++;
 envBoostMode=1; cupPad.trophy_boost=2; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 got(35010027,1); got(35010027,1); s=decide(&handled); assert(handled && s.stakes && s.code==35027004); cases++;
 checked[checkedCount++]=35027004; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 /* With a sweep: once the Win is collected the Win All Legs is at stake, at its own rank. */
 cupPad.sweep_location=35027204; cupPad.sweep_boost=0; s=decide(&handled); assert(handled && s.stakes && s.code==35027204); cases++;
 cupPad.sweep_boost=3; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 checked[checkedCount++]=35027204; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 /* A boss race in a content plan's garage (#133 PR 6): the physical
    garage's own check (bossID 0..3 the Boss Races, 4 and 5 N. Oxide's two
    challenges) with its rank and no racer; without the record the plan
    decides nothing for it, as before. */
 reset(AP_CONTENT_MODE_TROPHY,1); haveContext=0; game.gameMode1=ADVENTURE_BOSS; game.bossID=3;
 decide(&handled); assert(!handled); cases++;                                           /* bosses off */
 bossesRecord.present=1; roomCodes[roomCount++]=35011103; s=decide(&handled);
 assert(handled && s.stakes && s.code==35011103); cases++;
 envBoostMode=1; boostCode=35011103; boostRank=2; s=decide(&handled);
 assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 got(35010027,1); got(35010027,1); s=decide(&handled); assert(handled && s.stakes && s.code==35011103); cases++;
 checked[checkedCount++]=35011103; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 game.bossID=5; roomCodes[roomCount++]=35011105; s=decide(&handled); assert(handled && s.stakes && s.code==35011105); cases++;
 game.bossID=4; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_NO_ENTRY); cases++; /* not in the room */
 game.gameMode1=ADVENTURE_BOSS|ADVENTURE_CUP; decide(&handled); assert(!handled); cases++;
 /* The pure decision on its own. */
 s=AP_WinStakesContentDecide(0,0,0); assert(!s.stakes && s.why==AP_WS_WHY_NO_ENTRY); cases++;
 s=AP_WinStakesContentDecide(35011000,1,1); assert(!s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
 s=AP_WinStakesContentDecide(35011000,0,1); assert(!s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
 s=AP_WinStakesContentDecide(35011000,0,0); assert(s.stakes && s.code==35011000); cases++;
 printf("PASS: %d content-plan race-loss stakes cases through production code\n",cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "stakes.c"
    binary = Path(temporary) / "stakes"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-I", str(root / "include"), "-I", str(root), "-I", str(root / "ap"),
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
