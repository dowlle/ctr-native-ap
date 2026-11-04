"""Compile the production racer-lock gate and seating of content-plan pads (#133 PR 4b).

A content-plan pad's `racer_lock` is ANDed onto its gates by the production
AP_ContentPadOpen (ap_hooks_gates.c), which every content-plan consumer asks
(pad state, glow, capture, the load gate and the pad text). The parser mirrors
the lock into ctr_cfg.racer_lock / gem_cup_racer_lock, so the production
ctr_cfg_racer_lock, ctr_cfg_racer_lock_met and AP_RacerLock_ForceForWarp /
AP_RacerLock_RestoreOnHub (ap_charswap.c) seat the demanded racer at the warp
commit and give the player's racer back on the hub return, as on a normal
room. AP_CharacterUnlocked (ap_capability.c) is the real ownership answer.
Engine state is a controlled seam; this does not claim a Steam race.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
hooks = ap_hooks_unit.read()
capability = (root / "ap" / "ap_capability.c").read_text(encoding="utf-8")
charswap = (root / "ap" / "ap_charswap.c").read_text(encoding="utf-8")


def function(signature, text):
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
#include "ap/ap_seedcfg.h"
#include "ap/ap_content_plan.h"
#define AP_CAP_ROSTER_COUNT 16
#define PURA 7
#define NITROS_OXIDE 15
typedef short s16;
ctr_seed_config ctr_cfg;
static int active;
int ctr_cfg_active(void) { return active; }
static int ap_recv_count[15];
static int g_character_unlocked[AP_CAP_ROSTER_COUNT];
static struct { short characterIDs[8]; } data;
static struct { struct { short characterID; } advProgress; } sdataStore, *sdata = &sdataStore;
static char lastLog[256];
void AP_LogLine(const char *text) { snprintf(lastLog, sizeof lastLog, "%s", text); }
int AP_CharacterUnlocked(int characterID);
'''
fixture += function("int AP_CharacterUnlocked(int characterID)\n{", capability) + "\n"
fixture += function("int AP_ContentPadOpen(const ctr_content_pad *pad)\n{", hooks) + "\n"
fixture += function("int ctr_cfg_racer_lock(int physPadLevelID)\n{", hooks) + "\n"
fixture += function("int ctr_cfg_racer_lock_met(int physPadLevelID)\n{", hooks) + "\n"
fixture += function("static void ap_cs_seatApply(int characterID, const char *source)\n{", charswap) + "\n"
fixture += "static int ap_rl_prevRacer = -1;\n"
for signature in ("void AP_RacerLock_ForceForWarp(int physPadLevelID)\n{",
                  "void AP_RacerLock_RestoreOnHub(void)\n{"):
    fixture += function(signature, charswap) + "\n"
fixture += r'''
static ctr_content_pad pad;
static void reset(int physical, int lock, int keys) {
 memset(&ctr_cfg, 0, sizeof ctr_cfg); memset(&pad, 0, sizeof pad); memset(ap_recv_count, 0, sizeof ap_recv_count);
 memset(g_character_unlocked, 0, sizeof g_character_unlocked);
 for (int i = 0; i < CTR_CFG_PAD_COUNT; ++i) ctr_cfg.racer_lock[i] = -1;
 for (int i = 0; i < 5; ++i) ctr_cfg.gem_cup_racer_lock[i] = -1;
 active = 1; ap_rl_prevRacer = -1;
 /* What the content-plan parser writes: unlocks on, the start, the lock mirror. */
 ctr_cfg.character_phase_present = 1; ctr_cfg.character_unlocks = 1; ctr_cfg.starting_character = 4;
 pad.physical = physical; pad.occupied = 1; pad.keys = keys; pad.racer_lock = lock;
 if (lock >= 0) {
  if (physical >= 100) ctr_cfg.gem_cup_racer_lock[physical - 100] = lock;
  else ctr_cfg.racer_lock[physical] = lock;
 }
 data.characterIDs[0] = 4; sdataStore.advProgress.characterID = 4;
}
int main(void) {
 int cases = 0;
 /* A locked pad on a Key hub: closed without the Key, closed with the Key
    but without the racer, open with both. */
 reset(19, 14, 1);
 assert(!AP_ContentPadOpen(&pad)); cases++;
 ap_recv_count[14] = 1; assert(!AP_ContentPadOpen(&pad)); cases++;
 assert(!ctr_cfg_racer_lock_met(19) && ctr_cfg_racer_lock(19) == 14); cases++;
 g_character_unlocked[14] = 1; assert(AP_ContentPadOpen(&pad) && ctr_cfg_racer_lock_met(19)); cases++;
 /* The racer alone does not open a pad whose own gate is not met. */
 ap_recv_count[14] = 0; assert(!AP_ContentPadOpen(&pad)); cases++;
 /* A pad without a lock only asks its own gate. */
 reset(5, -1, 0); assert(AP_ContentPadOpen(&pad) && ctr_cfg_racer_lock(5) == -1 && ctr_cfg_racer_lock_met(5)); cases++;
 /* The Gem Cup seats 100..104 use the cup array. */
 reset(104, 13, 0); assert(ctr_cfg_racer_lock(104) == 13 && !AP_ContentPadOpen(&pad)); cases++;
 g_character_unlocked[13] = 1; assert(AP_ContentPadOpen(&pad)); cases++;
 /* All-unlocked mode (Character Unlocks off): every racer is owned. */
 reset(19, 14, 0); ctr_cfg.character_unlocks = 0; assert(AP_ContentPadOpen(&pad)); cases++;
 /* Warp commit: seat the demanded racer, then give the player's back at the hub. */
 reset(19, 14, 0); g_character_unlocked[14] = 1;
 AP_RacerLock_ForceForWarp(19);
 assert(data.characterIDs[0] == 14 && sdataStore.advProgress.characterID == 14 && strstr(lastLog, "racer lock")); cases++;
 AP_RacerLock_RestoreOnHub();
 assert(data.characterIDs[0] == 4 && ap_rl_prevRacer == -1); cases++;
 /* Never seat a racer the player does not own. */
 reset(19, 14, 0); AP_RacerLock_ForceForWarp(19); assert(data.characterIDs[0] == 4); cases++;
 /* An unlocked pad seats nobody. */
 reset(5, -1, 0); AP_RacerLock_ForceForWarp(5); assert(data.characterIDs[0] == 4 && ap_rl_prevRacer == -1); cases++;
 /* No room: no lock is read. */
 reset(19, 14, 0); active = 0; assert(ctr_cfg_racer_lock(19) == -1); cases++;
 printf("PASS: %d content-plan racer-lock cases through production code\n", cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "racer_lock.c"
    binary = Path(temporary) / "racer_lock"
    path.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-DCTR_AP", "-I", str(root / "include"), "-I", str(root),
                    "-I", str(root / "ap"), str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
