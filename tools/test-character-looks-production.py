"""Character looks (JL2): run the production ap/ap_looks.c with host seams.

Builds ap/ap_looks.c as a 32-bit host program (the native build's pointer size)
with stand-ins for the engine, loads the real bundled Komodo Moe file through
the production loader and checks the model VehBirth_GetModelByName would get:
the relocated header, the static frame, the found model's name and id. Also
checks the newest-wins and replay rules, the OPTIONS row, the fallbacks for a
missing or altered file, the content-plan envelope admission of
`character_looks` in the real slot_data parser, and the wiring in the drain,
the effect marker, VehBirth, the menu and packaging.
Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def text(rel):
    return (root / rel).read_text(encoding="utf-8")


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
arm = net[net.index("else if (AP_LookForItem(idx) >= 0)"):]
arm = arm[:arm.index("}") + 1]
assert "AP_LooksReceiveOwned(idx);" in arm and "AP_FxOffer(items[i], ap_net_recv_batch_index(i));" in arm
received = text("ap/ap_hooks_received.c")
apply_fn = received[received.index("static void AP_FxApply(long long item)"):]
apply_fn = apply_fn[:apply_fn.index("\n}\n")]
assert "AP_LooksReceiveNew(idx);" in apply_fn
assert "#define AP_VERIFY_ITEM_INDEX_COUNT (AP_LOOK_KOMODO_MOE_ITEM_INDEX + 1)" in received
unity = text("game/game_unity.h")
assert '#include "../ap/ap_looks.c"' in unity and '#include "../ap/ap_look_logic.h"' in unity
menu = text("game/230/MM_ConfigMenu.c")
assert menu.count("AP_LooksIsRowValue((const int *)e->valuePtr)") == 3
assert '"look_joe"' in text("include/ctr_menu_pages.h")
assert '"look_joe",                 "Joe Look"' in text("platform/native_config.c")
pack = text("tools/ci/package-client.py")
assert "assets/looks/komodo-moe/komodo_moe_hi.bin" in pack
assert "45634686aaa0ede40a2fe7025911ba7cf89199212057d689bc2289795fb20724" in pack
assert "45634686aaa0ede40a2fe7025911ba7cf89199212057d689bc2289795fb20724" in text("THIRD_PARTY_NOTICES.md")
assert '"KOMODO MOE MODEL - LOCKHEART"' in text("ap/ap_credits_data.h")

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
static char asset_dir[512];
static FILE *NativeAssets_OpenHost(const char *rel, const char *mode) {
  char p[1024]; snprintf(p, sizeof p, "%s/%s", asset_dir, rel); return fopen(p, mode);
}
static char last_log[512];
static int logs;
static void AP_LogLine(const char *m) { snprintf(last_log, sizeof last_log, "%s", m); logs++; fputs(m, stdout); }
SOURCE
int main(int argc, char **argv) {
  static char names[16][8] = {"crash","cortex","tiny","coco","ngin","dingo","polar","pura","pinstri","papu","roo","joe","penta","fake","oxide","ntropy"};
  struct ModelHeader joeHeader; struct Model joe, crash;
  assert(argc == 3);
  snprintf(asset_dir, sizeof asset_dir, "%s", argv[1]);
  for (int i = 0; i < 16; i++) data.MetaDataCharacters[i].name_Debug = names[i];
  memset(&joe, 0, sizeof joe); memset(&crash, 0, sizeof crash);
  strcpy(joe.name, "joe"); joe.id = 77; joe.numHeaders = 1; joe.headers = &joeHeader;
  strcpy(crash.name, "crash"); crash.id = 3;

  struct Model *m;
  if (strcmp(argv[2], "disk") == 0) {
    /* Nothing owned: Joe keeps his model, the row reads ORIGINAL and cannot move. */
    assert(AP_LooksModelFor("joe", &joe) == &joe);
    assert(strcmp(AP_LooksRowLabel(&g_config.lookJoe), "ORIGINAL") == 0);
    AP_LooksRowStep(&g_config.lookJoe, +1); assert(g_config.lookJoe == 0);
    assert(!AP_LooksRowHasChoice(&g_config.lookJoe));
    /* A replayed copy only marks the look owned: the saved choice stays. */
    AP_LooksReceiveOwned(215);
    assert(g_config.lookJoe == 0 && saves == 0);
    assert(AP_LooksModelFor("joe", &joe) == &joe);
    assert(AP_LooksRowHasChoice(&g_config.lookJoe));
    /* A new copy: the newest wins, saved once. */
    AP_LooksReceiveNew(215);
    assert(g_config.lookJoe == 215 && saves == 1);
    AP_LooksReceiveNew(215);
    assert(saves == 1);
    m = AP_LooksModelFor("joe", &joe);
    assert(m != &joe && m != NULL);
    assert(strcmp(m->name, "joe") == 0 && m->id == 77 && m->numHeaders == 1);
    struct ModelHeader *h = m->headers;
    assert(strcmp(h->name, "fake_hi") == 0 && h->maxDistanceLOD == -1);
    assert(h->numAnimations == 0 && h->ptrAnimations == NULL && h->animtex == NULL);
    unsigned char *base = (unsigned char *)m->headers - 0x18;
    assert((unsigned char *)(uintptr_t)h->ptrCommandList == base + 88);
    assert((unsigned char *)h->ptrFrameData == base + 8700);
    assert((unsigned char *)h->ptrColors == base + 15184);
    unsigned *cmd = (unsigned *)(uintptr_t)h->ptrCommandList;
    assert(cmd[0] == 64 && cmd[2152] == 0xFFFFFFFFu);
    short *frame = (short *)h->ptrFrameData;
    assert(frame[0] == -123 && frame[1] == 14 && frame[2] == -121);
    /* The same file serves every later birth: loaded and relocated once. */
    assert(AP_LooksModelFor("joe", &joe) == m && h->ptrCommandList == (unsigned)(uintptr_t)(base + 88));
    /* The look model times animations by the model the engine found. */
    assert(AP_LooksTimingModel(m) == &joe && AP_LooksTimingModel(&joe) == &joe && AP_LooksTimingModel(NULL) == NULL);
    {
      struct ModelHeader medHeader; struct Model joeMed = joe; joeMed.headers = &medHeader;
      struct Model *m2 = AP_LooksModelFor("joe", &joeMed);
      assert(m2 != m && AP_LooksTimingModel(m2) == &joeMed && AP_LooksTimingModel(m) == &joe);
      assert(m2->headers == m->headers);
    }
    /* Other racers and unknown names are untouched. */
    assert(AP_LooksModelFor("crash", &crash) == &crash);
    assert(AP_LooksModelFor("joe", NULL) == NULL);
    assert(AP_LooksModelFor("nobody", &joe) == &joe);
    /* OPTIONS row: KOMODO MOE, left to ORIGINAL (original model), right back. */
    assert(strcmp(AP_LooksRowLabel(&g_config.lookJoe), "KOMODO MOE") == 0);
    AP_LooksRowStep(&g_config.lookJoe, -1);
    assert(g_config.lookJoe == 0 && AP_LooksModelFor("joe", &joe) == &joe);
    AP_LooksRowStep(&g_config.lookJoe, +1);
    assert(g_config.lookJoe == 215 && AP_LooksModelFor("joe", &joe) == m);
    /* A reconnect clears ownership until the replay; the saved choice stays. */
    AP_LooksConnectReset();
    assert(AP_LooksModelFor("joe", &joe) == &joe && g_config.lookJoe == 215);
    assert(strcmp(AP_LooksRowLabel(&g_config.lookJoe), "ORIGINAL") == 0);
    AP_LooksReceiveOwned(215);
    assert(AP_LooksModelFor("joe", &joe) == m);
    /* Stepping a row that shows ORIGINAL because this seed has not delivered
       the saved look keeps the saved look. */
    AP_LooksConnectReset();
    AP_LooksRowStep(&g_config.lookJoe, +1); AP_LooksRowStep(&g_config.lookJoe, -1);
    assert(g_config.lookJoe == 215);
    AP_LooksReceiveOwned(215);
    /* A manual ORIGINAL survives a reconnect replay. */
    AP_LooksRowStep(&g_config.lookJoe, -1);
    AP_LooksConnectReset(); AP_LooksReceiveOwned(215);
    assert(g_config.lookJoe == 0 && AP_LooksModelFor("joe", &joe) == &joe);
    /* Quick load: a driver whose restored pointer names a stale model is born
       again by name; one already right is left alone. */
    {
      struct Model stale; struct Instance joeInst = {&stale}, crashInst = {&crash};
      struct Driver dJoe = {&joeInst, 0}, dCrash = {&crashInst, 1};
      found_joe = &joe; found_crash = &crash;
      data.characterIDs[0] = 11; data.characterIDs[1] = 0;
      gt.drivers[0] = &dJoe; gt.drivers[1] = &dCrash;
      g_config.lookJoe = 215;
      AP_LooksRebindDrivers();
      assert(joeInst.model != &stale && joeInst.model->headers == m->headers && AP_LooksTimingModel(joeInst.model) == &joe);
      assert(crashInst.model == &crash);
      g_config.lookJoe = 0;
      AP_LooksRebindDrivers();
      assert(joeInst.model == &joe);
      gt.drivers[0] = gt.drivers[1] = NULL;
    }
    /* A ghost wearing a look is detected for the quick-save refusal. */
    {
      struct Instance ghostInst = {&joe}; struct Thread ghost = {&ghostInst, NULL};
      gt.threadBuckets[GHOST].thread = &ghost;
      assert(!AP_LooksGhostWearsLook());
      g_config.lookJoe = 215;
      ghostInst.model = AP_LooksModelFor("joe", &joe);
      assert(AP_LooksGhostWearsLook());
      gt.threadBuckets[GHOST].thread = NULL;
      g_config.lookJoe = 0;
      assert(!AP_LooksGhostWearsLook());
    }
    /* Non-look rows and items are ignored. */
    int other = 5;
    assert(!AP_LooksIsRowValue(&other) && AP_LooksIsRowValue(&g_config.lookJoe));
    AP_LooksReceiveNew(203); AP_LooksReceiveOwned(214);
    assert(g_config.lookJoe == 0);
    puts("PASS: production loader, relocation, newest wins, replay and OPTIONS row");
  } else if (strcmp(argv[2], "reject") == 0) {
    AP_LooksReceiveNew(215);
    int before = logs;
    assert(AP_LooksModelFor("joe", &joe) == &joe);
    assert(logs == before + 1 && strstr(last_log, "the racer keeps the original model") != NULL);
    assert(AP_LooksModelFor("joe", &joe) == &joe && logs == before + 1);
  } else {
    /* Missing, altered or short file: use the embedded model. */
    AP_LooksReceiveNew(215);
    assert(g_config.lookJoe == 215);
    m = AP_LooksModelFor("joe", &joe);
    assert(m != &joe && m != NULL && m->headers != joe.headers);
    assert(strcmp(m->headers->name, "fake_hi") == 0);
    assert(((short *)m->headers->ptrFrameData)[0] == -123);
    int before = logs;
    assert(AP_LooksModelFor("joe", &joe) == m && logs == before);
  }
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
    bundled = root / "assets" / "looks" / "komodo-moe" / "komodo_moe_hi.bin"
    header = tmp / "ap_komodo_moe_embedded.h"

    def generate_header(asset):
        return subprocess.run(["cmake", f"-DLOOK_DEFINITIONS={root / 'ap/ap_look_logic.h'}",
                               f"-DOUTPUT={header}", f"-DINPUT={asset}", "-P", str(root / "cmake/EmbedKomodoMoe.cmake")],
                              text=True, capture_output=True)

    result = generate_header(bundled)
    assert result.returncode == 0, result.stderr
    generated = header.read_text()
    initializer = generated.split("{", 1)[1].split("}", 1)[0]
    assert bytes(int(token, 16) for token in initializer.rstrip().rstrip(",").split(",")) == bundled.read_bytes()
    src, exe = tmp / "looks.c", tmp / "looks"
    src.write_text(fixture)
    compile_loader = ["cc", "-m32", "-std=c11", "-DCTR_AP", "-Wall", "-Wextra", "-Wno-unused-function",
                      "-I", str(tmp), "-I", str(root / "ap"), "-I", str(root / "include"),
                      str(src), "-o", str(exe)]
    subprocess.run(compile_loader, check=True)

    def check_source(asset_dir, expected, case):
        result = subprocess.run([str(exe), str(asset_dir), expected], check=True, text=True, capture_output=True)
        loads = [line for line in result.stdout.splitlines() if line.startswith("[AP LOOK] loaded ")]
        assert len(loads) == 1 and f"from {expected} (15724 bytes, 717 triangles, pin ok)" in loads[0], result.stdout
        print(f"PASS: {case}: {loads[0]}")

    check_source(root / "assets", "disk", "valid file")
    asset_dir = tmp / "assets"
    candidate = asset_dir / bundled.relative_to(root / "assets")
    candidate.parent.mkdir(parents=True)
    check_source(asset_dir, "embedded fallback", "missing file")
    data = bytearray(bundled.read_bytes())
    data[200] ^= 1
    candidate.write_bytes(data)
    check_source(asset_dir, "embedded fallback", "altered file")
    result = generate_header(candidate)
    assert result.returncode != 0 and "model size or SHA-256 differs" in result.stderr, result.stderr
    assert header.read_text() == generated
    print("PASS: generator rejects altered source bytes")
    candidate.write_bytes(bundled.read_bytes()[:-4])
    check_source(asset_dir, "embedded fallback", "short file")
    candidate.write_bytes(bundled.read_bytes() + b"\x00")
    check_source(asset_dir, "embedded fallback", "oversized file")

    # Tamper with the generated initializer after generation to exercise the
    # runtime pin even though the generator rejects a bad source file.
    first_byte = f"0x{data[0]:02x}"
    corrupt_byte = f"0x{data[0] ^ 1:02x}"
    header.write_text(generated.replace(first_byte, corrupt_byte, 1))
    subprocess.run(compile_loader, check=True)
    result = subprocess.run([str(exe), str(tmp / "no-assets"), "reject"], check=True, text=True, capture_output=True)
    assert "SHA-256 does not match the pin" in result.stdout, result.stdout
    print("PASS: runtime rejects corrupt embedded bytes and keeps original Joe")

    src = tmp / "parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root / "ap"),
                    "-I", str(root / "ap/vendor/json/include"), str(src), str(root / "ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(root / "tools/fixtures/content-plan/baseline.json")], check=True)
print("PASS: character looks production checks")
