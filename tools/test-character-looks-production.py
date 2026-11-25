"""Verify the parked native hooks and legacy room compatibility."""
from pathlib import Path
import runpy
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def text(rel):
    return (root / rel).read_text(encoding="utf-8")


looks = root / "assets" / "looks"
assert not looks.is_symlink() and not any(p.is_file() or p.is_symlink() for p in looks.rglob("*"))
assert "EmbedKomodoMoe" not in text("CMakeLists.txt")
assert "ap_komodo_moe_embedded" not in text("ap/ap_looks.c")
assert not (root / "cmake" / "EmbedKomodoMoe.cmake").exists()

check_package = runpy.run_path(str(root / "tools/ci/package-client.py"))["require_no_character_models"]
with tempfile.TemporaryDirectory() as directory:
    bundle = Path(directory)
    check_package(bundle, b"clean executable")
    header = bytes.fromhex("503d000066616b65000000000000000000000000ffff01001800000066616b655f686900")
    try:
        check_package(bundle, b"prefix" + header + b"suffix")
    except ValueError as error:
        assert "embedded character model" in str(error)
    else:
        raise AssertionError("An embedded model passed the package check")
    extra = bundle / "assets" / "looks" / "future-look" / "model.bin"
    extra.parent.mkdir(parents=True)
    extra.touch()
    try:
        check_package(bundle, b"clean executable")
    except ValueError as error:
        assert "assets/looks/" in str(error)
    else:
        raise AssertionError("A model file passed the package check")


# --- wiring in the production sources ------------------------------------------
veh = text("game/Vehicle/VehBirth.c")
assert "return AP_LooksModelFor(searchName, VehBirth_FindModelByName(searchName));" in veh
net = text("ap/ap_hooks_net.c")
assert "AP_LooksConnectReset();" in net
reconnect = net[net.index("void AP_Net_Reconnect("):]
assert reconnect.index("AP_LooksConnectReset();") < reconnect.index("ap_net_shutdown();")
assert "model = AP_LooksTimingModel(model);" in text("game/Vehicle/VehFrame.c")
save = text("platform/native_savestate.c")
assert save.index("AP_LooksRebindDrivers();") < save.index("loaded quick state checksum")
assert "if (AP_LooksGhostWearsLook())" in save
assert "*humanPlyrDriverModel = AP_LooksModelFor(" in text("game/MAIN/MainInit.c")
author = text("ap/ap_author.c")
assert "else if (AP_AuthorGate_CloseOnEnter(ap_net_status()))\n\t{\n\t\tAP_LooksConnectReset();" in author
render = text("game/RenderBucket/RenderBucket_QueueExecute.c")
assert "return &ctx->packedStack[stackIndex];" in render and "RenderBucket_PackedVertexScratch(stackIndex)" not in render
assert "if (CTR_AP_CHARACTER_LOOKS || AP_LookForItem(idx) < 0)" in net
arm = net[net.index("else if (CTR_AP_CHARACTER_LOOKS && AP_LookForItem(idx) >= 0)"):]
arm = arm[:arm.index("}") + 1]
assert "AP_LooksReceiveOwned(idx);" in arm and "AP_FxOffer(items[i], ap_net_recv_batch_index(i));" in arm
received = text("ap/ap_hooks_received.c")
assert "else if (CTR_AP_CHARACTER_LOOKS && AP_LookForItem(idx) >= 0)" in received
assert "AP_LooksReceiveNew(idx);" in received
assert "AP_LOOK_KOMODO_MOE_ITEM_INDEX + 1" in received

