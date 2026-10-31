"""Compile the production race-loss stakes of content-plan races (one generator
row 6). A plan room carries `win_logic`, and the production AP_ContentWinSet
(ap_hooks_race.c) and AP_WinDecideContent (ap_win_logic.c) decide from it, as an
ordinary race does: only the win set comes from the pad. Engine and network state
are controlled seams; this does not claim a Steam race.

The Relic Race has stakes while a tier check is open and in logic. A ctr_first
CTR Challenge stakes its CTR Token and the Trophy Race check first place pays. A
plan room without the block keeps the 0.2.3 rule: every loss sends.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()
win_logic = (root / "ap" / "ap_win_logic.c").read_text(encoding="utf-8")
# Review finding 5: the identity hash must be computed in its own statement, not
# in the argument list of the call that reads it (C leaves that order open).
assert re.search(r"known = ap_net_identity_hash\(&id\);\s*if \(AP_WinStakesConnectHash\(&g_wl_latch, known, id\)\)", win_logic), \
    "AP_WinLogicTallyReset must hash the identity before the connect call"
assert "AP_WinStakesContentDecide" not in (root / "ap" / "ap_win_logic.h").read_text(encoding="utf-8")


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
static long long roomCodes[8]; static int roomCount;
const ctr_content_bosses *ap_content_plan_bosses(void) { return &bossesRecord; }
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
/* Boss cups (#133 PR 6b): no garage cup runs in these cases. */
int AP_ContentGarageCupRunning(void) { return -1; }
int ap_net_location_checked(long long code) {
 for (int i=0;i<checkedCount;i++) if (checked[i]==code) return 1;
 return 0;
}
void AP_LogLine(const char *text) { snprintf(lastLog,sizeof lastLog,"%s",text); }
static AP_ItemIdTally g_wl_tally;
static int envBoostMode, envStart, envUnlocks;
AP_WinLogic ctr_win_logic;
static int wl_node(int type, int a, int b, int c) {
 AP_WinLogicNode *n=&ctr_win_logic.nodes[ctr_win_logic.node_count]; n->tag=type; n->a=a; n->b=b; n->c=c;
 return ctr_win_logic.node_count++;
}
/* One win check whose rule is `rule` (a node index), in an always-open region. */
static void wl_check(long code, int rule) {
 AP_WinLogicCheck *e=&ctr_win_logic.checks[ctr_win_logic.check_count++];
 memset(e,0,sizeof *e); e->code=code; e->region=0; e->rule=rule;
 ctr_win_logic.region_root[0]=wl_node(AP_WL_T_TRUE,0,0,0); ctr_win_logic.region_count=1;
}
static int itemCount(void *user, long id) { (void)user; return AP_ItemIdTallyCount(&g_wl_tally,(long long)id); }
static int bossCount(void *user) { (void)user; return 0; }
static int collectedAt(void *user, long code) { (void)user; return ap_net_location_checked(code); }
static AP_WinLogicEnv AP_WinEnvNow(void) {
 AP_WinLogicEnv env; memset(&env,0,sizeof env);
 env.item_count=itemCount; env.bosses_won=bossCount; env.collected=collectedAt;
 env.boost_mode=envBoostMode; env.starting_character=envStart; env.character_unlocks=envUnlocks;
 env.unreliable=g_wl_tally.overflow; return env;
}
static void got(long long id, unsigned flags) { AP_ItemIdTallyReceive(&g_wl_tally,id,7,flags); }
'''
for signature in ("static const ctr_content_pad *AP_StandaloneRoutePad(", "static int AP_ContentWinAdd(",
                  "int AP_ContentWinSet(long set[AP_WS_WINSET_MAX])\n{"):
    fixture += function(signature) + "\n"
fixture += function("static int AP_WinDecideContent(", win_logic) + "\n"
fixture += r'''
#define CC 3
static void reset(int mode, int retail) {
 memset(&game,0,sizeof game); memset(&context,0,sizeof context); memset(&pad,0,sizeof pad); memset(&crystal,0,sizeof crystal);
 planActive=haveContext=1; crystalRunning=0; cupRunning=0; memset(&cupPad,0,sizeof cupPad); epoch=4; checkedCount=0; lastLog[0]=0;
 AP_ItemIdTallyReset(&g_wl_tally); envBoostMode=0; envStart=0; envUnlocks=0;
 memset(&bossesRecord,0,sizeof bossesRecord);
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
static void room(const long *codes, int n, int rule) {
 memset(&ctr_win_logic,0,sizeof ctr_win_logic); ctr_win_logic.state=AP_WL_VALID; roomCount=0;
 for (int i=0;i<n;i++) { int node=rule>=0 ? rule : wl_node(AP_WL_T_TRUE,0,0,0); wl_check(codes[i],node); roomCodes[roomCount++]=codes[i]; }
}
int main(void) {
 int cases=0, handled;
 AP_WinStakes s;
 /* Trophy Race: open and in logic -> a loss sends; collected -> exempt. */
 { const long codes[]={35011000}; reset(AP_CONTENT_MODE_TROPHY,1); room(codes,1,-1); s=decide(&handled);
   assert(handled && s.stakes && s.why==AP_WS_WHY_STAKES && s.code==35011000 && strstr(lastLog,"content-plan")); cases++;
   checked[checkedCount++]=35011000; s=decide(&handled);
   assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++; }
 /* Out of the exported logic: exempt. A received item puts it in. */
 { const long codes[]={35011000}; reset(AP_CONTENT_MODE_TROPHY,1);
   memset(&ctr_win_logic,0,sizeof ctr_win_logic); ctr_win_logic.state=AP_WL_VALID;
   ctr_win_logic.ids[0]=35010031; ctr_win_logic.id_count=1;
   room(codes,0,-1); ctr_win_logic.state=AP_WL_VALID; ctr_win_logic.ids[0]=35010031; ctr_win_logic.id_count=1;
   wl_check(35011000,wl_node(AP_WL_T_ITEMS_SUM,1,0,1)); roomCodes[roomCount++]=35011000;
   s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_OUT_OF_LOGIC); cases++;
   got(35010031,1); s=decide(&handled); assert(handled && s.stakes); cases++; }
 /* Without a block: every loss sends, the 0.2.3 rule. */
 { const long codes[]={35011000}; reset(AP_CONTENT_MODE_TROPHY,1); room(codes,1,-1); ctr_win_logic.state=AP_WL_ABSENT; s=decide(&handled);
   assert(handled && s.stakes && s.why==AP_WS_WHY_NO_BLOCK); cases++; }
 /* Custom track: the pad's own code, never the host's. */
 { const long codes[]={35201600}; reset(AP_CONTENT_MODE_TROPHY,0); pad.trophy_location=context.trophyLocation=35201600; room(codes,1,-1); s=decide(&handled);
   assert(handled && s.stakes && s.code==35201600); cases++; }
 /* CTR Challenge: the token check. */
 { const long codes[]={35012300}; reset(AP_CONTENT_MODE_CTR,1); room(codes,1,-1); s=decide(&handled);
   assert(handled && s.stakes && s.code==35012300); cases++;
   checked[checkedCount++]=35012300; s=decide(&handled);
   assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++; }
 /* Relic Race: stakes while a tier check is open and in logic (Stef, 2026-10-29). */
 { const long codes[]={35012000,35012100}; reset(AP_CONTENT_MODE_RELIC,1); pad.relic_locations[2]=0; room(codes,2,-1); s=decide(&handled);
   assert(handled && s.stakes && s.code==35012000); cases++;
   checked[checkedCount++]=35012000; s=decide(&handled); assert(handled && s.stakes && s.code==35012100); cases++;
   checked[checkedCount++]=35012100; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++; }
 /* A Relic Race with no tier check here has no win: exempt. */
 { reset(AP_CONTENT_MODE_RELIC,1); room(NULL,0,-1); pad.relic_route=35012000; pad.relic_locations[0]=pad.relic_locations[1]=pad.relic_locations[2]=0; s=decide(&handled);
   assert(handled && !s.stakes && s.why==AP_WS_WHY_NO_ENTRY); cases++; }
 /* Crystal Challenge: its completion code. */
 { const long codes[]={35013018}; reset(AP_CONTENT_MODE_TROPHY,1); room(codes,1,-1); haveContext=0; crystalRunning=1; crystal.trophy_location=35013018; s=decide(&handled);
   assert(handled && s.stakes && s.code==35013018); cases++; }
 /* Not a content-plan race on its route: the legacy path decides. */
 reset(AP_CONTENT_MODE_TROPHY,1); planActive=0; decide(&handled); assert(!handled); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); haveContext=0; decide(&handled); assert(!handled); cases++;
 reset(AP_CONTENT_MODE_TROPHY,1); epoch=5; decide(&handled); assert(!handled); cases++;   /* stale route */
 reset(AP_CONTENT_MODE_TROPHY,1); pad.hub=27; decide(&handled); assert(!handled); cases++; /* another pad */
 /* A cup: its Win and its Win All Legs, kept for the whole cup. */
 { const long codes[]={35027004,35027204}; reset(AP_CONTENT_MODE_TROPHY,1); room(codes,2,-1); cupRunning=1; cupPad.cup=1; cupPad.trophy_location=35027004; cupPad.sweep_location=35027204; cupPad.racer_lock=-1; s=decide(&handled);
   assert(handled && s.stakes && s.code==35027004); cases++;
   checked[checkedCount++]=35027004; s=decide(&handled); assert(handled && s.stakes && s.code==35027204); cases++;
   checked[checkedCount++]=35027204; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++; }
 /* A boss race in a plan's garage: the physical garage's own check; bossID 4
    and 5 are N. Oxide's two challenges; without the record the plan decides
    nothing for it. */
 { const long codes[]={35011103,35011105}; reset(AP_CONTENT_MODE_TROPHY,1); room(codes,2,-1); haveContext=0; game.gameMode1=ADVENTURE_BOSS; game.bossID=3;
   decide(&handled); assert(!handled); cases++;
   bossesRecord.present=1; s=decide(&handled); assert(handled && s.stakes && s.code==35011103); cases++;
   game.bossID=5; s=decide(&handled); assert(handled && s.stakes && s.code==35011105); cases++;
   game.bossID=4; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_NO_ENTRY); cases++;
   game.gameMode1=ADVENTURE_BOSS|ADVENTURE_CUP; decide(&handled); assert(!handled); cases++; }
 /* ctr_first: the CTR Token and the Trophy Race check first place pays. */
 { const long codes[]={35011000,35012300}; reset(AP_CONTENT_MODE_CTR,1); room(codes,2,-1);
   pad.ctr_first=1; pad.mode_count=2; pad.modes[0]=AP_CONTENT_MODE_CTR; pad.modes[1]=AP_CONTENT_MODE_RELIC;
   s=decide(&handled); assert(handled && s.stakes && s.code==35012300); cases++;
   checked[checkedCount++]=35012300; s=decide(&handled); assert(handled && s.stakes && s.code==35011000); cases++;
   checked[checkedCount++]=35011000; s=decide(&handled); assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++; }
 /* A classic CTR Challenge pays no Trophy, a forged flag on a pad with a Trophy mode neither. */
 { const long codes[]={35011000,35012300}; reset(AP_CONTENT_MODE_CTR,1); room(codes,2,-1); checked[checkedCount++]=35012300; s=decide(&handled);
   assert(handled && !s.stakes && s.why==AP_WS_WHY_COLLECTED); cases++;
   pad.ctr_first=1; s=decide(&handled); assert(handled && !s.stakes); cases++; }
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
