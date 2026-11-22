"""Run the co-op result, pickup and steering code with isolated engine fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile

from coop_fixture import ROOT, function


COMMON = r'''
#include <assert.h>
#include <common.h>
#include "ap/ap_coop.h"
#include "ap/ap_seedcfg.h"
#include "platform/native_controls.h"
struct sData sdata_static;
struct Data data;
ctr_seed_config ctr_cfg;
static struct GameTracker gt;
static struct Driver drivers[2];
static struct GamepadSystem pads;

static void setup(void)
{
    sdata->gGT = &gt;
    sdata->gGamepads = &pads;
    sdata->Loading.stage = LOAD_IDLE;
    gt.gameMode1 = ADVENTURE_MODE;
    gt.numPlyrCurrGame = 2;
    gt.levelID = CRASH_COVE;
    gt.drivers[0] = &drivers[0];
    gt.drivers[1] = &drivers[1];
}
'''

RESULT_STUBS = r'''
static const SVec2 s_battleStatsPos3P4P[4];
void DecalFont_DrawLine(char *str, int x, int y, s16 font, int flags) {}
void CTR_Box_DrawClearBox(const RECT *r, const Color *color, int flags, u32 *ot) {}
'''

DISCONNECT = RESULT_STUBS + r'''
static u64 ap_coop_p2_device = 101;
static u64 assignedP2Device = 101;
u64 NativeControls_GetPlayerDeviceIdentity(int player) { return assignedP2Device; }
''' + function("ap/ap_coop.c", "int AP_CoopP2Available(") + function(
    "game/UI/UI_VsQuip.c", "void UI_VsWaitForPressX("
) + r'''
int main(void)
{
    static struct ControllerPacket p2;
    static struct DB backBuffer;
    setup();
    gt.gameMode1 |= END_OF_RACE;
    gt.backBuffer = &backBuffer;
    gt.timerEndOfRaceVS = 30;
    drivers[0].actionsFlagSet = drivers[1].actionsFlagSet = ACTION_RACE_FINISHED | ACTION_BOT;
    p2.plugged = PLUGGED;
    pads.gamepad[1].ptrControllerPacket = &p2;
    pads.gamepad[0].buttonsTapped = BTN_CROSS_one;
    UI_VsWaitForPressX();
    pads.gamepad[0].buttonsTapped = 0;
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 30); /* Connected P2 still confirms. */
    pads.gamepad[1].buttonsTapped = BTN_CROSS_one;
    UI_VsWaitForPressX();
    pads.gamepad[1].buttonsTapped = 0;
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 0);

    gt.timerEndOfRaceVS = 30;
    assignedP2Device = 102; /* An unrelated/replugged SDL instance cannot hold results. */
    pads.gamepad[0].buttonsTapped = BTN_CROSS_one;
    UI_VsWaitForPressX();
    pads.gamepad[0].buttonsTapped = 0;
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 0);

    gt.timerEndOfRaceVS = 30;
    assignedP2Device = UINT64_MAX; /* The keyboard still reports a connected pad. */
    pads.gamepad[0].buttonsTapped = BTN_CROSS_one;
    UI_VsWaitForPressX();
    pads.gamepad[0].buttonsTapped = 0;
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 0);

    gt.timerEndOfRaceVS = 30;
    p2.plugged = (u8)UNPLUGGED;
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 30); /* P1 still has to confirm. */
    pads.gamepad[0].buttonsTapped = BTN_CROSS_one;
    UI_VsWaitForPressX();
    pads.gamepad[0].buttonsTapped = 0;
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 0);

    gt.timerEndOfRaceVS = 30;
    gt.gameMode1 = ARCADE_MODE | END_OF_RACE;
    pads.gamepad[1].ptrControllerPacket = NULL;
    pads.gamepad[0].buttonsTapped = BTN_CROSS_one;
    UI_VsWaitForPressX();
    UI_VsWaitForPressX();
    assert(gt.timerEndOfRaceVS == 30);
}
'''

FORCED_LOSS = r'''
#include "ap/ap_race_attempt_logic.h"
static APRaceAttemptState g_dl_race_attempt;
int AP_CoopHardRace(void) { return 0; }
''' + function("ap/ap_deathlink.c", "int AP_RaceAttemptIsForcedLoss(") + function(
    "game/MAIN/MainGameEnd.c", "static void MainGameEnd_UpdateAdventureLosses("
) + r'''
int main(void)
{
    setup();
    drivers[0].driverRank = 5;
    drivers[1].driverRank = 0;
    assert(AP_CoopWon(&gt));
    MainGameEnd_UpdateAdventureLosses(&gt, &drivers[0]);
    assert(sdata->advProgress.timesLostRacePerLev[CRASH_COVE] == 0);

    AP_RaceAttempt_ArmForcedLoss(&g_dl_race_attempt);
    assert(!AP_CoopWon(&gt)); /* The Adventure end menu uses this for Retry. */
    MainGameEnd_UpdateAdventureLosses(&gt, &drivers[0]);
    assert(sdata->advProgress.timesLostRacePerLev[CRASH_COVE] == 1);

    gt.numPlyrCurrGame = 1;
    drivers[0].driverRank = 0;
    MainGameEnd_UpdateAdventureLosses(&gt, &drivers[0]);
    assert(sdata->advProgress.timesLostRacePerLev[CRASH_COVE] == 0);
}
'''

BOX = r'''
#include "ap/ap_boxes.h"
#define AP_BOX_LIVE_MAX 1
#define AP_SPAWN_INVALID -1
#define AP_PAD_GHOST_ALPHA 0xa00
static struct { int used, code, spawn, slot, ghostTouched; } s_live[AP_BOX_LIVE_MAX];
static int s_liveCount, s_level, s_boxPresent, breaks;
static struct Instance box;
static int ap_net_location_checked(long long code) { return 0; }
static void AP_Spawn_Remove(int spawn) {}
static void AP_BoxesFreeSeat(int i) {}
static void AP_BoxesSpawnOne(struct GameTracker *tracker, int i) {}
static void AP_BoxesApplyColour(struct GameTracker *tracker, int i) {}
static struct Instance *AP_Spawn_Instance(int spawn) { return &box; }
static int AP_BoxPresentationCollectable(int state) { return 1; }
static void AP_BoxBreak(struct GameTracker *tracker, int i, struct Instance *inst) { breaks++; }
void AP_LogLine(const char *line) {}
'''
for source, signature in (
    ("game/LinkedCollide.c", "struct Instance *LinkedCollide_Radius("),
    ("ap/ap_boxes.c", "static int AP_BoxOwnedByLocalPlayer("),
    ("ap/ap_boxes.c", "static int AP_BoxHitByLocalPlayer("),
    ("ap/ap_boxes.c", "static void AP_BoxGhostVisual("),
    ("ap/ap_boxes.c", "static void AP_BoxesTick("),
):
    BOX += function(source, signature)
BOX += r'''
int main(void)
{
    static struct Model model;
    static struct Instance karts[2];
    static struct Thread players[2];
    setup();
    box.model = &model;
    s_live[0].used = 1;
    for (int i = 0; i < 2; i++)
    {
        players[i].object = &drivers[i];
        players[i].inst = &karts[i];
        karts[i].thread = &players[i];
    }
    players[0].siblingThread = &players[1];
    gt.threadBuckets[PLAYER].thread = players;
    drivers[0].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    karts[1].matrix.t[0] = AP_BOX_HIT_RADIUS;
    AP_BoxesTick(&gt);
    assert(breaks == 0);
    karts[1].matrix.t[0]--;
    AP_BoxesTick(&gt);
    assert(breaks == 1);
    gt.numPlyrCurrGame = 1;
    AP_BoxesTick(&gt);
    assert(breaks == 1);
}
'''

TIZI = r'''
#include "ap/ap_tizi.h"
static struct Instance crates[2];
static int AP_TiziActive(void) { return 1; }
static int AP_TiziIsRowCrate(struct Instance *crate) { return crate == &crates[0] || crate == &crates[1]; }
'''
TIZI += next(line for line in (ROOT / "ap/ap_tizi.c").read_text().splitlines()
             if " g_tizi_pending = " in line and line.startswith("static ")) + "\n"
for signature in ("void AP_TiziTick(", "void AP_TiziOnWeaponBoxHit(", "int AP_TiziFilterRoll("):
    TIZI += function("ap/ap_tizi.c", signature)
TIZI += r'''
int main(void)
{
    setup();
    AP_TiziOnWeaponBoxHit(&drivers[0], &crates[0]);
    AP_TiziOnWeaponBoxHit(&drivers[1], &crates[1]);
    drivers[0].heldItemID = drivers[1].heldItemID = 0x10;
    AP_TiziTick(&gt);
    assert(AP_TiziFilterRoll(&drivers[1], 3) == AP_TIZI_HELD_ITEM_MASK);
    assert(AP_TiziFilterRoll(&drivers[0], 3) == AP_TIZI_HELD_ITEM_MASK);
    assert(AP_TiziFilterRoll(&drivers[1], 3) == 3);

    AP_TiziOnWeaponBoxHit(&drivers[0], &crates[0]);
    AP_TiziOnWeaponBoxHit(&drivers[1], &crates[1]);
    drivers[0].heldItemID = 3;
    AP_TiziTick(&gt);
    assert(AP_TiziFilterRoll(&drivers[0], 3) == 3);
    assert(AP_TiziFilterRoll(&drivers[1], 3) == AP_TIZI_HELD_ITEM_MASK);
}
'''

WUMPA = r'''
#include "ap/ap_wumpa.c"
#include "ap/ap_wumpa_dispatch.h"
AP_WumpaShopConfig ctr_wumpa_shop;
static int checks, banked;
static const long p2Code = 35016100;
int ctr_cfg_active(void) { return 1; }
int LOAD_IsOpen_RacingOrBattle(void) { return 1; }
int AP_RaceAttemptIsForcedLoss(void) { return 0; }
void AP_LogLine(const char *line) {}
void ap_net_wumpa_credit(int amount) { banked += amount; }
long long ap_net_wumpa_lifetime(void) { return 0; }
void AP_NavRec_NoteWumpaGrant(int amount) {}
int OtherFX_Play(u32 id, int flags) { return 0; }
int AP_LocationChecksAllowed(void) { return 1; }
int ap_net_location_exists(long long code) { return code == p2Code; }
static void AP_AppendLog(const char *line) {}
static void AP_WumpaGatherFacts(struct GameTracker *tracker, struct AP_WumpaDispatchFacts *facts)
{
    static const ctr_wumpa_checks wumpa = { .mode = 1, .global_code = 35016100 };
    *facts = (struct AP_WumpaDispatchFacts){ .wumpa = &wumpa };
}
static int AP_EmitClassCheck(long code, int ledger, int bit, int tag, int toast, const char *fmt, ...)
{
    assert(code == p2Code);
    checks++;
    return 1;
}
''' + function("ap/ap_hooks_race.c", "void AP_WumpaReachedTen(") + function(
    "game/231/RB_Player.c", "void RB_Player_ModifyWumpa("
) + r'''
int main(void)
{
    setup();
    gt.trafficLightsTimer = 1;
    AP_WumpaTick(&gt);
    gt.trafficLightsTimer = 0;
    gt.gameMode1 |= PAUSE_ALL;
    drivers[0].numWumpas = 10; /* Only P2 can cross ten on the grant. */
    drivers[1].numWumpas = 8;
    for (int i = 0; i < 12; i++)
        AP_WumpaReceive(10);
    AP_WumpaTick(&gt);
    assert(drivers[1].numWumpas == 8 && checks == 0);
    gt.gameMode1 &= ~PAUSE_ALL;
    AP_WumpaTick(&gt);
    assert(drivers[0].numWumpas == 10 && drivers[1].numWumpas == 10);
    assert(checks == 1);
    AP_WumpaTick(&gt);
    assert(checks == 1);

    ctr_wumpa_shop.goal = 1;
    drivers[0].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    AP_WumpaTick(&gt);
    int before = banked;
    drivers[1].numWumpas = 8;
    AP_WumpaReceive(120);
    AP_WumpaTick(&gt);
    assert(banked - before == 120 && drivers[0].numWumpas == 10);
    assert(drivers[1].numWumpas == 10 && checks == 2);
    AP_WumpaTick(&gt);
    assert(banked - before == 120 && checks == 2);
}
'''

STEERING = r'''
#include "ap/ap_traps.h"
#include "ap/ap_trap_alpha3_logic.h"
#define AP_STICK_NEUTRAL 0x80
static int g_trap_use_pending, g_trap_forced_press, g_trap_stuck_side;
static unsigned char g_active[AP_TRAP_EFFECT_COUNT];
static int AP_TrapReverseRestored(void) { return 0; }
int AP_CapabilityRoomStatsRace(void) { return 1; }
'''
STEERING += next(line for line in (ROOT / "ap/ap_traps.c").read_text().splitlines()
                 if " g_trap_steer_mirrored = " in line and line.startswith("static ")) + "\n"
for signature in ("static struct Driver *AP_TrapLocalDriver(", "static int AP_TrapIsLocal(",
                  "static int AP_TrapMirrorStick(", "void AP_TrapDriveInput("):
    STEERING += function("ap/ap_traps.c", signature)
STEERING += r'''
int main(void)
{
    u32 held = 0, tapped = 0, cross = 0, square = 0;
    setup();
    g_active[AP_TRAP_REVERSE_STEERING] = 1;
    pads.gamepad[0].stickLX = 32;
    pads.gamepad[1].stickLX = 200;
    for (int call = 0; call < 2; call++)
        for (int i = 0; i < 2; i++)
            AP_TrapDriveInput(&drivers[i], &pads.gamepad[i], &held, &tapped, &cross, &square);
    assert(pads.gamepad[0].stickLX == 224 && pads.gamepad[1].stickLX == 56);
}
'''

PARTNER_DEATHLINK = r'''
#include "ap/ap_race_attempt_logic.h"
static int g_dl_partner_effect[2], g_dl_partner_grace[2], hard, forced, window;
int AP_CoopHardRace(void) { return hard; }
int AP_RaceAttemptIsForcedLoss(void) { return forced; }
static int AP_DeathLinkWindow(struct GameTracker *gt) { (void)gt; return window; }
'''
PARTNER_DEATHLINK += function("ap/ap_deathlink.c", "static void AP_DeathLinkQueuePartner(")
PARTNER_DEATHLINK += r'''
int main(void)
{
    setup();
    window = AP_DL_WIN_LIVE;
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_FALL);
    assert(g_dl_partner_effect[1] == CTR_DL_MASK_RESET && !g_dl_partner_effect[0]);
    g_dl_partner_effect[1] = 0;
    hard = 1;
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_FALL);
    assert(!g_dl_partner_effect[1]); /* Seed off. */
    ctr_cfg.death_link = CTR_DL_RACE_LOSS;
    ctr_cfg.death_link_send = AP_DL_TRIG_FALL;
    drivers[0].actionsFlagSet = ACTION_RACE_FINISHED | ACTION_BOT;
    AP_DeathLinkQueuePartner(&gt, &drivers[1], AP_DL_TRIG_FALL);
    assert(g_dl_partner_effect[0] == CTR_DL_RACE_LOSS); /* Still loses the team race. */
    drivers[0].actionsFlagSet = 0;
    ctr_cfg.death_link = CTR_DL_WEAPON_HIT;
    ctr_cfg.death_link_send = AP_DL_TRIG_HIT;
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_FALL);
    assert(!g_dl_partner_effect[1]);
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_HIT);
    assert(g_dl_partner_effect[1] == CTR_DL_WEAPON_HIT);
    hard = 0;
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_HIT);
    assert(g_dl_partner_effect[1] == CTR_DL_MASK_RESET);
    g_dl_partner_effect[1] = 0;
    window = AP_DL_WIN_PAUSED;
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_FALL);
    assert(!g_dl_partner_effect[1]);
    gt.numPlyrCurrGame = 1;
    window = AP_DL_WIN_LIVE;
    AP_DeathLinkQueuePartner(&gt, &drivers[0], AP_DL_TRIG_FALL);
    assert(!g_dl_partner_effect[1]);
}
'''

FOLLOWER = function("game/231/RB_Follower.c", "static struct InstDrawPerPlayer *RB_Follower_GetIDPP(")
FOLLOWER += function("game/231/RB_Follower.c", "void RB_Follower_ProcessBucket(")
FOLLOWER += r'''
int main(void)
{
    static struct {
        struct Instance inst;
        struct InstDrawPerPlayer views[2];
    } follower, mine;
    static struct Thread followerThread, mineThread;
    static struct Follower state;
    setup();
    gt.numPlyrNextGame = 1;
    followerThread.inst = &follower.inst;
    followerThread.object = &state;
    mineThread.inst = &mine.inst;
    state.mineTh = &mineThread;
    for (int owner = 0; owner < 2; owner++)
    {
        state.driver = &drivers[owner];
        state.driver->driverID = owner;
        follower.views[0].instFlags = follower.views[1].instFlags = DRAW_SUCCESSFUL;
        mine.views[0].instFlags = mine.views[1].instFlags = DRAW_SUCCESSFUL;
        RB_Follower_ProcessBucket(&followerThread);
        assert(follower.views[owner].instFlags & DRAW_SUCCESSFUL);
        assert(!(follower.views[!owner].instFlags & DRAW_SUCCESSFUL));
        assert(!(mine.views[owner].instFlags & DRAW_SUCCESSFUL));
        assert(mine.views[!owner].instFlags & DRAW_SUCCESSFUL);
    }
}
'''

HUB_FREEZE = r'''
void VehPhysProc_Driving_Init(struct Thread *t, struct Driver *d) {}
void VehPhysProc_FreezeEndEvent_Init(struct Thread *t, struct Driver *d) {}
''' + function("ap/ap_charswap.c", "void AP_HubPickerFreeze(") + r'''
int main(void)
{
    setup();
    gt.gameMode1 |= ADVENTURE_ARENA;
    gt.gameMode2 = TOKEN_RACE;
    AP_HubPickerFreeze(&gt, 1);
    assert(gt.gameMode2 & VEH_FREEZE_DOOR);
    assert(drivers[0].funcPtrs[DRIVER_FUNC_INIT] == VehPhysProc_FreezeEndEvent_Init);
    AP_HubPickerFreeze(&gt, 0);
    assert(gt.gameMode2 == TOKEN_RACE);
    assert(drivers[0].funcPtrs[DRIVER_FUNC_INIT] == VehPhysProc_Driving_Init);
}
'''

cases = {
    "disconnect": DISCONNECT,
    "forced-loss": FORCED_LOSS,
    "box": BOX,
    "tizi": TIZI,
    "wumpa": WUMPA,
    "steering": STEERING,
    "partner-deathlink": PARTNER_DEATHLINK,
    "follower": FOLLOWER,
    "hub-freeze": HUB_FREEZE,
}
with tempfile.TemporaryDirectory() as directory:
    for name in sys.argv[1:] or cases:
        path = Path(directory)
        source = path / f"{name}.c"
        executable = path / name
        source.write_text(COMMON + cases[name])
        subprocess.run(["cc", "-m32", "-std=c99", "-DCTR_AP", "-DCTR_NATIVE", "-DBUILD=926",
                        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                        "-I", str(ROOT), "-I", str(ROOT / "include"), "-I", str(ROOT / "ap"),
                        str(source), "-o", str(executable), "-lm"], check=True)
        subprocess.run([str(executable)], check=True)
        print(f"PASS co-op production regression: {name}")
