"""Compile the production glow pass against a bounded mock GPU arena."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks_sanity.c").read_text()
start = source.index("void AP_SanityDrawGlow(")
end = source.index("\n#endif", start)
production = source[start:end]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ap_sanity_colour.h"
typedef uint32_t u32;
typedef uint16_t u16;
typedef int32_t s32;
typedef struct { short vx, vy, vz, pad; } SVECTOR;
#define DRAW_INSTANCE 1
#define HIDE_MODEL 128
#define LOAD_IDLE 0
struct Matrix { int t[3]; };
struct Instance { unsigned flags; struct Matrix matrix; struct { short x,y,z; } scale; };
struct PushBuffer { int distanceToScreen_PREV; struct Matrix matrix_ViewProj; struct { int w,h; } rect; u32 *ptrOT; };
struct PrimMem { void *end, *cursor; };
struct DB { struct PrimMem primMem; };
struct GameTracker { int numPlyrCurrGame; struct PushBuffer pushBuffer[4]; struct DB *backBuffer; };
static struct { struct { int stage; } Loading; } globals, *sdata = &globals;
static int ap_sanity_ready = 1, ap_sanity_size = 1, packets, mockDepth, mockFlag, checked;
static struct { struct Instance *inst; long code; } ap_sanity_objects[1];
static void *AP_SanityRoute(struct GameTracker *gt) { return gt; }
static int AP_SanityAlreadySent(long code) { (void)code; return checked; }
static int AP_LocationLogicState(long code) { (void)code; return 1; }
#define gte_SetGeomScreen(x) ((void)(x))
#define gte_SetGeomOffset(x,y) ((void)(x),(void)(y))
#define gte_SetRotMatrix(x) ((void)(x))
#define gte_SetTransMatrix(x) ((void)(x))
#define CTR_GteLoadSV0(x) (mockDepth = (x)->vz)
#define gte_rtps() ((void)0)
#define CTR_GteStoreSXY(x) ((x)[0]=256,(x)[1]=108)
#define gte_stsz(x) (*(x)=mockDepth)
#define gte_stflg(x) (*(x)=mockFlag)
static void CtrGpu_LinkPacket24(u32 *ot, u32 *tag, void *packet, u32 length) {
    u32 *p = packet;
    (void)ot; (void)tag;
    assert(length == 0x07000000 && (p[1] == 0xe1000a20 || p[1] == 0xe1000a40) && (p[2] >> 24) == 0x32);
    ++packets;
}
'''
tests = r'''
int main(void) {
    u32 arena[8192], ot[1024];
    struct Instance inst = {DRAW_INSTANCE, {{0,0,1000}}, {4096,4096,4096}};
    struct DB db = {{arena + 8192, arena}};
    struct GameTracker gt = {0};
    gt.numPlyrCurrGame = 1; gt.backBuffer = &db;
    gt.pushBuffer[0].distanceToScreen_PREV = 256;
    gt.pushBuffer[0].rect.w = 512; gt.pushBuffer[0].rect.h = 216; gt.pushBuffer[0].ptrOT = ot;
    ap_sanity_objects[0].inst = &inst;
    AP_SanityDrawGlow(&gt); assert(packets == 16 && db.primMem.cursor == arena + 128);
    assert((arena[2] & 0xffffff) == AP_SanityHaloColour(AP_PAD_LOOK_OPEN, AP_SANITY_VISIBILITY));
    assert(arena[65] == 0xe1000a40);
    assert((arena[66] & 0xffffff) == AP_SanityHaloFilterColour(AP_PAD_LOOK_OPEN, AP_SANITY_VISIBILITY));
    packets = 0; db.primMem.cursor = arena; checked = 1;
    AP_SanityDrawGlow(&gt); assert(packets == 8);
    assert((arena[2] & 0xffffff) == AP_SanityHaloColour(AP_PAD_LOOK_DONE, AP_SANITY_VISIBILITY));
    for (int axis = 0; axis < 3; ++axis) {
        if (axis == 0) inst.scale.x = 0;
        if (axis == 1) inst.scale.y = 0;
        if (axis == 2) inst.scale.z = 0;
        packets = 0; db.primMem.cursor = arena;
        AP_SanityDrawGlow(&gt); assert(!packets && db.primMem.cursor == arena);
        inst.scale.x = inst.scale.y = inst.scale.z = 4096;
    }
    inst.flags = DRAW_INSTANCE | HIDE_MODEL; AP_SanityDrawGlow(&gt); assert(!packets);
    inst.flags = 0; AP_SanityDrawGlow(&gt); assert(!packets);
    inst.flags = DRAW_INSTANCE; db.primMem.end = arena + 63;
    AP_SanityDrawGlow(&gt); assert(!packets && db.primMem.cursor == arena);
    db.primMem.end = arena + 8192; inst.matrix.t[2] = 100;
    AP_SanityDrawGlow(&gt); assert(!packets);
    inst.matrix.t[2] = 7000; AP_SanityDrawGlow(&gt); assert(!packets);
    inst.matrix.t[2] = 1000; mockFlag = 0x80000000u;
    AP_SanityDrawGlow(&gt); assert(!packets);
    mockFlag = 0; ap_sanity_ready = -1; AP_SanityDrawGlow(&gt); assert(!packets);
    ap_sanity_ready = 1; sdata->Loading.stage = 1; AP_SanityDrawGlow(&gt); assert(!packets);
    puts("production sanity glow: packets, palette, zero scale, flags, arena, depth and load guards pass");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "glow.c", Path(tmp) / "glow"
    src.write_text(fixture + production + tests)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