# --- the runtime module with host seams ----------------------------------------
source = "\n".join(line for line in text("ap/ap_looks.c").splitlines()
                   if line not in ("#include <common.h>", "#include <platform/native_assets.h>",
                                   "#include <platform/native_config.h>", '#include "ap_hooks.h" // AP_LogLine'))
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef short s16;
typedef unsigned short u16;
struct ModelHeader {
  char name[0x10]; int unk1; s16 maxDistanceLOD; u16 flags; s16 scale[3]; s16 pad;
  unsigned ptrCommandList; void *ptrFrameData; void **ptrTexLayout; unsigned *ptrColors; unsigned unk3;
  unsigned numAnimations; void **ptrAnimations; void *animtex;
};
struct Model { char name[0x10]; s16 id; s16 numHeaders; struct ModelHeader *headers; };
_Static_assert(sizeof(void *) == 4, "the native build is 32-bit");
_Static_assert(offsetof(struct ModelHeader, ptrCommandList) == 0x20, "layout");
_Static_assert(offsetof(struct ModelHeader, ptrAnimations) == 0x38, "layout");
_Static_assert(sizeof(struct ModelHeader) == 0x40, "layout");
_Static_assert(offsetof(struct Model, headers) == 0x14, "layout");
struct MetaDataCharacter { char *name_Debug; };
static struct { struct MetaDataCharacter MetaDataCharacters[16]; short characterIDs[8]; } data;
struct Instance { struct Model *model; };
struct Driver { struct Instance *instSelf; int driverID; };
struct Thread { struct Instance *inst; struct Thread *siblingThread; };
struct ThreadBucket { struct Thread *thread; };
enum { GHOST = 2 };
struct GameTracker { struct Driver *drivers[8]; struct ThreadBucket threadBuckets[4]; };
static struct GameTracker gt;
static struct { struct GameTracker *gGT; } sdata_store = {&gt}, *sdata = &sdata_store;
static struct Model *found_joe, *found_crash;
struct Model *AP_LooksModelFor(const char *searchName, struct Model *found);
static struct Model *VehBirth_GetModelByName(char *name) {
  struct Model *f = strcmp(name, "joe") == 0 ? found_joe : strcmp(name, "crash") == 0 ? found_crash : NULL;
  return AP_LooksModelFor(name, f);
}
static struct { int lookJoe; } g_config;
static int saves;
static void NativeConfig_Save(void) { saves++; }
static int opened, logs;
static FILE *NativeAssets_OpenHost(const char *rel, const char *mode) {
  (void)rel; (void)mode; opened++; return NULL;
}
static void AP_LogLine(const char *m) { (void)m; logs++; }
SOURCE
int main(void) {
  struct Model joe = {0};
  found_joe = &joe;
  data.MetaDataCharacters[AP_LOOK_RACER_JOE].name_Debug = "joe";
  const int saved = AP_LOOK_KOMODO_MOE_ITEM_INDEX;
  g_config.lookJoe = saved;
  AP_LooksConnectReset();
  AP_LooksReceiveOwned(saved);
  AP_LooksReceiveNew(saved);
  assert(AP_LooksModelFor("joe", &joe) == &joe);
  assert(AP_LooksTimingModel(&joe) == &joe);
  assert(!AP_LooksIsRowValue(&g_config.lookJoe));
  assert(!AP_LooksRowHasChoice(&g_config.lookJoe));
  AP_LooksRowStep(&g_config.lookJoe, -1);
  AP_LooksRebindDrivers();
  assert(!AP_LooksGhostWearsLook());
  assert(g_config.lookJoe == saved && !saves && !opened && !logs);
  puts("PASS: parked look leaves the original model and saved choice alone");
  return 0;
}
'''.replace("SOURCE", source)

parser = r'''
#include <cassert>
#include <fstream>
#include "ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]); nlohmann::json plan; input >> plan;
  ap_seedcfg_parse_json(plan); assert(ctr_cfg_active());
  plan["ctr_options"]["character_looks"] = nlohmann::json::array({"Joe Mama"});
  ap_seedcfg_parse_json(plan); assert(ctr_cfg_active());
  plan["ctr_options"]["character_looks"] = nlohmann::json::array();
  ap_seedcfg_parse_json(plan); assert(ctr_cfg_active());
  /* Not a list of names: left in, and the closed envelope refuses it. */
  plan["ctr_options"]["character_looks"] = nlohmann::json::array({1});
  ap_seedcfg_parse_json(plan); assert(!ctr_cfg_active());
  plan["ctr_options"]["character_looks"] = true;
  ap_seedcfg_parse_json(plan); assert(!ctr_cfg_active());
  plan["ctr_options"]["character_looks"] = nlohmann::json::array({"Joe Mama"});
  plan["ctr_options"]["reserve_top_up"] = true;
  ap_seedcfg_parse_json(plan); assert(ctr_cfg_active() && ctr_cfg.reserve_top_up == 1);
  puts("PASS: content-plan envelope admits character_looks as a list of names only");
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="ctr-character-looks-") as tmp:
    tmp = Path(tmp)
    src, exe = tmp / "looks.c", tmp / "looks"
    src.write_text(fixture)
    subprocess.run(["cc", "-m32", "-std=c11", "-DCTR_AP", "-Wall", "-Wextra",
                    "-I", str(root / "ap"), "-I", str(root / "include"),
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

    src = tmp / "parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root / "ap"),
                    "-I", str(root / "ap/vendor/json/include"), str(src), str(root / "ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(root / "tools/fixtures/content-plan/baseline.json")], check=True)
print("PASS: character looks production checks")
