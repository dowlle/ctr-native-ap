"""Start pad activities from the in-game tracker (#375, one generator row 6):
execute the shared activity decision, the start lifecycle and the real schema-18
parser on the host, and pin the production call sites.

Plan pads only: a room without a plan keeps an inspection-only tracker. For every
combination of hub Keys, pad state, stage 1, racer lock, replay, custom content
and open modes, a row is open exactly when the plan pad's own entry takes the
kart and offers that race. Cups, Crystal Challenges and garages are pinned the
same way. The start lifecycle never loads anything itself and drops on a timeout,
a different level or a closed gate. Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

logic = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CTR_AP 1
#include "ap_content_modes.h"
#include "ap_pad_activity_logic.h"
#include "ap_tracker_launch_logic.h"

static int64_t g_won;
static int won_fn(int64_t code) { return g_won && code == g_won; }

enum { T = 0, C = 1, R = 2 };

/* The plan pad's entry in AH_WarpPad_ThTick order: an occupied, Ready pad; not
 * Done unless it replays; stage 1 with the racer lock; not closed again. */
static int pad_takes(int state, int ready, int s1, int replay)
{
 if (!ready || (state == 5 && !replay) || !s1) return 0;
 return state != 3;
}

static void content_parity(void)
{
 int cases = 0;
 for (int keys = 0; keys < 2; keys++)
 for (int ready = 0; ready < 2; ready++)
 for (int s1 = 0; s1 < 2; s1++)
 for (int racer = 0; racer < 2; racer++)
 for (int state = 1; state <= 5; state++)
 for (int replay = 0; replay < 2; replay++)
 for (int n = 1; n <= 3; n++)
 for (int open = 0; open < 8; open++) {
  ap_padact_facts f; ap_padact_list l; int i;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_CONTENT; f.occupied = 1; f.keysHave = keys; f.keysNeed = 1; f.padState = state;
  f.contentReady = ready; f.stage1Met = s1; f.padReqMet = s1; f.racerMet = racer; f.modeCount = n; f.replayable = replay;
  for (i = 0; i < n; i++) { f.contentMode[i] = (i + 1) % 3; f.contentOpen[i] = (open >> i) & 1; f.contentLeft[i] = i & 1; }
  AP_PadActivityDecide(&f, &l);
  assert(l.count == n);
  for (i = 0; i < n; i++) {
   int takes = keys && pad_takes(state, ready, s1, replay) && f.contentOpen[i];
   assert((l.reason[i] == AP_PADACT_OPEN) == takes);
   assert(l.activity[i] == f.contentMode[i]);
   /* The row answers the pad's menu, one row or three. */
   assert(l.launchMode[i] == f.contentMode[i]);
   assert(l.left[i] == (i & 1));
   cases++;
  }
 }
 printf("content parity: %d cases\n", cases);
}

/* The menu's rows are the open rows of the list on the same pad. */
static void menu_parity(void)
{
 int cases = 0;
 for (int order = 0; order < 2; order++)
 for (int won = 0; won < 2; won++)
 for (int stage2 = 0; stage2 < 2; stage2++) {
  ctr_content_pad pad; ap_rs_rows rows; ap_content_selector sel; ap_padact_facts f; ap_padact_list l;
  int counts[15] = {0}; int i, r;
  memset(&pad, 0, sizeof pad);
  pad.occupied = 1; pad.first_race_rule = 1; pad.mode_count = 3; pad.racer_lock = -1;
  pad.modes[0] = order ? AP_CONTENT_MODE_RELIC : AP_CONTENT_MODE_TROPHY;
  pad.modes[1] = AP_CONTENT_MODE_CTR;
  pad.modes[2] = order ? AP_CONTENT_MODE_TROPHY : AP_CONTENT_MODE_RELIC;
  pad.stages[0] = 1; pad.stages[1] = pad.stages[2] = 2; pad.has_stage2 = 1; pad.stage2_count = 1; pad.stage2_mask = 1;
  pad.trophy_location = 1; pad.ctr_location = 2; pad.relic_locations[0] = 3;
  g_won = won ? (order ? 3 : 1) : 0;
  counts[0] = stage2;
  ap_content_menu_rows(&pad, counts, won_fn, 0, &rows);
  ap_content_selector_build(&pad, counts, won_fn, &sel);
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_CONTENT; f.occupied = 1; f.contentReady = 1; f.padState = 2; f.stage1Met = f.padReqMet = f.racerMet = 1;
  f.firstRaceWon = won; f.stage2Open = stage2; f.modeCount = sel.count;
  for (i = 0; i < sel.count; i++) { f.contentMode[i] = sel.mode[i]; f.contentOpen[i] = sel.open[i]; f.contentLeft[i] = sel.pending[i]; }
  AP_PadActivityDecide(&f, &l);
  for (i = 0, r = 0; i < l.count; i++) if (l.reason[i] == AP_PADACT_OPEN) {
   assert(r < rows.count && rows.mode[r] == l.launchMode[i]); r++;
  }
  assert(r == rows.count);
  /* A later race waits for the first race, then for stage 2, and says which. */
  if (!won) assert(l.mask[1] & AP_PADACT_BIT(AP_PADACT_LOCK_TROPHY));
  if (!stage2) assert(l.mask[1] & AP_PADACT_BIT(AP_PADACT_LOCK_STAGE2));
  cases++;
 }
 printf("menu parity: %d cases\n", cases);
}

static void single_and_garage(void)
{
 for (int keys = 0; keys < 2; keys++)
 for (int open = 0; open < 2; open++) {
  ap_padact_facts f; ap_padact_list l;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_GARAGE; f.keysHave = keys; f.keysNeed = 1; f.garageOpen = open;
  AP_PadActivityDecide(&f, &l);
  assert(l.count == 1 && l.activity[0] == AP_PADACT_BOSS);
  assert(l.reason[0] == (!keys ? AP_PADACT_LOCK_HUB : open ? AP_PADACT_OPEN : AP_PADACT_LOCK_BOSS));
 }
 {
  ap_padact_facts f; ap_padact_list l;
  memset(&f, 0, sizeof f);
  AP_PadActivityDecide(&f, &l);
  assert(l.count == 0);
 }
 /* Cups and Crystal Challenges: a finished cup replays, a finished Crystal
  * Challenge does not, and neither starts on a pad without its content. */
 for (int state = 1; state <= 5; state++)
 for (int ready = 0; ready < 2; ready++)
 for (int replay = 0; replay < 2; replay++)
 for (int s1 = 0; s1 < 2; s1++) {
  ap_padact_facts f; ap_padact_list l;
  if (state == 3 || state == 4) continue;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_SINGLE; f.occupied = 1; f.single = replay ? AP_PADACT_CUP : AP_PADACT_CRYSTAL;
  f.keysHave = f.keysNeed = 1; f.padState = state; f.contentReady = ready; f.stage1Met = s1; f.padReqMet = s1;
  f.racerMet = 1; f.replayable = replay;
  AP_PadActivityDecide(&f, &l);
  assert(l.count == 1 && l.launchMode[0] == -1);
  assert((l.reason[0] == AP_PADACT_OPEN) == (ready && s1 && (state != 5 || replay)));
  if (ready && state == 5 && !replay) assert(l.reason[0] == AP_PADACT_LOCK_DONE);
  if (state == 5) assert(!l.left[0] || l.reason[0] != AP_PADACT_OPEN);
 }
 /* A spare pad is closed and asks for nothing. */
 {
  ap_padact_facts f; ap_padact_list l;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_CONTENT; f.padState = 5;
  AP_PadActivityDecide(&f, &l);
  assert(l.count == 0);
 }
}

static void hub_keys(void)
{
 /* world.json region entrances and the Cups Room door. */
 assert(AP_PadActivityHubKeys(1, 0) == 0);  /* N. Sanity Beach */
 assert(AP_PadActivityHubKeys(0, 16) == 1); /* Gem Stone Valley */
 assert(AP_PadActivityHubKeys(2, 3) == 1);  /* The Lost Ruins */
 assert(AP_PadActivityHubKeys(3, 7) == 2);  /* Glacier Park */
 assert(AP_PadActivityHubKeys(4, 11) == 3); /* Citadel City */
 for (int cup = 100; cup < 105; cup++) assert(AP_PadActivityHubKeys(0, cup) == 2);
 assert(AP_PadActivityHubKeys(0, -1) == 1); /* Oxide's garage; its own gate needs more */
 assert(AP_PadActivityHubKeys(5, 0) > 4 && AP_PadActivityHubKeys(-1, 0) > 4);
}

static void cursor(void)
{
 ap_padact_list l; memset(&l, 0, sizeof l);
 l.count = 3;
 l.reason[0] = AP_PADACT_OPEN; l.left[0] = 0;
 l.reason[1] = AP_PADACT_LOCK_STAGE2; l.left[1] = 1;
 l.reason[2] = AP_PADACT_OPEN; l.left[2] = 1;
 assert(AP_PadActivityCursor(&l) == 2);
 l.left[2] = 0; assert(AP_PadActivityCursor(&l) == 0);
 l.reason[0] = l.reason[2] = AP_PADACT_LOCK_HUB; assert(AP_PadActivityCursor(&l) == 0);
 l.activity[0] = T; l.launchMode[0] = T; l.reason[0] = AP_PADACT_OPEN;
 assert(AP_PadActivityStillOpen(&l, T, T));
 l.reason[0] = AP_PADACT_LOCK_RACER; assert(!AP_PadActivityStillOpen(&l, T, T));
 assert(!AP_PadActivityStillOpen(&l, AP_PADACT_BOSS, -1));
}

static void lifecycle(void)
{
 ap_tl_request r; int i;
 AP_TLReset(&r);
 /* Same hub: wait until the hub is safe (pause menu closed), then the pad
  * takes the kart and its menu takes the mode. No hub load is asked. */
 assert(AP_TLBegin(&r, 1, 1, 3, 7, C, C) == AP_TL_BEGIN_HERE && r.stage == AP_TL_LOADING);
 assert(AP_TLBegin(&r, 1, 1, 3, 7, C, C) == AP_TL_BEGIN_REFUSED); /* one start at a time */
 assert(!AP_TLPadArrives(&r, 1, 3, 7));
 for (i = 0; i < 5; i++) assert(AP_TLFrame(&r, 1, 1, 1) == AP_TL_DROP_NONE && r.stage == AP_TL_LOADING);
 assert(!AP_TLWantsLoad(&r));
 assert(AP_TLFrame(&r, 1, 0, 1) == AP_TL_DROP_NONE && r.stage == AP_TL_ARRIVE);
 assert(AP_TLPadArrives(&r, 1, 3, 7) && !AP_TLPadArrives(&r, 1, 3, 8) && !AP_TLPadArrives(&r, 2, 3, 7));
 assert(!AP_TLGarageArrives(&r, 1));
 AP_TLTaken(&r); assert(r.stage == AP_TL_TAKEN && !AP_TLPadArrives(&r, 1, 3, 7));
 assert(AP_TLTakeMode(&r, 3, 8) == -1);
 assert(AP_TLTakeMode(&r, 3, 7) == C && r.stage == AP_TL_IDLE);
 assert(AP_TLTakeMode(&r, 3, 7) == -1);
 /* A pad without a menu is done when it takes the kart. */
 AP_TLBegin(&r, 0, 0, 100, 100, AP_PADACT_CUP, -1); AP_TLFrame(&r, 0, 0, 1);
 AP_TLTaken(&r); assert(r.stage == AP_TL_IDLE);
 /* Another hub: the load is asked once, when the hub is safe; then the
  * start waits through the load and arrives in the pad's hub. */
 assert(AP_TLBegin(&r, 1, 4, 11, 11, T, T) == AP_TL_BEGIN_LOAD_HUB && r.stage == AP_TL_LOADING);
 assert(AP_TLFrame(&r, 1, 1, 1) == AP_TL_DROP_NONE); /* not safe yet */
 assert(AP_TLFrame(&r, 1, 0, 1) == AP_TL_DROP_NONE && r.stage == AP_TL_LOADING);
 assert(AP_TLWantsLoad(&r) && !AP_TLWantsLoad(&r));
 assert(!AP_TLPadArrives(&r, 4, 11, 11));
 for (i = 0; i < 100; i++) assert(AP_TLFrame(&r, -1, 1, 1) == AP_TL_DROP_NONE);
 assert(AP_TLFrame(&r, 4, 1, 1) == AP_TL_DROP_NONE && r.stage == AP_TL_LOADING);
 assert(AP_TLFrame(&r, 4, 0, 1) == AP_TL_DROP_NONE && r.stage == AP_TL_ARRIVE);
 assert(AP_TLPadArrives(&r, 4, 11, 11));
 /* The pad never takes the kart: dropped after a second. */
 for (i = 1; i < AP_TL_ARRIVE_FRAMES; i++) assert(AP_TLFrame(&r, 4, 0, 1) == AP_TL_DROP_NONE);
 assert(AP_TLFrame(&r, 4, 0, 1) == AP_TL_DROP_TIMEOUT && r.stage == AP_TL_IDLE);
 /* The gate closes before the load: dropped, nothing loaded. */
 AP_TLBegin(&r, 1, 4, 11, 11, T, T);
 assert(AP_TLFrame(&r, 1, 0, 0) == AP_TL_DROP_GATE && r.stage == AP_TL_IDLE && !AP_TLWantsLoad(&r));
 /* The gate closes on arrival: dropped, never entered. */
 AP_TLBegin(&r, 1, 4, 11, 11, T, T); AP_TLFrame(&r, 1, 0, 1); AP_TLWantsLoad(&r);
 assert(AP_TLFrame(&r, 4, 0, 1) == AP_TL_DROP_NONE && r.stage == AP_TL_ARRIVE);
 assert(AP_TLFrame(&r, 4, 0, 0) == AP_TL_DROP_GATE && r.stage == AP_TL_IDLE);
 /* The player ends up somewhere else after arriving: dropped. */
 AP_TLBegin(&r, 4, 4, 11, 11, T, T); AP_TLFrame(&r, 4, 0, 1);
 assert(AP_TLFrame(&r, -1, 0, 1) == AP_TL_DROP_ELSEWHERE && r.stage == AP_TL_IDLE);
 /* Arrived, then an unrelated load leaves the hub before the pad takes the
  * kart: dropped at once, even while loading, so a later return cannot fire. */
 AP_TLBegin(&r, 4, 4, 11, 11, T, T); AP_TLFrame(&r, 4, 0, 1); assert(r.stage == AP_TL_ARRIVE);
 assert(AP_TLFrame(&r, -1, 1, 1) == AP_TL_DROP_ELSEWHERE && r.stage == AP_TL_IDLE);
 /* A same-hub start that leaves its hub before arriving: dropped. */
 AP_TLBegin(&r, 4, 4, 11, 11, T, T);
 assert(AP_TLFrame(&r, 4, 1, 1) == AP_TL_DROP_NONE);
 assert(AP_TLFrame(&r, -1, 1, 1) == AP_TL_DROP_ELSEWHERE && r.stage == AP_TL_IDLE);
 /* A hub that never becomes safe, or a load that never ends: dropped. */
 AP_TLBegin(&r, 1, 4, 11, 11, T, T);
 for (i = 0; i < AP_TL_LOAD_FRAMES; i++) assert(AP_TLFrame(&r, -1, 1, 1) == AP_TL_DROP_NONE);
 assert(AP_TLFrame(&r, -1, 1, 1) == AP_TL_DROP_TIMEOUT && r.stage == AP_TL_IDLE);
 /* A pad that holds the kart but never asks: dropped, not leaked, even
  * while the hub is not idle. */
 AP_TLBegin(&r, 2, 2, 3, 3, R, R); AP_TLFrame(&r, 2, 0, 1); AP_TLTaken(&r);
 for (i = 0; i < AP_TL_TAKEN_FRAMES; i++) assert(AP_TLFrame(&r, 2, i & 1, 0) == AP_TL_DROP_NONE);
 assert(AP_TLFrame(&r, 2, 1, 0) == AP_TL_DROP_TIMEOUT && r.stage == AP_TL_IDLE);
 /* A held start whose warp ends in another level: dropped at once, so a
  * later visit to the same pad cannot take its mode. */
 AP_TLBegin(&r, 2, 2, 3, 3, R, R); AP_TLFrame(&r, 2, 0, 1); AP_TLTaken(&r);
 assert(AP_TLFrame(&r, -1, 1, 0) == AP_TL_DROP_ELSEWHERE && AP_TLTakeMode(&r, 3, 3) == -1);
 /* A garage start. */
 AP_TLBegin(&r, 3, 3, -1, -1, AP_PADACT_BOSS, -1); AP_TLFrame(&r, 3, 0, 1);
 assert(AP_TLGarageArrives(&r, 3) && !AP_TLPadArrives(&r, 3, -1, -1) && !AP_TLGarageArrives(&r, 2));
 AP_TLTaken(&r); assert(r.stage == AP_TL_IDLE);
 /* Bad hubs are refused. */
 assert(AP_TLBegin(&r, 5, 0, 0, 0, T, T) == AP_TL_BEGIN_REFUSED);
 assert(AP_TLBegin(&r, 0, -1, 0, 0, T, T) == AP_TL_BEGIN_REFUSED);
 {
  int modes[2] = {T, R};
  assert(AP_TLModeOffered(modes, 2, R) && !AP_TLModeOffered(modes, 2, C) && !AP_TLModeOffered(modes, 0, T));
 }
}

/* Decision 34: a locked row lists every missing requirement. */
static ap_padact_facts content_facts(void)
{
 ap_padact_facts f;
 memset(&f, 0, sizeof f);
 f.node = AP_PADACT_NODE_CONTENT; f.occupied = 1; f.contentReady = 1; f.padState = 2; f.keysHave = f.keysNeed = 1;
 f.stage1Met = f.padReqMet = f.racerMet = 1; f.modeCount = 3; f.stage2Open = 1; f.firstRaceWon = 1;
 f.contentMode[0] = T; f.contentMode[1] = C; f.contentMode[2] = R; f.contentOpen[0] = f.contentOpen[1] = f.contentOpen[2] = 1;
 return f;
}

static void missing_all(void)
{
 ap_padact_facts f = content_facts(); ap_padact_list l; char lines[2][112];
 int need = AP_PADACT_BIT(AP_PADACT_LOCK_HUB) | AP_PADACT_BIT(AP_PADACT_LOCK_PAD) | AP_PADACT_BIT(AP_PADACT_LOCK_RACER);
 f.keysHave = 0; f.keysNeed = 2; f.padState = 1; f.stage1Met = f.padReqMet = f.racerMet = 0;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_LOCK_HUB && (l.mask[i] & need) == need);
 /* Two lines when they do not fit one; one reason or a short list stays on one. */
 f = content_facts(); f.keysHave = 0; f.keysNeed = 2; f.padState = 1; f.stage1Met = f.racerMet = 1; f.padReqMet = 1;
 f.contentOpen[1] = 0; f.firstRaceWon = 0; f.stage2Open = 0;
 AP_PadActivityDecide(&f, &l);
 assert(AP_PadActivityReasonLines(l.mask[1], 24, lines) == 2 && lines[0][0] && lines[1][0]);
 assert(strstr(lines[0], "NEEDS HUB KEYS") && strstr(lines[1], "SECOND REQUIREMENT"));
 assert(AP_PadActivityReasonLines(AP_PADACT_BIT(AP_PADACT_LOCK_RACER), 34, lines) == 1 && !strcmp(lines[0], "NEEDS RACER"));
 assert(AP_PadActivityReasonLines(0, 34, lines) == 0);
 /* Its rule met, the racer missing: stage 1 reads closed, the racer is named. */
 f = content_facts(); f.stage1Met = 0; f.racerMet = 0;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_LOCK_RACER && l.mask[i] == AP_PADACT_BIT(AP_PADACT_LOCK_RACER));
 f.padReqMet = 0;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.mask[i] == (AP_PADACT_BIT(AP_PADACT_LOCK_RACER) | AP_PADACT_BIT(AP_PADACT_LOCK_PAD)));
 /* The pad's own rule alone: no invented second requirement. */
 f = content_facts(); f.stage1Met = f.padReqMet = 0; f.contentOpen[0] = f.contentOpen[1] = f.contentOpen[2] = 0;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.mask[i] == AP_PADACT_BIT(AP_PADACT_LOCK_PAD));
 /* Closed again after the first race: every row says the second requirement. */
 f = content_facts(); f.padState = 3; f.contentOpen[1] = f.contentOpen[2] = 0; f.stage2Open = 0;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_LOCK_STAGE2);
 /* A finished pad stands alone; a replay opens its open rows. */
 f = content_facts(); f.padState = 5;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.mask[i] == AP_PADACT_BIT(AP_PADACT_LOCK_DONE));
 f.replayable = 1;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_OPEN && !l.left[i] && l.mask[i] == 0);
 /* ctr_first names the CTR Challenge, not the Trophy Race. */
 f = content_facts(); f.ctrFirst = 1; f.firstRaceWon = 0; f.modeCount = 2; f.contentMode[0] = C; f.contentMode[1] = R; f.contentOpen[1] = 0;
 AP_PadActivityDecide(&f, &l);
 assert(l.mask[1] == AP_PADACT_BIT(AP_PADACT_LOCK_CTRCHAL));
 /* Garage: keys and the garage's own requirement. */
 memset(&f, 0, sizeof f); f.node = AP_PADACT_NODE_GARAGE; f.keysNeed = 1; f.garageOpen = 0;
 AP_PadActivityDecide(&f, &l);
 assert(l.reason[0] == AP_PADACT_LOCK_HUB && l.mask[0] == (AP_PADACT_BIT(AP_PADACT_LOCK_HUB) | AP_PADACT_BIT(AP_PADACT_LOCK_BOSS)));
 /* Invariant: open iff no mask bit, the first reason is a bit of the mask. */
 for (int state = 0; state <= 5; state++) for (int bits = 0; bits < 64; bits++) for (int node = 2; node <= 3; node++) {
  memset(&f, 0, sizeof f); f.node = node; f.single = AP_PADACT_CUP; f.occupied = (bits >> 5) & 1;
  f.keysNeed = 1; f.keysHave = (bits >> 1) & 1; f.padState = state; f.stage1Met = (bits >> 2) & 1;
  f.padReqMet = f.stage1Met; f.racerMet = (bits >> 3) & 1; f.contentReady = (bits >> 4) & 1; f.replayable = state & 1;
  f.modeCount = 1; f.contentMode[0] = T; f.contentOpen[0] = bits & 1;
  AP_PadActivityDecide(&f, &l);
  for (int i = 0; i < l.count; i++) {
   assert((l.reason[i] == AP_PADACT_OPEN) == (l.mask[i] == 0));
   if (l.reason[i] != AP_PADACT_OPEN) assert(l.mask[i] & AP_PADACT_BIT(l.reason[i]));
  }
 }
}

/* Decision 33: the kart appears on the pad. */
static void kart_on_pad(void)
{
 int pad[3] = {1000, 300, -2000}, pos[3], ground;
 AP_TLKartOnPad(pad, (100 << 8) + 0x1200, 100 << 8, pos, &ground);
 assert(pos[0] == 1000 * 256 && pos[2] == -2000 * 256 && ground == 300 * 256 && pos[1] == ground + 0x1200);
 AP_TLKartOnPad(pad, 5 << 8, 100 << 8, pos, &ground); /* below the floor: no height */
 assert(pos[1] == ground);
 AP_TLKartOnPad(pad, (900 << 8), 100 << 8, pos, &ground); /* airborne far up: no height */
 assert(pos[1] == ground);
}

int main(void)
{
 missing_all(); kart_on_pad(); content_parity(); menu_parity(); single_and_garage(); hub_keys(); cursor(); lifecycle();
 puts("PASS: tracker activity decision and start lifecycle");
 return 0;
}
'''

parser = r'''
#include <cassert>
#include <cstdio>
#include <fstream>
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
extern "C" void AP_LogLine(const char *) {}
using J = nlohmann::json;
int main(int argc, char **argv){
 // A room without a plan: the key is not read, whatever it says.
 J j={{"ctr_options",{{"schema_version",18},{"tracker_pad_launch",true},{"race_type_selector","productive"}}}};
 ap_seedcfg_parse_json(j);assert(ctr_cfg_active()&&!ap_content_plan_pad_client()->present);
 assert(argc==2);
 std::ifstream input(argv[1]);J plan;input>>plan;
 // A plan from before the record: nothing.
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&!ap_content_plan_pad_client()->present);
 std::puts("PASS: tracker_pad_launch parse");
 return 0;
}
'''


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at)]


# Production call sites.
warp = (root / "game/232/AH_WarpPad.c").read_text()
tick = body(warp, "void AH_WarpPad_ThTick(struct Thread *t)")
arrive = tick.index("AP_TrackerLaunchPadArrives(ctr_cfg_warp_phys(levelID), levelID)")
# The arrival only stands the kart on the pad: it comes after the distance and
# before every entry gate, so the pad's own gates all run.
assert tick.index("dist = x * x + y * y + z * z;") < arrive
for gate in ("if ((dist > 0x8fff) && (warppadObj->boolEnteredWarppad == 0))",
             "AP_PadState(ctr_cfg_warp_phys(warppadObj->levelID), warppadObj->levelID) == 5",
             "if (!ctr_cfg_racer_lock_met(physLevelID))",
             "AP_ContentMenuRows(pad, &apRsRows);"):
    assert arrive < tick.index(gate), gate
assert "dist = 0;" in tick[arrive:arrive + 1500] and "AP_TrackerLaunchTaken();" in tick[arrive:arrive + 1500]
assert "LOAD_RequestLoad" not in tick[arrive:arrive + 1500] and "MainRaceTrack_RequestLoad" not in tick[arrive:arrive + 1500]
# Decision 33: the kart is put on the pad on arrival, before the warp capture
# remembers its height and before the entry gates.
place = tick.index("AH_WarpPad_PlaceKartOnPad(driver, driverInst, warppadMatrix);", arrive)
assert place < tick.index("dist = 0;", place) and place < tick.index("AH_WarpPad_WarpCapture(gGT);", arrive)
assert "driver->quadBlockHeight = ground;" in warp[:warp.index("void AH_WarpPad_ThTick")] and "AP_TLKartOnPad(padPos" in warp
# A pad that refused the kart earlier (the entry latch) is not placed on again.
assert "else if (apEntryRefusedDest == levelID)" in tick[arrive:place]
# The menu takes the tracker's row only if the pad offers it.
sel = tick[tick.index("AP_ContentMenuRows(pad, &apRsRows);") - 200:]
sel = sel[:sel.index("host = AP_ContentPreparePad(physLevelID, mode);")]
assert "AP_TrackerLaunchTakeMode(physLevelID, levelID)" in sel
assert "AP_TLModeOffered(apRsRows.mode, apRsRows.count, apTlMode) ? apTlMode : AP_RS_NO_ROUTE" in sel
assert "if (apTlMode == -2)\n\t\t\t\t\tapRsMode = AP_RS_NO_ROUTE;" in sel
take = body((root / "ap/ap_tracker_launch.c").read_text(), "int AP_TrackerLaunchTakeMode(")
assert take.index("AP_TLGateOpen(r)") < take.index("AP_TLTakeMode(r, physical, dest)") and "return -2;" in take
assert "AP_TrackerLaunchDrop(AP_TL_DROP_REFUSED);" in warp[warp.index("WarpPad_RefuseEntry:"):][:200]

garage = (root / "game/232/AH_Garage.c").read_text()
gtick = body(garage, "void AH_Garage_ThTick(struct Thread *t)")
garrive = gtick.index("AP_TrackerLaunchGarageArrives()")
# Only reached with the garage open, before its own load.
assert gtick.index("LAB_800aede8:") < garrive < gtick.index("if (gGT->pushBuffer_UI.fadeFromBlack_currentValue == 0)")
assert gtick.index("if (!bossIsOpen)\n\t{\n\t\treturn;") < gtick.index("LAB_800aede8:")

launch = (root / "ap/ap_tracker_launch.c").read_text()
acts = body(launch, "int AP_PadActivities(")
for call in ("AP_PadState(physical, dest)", "AP_PadReplayable(physical, dest)", "AP_ContentPadReady(physical)",
             "AP_ContentSelectorBuild(pad, &sel)", "AP_ContentPadOpen(pad)", "AP_ContentPadBaseOpen(pad)",
             "AP_ContentPadRacerMet(pad)", "AP_PadActivityHubKeys(hub, physical)", "AP_PadActivityDecide(&f, out)"):
    assert call in acts, call
# A room without a plan has no list and no start.
assert "ap_content_plan_pad_client()->tracker_pad_launch" in body(launch, "int AP_TrackerLaunchEnabled(")
assert "if (!AP_TrackerLaunchEnabled() || physical < 0)" in acts
gacts = body(launch, "int AP_GarageActivities(")
assert "AP_OxideGarageOpen()" in gacts and "AP_BossGarageOpen(hub - 1)" in gacts
begin = body(launch, "int AP_TrackerLaunchBegin(")
assert begin.index("AP_TLGateOpen(&probe)") < begin.index("MainFreeze_APResumeFromTracker();")
assert "MainRaceTrack_RequestLoad" not in begin
lframe = body(launch, "void AP_TrackerLaunchFrame(")
assert "AP_HubSafeForRequest(gt)" in lframe
assert "MainRaceTrack_RequestLoad((s16)(GEM_STONE_VALLEY + r->hub));" in lframe
assert lframe.index("AP_TLWantsLoad(r)") < lframe.index("MainRaceTrack_RequestLoad(")
# The tracker never loads a race itself: its only load is a hub.
assert launch.count("MainRaceTrack_RequestLoad(") == 1
cs = (root / "ap/ap_charswap.c").read_text()
assert "return gGT != NULL && gGT->drivers[0] != NULL && ap_cs_safeToOpen(gGT);" in body(cs, "int AP_HubSafeForRequest(")
frame = (root / "ap/ap_hooks_frame.c").read_text()
assert "AP_TrackerLaunchFrame(gGT);" in body(frame, "void AP_OnFrame(struct GameTracker *gGT)")

tracker = (root / "ap/ap_tracker.c").read_text()
menu = body(tracker, "int AP_TrackerMenuFrame(void)")
assert "AP_TrackerLaunchBegin(ap_tracker.hub" in menu
assert "ap_tracker.acts.reason[ap_tracker.acts_cursor]==AP_PADACT_OPEN" in menu
# Decision 34: every missing requirement, on up to two lines.
drawn = (root / "ap/ap_tracker.c").read_text()
assert "AP_PadActivityReasonLines(ap_tracker.acts.mask[i]" in drawn
assert "AP_TrackerLaunchEnabled()" in menu
freeze = (root / "game/MAIN/MainFreeze.c").read_text()
resume = body(freeze, "void MainFreeze_APResumeFromTracker(void)")
assert "gGT->gameMode1 &= ~PAUSE_1;" in resume and "MainFreeze_SafeAdvDestroy();" in resume

with tempfile.TemporaryDirectory(prefix="ctr-tracker-launch-") as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    src.write_text(logic)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-fsanitize=undefined", "-Wall", "-Wextra",
                    "-I", str(root / "ap"), "-I", str(root), "-I", str(root / "include"), str(src), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
    src = Path(tmp) / "parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root / "ap"),
                    "-I", str(root / "ap/vendor/json/include"), str(src), str(root / "ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(root / "tools/fixtures/content-plan/baseline.json")], check=True)
print("PASS: tracker pad launch production call sites")
