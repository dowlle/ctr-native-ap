"""Issue #377: compile the production network glue for the once-per-seed Oxide
Final Challenge scene flag (ap/ap_net.cpp) against a stub client, and check the
wiring of the three scene sites and the podium-skip keep rule.

The flag struct itself is covered by tools/test-oxide-scene-seen.cpp.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
net_source = (root / 'ap/ap_net.cpp').read_text()


def extract(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


net = extract(net_source, 'static void ap_oxide_scene_flush()\n{')
net += extract(net_source, 'extern "C" int ap_net_oxide_scene_known(void)')
net += next(line for line in net_source.splitlines(True)
            if line.startswith('extern "C" int ap_net_oxide_scene_seen('))
net += extract(net_source, 'extern "C" void ap_net_oxide_scene_record(void)')

fixture = r'''
#include <cassert>
#include <cstdio>
#include "ap_oxide_scene_seen.h"
void AP_LogLine(const char*) {}
#define AP_NET_GUARD(label, ...) __VA_ARGS__
struct APClient {
 struct DataStorageOperation {std::string operation; nlohmann::json value;};
 int calls=0; bool success=true; std::string key;
 bool Set(const std::string &k,int d,bool reply,std::initializer_list<DataStorageOperation> ops) {
  assert(d==0 && reply); assert(ops.begin()->operation=="or");
  assert(ops.begin()->value.get<int>()==1);
  calls++; key=k; return success;
 }
};
static APOxideSceneSeen g_oxide_scene;
static APClient client, *g_ap=&client;
static bool g_connected=true, g_rejected=false;
static struct {int schema_newer=0;} ctr_cfg;
static int configActive=1;
int ctr_cfg_active(){return configActive;}
'''
tests = r'''
int main() {
 g_oxide_scene.connect("host","seed:a",0,1);
 /* Fail safe: not known until the Get reply. */
 assert(!ap_net_oxide_scene_known());
 g_oxide_scene.retrieved(nullptr);
 assert(ap_net_oxide_scene_known() && !ap_net_oxide_scene_seen());
 g_connected=false; assert(!ap_net_oxide_scene_known()); g_connected=true;
 g_rejected=true; assert(!ap_net_oxide_scene_known()); g_rejected=false;
 /* The play is recorded and sent once. */
 ap_net_oxide_scene_record();
 assert(ap_net_oxide_scene_seen() && client.calls==1 && client.key==g_oxide_scene.key);
 ap_net_oxide_scene_record(); ap_oxide_scene_flush(); assert(client.calls==1);
 g_oxide_scene.reply(1); assert(!g_oxide_scene.pending);
 /* A failed Set stays pending and goes out on the next poll flush. */
 g_oxide_scene.connect("host","seed:b",0,1); g_oxide_scene.retrieved(nullptr);
 client.success=false; ap_net_oxide_scene_record();
 assert(client.calls==2 && g_oxide_scene.pending && !g_oxide_scene.sent);
 client.success=true; ap_oxide_scene_flush(); assert(client.calls==3 && g_oxide_scene.sent);
 /* No send without slot_data, from a newer schema, or while disconnected. */
 g_oxide_scene.connect("host","seed:c",0,1); g_oxide_scene.retrieved(nullptr);
 configActive=0; ap_net_oxide_scene_record(); assert(client.calls==3);
 configActive=1; ctr_cfg.schema_newer=1; ap_oxide_scene_flush(); assert(client.calls==3);
 ctr_cfg.schema_newer=0; g_connected=false; ap_oxide_scene_flush(); assert(client.calls==3);
 g_connected=true; ap_oxide_scene_flush(); assert(client.calls==4);
 /* A refused seed records nothing. */
 g_oxide_scene.connect("host","seed:d",0,1); g_oxide_scene.retrieved(nullptr);
 g_rejected=true; ap_net_oxide_scene_record(); assert(!ap_net_oxide_scene_seen());
 std::puts("ok");
}
'''

# Wiring. Counts pin every site so a new consumer has to be looked at.
assert net_source.count('g_oxide_scene.disconnected();') == 3
assert net_source.count('g_oxide_scene.connect(') == 1
connect_at = net_source.index('g_oxide_scene.connect(')
assert net_source.index('g_ap->Get({g_oxide_scene.key});') > connect_at
assert net_source.index('g_ap->SetNotify({g_oxide_scene.key});') > connect_at
assert 'g_oxide_scene.retrieved(scene->second);' in net_source
assert 'g_oxide_scene.reply(value);' in net_source
assert re.search(r'ap_doors_flush\(\);\n\tap_oxide_scene_flush\(\);\n\}', net_source)

camera = (root / 'game/233/CS_Camera.c').read_text()
thread = (root / 'game/233/CS_Thread.c').read_text()
skip = (root / 'ap/ap_podium_skip.c').read_text()
for name, text, n in (('CS_Camera.c', camera, 2), ('CS_Thread.c', thread, 1),
                      ('ap_podium_skip.c', skip, 1)):
    assert text.count('AP_OxideFinalSceneReady(') == n, name
    assert 'AP_OxideFinalEncounterPresentationReady' not in text, name
# The play is recorded only where the Oxide relic index is committed, after
# both CS_Camera.c sites have asked.
mark = camera.index('AP_OxideFinalSceneMarkPlayed();')
assert camera.count('AP_OxideFinalSceneMarkPlayed();') == 1
assert camera.rindex('OXIDE_RELICS_GEMSTONE;', 0, mark) > camera.rindex('AP_OxideFinalSceneReady(', 0, mark)
tmark = thread.index('AP_OxideFinalSceneMarkPlayed();')
assert thread.count('AP_OxideFinalSceneMarkPlayed();') == 1
assert thread.rindex('D233.bossCutsceneIndex = 9;', 0, tmark) > thread.rindex('AP_OxideFinalSceneReady(', 0, tmark)
assert 'AP_OxideFinalSceneMarkPlayed' not in skip

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'net.cpp'
    exe = Path(tmp) / 'net'
    src.write_text(fixture + net + tests)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=undefined',
                    '-I', str(root / 'ap'), '-I', str(root / 'ap/vendor/json/include'),
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Production Oxide scene flag network glue and site wiring passed')
