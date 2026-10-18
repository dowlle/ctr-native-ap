"""#133 PR 4e: AP Item Boxes seated on a custom track's own weapon crates.

The production seating code is lifted out of ap/ap_boxes.c (AP_BoxesCrateDef,
AP_BoxesSeatCustom, AP_BoxesFreeSeat and the ApBoxLive record) and compiled
against a fixture LEV instance table, so these checks run on the statement list
the game runs:

  1. a named custom track stands one box per seat, each at its weapon crate's
     own position, with that crate's draw and collision bits cleared and saved;
     other instances are untouched;
  2. a box already checked, or one the room does not hold, stands nothing and
     leaves its crate alone;
  3. more crates than seats spread the seats over the table (seat i on crate
     floor(i * crates / 15));
  4. a crate table the seed was not built for (one crate more or fewer), a pad
     without box ids, no plan, or an unreadable instance table stand nothing and
     touch nothing;
  5. freeing a seat gives the crate its bits back, but never after a pool reset.

A source scan then checks the wiring that cannot be called out of engine: every
path that ends a live box frees its seat, the pool-reset path drops seats
without writing to old instances, a seated box spawns at the crate position
without the ground lift, and a custom-bytes race never falls through to the
host's retail placements.

Run: python3 tools/test-box-seat-production.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
boxes = (root / "ap/ap_boxes.c").read_text()


def lift(signature):
    start = boxes.index(signature)
    return boxes[start: boxes.index("\n}\n", start) + 3]


record = boxes[boxes.index("struct ApBoxLive\n{"): boxes.index("};", boxes.index("struct ApBoxLive\n{")) + 2]
units = "\n".join(lift(s) for s in (
    "static struct InstDef *AP_BoxesCrateDef(",
    "static void AP_BoxesFreeSeat(int i)",
    "static void AP_BoxesSeatCustom(",
))

fixture = r"""
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef unsigned int u32;
typedef int AP_SpawnHandle;
#define AP_SPAWN_INVALID (-1)
#define AP_BOX_SLOTS_PER_TRACK 15
#define PU_RANDOM_CRATE 8
#define DRAW_COLLISION_MASK 0xf
struct Model { short id; };
struct Instance { unsigned int flags; };
typedef union { struct { short x, y, z; }; short v[3]; } SVec3;
struct InstDef { struct Model *model; struct Instance *ptrInstance; SVec3 pos; SVec3 rot; };
struct Level { u32 numInstances; struct InstDef *ptrInstDefs; };
struct GameTracker { struct Level *level1; };
struct CustomTrackRaceContext { int physicalPad; int retail; };
#include "ap/ap_content_plan.h"
#include "ap/ap_box_seat_logic.h"

static int g_gen, g_active = 1, g_spawned[15];
static char g_log[8192];
static ctr_content_pad g_pad;
static long g_room[32], g_checked[32];
static int g_rooms, g_checks;
static int s_level = 6, s_liveCount;
int AP_Spawn_Generation(void) { return g_gen; }
void AP_LogLine(const char *m) { strncat(g_log, m, sizeof g_log - strlen(g_log) - 1); }
int ap_content_plan_active(void) { return g_active; }
const ctr_content_pad *ap_content_plan_pad(int p) { return p == g_pad.physical ? &g_pad : 0; }
static int AP_BoxInSeed(long code, void *ctx) { int i; (void)ctx; for (i = 0; i < g_rooms; i++) if (g_room[i] == code) return 1; return 0; }
static int AP_BoxChecked(long code, void *ctx) { int i; (void)ctx; for (i = 0; i < g_checks; i++) if (g_checked[i] == code) return 1; return 0; }
static void AP_BoxProbeSeedClass(void) {}
"""

after_record = r"""
static struct ApBoxLive s_live[AP_BOX_SLOTS_PER_TRACK];
static void AP_BoxesSpawnOne(struct GameTracker *gGT, int i) { (void)gGT; g_spawned[i]++; }
"""

tests = r"""
static int failures, checks;
static void expect(int ok, const char *what) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n", what); } }

static struct Model crate_model = {PU_RANDOM_CRATE}, fruit_model = {7}, wumpa_model = {0x6f};
static struct InstDef defs[200];
static struct Instance insts[200];
static struct Level lev;
static struct GameTracker gGT;
static struct CustomTrackRaceContext race = {12, 0};

