"""Issue #377: the once-per-hub boss-door scene after a Trophy podium.

1. Compile the production VehBirth_ShouldSpawnOutsideBoss (game/Vehicle/
   VehBirth.c) with and without CTR_AP against stub engine state, and check
   that the retail rule is unchanged without CTR_AP and without slot_data,
   and that with slot_data the decision is AP_BossDoorSceneReady for the
   podium's hub.
2. Compile the production network glue for the flag (ap/ap_net.cpp) against a
   stub client.
3. Check the wiring of the spawn site, the record site in CS_Camera.c and the
   flag's connection lifecycle.

The pure rule and its composition are covered by tools/test-boss-door-scene.c,
the flag struct by tools/test-boss-door-scene-seen.cpp.
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


# ── 1. the spawn decision, compiled from VehBirth.c ─────────────────────────
vehbirth = (root / 'game/Vehicle/VehBirth.c').read_text()
spawn_fn = extract(vehbirth, 'static int VehBirth_ShouldSpawnOutsideBoss(struct GameTracker *gGT)\n{')

spawn_fixture = r'''
#include <stdio.h>
#include <string.h>
#define STATIC_TROPHY 0x62
#define STATIC_KEY 0x63
#define STATIC_RELIC 0x61
#define GEM_STONE_VALLEY 25
#define N_SANITY_BEACH 26
#define ADV_REWARD_FIRST_TROPHY 0x06
#define ADV_REWARD_FIRST_BOSS_KEY 0x5e
#define MEMCARD_BIT_WORD(b) ((b) >> 5)
#define CHECK_ADV_BIT(rewards, bitIndex)  (((rewards)[MEMCARD_BIT_WORD(bitIndex)] >> ((bitIndex) & 0x1f)) & 1)
struct GameTracker { int podiumRewardID; int levelID; };
static struct { struct { unsigned rewards[8]; } advProgress; } sd, *sdata = &sd;
/* Stand-in hub track ids: four distinct tracks per hub. */
static struct { short advHubTrackIDs[16]; } data = {{
    2, 6, 1, 9, 3, 5, 4, 10, 7, 11, 12, 8, 14, 15, 13, 16}};
#ifdef CTR_AP
static int cfgActive, readyAnswer, readyCalls, readyHub;
int ctr_cfg_active(void) { return cfgActive; }
int AP_BossDoorSceneReady(int hub) { readyCalls++; readyHub = hub; return readyAnswer; }
#endif
static void setbit(int b) { sdata->advProgress.rewards[b >> 5] |= 1u << (b & 31); }
'''

spawn_tests = r'''
/* The retail rule, transcribed from the pre-#377 source: all four hub
   Trophy bits set and the hub's boss Key bit clear. */
static int retail(int reward, int level)
{
    int i, base;
    if (reward != STATIC_TROPHY) return 0;
    base = (level - N_SANITY_BEACH) * 4;
    for (i = 0; i < 4; i++)
        if (!CHECK_ADV_BIT(sdata->advProgress.rewards, data.advHubTrackIDs[base + i] + ADV_REWARD_FIRST_TROPHY))
            return 0;
    return !CHECK_ADV_BIT(sdata->advProgress.rewards, level - N_SANITY_BEACH + ADV_REWARD_FIRST_BOSS_KEY);
}

int main(void)
{
    int fails = 0, reward, hub, mask, key, cases = 0;
    const int rewards[] = {0, STATIC_TROPHY, STATIC_KEY, STATIC_RELIC};
    for (reward = 0; reward < 4; reward++)
    for (hub = 0; hub < 4; hub++)
    for (mask = 0; mask < 16; mask++)
    for (key = 0; key < 2; key++)
    {
        struct GameTracker g;
        int i, got;
        memset(sdata, 0, sizeof *sdata);
        for (i = 0; i < 4; i++)
            if (mask & (1 << i)) setbit(data.advHubTrackIDs[hub * 4 + i] + ADV_REWARD_FIRST_TROPHY);
        if (key) setbit(ADV_REWARD_FIRST_BOSS_KEY + hub);
        g.podiumRewardID = rewards[reward];
        g.levelID = N_SANITY_BEACH + hub;
#ifdef CTR_AP
        /* Without slot_data the AP build keeps the retail rule. */
        cfgActive = 0; readyCalls = 0; readyAnswer = 1;
        got = VehBirth_ShouldSpawnOutsideBoss(&g);
        if (got != retail(g.podiumRewardID, g.levelID) || readyCalls) { printf("FAIL AP no slot_data r=%d h=%d m=%d k=%d\n", reward, hub, mask, key); fails++; }
        /* With slot_data the AP decision answers for the podium's hub, and
           the received-item bits are not read. */
        {
            int answer;
            for (answer = 0; answer < 2; answer++)
            {
                cfgActive = 1; readyCalls = 0; readyHub = -99; readyAnswer = answer;
                got = VehBirth_ShouldSpawnOutsideBoss(&g);
                if (g.podiumRewardID == STATIC_TROPHY)
                {
                    if (got != answer || readyCalls != 1 || readyHub != hub) { printf("FAIL AP trophy h=%d a=%d\n", hub, answer); fails++; }
                }
                else if (got != 0 || readyCalls != 0) { printf("FAIL AP non-trophy r=%d\n", reward); fails++; }
            }
        }
#else
        got = VehBirth_ShouldSpawnOutsideBoss(&g);
        if (got != retail(g.podiumRewardID, g.levelID)) { printf("FAIL retail r=%d h=%d m=%d k=%d\n", reward, hub, mask, key); fails++; }
#endif
        cases++;
    }
#ifdef CTR_AP
    /* Gem Stone Valley and any other level are hub -1 or out of range; the
       AP decision rejects them (tools/test-boss-door-scene.c). */
    {
        struct GameTracker g = {STATIC_TROPHY, GEM_STONE_VALLEY};
        cfgActive = 1; readyCalls = 0; readyHub = -99; readyAnswer = 0;
        VehBirth_ShouldSpawnOutsideBoss(&g);
        if (readyHub != -1) { printf("FAIL gemstone hub %d\n", readyHub); fails++; }
    }
#endif
    printf("%s %d cases\n", fails ? "FAIL" : "ok", cases);
    return fails != 0;
}
'''

# ── 2. the flag's network glue, compiled from ap_net.cpp ────────────────────
net_source = (root / 'ap/ap_net.cpp').read_text()
net = extract(net_source, 'static void ap_boss_door_scene_flush()\n{')
net += extract(net_source, 'extern "C" int ap_net_boss_door_scene_known(void)')
net += next(line for line in net_source.splitlines(True)
            if line.startswith('extern "C" int ap_net_boss_door_scene_seen('))
net += extract(net_source, 'extern "C" void ap_net_boss_door_scene_record(int hub)')

net_fixture = r'''
#include <cassert>
#include <cstdio>
#include "ap_boss_door_scene_seen.h"
void AP_LogLine(const char*) {}
#define AP_NET_GUARD(label, ...) __VA_ARGS__
struct APClient {
 struct DataStorageOperation {std::string operation; nlohmann::json value;};
 int calls=0, lastValue=0; bool success=true; std::string key;
 bool Set(const std::string &k,int d,bool reply,std::initializer_list<DataStorageOperation> ops) {
  assert(d==0 && reply); assert(ops.begin()->operation=="or");
  lastValue=ops.begin()->value.get<int>();
  assert(lastValue>=1 && lastValue<=15);
  calls++; key=k; return success;
 }
};
static APBossDoorSceneSeen g_boss_door_scene;
static APClient client, *g_ap=&client;
static bool g_connected=true, g_rejected=false;
static struct {int schema_newer=0;} ctr_cfg;
static int configActive=1;
int ctr_cfg_active(){return configActive;}
'''
net_tests = r'''
int main() {
 g_boss_door_scene.connect("host","seed:a",0,1);
 /* Fail safe: not known until the Get reply. */
 assert(!ap_net_boss_door_scene_known());
 g_boss_door_scene.retrieved(nullptr);
 assert(ap_net_boss_door_scene_known() && !ap_net_boss_door_scene_seen(0));
 g_connected=false; assert(!ap_net_boss_door_scene_known()); g_connected=true;
 g_rejected=true; assert(!ap_net_boss_door_scene_known()); g_rejected=false;
 /* A hub is recorded and sent once, as its own bit. */
 ap_net_boss_door_scene_record(2);
 assert(ap_net_boss_door_scene_seen(2) && !ap_net_boss_door_scene_seen(1));
 assert(client.calls==1 && client.lastValue==4 && client.key==g_boss_door_scene.key);
 ap_net_boss_door_scene_record(2); ap_boss_door_scene_flush(); assert(client.calls==1);
 g_boss_door_scene.reply(4); assert(!g_boss_door_scene.pending);
 ap_net_boss_door_scene_record(0);
 assert(client.calls==2 && client.lastValue==1);
 g_boss_door_scene.reply(5);
 /* Out of range hubs record nothing. */
 ap_net_boss_door_scene_record(-1); ap_net_boss_door_scene_record(4); assert(client.calls==2);
 /* A failed Set stays pending and goes out on the next poll flush. */
 g_boss_door_scene.connect("host","seed:b",0,1); g_boss_door_scene.retrieved(nullptr);
 client.success=false; ap_net_boss_door_scene_record(3);
 assert(client.calls==3 && g_boss_door_scene.pending && !g_boss_door_scene.sent);
 client.success=true; ap_boss_door_scene_flush(); assert(client.calls==4 && g_boss_door_scene.sent);
 /* No send without slot_data, from a newer schema, or while disconnected. */
 g_boss_door_scene.connect("host","seed:c",0,1); g_boss_door_scene.retrieved(nullptr);
 configActive=0; ap_net_boss_door_scene_record(1); assert(client.calls==4);
 configActive=1; ctr_cfg.schema_newer=1; ap_boss_door_scene_flush(); assert(client.calls==4);
 ctr_cfg.schema_newer=0; g_connected=false; ap_boss_door_scene_flush(); assert(client.calls==4);
 g_connected=true; ap_boss_door_scene_flush(); assert(client.calls==5 && client.lastValue==2);
 /* A refused seed records nothing. */
 g_boss_door_scene.connect("host","seed:d",0,1); g_boss_door_scene.retrieved(nullptr);
 g_rejected=true; ap_net_boss_door_scene_record(0); assert(!ap_net_boss_door_scene_seen(0));
 g_rejected=false;
 /* Seen on the server before this session. */
 g_boss_door_scene.connect("host","seed:e",0,1); g_boss_door_scene.retrieved(8);
 assert(ap_net_boss_door_scene_seen(3) && !ap_net_boss_door_scene_seen(0));
 std::puts("ok");
}
'''

# ── 3. wiring ───────────────────────────────────────────────────────────────
assert net_source.count('g_boss_door_scene.disconnected();') == 3
assert net_source.count('g_boss_door_scene.connect(') == 1
connect_at = net_source.index('g_boss_door_scene.connect(')
assert net_source.index('g_ap->Get({g_boss_door_scene.key});') > connect_at
assert net_source.index('g_ap->SetNotify({g_boss_door_scene.key});') > connect_at
assert 'g_boss_door_scene.retrieved(bossDoor->second);' in net_source
assert 'g_boss_door_scene.reply(value);' in net_source
assert re.search(r'\n\tap_boss_door_scene_flush\(\);\n\tap_doors_flush\(\);\n\tap_oxide_scene_flush\(\);\n\}', net_source)

# The spawn decision is made in one place, only for the retail "spawn outside
# the boss" branch: the first-Key door freeze and SPAWN_AT_BOSS are untouched.
assert vehbirth.count('AP_BossDoorSceneReady(') == 1
assert vehbirth.count('spawnOutsideBoss = VehBirth_ShouldSpawnOutsideBoss(gGT);') == 1
call = vehbirth.index('spawnOutsideBoss = VehBirth_ShouldSpawnOutsideBoss(gGT);')
assert vehbirth.rindex('doorInst = VehBirth_FindDoor5(level1);', 0, call) < call
assert vehbirth.index('if (spawnAtBoss != 0)\n\t\t{\n\t\t\tspawnOutsideBoss = 1;', call) > call
assert 'if (ctr_cfg_active())\n\t{\n\t\treturn AP_BossDoorSceneReady(gGT->levelID - N_SANITY_BEACH);' in spawn_fn

# The hub is recorded once, in the podium camera, for a Trophy podium that
# hands over to the scene, before the Skip Cutscenes exit or the watched path
# is taken, so both count.
camera = (root / 'game/233/CS_Camera.c').read_text()
assert camera.count('AP_BossDoorSceneMarkPlayed(') == 1
mark = camera.index('AP_BossDoorSceneMarkPlayed(gGT->levelID - N_SANITY_BEACH);')
taken = camera.rindex('AP_BossDoorSceneTaken(rewardId == STATIC_TROPHY,', 0, mark)
assert 'CS_Camera_BoolGotoBoss() != 0))' in camera[taken:mark]
assert camera.rindex('int apSkipScene = AP_CutsceneSkipDecision(', 0, taken) < taken
assert camera.index('if (apSkipScene || CS_Camera_BoolGotoBoss() == 0)', mark) > mark
assert 'AP_BossDoorScene' not in (root / 'game/233/CS_Podium.c').read_text()
assert '#include "../ap/ap_boss_door_scene.c"' in (root / 'game/game_unity.h').read_text()

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'spawn.c').write_text(spawn_fixture + spawn_fn + spawn_tests)
    for flags, name in (([], 'retail'), (['-DCTR_AP'], 'ap')):
        exe = tmp / ('spawn-' + name)
        subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-fsanitize=undefined', *flags,
                        str(tmp / 'spawn.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    (tmp / 'net.cpp').write_text(net_fixture + net + net_tests)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=undefined',
                    '-I', str(root / 'ap'), '-I', str(root / 'ap/vendor/json/include'),
                    str(tmp / 'net.cpp'), '-o', str(tmp / 'net')], check=True)
    subprocess.run([str(tmp / 'net')], check=True)
print('Production boss-door scene spawn rule, flag network glue and site wiring passed')
