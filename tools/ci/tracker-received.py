"""Run production receipt and location-state logic against two R1 reward rules."""

from pathlib import Path
import subprocess
import sys
import tempfile


root = Path(__file__).resolve().parents[2]
source = (root / "ap/ap_win_logic.c").read_text(encoding="utf-8")
# Build the production glue above its first engine-specific function.
glue = source[source.index("static AP_ItemIdTally"):source.index("static long AP_WinBitCode")]
fixture = r"""
#include <assert.h>
#include <string.h>
#define AP_WIN_LOGIC_FREESTANDING_ONLY 1
#include "ap_win_logic.h"
#include "ap_pad_hints_logic.h"
#include "ap_pad_logic_look.h"

AP_WinLogic ctr_win_logic;
static struct {
    int boost_mode, starting_character, character_unlocks;
} ctr_cfg;

static int AP_ComposedBossesWon(void) { return 0; }
static int ap_net_location_checked(long long code) { (void)code; return 0; }
static int ap_net_identity_hash(unsigned long long *id) { (void)id; return 0; }
static void AP_LogLine(const char *line) { (void)line; }
""" + glue + r"""

enum {
    KEY = 35010014,
    TROPHY = 35010000,
    TURBO_TROPHY = 35016201,
    SEWER_TROPHY = 35011003,
    SEWER_CTR = 35012303,
};

static void test_keys(void)
{
    ctr_win_logic.nodes[0] = (AP_WinLogicNode){AP_WL_T_REQ, 2, 2, -1};
    ctr_win_logic.nodes[1] = (AP_WinLogicNode){AP_WL_T_TRUE};
    ctr_win_logic.checks[0] = (AP_WinLogicCheck){TURBO_TROPHY, AP_WL_KIND_TROPHY, 0, 1};
    assert(AP_LocationLogicState(TURBO_TROPHY) == AP_PAD_LOGIC_OUT);
    AP_WinLogicTallyItem(KEY, -1, 0);
    assert(AP_LocationLogicState(TURBO_TROPHY) == AP_PAD_LOGIC_OUT);
    AP_WinLogicTallyItem(KEY, -1, 0);
    assert(AP_LocationLogicState(TURBO_TROPHY) == AP_PAD_LOGIC_IN);
    assert(!AP_LocationInLogic(TURBO_TROPHY)); /* progression-only approach hints */
    AP_WinLogicTallyReset();
    assert(AP_LocationLogicState(TURBO_TROPHY) == AP_PAD_LOGIC_OUT);

    ctr_win_logic.start_count = 1;
    ctr_win_logic.start_id[0] = KEY;
    ctr_win_logic.start_n[0] = 1;
    AP_WinLogicTallyItem(KEY, -2, 0);
    assert(AP_LocationLogicState(TURBO_TROPHY) == AP_PAD_LOGIC_OUT);
    AP_WinLogicTallyItem(KEY, 35014194, 2);
    assert(AP_LocationLogicState(TURBO_TROPHY) == AP_PAD_LOGIC_IN);
}

static void test_second_stage(void)
{
    /* R1 exports the reach rule of the first race (true on this pad)
     * in the later check's rule, leaving a two-Trophy requirement. */
    ctr_win_logic.nodes[0] = (AP_WinLogicNode){AP_WL_T_TRUE};
    ctr_win_logic.nodes[1] = (AP_WinLogicNode){AP_WL_T_REQ, 1, 2, -1};
    ctr_win_logic.checks[0] = (AP_WinLogicCheck){SEWER_TROPHY, AP_WL_KIND_TROPHY, 0, 0};
    ctr_win_logic.checks[1] = (AP_WinLogicCheck){SEWER_CTR, AP_WL_KIND_CTR, 0, 1};
    ctr_win_logic.check_count = 2;
    assert(AP_LocationLogicState(SEWER_TROPHY) == AP_PAD_LOGIC_IN);
    AP_WinLogicTallyItem(TROPHY, 35014588, 1);
    assert(AP_LocationLogicState(SEWER_CTR) == AP_PAD_LOGIC_OUT);
    AP_WinLogicTallyItem(TROPHY, 35014762, 1);
    assert(AP_LocationLogicState(SEWER_CTR) == AP_PAD_LOGIC_IN);
    AP_WinLogicTallyReset();
    AP_WinLogicTallyItem(TROPHY, 35014588, 1);
    AP_WinLogicTallyItem(TROPHY, -1, 0);
    assert(AP_LocationLogicState(SEWER_CTR) == AP_PAD_LOGIC_IN);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    AP_WinLogicTallyReset();
    ctr_win_logic.state = AP_WL_VALID;
    ctr_win_logic.node_count = 2;
    ctr_win_logic.region_count = 1;
    ctr_win_logic.check_count = 1;
    if (!strcmp(argv[1], "keys"))
        test_keys();
    else {
        assert(!strcmp(argv[1], "second-stage"));
        test_second_stage();
    }
}
"""

with tempfile.TemporaryDirectory(prefix="tracker-received-") as directory:
    source_path = Path(directory) / "tracker.c"
    binary = Path(directory) / "tracker"
    source_path.write_text(fixture, encoding="utf-8")
    subprocess.run(["cc", "-std=c99", "-Werror", "-I", "ap",
                    str(source_path), "-o", str(binary)], cwd=root, check=True)
    subprocess.run([str(binary), sys.argv[1]], cwd=directory, check=True)