/* n instances; every third one is not a crate, one has no model. */
static int table(int crates_wanted) {
    int n = 0, crates = 0;
    memset(defs, 0, sizeof defs); memset(insts, 0, sizeof insts);
    while (crates < crates_wanted) {
        struct InstDef *d = &defs[n];
        d->ptrInstance = &insts[n];
        insts[n].flags = 0x7 | 0x100;
        d->pos.x = (short)(100 + n); d->pos.y = (short)(-5 * n); d->pos.z = (short)(7 * n); d->rot.y = (short)(16 * n);
        if (n % 3 == 1) d->model = (n % 2) ? &fruit_model : &wumpa_model;
        else if (n == 5) d->model = 0;
        else { d->model = &crate_model; crates++; }
        n++;
    }
    lev.numInstances = (u32)n; lev.ptrInstDefs = defs; gGT.level1 = &lev;
    return n;
}
static int crate_index(int ordinal, int n) {
    int k, seen = 0;
    for (k = 0; k < n; k++) if (defs[k].model == &crate_model && seen++ == ordinal) return k;
    return -1;
}
static void reset(int crates, int boxes_in_plan) {
    int b;
    memset(s_live, 0, sizeof s_live); s_liveCount = 0; g_log[0] = 0; memset(g_spawned, 0, sizeof g_spawned);
    memset(&g_pad, 0, sizeof g_pad);
    g_pad.physical = 12; g_pad.occupied = 1; g_pad.box_count = boxes_in_plan; g_pad.box_crates = crates;
    g_rooms = g_checks = 0;
    for (b = 0; b < boxes_in_plan; b++) { g_pad.box_locations[b] = 35201620 + b; g_room[g_rooms++] = 35201620 + b; }
    for (b = 0; b < AP_BOX_SLOTS_PER_TRACK; b++) s_live[b].spawn = AP_SPAWN_INVALID;
}
static int untouched(int n) {
    int k;
    for (k = 0; k < n; k++) if (insts[k].flags != (0x7u | 0x100u)) return 0;
    return 1;
}

