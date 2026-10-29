"""Start pad activities from the in-game tracker (#375): execute the shared
activity decision, the start lifecycle and the real schema-18 parser on the
host, and pin the production call sites.

Parity: for every combination of hub Keys, pad state, stage 1, racer lock,
replay, custom content, structure, checked Trophy, stage 2 and the modes a
destination has, the tracker's open rows are exactly the rows the pad's own
race type menu builds (AP_RaceSelectorBuild over the same facts) whenever the
pad takes the kart, and no row is open when it does not. Content-plan pads
and single-route pads (cups, arenas, trials, garages) are pinned the same way.
The start lifecycle never loads anything itself and drops on a timeout, a
different level or a closed gate. Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

logic = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_pad_activity_logic.h"
#include "ap_tracker_launch_logic.h"

enum { T = AP_RS_TROPHY, C = AP_RS_CTR, R = AP_RS_RELIC };

/* The pad's own entry, in AH_WarpPad_ThTick order, for a selector pad: does
 * it take the kart at all? Done without a replay returns, a closed pad never
 * reaches entry (state 1 locked, state 3 re-locked), the racer lock and
 * custom content refuse, and a refused structure has no row. */
static int pad_takes(int state, int racer, int replayable, int content, int structure)
{
 if (state == 0) return 0;
 if (state == 5 && !replayable) return 0;
 if (state == 1 || state == 3) return 0;
 if (!racer || !content) return 0;
 if (structure == AP_RS_STRUCTURE_REFUSED) return 0;
 return 1;
}

static void selector_parity(void)
{
 int cases = 0;
 for (int keys = 0; keys < 2; keys++)
 for (int state = 0; state <= 5; state++)
 for (int s1 = 0; s1 < 2; s1++)
 for (int racer = 0; racer < 2; racer++)
 for (int rep = 0; rep < 2; rep++)
 for (int content = 0; content < 2; content++)
 for (int st = -1; st <= 1; st++)
 for (int tc = 0; tc < 2; tc++)
 for (int s2 = 0; s2 < 2; s2++)
 for (int ctrPresent = 0; ctrPresent < 2; ctrPresent++)
 for (int prod = 0; prod < 8; prod++) {
  ap_padact_facts f; ap_padact_list l; ap_rs_rows rows; int admitted[3], i, open = 0;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_SELECTOR; f.keysHave = keys; f.keysNeed = 1;
  f.padState = state; f.stage1Met = s1; f.padReqMet = s1; f.racerMet = racer; f.replayable = rep;
  f.contentReady = content; f.structure = st; f.trophyChecked = tc; f.stage2Met = s2;
  f.rs.present[T] = 1; f.rs.present[C] = ctrPresent; f.rs.present[R] = 1;
  for (i = 0; i < 3; i++) f.rs.productive[i] = (prod >> i) & 1;
  AP_PadActivityDecide(&f, &l);
  /* Every mode the destination has is listed, open or not. */
  assert(l.count == 2 + ctrPresent);
  /* The pad's own menu over the same facts (AP_PadSelectorRows). */
  AP_RaceStructureAdmit(st == AP_RS_STRUCTURE_CTR_FIRST ? st : AP_RS_STRUCTURE_CLASSIC, tc, s2, admitted);
  AP_RaceSelectorBuild(&f.rs, admitted, &rows);
  for (i = 0; i < l.count; i++) {
   int inMenu = AP_TLModeOffered(rows.mode, rows.count, l.launchMode[i]);
   int takes = keys && pad_takes(state, racer, rep, content, st);
   assert(l.launchMode[i] == l.activity[i]);
   if (l.reason[i] == AP_PADACT_OPEN) { open++; assert(takes && inMenu); }
   else {
    assert(!takes || !inMenu);
    assert(l.reason[i] > 0 && l.reason[i] < AP_PADACT_REASONS);
    assert(AP_PadActivityReasonName(l.reason[i])[0]);
   }
   if (takes && inMenu) assert(l.reason[i] == AP_PADACT_OPEN);
  }
  /* Missing hub Keys come first, for every row. */
  if (!keys) for (i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_LOCK_HUB);
  if (keys && state == 1) for (i = 0; i < l.count; i++)
   assert(l.reason[i] == (s1 ? AP_PADACT_LOCK_RACER : AP_PADACT_LOCK_PAD));
  if (keys && state == 5 && !rep) for (i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_LOCK_DONE);
  /* A mode the gates hold back names the gate: the Trophy first in classic. */
  if (keys && pad_takes(state, racer, rep, content, st) && st == AP_RS_STRUCTURE_CLASSIC && !tc)
   for (i = 0; i < l.count; i++) if (l.activity[i] != T) assert(l.reason[i] == AP_PADACT_LOCK_TROPHY);
  if (keys && pad_takes(state, racer, rep, content, st) && tc && !s2)
   for (i = 0; i < l.count; i++) if (l.activity[i] == R) assert(l.reason[i] == AP_PADACT_LOCK_STAGE2);
  /* Grey rows in the pad's menu are replays in the tracker. */
  for (i = 0; i < l.count; i++) assert(l.left[i] == f.rs.productive[l.activity[i]]);
  cases++;
 }
 printf("selector parity: %d cases\n", cases);
}

static void content_parity(void)
{
 for (int keys = 0; keys < 2; keys++)
 for (int ready = 0; ready < 2; ready++)
 for (int s1 = 0; s1 < 2; s1++)
 for (int racer = 0; racer < 2; racer++)
 for (int n = 1; n <= 3; n++)
 for (int open = 0; open < 8; open++) {
  ap_padact_facts f; ap_padact_list l; int i;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_CONTENT; f.content = 1; f.keysHave = keys; f.keysNeed = 1;
  f.contentReady = ready; f.stage1Met = s1; f.racerMet = racer; f.modeCount = n;
  for (i = 0; i < n; i++) { f.contentMode[i] = (i + 1) % 3; f.contentOpen[i] = (open >> i) & 1; f.contentLeft[i] = i & 1; }
  AP_PadActivityDecide(&f, &l);
  assert(l.count == n);
  for (i = 0; i < n; i++) {
   /* The content-plan entry: occupied and Ready, AP_ContentPadOpen, then
    * the pad's selector row must be open. */
   int takes = keys && ready && s1 && f.contentOpen[i];
   assert((l.reason[i] == AP_PADACT_OPEN) == takes);
   assert(l.activity[i] == f.contentMode[i]);
   /* One mode: the pad asks nothing; several: the row answers its menu. */
   assert(l.launchMode[i] == (n > 1 ? f.contentMode[i] : -1));
  }
 }
}

static void single_and_garage(void)
{
 for (int keys = 0; keys < 2; keys++)
 for (int state = 0; state <= 5; state++)
 for (int racer = 0; racer < 2; racer++)
 for (int content = 0; content < 2; content++) {
  ap_padact_facts f; ap_padact_list l;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_SINGLE; f.single = AP_PADACT_CUP; f.keysHave = keys; f.keysNeed = 1;
  f.padState = state; f.stage1Met = 1; f.racerMet = racer; f.contentReady = content;
  AP_PadActivityDecide(&f, &l);
  assert(l.count == 1 && l.activity[0] == AP_PADACT_CUP && l.launchMode[0] == -1);
  assert((l.reason[0] == AP_PADACT_OPEN) == (keys && pad_takes(state, racer, 0, content, 0)));
 }
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
 /* Content-plan cups and Crystal Challenges: the pad's Done gate runs before
  * its content branch and never replays a content pad, so state 5 (a
  * finished crystal, an empty slot, content not Ready) never starts; a
  * finished cup (state 4) replays. */
 for (int state = 1; state <= 5; state++)
 for (int ready = 0; ready < 2; ready++)
 for (int s1 = 0; s1 < 2; s1++) {
  ap_padact_facts f; ap_padact_list l;
  memset(&f, 0, sizeof f);
  f.node = AP_PADACT_NODE_SINGLE; f.content = 1; f.single = AP_PADACT_CRYSTAL;
  f.keysHave = f.keysNeed = 1; f.padState = state; f.contentReady = ready; f.stage1Met = s1; f.racerMet = 1;
  AP_PadActivityDecide(&f, &l);
  assert(l.count == 1);
  assert((l.reason[0] == AP_PADACT_OPEN) == (ready && s1 && state != 5));
  if (ready && state == 5) assert(l.reason[0] == AP_PADACT_LOCK_DONE);
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
static void missing_all(void)
{
 ap_padact_facts f; ap_padact_list l; char lines[2][112];
 memset(&f, 0, sizeof f);
 f.node = AP_PADACT_NODE_SELECTOR; f.keysHave = 0; f.keysNeed = 2;
 f.padState = 1; f.stage1Met = 0; f.padReqMet = 0; f.racerMet = 0; f.contentReady = 1;
 f.structure = AP_RS_STRUCTURE_CLASSIC; f.trophyChecked = 0; f.stage2Met = 0;
 f.rs.present[T] = f.rs.present[C] = f.rs.present[R] = 1;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) {
  int need = AP_PADACT_BIT(AP_PADACT_LOCK_HUB) | AP_PADACT_BIT(AP_PADACT_LOCK_PAD) | AP_PADACT_BIT(AP_PADACT_LOCK_RACER);
  assert(l.reason[i] == AP_PADACT_LOCK_HUB && (l.mask[i] & need) == need);
  if (l.activity[i] != T) assert(l.mask[i] & AP_PADACT_BIT(AP_PADACT_LOCK_TROPHY) && l.mask[i] & AP_PADACT_BIT(AP_PADACT_LOCK_STAGE2));
 }
 /* Two lines when they do not fit one; one reason or a short list stays on one. */
 assert(AP_PadActivityReasonLines(l.mask[2], 34, lines) == 2 && lines[0][0] && lines[1][0]);
 assert(strstr(lines[0], "NEEDS HUB KEYS") && strstr(lines[1] , "SECOND REQUIREMENT"));
 assert(AP_PadActivityReasonLines(AP_PADACT_BIT(AP_PADACT_LOCK_RACER), 34, lines) == 1 && !strcmp(lines[0], "NEEDS RACER"));
 assert(AP_PadActivityReasonLines(0, 34, lines) == 0);
 /* Keys back, racer back: the pad requirement alone is left. */
 f.keysHave = 2; f.racerMet = 1;
 AP_PadActivityDecide(&f, &l);
 assert(l.reason[0] == AP_PADACT_LOCK_PAD && (l.mask[0] & AP_PADACT_BIT(AP_PADACT_LOCK_PAD)));
 assert(!(l.mask[0] & AP_PADACT_BIT(AP_PADACT_LOCK_HUB)) && !(l.mask[0] & AP_PADACT_BIT(AP_PADACT_LOCK_RACER)));
 /* A finished pad stands alone; open rows carry no mask. */
 f.padState = 5; f.replayable = 0; f.padReqMet = 1; f.stage1Met = 1;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.mask[i] == AP_PADACT_BIT(AP_PADACT_LOCK_DONE));
 f.padState = 2; f.trophyChecked = 1; f.stage2Met = 1;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) assert(l.reason[i] == AP_PADACT_OPEN && l.mask[i] == 0);
 /* ctr_first names the CTR Challenge, not the Trophy Race. */
 f.structure = AP_RS_STRUCTURE_CTR_FIRST; f.trophyChecked = 0; f.stage2Met = 1;
 AP_PadActivityDecide(&f, &l);
 for (int i = 0; i < l.count; i++) if (l.activity[i] == R)
  assert(l.mask[i] == AP_PADACT_BIT(AP_PADACT_LOCK_CTRCHAL));
 /* One open row still lists (dispatcher decision, as decision 13). */
 assert(l.count >= 1);
 /* Garage: keys and the garage's own requirement. */
 memset(&f, 0, sizeof f); f.node = AP_PADACT_NODE_GARAGE; f.keysNeed = 1; f.garageOpen = 0;
 AP_PadActivityDecide(&f, &l);
 assert(l.reason[0] == AP_PADACT_LOCK_HUB && l.mask[0] == (AP_PADACT_BIT(AP_PADACT_LOCK_HUB) | AP_PADACT_BIT(AP_PADACT_LOCK_BOSS)));
 /* Invariant: open iff no mask bit, the first reason is a bit of the mask. */
 for (int state = 0; state <= 5; state++) for (int bits = 0; bits < 32; bits++) for (int node = 1; node <= 3; node++) {
  memset(&f, 0, sizeof f); f.node = node; f.single = AP_PADACT_CUP; f.content = bits & 1;
  f.keysNeed = 1; f.keysHave = (bits >> 1) & 1; f.padState = state; f.stage1Met = (bits >> 2) & 1;
  f.padReqMet = f.stage1Met; f.racerMet = (bits >> 3) & 1; f.contentReady = (bits >> 4) & 1; f.replayable = state & 1;
  f.rs.present[T] = 1; f.modeCount = 1; f.contentMode[0] = T; f.contentOpen[0] = bits & 1;
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
 missing_all(); kart_on_pad(); selector_parity(); content_parity(); single_and_garage(); hub_keys(); cursor(); lifecycle();
 puts("PASS: tracker activity decision and start lifecycle");
 return 0;
}
'''

parser = r'''
#include <cassert>
#include <cstdio>
#include <fstream>
#include "ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
using J = nlohmann::json;
int main(int argc, char **argv){
 J j={{"ctr_options",{{"schema_version",18}}}};
 // Absent: off, inspection only.
 ap_seedcfg_parse_json(j);assert(ctr_cfg_active()&&ctr_cfg.tracker_pad_launch==0);
 j["ctr_options"]["tracker_pad_launch"]=true;ap_seedcfg_parse_json(j);assert(ctr_cfg.tracker_pad_launch==1);
 j["ctr_options"]["tracker_pad_launch"]=false;ap_seedcfg_parse_json(j);assert(ctr_cfg.tracker_pad_launch==0);
 // Not a Boolean: off, never on.
 j["ctr_options"]["tracker_pad_launch"]=1;ap_seedcfg_parse_json(j);assert(ctr_cfg.tracker_pad_launch==0);
 j["ctr_options"]["tracker_pad_launch"]="true";ap_seedcfg_parse_json(j);assert(ctr_cfg.tracker_pad_launch==0);
 // A reparse without the key forgets an earlier true.
 j["ctr_options"]["tracker_pad_launch"]=true;ap_seedcfg_parse_json(j);
 j["ctr_options"].erase("tracker_pad_launch");ap_seedcfg_parse_json(j);assert(ctr_cfg.tracker_pad_launch==0);
 // A content-plan room carries it as an additive envelope field.
 assert(argc==2);
 std::ifstream input(argv[1]);J plan;input>>plan;
 plan["ctr_options"]["tracker_pad_launch"]=true;
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.tracker_pad_launch==1);
 plan["ctr_options"]["tracker_pad_launch"]=false;
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.tracker_pad_launch==0);
 plan["ctr_options"].erase("tracker_pad_launch");
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.tracker_pad_launch==0);
 // A non-Boolean value stays in the closed envelope, which refuses it.
 plan["ctr_options"]["tracker_pad_launch"]=1;
 ap_seedcfg_parse_json(plan);assert(!ctr_cfg_active()&&ctr_cfg.tracker_pad_launch==0);
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
             "if (AP_PadUsesSelector(levelID))"):
    assert arrive < tick.index(gate), gate
assert "dist = 0;" in tick[arrive:arrive + 1500] and "AP_TrackerLaunchTaken();" in tick[arrive:arrive + 1500]
assert "LOAD_RequestLoad" not in tick[arrive:arrive + 1500] and "MainRaceTrack_RequestLoad" not in tick[arrive:arrive + 1500]
# Decision 33: the kart is put on the pad on arrival, before the warp capture
# remembers its height and before the entry gates.
place = tick.index("AP_TLKartOnPad(apPadPos", arrive)
assert place < tick.index("dist = 0;", arrive) and place < tick.index("AH_WarpPad_WarpCapture(gGT);", arrive)
assert "driver->quadBlockHeight = apGround;" in tick[place:place + 900]
# The selector's menu takes the tracker's row only if the pad offers it.
sel = tick[tick.index("if (AP_PadUsesSelector(levelID))"):]
sel = sel[:sel.index("goto WarpPad_RequestLoad;")]
assert "AP_TrackerLaunchTakeMode(physLevelID, levelID)" in sel
assert "AP_PadSelectorRows(physLevelID, levelID, &apRsRows);" in sel
assert "AP_TLModeOffered(apRsRows.mode, apRsRows.count, apTlMode) ? apTlMode : AP_RS_NO_ROUTE" in sel
assert "if (apTlMode == -2)\n\t\t\t\tapRsMode = AP_RS_NO_ROUTE;" in sel
take = body((root / "ap/ap_tracker_launch.c").read_text(), "int AP_TrackerLaunchTakeMode(")
assert take.index("AP_TLGateOpen(r)") < take.index("AP_TLTakeMode(r, physical, dest)") and "return -2;" in take
content = tick[tick.index("if (!pad->crystal && pad->mode_count > 1)"):]
content = content[:content.index("host = AP_ContentPreparePad(physLevelID, mode);")]
assert "AP_TrackerLaunchTakeMode(physLevelID, levelID)" in content
assert "sel.mode[k] == apTlMode && sel.open[k]" in content
assert "if (apTlMode == -2)\n\t\t\t\t\tapContentModeChoice = -1;" in content
assert "AP_TrackerLaunchDrop(AP_TL_DROP_REFUSED);" in warp[warp.index("WarpPad_RefuseEntry:"):][:200]

garage = (root / "game/232/AH_Garage.c").read_text()
gtick = body(garage, "void AH_Garage_ThTick(struct Thread *t)")
garrive = gtick.index("AP_TrackerLaunchGarageArrives()")
# Only reached with the garage open, before its own load.
assert gtick.index("LAB_800aede8:") < garrive < gtick.index("if (gGT->pushBuffer_UI.fadeFromBlack_currentValue == 0)")
assert gtick.index("if (!bossIsOpen)\n\t{\n\t\treturn;") < gtick.index("LAB_800aede8:")

pads = (root / "ap/ap_hooks_pads.c").read_text()
rows = body(pads, "int AP_PadSelectorRows(")
assert "AP_PadSelectorFacts(physLevelID, destLevelID, &f, admitted, 0, 0)" in rows
assert "AP_RaceSelectorBuild(&f, admitted, rows);" in rows
facts = body(pads, "int AP_PadSelectorFacts(")
assert "rungsLeft" in facts and "AP_RaceStructureAdmit(structure, trophyChecked, stage2Met, admitted);" in facts

launch = (root / "ap/ap_tracker_launch.c").read_text()
acts = body(launch, "int AP_PadActivities(")
for call in ("AP_PadSelectorFacts(physical, dest, &f.rs, admitted, &f.trophyChecked, &f.stage2Met)",
             "AP_PadState(physical, dest)", "AP_PadStage1Met(physical)", "AP_PadReplayable(physical, dest)",
             "ctr_cfg_racer_lock_met(physical)", "AP_CustomContentRequired()", "AP_ContentSelectorBuild(pad, &sel)",
             "AP_ContentPadOpen(pad)", "AP_PadActivityHubKeys(hub, physical)", "AP_PadActivityDecide(&f, out)"):
    assert call in acts, call
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
                    "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    src = Path(tmp) / "parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root / "ap"),
                    "-I", str(root / "ap/vendor/json/include"), str(src), str(root / "ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(root / "tools/fixtures/content-plan/baseline.json")], check=True)
print("PASS: tracker pad launch production call sites")
