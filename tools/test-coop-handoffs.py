"""Drive real handout ticks while one co-op human has finished."""
from pathlib import Path
import subprocess
import tempfile
from coop_fixture import function

root = Path(__file__).resolve().parents[1]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_coop.h"
#include "ap/ap_seedcfg.h"
#include "ap/ap_turbogrant.h"
#include "ap/ap_turbogrant_logic.h"
#include "ap/ap_useful_logic.h"
#include "ap/ap_useful.h"
#include "ap/ap_wumpa_shop.h"
struct sData sdata_static;
ctr_seed_config ctr_cfg;
static struct GameTracker gt;
static struct Driver humans[2];
static int active = 1, onTrack = 1, g_tg_received = 1, g_tg_turbo_weapon = 1;
static AP_TurboGrantRace g_tg_race;
static int g_usefulCountdownSeen;
static unsigned g_usefulReceived, g_usefulApplied;
static struct Driver *recipient;
int ctr_cfg_active(void) { return active; }
int LOAD_IsOpen_RacingOrBattle(void) { return onTrack; }
int OtherFX_Play(u32 id, int flags) { (void)id; (void)flags; return 0; }
void AP_LogLine(const char *line) { (void)line; }
int AP_ItemsanityWeaponAvailable(int item) { (void)item; return 1; }
void VehPickupItem_ShootNow(struct Driver *driver, int item, int flags)
{ (void)item; (void)flags; recipient = driver; }
static void AP_UsefulStore(void) {}
static int AP_TurboGrantItemsanityOn(void) { return 1; }
'''
for path, signature in (
    ('ap/ap_turbogrant.c', 'static struct Driver *AP_TurboGrantLocalDriver('),
    ('ap/ap_turbogrant.c', 'static int AP_TurboGrantSlotFree('),
    ('ap/ap_turbogrant.c', 'void AP_TurboGrantTick('),
    ('ap/ap_useful.c', 'static int AP_UsefulRaceActive('),
    ('ap/ap_useful.c', 'void AP_UsefulTick('),
    ('ap/ap_wumpa.c', 'static int AP_WumpaAdventureRace('),
    ('ap/ap_wumpa.c', 'static int AP_WumpaReceiptInRace('),
):
    fixture += function(path, signature)
fixture += r'''
static void reset(void) {
    memset(&gt, 0, sizeof gt); memset(humans, 0, sizeof humans);
    sdata->gGT = &gt; sdata->Loading.stage = LOAD_IDLE;
    gt.gameMode1 = ADVENTURE_MODE; gt.numPlyrCurrGame = 2;
    gt.drivers[0] = &humans[0]; gt.drivers[1] = &humans[1];
    humans[0].heldItemID = humans[1].heldItemID = 15;
    g_tg_race.countdownSeen = 1; g_tg_race.givenThisRace = 0;
    g_usefulCountdownSeen = 1; g_usefulReceived = 1; g_usefulApplied = 0;
    recipient = NULL;
}
int main(void) {
    reset(); AP_TurboGrantTick(&gt);
    assert(humans[0].heldItemID == 0 && humans[1].heldItemID == 15);
    reset(); humans[0].heldItemID = 1; AP_TurboGrantTick(&gt);
    assert(humans[0].heldItemID == 1 && humans[1].heldItemID == 0);
    reset(); humans[0].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    AP_TurboGrantTick(&gt); assert(humans[1].heldItemID == 0);
    reset(); humans[0].actionsFlagSet = humans[1].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    AP_TurboGrantTick(&gt); assert(!g_tg_race.givenThisRace);
    reset(); AP_UsefulTick(&gt); assert(recipient == &humans[0]);
    reset(); humans[0].heldItemID = 1; AP_UsefulTick(&gt); assert(recipient == &humans[1]);
    reset(); humans[0].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    AP_UsefulTick(&gt); assert(recipient == &humans[1]);
    reset(); humans[0].actionsFlagSet = humans[1].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    AP_UsefulTick(&gt); assert(!recipient && !g_usefulApplied);
    reset(); humans[0].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    assert(AP_WumpaReceiptInRace());
    humans[1].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    assert(!AP_WumpaReceiptInRace());
    reset(); humans[1].actionsFlagSet = ACTION_BOT | ACTION_RACE_FINISHED;
    assert(AP_WumpaReceiptInRace());
    gt.gameMode1 |= END_OF_RACE; assert(!AP_WumpaReceiptInRace());
    reset(); gt.gameMode1 = ARCADE_MODE; assert(!AP_WumpaReceiptInRace());
    reset(); onTrack = 0; assert(!AP_WumpaReceiptInRace());
    puts("PASS production co-op handouts: prefer P1, busy/finished P1 falls back to P2, both finished waits, fruit remains in the live race");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, binary = Path(tmp) / 'handoffs.c', Path(tmp) / 'handoffs'
    src.write_text(fixture)
    subprocess.run(['cc', '-m32', '-std=c99', '-DCTR_AP', '-DCTR_NATIVE', '-DBUILD=926',
                    '-I', str(root), '-I', str(root / 'include'), '-I', str(root / 'ap'),
                    str(src), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