int main(void) {
    int n, i;
    /* 1. Eight crates, eight boxes: every crate seated, in order. */
    n = table(8); reset(8, 8);
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 8, "eight boxes stand on eight crates");
    for (i = 0; i < 8; i++) {
        int k = crate_index(i, n);
        expect(s_live[i].used && s_live[i].slot == i && s_live[i].code == 35201620 + i, "box slot and code");
        expect(s_live[i].x == defs[k].pos.x && s_live[i].y == defs[k].pos.y && s_live[i].z == defs[k].pos.z &&
               s_live[i].rotY == defs[k].rot.y, "box stands at its crate's position and turn");
        expect(s_live[i].seated && s_live[i].seatInst == &insts[k] && s_live[i].seatFlags == 0x7 &&
               insts[k].flags == 0x100, "its crate is hidden, the draw and collision bits saved");
        expect(g_spawned[i] == 1, "the box is spawned once");
    }
    for (i = 0; i < n; i++)
        if (defs[i].model != &crate_model) expect(insts[i].flags == (0x7u | 0x100u), "other instances untouched");
    expect(strstr(g_log, "8 of 8 Item Box(es) standing") != 0, "one log line names the count");

    /* 5. Freeing a seat gives the bits back; after a pool reset it does not write. */
    AP_BoxesFreeSeat(2);
    expect(insts[crate_index(2, n)].flags == (0x7u | 0x100u) && !s_live[2].seated && !s_live[2].seatInst,
           "a freed seat gives the crate its bits back");
    g_gen++;
    AP_BoxesFreeSeat(3);
    expect(insts[crate_index(3, n)].flags == 0x100 && !s_live[3].seated, "after a pool reset the old crate is not written");
    g_gen--;

    /* 2. A checked box and a box the room lacks stand nothing; their crates stay. */
    n = table(8); reset(8, 8);
    g_checked[g_checks++] = 35201621;
    g_room[4] = 1; /* slot 4's code is not in this room */
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 6 && !s_live[1].used && !s_live[4].used, "checked and absent boxes do not stand");
    expect(insts[crate_index(1, n)].flags == (0x7u | 0x100u) && insts[crate_index(4, n)].flags == (0x7u | 0x100u),
           "their crates keep their bits");

    /* 3. 69 crates, 15 seats spread over the table. */
    n = table(69); reset(69, 15);
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 15, "fifteen boxes on a long table");
    for (i = 0; i < 15; i++) {
        int k = crate_index((int)AP_BoxSeat_Ordinal(i, 69), n);
        expect(s_live[i].seatInst == &insts[k], "seat i takes crate floor(i * 69 / 15)");
    }
    expect(s_live[1].seatInst == &insts[crate_index(4, n)], "the second seat is the fifth crate");

    /* 4. Refusals touch nothing. */
    n = table(9); reset(8, 8);
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n) && strstr(g_log, "has 9 weapon crate(s), the seed was built for 8"),
           "one crate more than the seed knows stands nothing");
    n = table(7); reset(8, 8);
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "one crate fewer stands nothing");
    n = table(8); reset(8, 8); g_pad.box_count = 0;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n) && strstr(g_log, "has no Item Box checks"), "a pad without box ids stands nothing");
    n = table(8); reset(8, 8); g_pad.box_count = 9;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "a forged seat count stands nothing");
    n = table(8); reset(8, 8); g_active = 0;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "no active plan stands nothing");
    g_active = 1;
    n = table(8); reset(8, 8); race.physicalPad = 13;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "a race from another pad stands nothing");
    race.physicalPad = 12;
    n = table(8); reset(8, 8); lev.numInstances = 0;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0, "an empty instance table stands nothing");
    n = table(8); reset(8, 8); lev.numInstances = 65537;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "an instance count past the verifier ceiling stands nothing");
    n = table(8); reset(8, 8); gGT.level1 = 0;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "no level stands nothing");
    n = table(8); reset(8, 8); lev.ptrInstDefs = 0;
    AP_BoxesSeatCustom(&gGT, &race);
    expect(s_liveCount == 0 && untouched(n), "no instance table stands nothing");
    (void)n;
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "seat.c"
    src.write_text(fixture + record + after_record + units + tests)
    exe = Path(tmp) / "seat"
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", str(root), "-o", str(exe), str(src)],
                   check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    assert run.returncode == 0, run.stdout

# ---------------------------------------------------------------------------
# Wiring that cannot be called out of engine, checked in the source.
# ---------------------------------------------------------------------------
def body(signature):
    return lift(signature)


brk = body("static void AP_BoxBreak(")
assert "AP_BoxesFreeSeat(i)" in brk and brk.index("AP_EmitBoxCheck") < brk.index("AP_BoxesFreeSeat(i)"), \
    "a broken box gives its seat back after the check is emitted"
tick = body("static void AP_BoxesTick(")
checked = tick[tick.index("ap_net_location_checked((long long)s_live[i].code)"):]
checked = checked[: checked.index("continue;")]
assert "AP_BoxesFreeSeat(i)" in checked, "a box checked from elsewhere gives its seat back"
clear = body("static void AP_BoxesClear(void)")
assert "AP_BoxesFreeSeat(i)" in clear, "clearing the set gives every seat back"
forget = body("static void AP_BoxesForget(void)")
assert "s_live[i].seated = 0" in forget and "seatInst = 0" in forget and "flags" not in forget, \
    "a pool reset drops the seats without writing to old instances"
spawn = body("static void AP_BoxesSpawnOne(")
seated = spawn[spawn.index("if (s_live[i].seated)"):]
assert seated.index("pos.y = s_live[i].y;") < seated.index("else") < seated.index("AP_BoxModel_SpawnPos"), \
    "a seated box spawns at the crate position, without the ground-anchor lift"
rebuild = body("static void AP_BoxesRebuild(")
branch = rebuild.index("CustomTrack_StandaloneContext(level)")
assert branch < rebuild.index("AP_CortexTrackActive()") < rebuild.index("AP_BoxMap_BuildSet"), \
    "a custom-bytes race is decided before the host's retail placements are read"
assert re.search(r"race != 0 && !race->retail\)\s*\{\s*AP_BoxesSeatCustom\(gGT, race\);\s*return;", rebuild), \
    "a custom-bytes race returns after seating, never falling through to the host identity"
print("PASS wiring: break, server-checked removal, clear and pool reset free seats; seated spawn; custom branch first")
