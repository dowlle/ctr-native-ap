"""Issue #458: boss wins and Oxide's Challenge count only from the player's own races.

1. Compile the production network glue for the boss-won flags (ap/ap_net.cpp:
   flush, the one-time migration and the C accessors) against a stub client
   and a stub checked-location set.
2. Compile the production readers from ap/ap_hooks.c (AP_ComposedBossesWon,
   AP_OxideFirstChallengeCleared) against stub flags and a checked-location
   stub that reports everything checked, and check that a Collect-marked
   location no longer counts.
3. Check the wiring: the key's connection lifecycle, the record sites in the
   boss-win and Oxide-win paths, and every reader that must use the flags.

The flag struct, migration rule and the goal and garage decisions are covered
by tools/test-boss-won-flags.cpp.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def extract(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


# ── 1. network glue, compiled from ap_net.cpp ───────────────────────────────
net_source = (root / 'ap/ap_net.cpp').read_text()
net = extract(net_source, 'static void ap_boss_won_flush()\n{')
net += extract(net_source, 'static unsigned ap_boss_won_migration_bits()\n{')
net += extract(net_source, 'extern "C" int ap_net_boss_won_known(void)')
net += extract(net_source, 'extern "C" unsigned ap_net_boss_won_bits(void)')
net += extract(net_source, 'extern "C" void ap_net_boss_won_record(unsigned bit)')

net_fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <set>
#include "ap_boss_won_flags.h"
void AP_LogLine(const char*) {}
#define AP_NET_GUARD(label, ...) __VA_ARGS__
struct APClient {
 struct DataStorageOperation {std::string operation; nlohmann::json value;};
 int calls=0; unsigned lastValue=0; bool success=true; std::string key;
 bool Set(const std::string &k,int d,bool reply,std::initializer_list<DataStorageOperation> ops) {
  assert(d==0 && reply); assert(ops.begin()->operation=="or");
  lastValue=ops.begin()->value.get<unsigned>();
  assert(lastValue>=1 && lastValue<=AP_BOSS_WON_ALL);
  calls++; key=k; return success;
 }
};
static APBossWonFlags g_boss_won;
static APClient client, *g_ap=&client;
static bool g_connected=true, g_rejected=false;
static std::set<int64_t> g_locs_chk;
static int refreshes=0;
static void ap_locs_refresh(void) { refreshes++; }
static struct {int schema_newer=0; int goal_oxide=0;} ctr_cfg;
static int configActive=1;
int ctr_cfg_active(){return configActive;}
'''
net_tests = r'''
static void connectFresh(const char *seed) { g_boss_won.connect("host", seed, 0, 1); }
int main() {
 /* Old seed in progress: Ripper Roo (35011100), Komodo Joe (35011102) and
    Oxide's Challenge (35011104) checked, key absent. */
 g_locs_chk = {35011100, 35011102, 35011104, 35011200};
 ctr_cfg.goal_oxide = 2;
 connectFresh("seed:a");
 assert(!ap_net_boss_won_known());
 assert(g_boss_won.retrievedNeedsSeed(nullptr));
 g_boss_won.seed(ap_boss_won_migration_bits());
 assert(refreshes==1);
 assert(ap_net_boss_won_known());
 assert(ap_net_boss_won_bits()==(1u|4u|AP_BOSS_WON_OXIDE_FIRST|AP_BOSS_WON_PRESENT));
 ap_boss_won_flush();
 assert(client.calls==1 && client.lastValue==(1u|4u|AP_BOSS_WON_OXIDE_FIRST|AP_BOSS_WON_PRESENT));
 assert(client.key==g_boss_won.key);
 g_boss_won.reply(client.lastValue);
 /* any_percent: a checked Oxide's Challenge is not copied. */
 ctr_cfg.goal_oxide = 1;
 connectFresh("seed:b");
 assert(g_boss_won.retrievedNeedsSeed(nullptr));
 g_boss_won.seed(ap_boss_won_migration_bits());
 assert(ap_net_boss_won_bits()==(1u|4u|AP_BOSS_WON_PRESENT));
 ap_boss_won_flush(); g_boss_won.reply(client.lastValue);
 /* A live win is recorded and sent once. */
 ap_net_boss_won_record(AP_BOSS_WON_BIT(3));
 assert(client.calls==3 && client.lastValue==8u);
 ap_net_boss_won_record(AP_BOSS_WON_BIT(3)); assert(client.calls==3);
 g_boss_won.reply(1u|4u|8u|AP_BOSS_WON_PRESENT);
 /* Disconnected: recorded, held, sent after the next Get for the same seed. */
 g_connected=false; g_boss_won.disconnected();
 ap_net_boss_won_record(AP_BOSS_WON_BIT(1));
 assert(client.calls==3 && (ap_net_boss_won_bits() & 2u));
 assert(!ap_net_boss_won_known());
 g_connected=true; connectFresh("seed:b");
 ap_boss_won_flush(); assert(client.calls==3); /* not before the Get */
 assert(!g_boss_won.retrievedNeedsSeed(1u|4u|8u|AP_BOSS_WON_PRESENT));
 ap_boss_won_flush(); assert(client.calls==4 && client.lastValue==2u);
 /* No send without slot_data or from a newer schema. */
 connectFresh("seed:c"); g_boss_won.retrievedNeedsSeed(AP_BOSS_WON_PRESENT);
 configActive=0; ap_net_boss_won_record(1u); assert(client.calls==4);
 configActive=1; ctr_cfg.schema_newer=1; ap_boss_won_flush(); assert(client.calls==4);
 ctr_cfg.schema_newer=0; ap_boss_won_flush(); assert(client.calls==5);
 /* A refused seed records nothing and reads as no wins. */
 connectFresh("seed:d"); g_boss_won.retrievedNeedsSeed(15);
 g_rejected=true;
 assert(ap_net_boss_won_bits()==0 && !ap_net_boss_won_known());
 ap_net_boss_won_record(AP_BOSS_WON_OXIDE_FIRST); assert(client.calls==5);
 g_rejected=false;
 assert(!(ap_net_boss_won_bits() & AP_BOSS_WON_OXIDE_FIRST));
 std::puts("ok net glue");
}
'''

# ── 2. readers, compiled from ap_hooks.c ────────────────────────────────────
hooks = (root / 'ap/ap_hooks.c').read_text()
bosses_fn = extract(hooks, 'static int AP_ComposedBossesWon(void)\n{')
first_fn = extract(hooks, 'int AP_OxideFirstChallengeCleared(void)\n{')
assert 'AP_LocationCheckedByBit' not in bosses_fn
assert 'AP_LocationCheckedByBit' not in first_fn

readers_fixture = r'''
#include <stdio.h>
#include "ap_boss_won_flags.h"
#define ADV_REWARD_FIRST_BOSS_KEY 94
#define AP_GOAL_BIT_OXIDE_FIRST 115
static unsigned g_bits;
static int g_checkedReads;
unsigned ap_net_boss_won_bits(void) { return g_bits; }
/* Every location reads as checked, as after a full Collect. */
int AP_LocationCheckedByBit(int bit) { (void)bit; g_checkedReads++; return 1; }
'''
readers_tests = r'''
int main(void)
{
    int fails = 0;
    g_bits = 0;
    if (AP_ComposedBossesWon() != 0) { puts("FAIL collect-marked bosses counted"); fails++; }
    if (AP_OxideFirstChallengeCleared() != 0) { puts("FAIL collect-marked Oxide's Challenge cleared"); fails++; }
    g_bits = AP_BOSS_WON_BIT(0) | AP_BOSS_WON_BIT(3) | AP_BOSS_WON_PRESENT;
    if (AP_ComposedBossesWon() != 2) { puts("FAIL two wins"); fails++; }
    if (AP_OxideFirstChallengeCleared() != 0) { puts("FAIL Oxide not won"); fails++; }
    g_bits = AP_BOSS_WON_ALL;
    if (AP_ComposedBossesWon() != 4 || AP_OxideFirstChallengeCleared() != 1) { puts("FAIL all won"); fails++; }
    if (g_checkedReads != 0) { puts("FAIL a checked location was read"); fails++; }
    puts(fails ? "FAIL readers" : "ok readers");
    return fails != 0;
}
'''

# ── 3. wiring ───────────────────────────────────────────────────────────────
assert net_source.count('g_boss_won.disconnected();') == 3
assert net_source.count('g_boss_won.connect(') == 1
connect_at = net_source.index('g_boss_won.connect(')
assert net_source.index('g_ap->SetNotify({g_boss_won.key});') > connect_at
assert net_source.index('g_ap->Get({g_boss_won.key});') > connect_at
assert ('if (g_boss_won.retrievedNeedsSeed(bossWon->second))\n'
        '\t\t\t\tg_boss_won.seed(ap_boss_won_migration_bits());') in net_source
assert 'g_boss_won.reply(value);' in net_source
assert re.search(r'\n\tap_boss_won_flush\(\);\n\tap_boss_door_scene_flush\(\);\n\tap_doors_flush\(\);\n\tap_oxide_scene_flush\(\);\n\}', net_source)

# The boss win is recorded in the shared reward body before the per-session
# dedup, on every win, and arms the goal when it is new.
impl = extract(hooks, 'static void AP_NotifyAdvRewardImpl(int rewardBit)\n{')
rec = impl.index('ap_net_boss_won_record(bit);')
assert rec < impl.index('if (ap_notified_mask[w] & (1u << b))')
assert 'if ((newEarn || bossWonNew) && rewardBit >= ADV_REWARD_FIRST_BOSS_KEY &&' in impl
assert hooks.count('ap_net_boss_won_record(') == 2
goal = extract(hooks, 'void AP_NotifyGoal(int oxideSecond)\n{')
assert 'ap_net_boss_won_record(AP_BOSS_WON_OXIDE_FIRST);' in goal
assert goal.index('ap_net_boss_won_record(AP_BOSS_WON_OXIDE_FIRST);') > goal.index('AP_RaceAttempt_ProducerBlocked')

# Every "beaten" reader goes through the two functions.
inputs = extract(hooks, 'static AP_OxideGarageInputs AP_OxideInputs(void)\n{')
assert 'in.firstCleared = AP_OxideFirstChallengeCleared();' in inputs
assert 'in.bossesWon = AP_ComposedBossesWon();' in inputs
assert 'int won = AP_ComposedBossesWon();' in extract(hooks, 'void AP_EvaluateGoal(void)\n{')
assert 'AP_ComposedBossesWon()' in extract(hooks, 'int AP_GoalAdvert(char *out, int cap)\n{')
for b in ('ADV_REWARD_FIRST_BOSS_KEY + b', 'AP_GOAL_BIT_OXIDE_FIRST)'):
    assert ('AP_LocationCheckedByBit(' + b) not in hooks, b
door = (root / 'ap/ap_boss_door_scene.c').read_text()
assert 'ap_net_boss_won_bits() & AP_BOSS_WON_BIT(hub)' in door
assert 'AP_LocationCheckedByBit(ADV_REWARD_FIRST_BOSS_KEY' not in door

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'net.cpp').write_text(net_fixture + net + net_tests)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=undefined',
                    '-I', str(root / 'ap'), '-I', str(root / 'ap/vendor/json/include'),
                    str(tmp / 'net.cpp'), '-o', str(tmp / 'net')], check=True)
    subprocess.run([str(tmp / 'net')], check=True)
    (tmp / 'readers.c').write_text(readers_fixture + bosses_fn + first_fn + readers_tests)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Wno-unused-function',
                    '-fsanitize=undefined', '-I', str(root / 'ap'),
                    str(tmp / 'readers.c'), '-o', str(tmp / 'readers')], check=True)
    subprocess.run([str(tmp / 'readers')], check=True)
print('Production boss-won flag network glue, readers and record sites passed')
