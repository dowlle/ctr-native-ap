"""A5-4: Increase Draw Distance scales the BSP and texture LOD thresholds, the far clip and the render arenas by one factor.
A5-13c: the top-level and recursive near-subdivision thresholds keep retail's value with the option on.

Ported from the CTR Native Expanded (thecodingbob) tests tests/test_draw_distance.c and
tests/test_level_lod.c, which this repository has no CTest target for. The production
headers include/platform/native_draw_distance.h and game/RenderLevel/RenderLevelLod.h are
compiled against small stubs for the engine types they need, with the retail values as
independent fixtures. Then the source is checked: every renderer path asks those helpers,
the clip-record arena goes through one function everywhere, and the primitive and
ordering-table arenas grow in the 8 MiB MEMPACK build, before the custom-track 1P floor:
on a 1P load only with the option on, on a split-screen (2P to 4P) load always (A5-4b,
maintainer ruling 2026-11-15), so the option works at once when it is switched on mid-race.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
failures = []


def read(rel):
    return (root / rel).read_text()


def check(cond, name):
    print(("ok    " if cond else "FAIL  ") + name)
    if not cond:
        failures.append(name)


def enum_value(rel, name):
    return int(re.search(r"\b" + name + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)", read(rel)).group(1), 0)


def enum_index(rel, name):
    body = read(rel)
    body = body[body.index("enum LevelID"):]
    value = -1
    for line in body.splitlines()[1:]:
        m = re.match(r"\s*([A-Z_0-9]+)\s*(=\s*(\d+))?\s*,", line)
        if not m:
            continue
        value = int(m.group(3)) if m.group(3) else value + 1
        if m.group(1) == name:
            return value
    raise SystemExit("level id not found: " + name)


dd = read("include/platform/native_draw_distance.h")
lod = read("game/RenderLevel/RenderLevelLod.h")
lod_body = lod[lod.index("enum RenderLevelLod2PConstants"):lod.rindex("#endif")]

fixture = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int32_t s32;
typedef uint32_t u32;
typedef uint8_t u8;
typedef uint64_t u64;
static inline s32 CTR_MipsSra(s32 value, u32 shift) { shift &= 0x1f; return (s32)(((u32)value >> shift) | ((value < 0) ? ~(0xffffffffu >> shift) : 0)); }
static inline s32 CTR_MipsMulLo(s32 lhs, s32 rhs) { return (s32)((u32)lhs * (u32)rhs); }
static inline s32 CTR_MipsAddLo(s32 lhs, s32 rhs) { return (s32)((u32)lhs + (u32)rhs); }
enum { CRASH_COVE = %(crash_cove)d, INTRO_CRASH = %(intro_crash)d, ADVENTURE_GARAGE = %(garage)d };
#define GAME_CUTSCENE 0x%(cutscene)xu
#define MAIN_RENDER_LEVEL_GEOMETRY_FULL_DYNAMIC_FADE_OFFSET %(fade)d
struct MainRenderLevelGeometryScratch
{
	u8 reserved0[0x14];
	s32 depthScale;
	s32 bspLodDistanceThreshold;
	s32 textureLodDepthThreshold0;
	s32 textureLodDepthThreshold1;
	s32 topLevelNearDepthThreshold;
	s32 recursiveNearDepthThreshold;
	s32 fullDynamicFadeDepthStart;
};
%(dd)s
%(lod)s
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL  %%s (line %%d)\n", msg, __LINE__); fails++; } } while (0)
struct V { int depthScale, bsp, t0, t1, top, rec; };
/* Independent retail fixtures (Expanded tests/test_level_lod.c). */
static const struct V special = {0x1e00, 1600, 1600, 1280, 640, 320};
static const struct V twoPlayer = {0x19c0, 0, 4096, 2048, 1536, 768};
static const struct V splitGround = {0x780, 0, 1600, 1280, 640, 320};
static void textures(const struct MainRenderLevelGeometryScratch *s, const struct V *r, bool on)
{
	int f = on ? NATIVE_DRAW_DISTANCE_SCALE : 1;
	CHECK(s->depthScale == r->depthScale, "depth projection does not depend on the option");
	CHECK(s->textureLodDepthThreshold0 == r->t0 * f, "texture LOD 0 scaled exactly once");
	CHECK(s->textureLodDepthThreshold1 == r->t1 * f, "texture LOD 1 scaled exactly once");
	/* A5-13c: the near-subdivision thresholds stay retail with the option on. */
	CHECK(s->topLevelNearDepthThreshold == r->top, "top-level subdivision stays retail");
	CHECK(s->recursiveNearDepthThreshold == r->rec, "recursive mosaic subdivision stays retail");
	CHECK(s->textureLodDepthThreshold0 >= s->textureLodDepthThreshold1 && s->textureLodDepthThreshold1 >= s->topLevelNearDepthThreshold &&
	      s->topLevelNearDepthThreshold >= s->recursiveNearDepthThreshold, "thresholds keep their near-to-far order");
}
static void onep(const struct MainRenderLevelGeometryScratch *s, const struct V *r, bool on)
{
	int f = on ? NATIVE_DRAW_DISTANCE_SCALE : 1;
	textures(s, r, on);
	CHECK(s->bspLodDistanceThreshold == r->bsp * f, "1P BSP threshold uses the same factor as the textures");
	CHECK(s->fullDynamicFadeDepthStart == r->bsp * f + MAIN_RENDER_LEVEL_GEOMETRY_FULL_DYNAMIC_FADE_OFFSET, "fade start follows the scaled BSP threshold");
	CHECK(RenderLevelLod_BspThreshold(s, 1, on) == r->bsp * f, "the 1P BSP walk does not scale twice");
}
int main(void)
{
	const int v = 1600;
	CHECK(NativeDrawDistance_FarClip(false) == 0x100, "off: far clip is retail 0x100");
	CHECK(NativeDrawDistance_GeomScreenHalf(v, false) == v, "off: near reject depth untouched");
	CHECK(NativeDrawDistance_ProjectedLodDistance(v, false) == v, "off: model LOD depth untouched");
	CHECK(NativeDrawDistance_BspLodThreshold(v, false) == v, "off: BSP LOD untouched");
	CHECK(NativeDrawDistance_TextureLodThreshold(v, false) == v, "off: texture LOD untouched");
	CHECK(NativeDrawDistance_ArenaBytes(v, false) == v, "off: arena size untouched");
	CHECK(NativeDrawDistance_FarClip(true) / 0x100 == NATIVE_DRAW_DISTANCE_SCALE, "on: far clip scaled by the factor");
	CHECK(v / NativeDrawDistance_GeomScreenHalf(v, true) == NATIVE_DRAW_DISTANCE_SCALE, "on: near reject depth divided by the factor");
	CHECK(v / NativeDrawDistance_ProjectedLodDistance(v, true) == NATIVE_DRAW_DISTANCE_SCALE, "on: model LOD depth divided by the factor");
	CHECK(NativeDrawDistance_BspLodThreshold(v, true) / v == NATIVE_DRAW_DISTANCE_SCALE, "on: BSP LOD scaled by the factor");
	CHECK(NativeDrawDistance_TextureLodThreshold(v, true) / v == NATIVE_DRAW_DISTANCE_SCALE, "on: texture LOD scaled by the factor");
	CHECK(NativeDrawDistance_SubdivisionThreshold(v, false) == v, "off: subdivision untouched");
	CHECK(NativeDrawDistance_SubdivisionThreshold(v, true) == v, "on: subdivision stays retail (A5-13c)");
	CHECK(NativeDrawDistance_ArenaBytes(v, true) / v == NATIVE_DRAW_DISTANCE_SCALE, "on: arena scaled by the factor");
	/* A5-4b: who grows. 1P follows the option at level load; 2P to 4P always grow;
	   the attract path (0 players), the main menu and cutscenes follow the option. */
	CHECK(!NativeDrawDistance_ArenaGrows(false, 1, false), "1P, option off: arenas keep retail size");
	CHECK(NativeDrawDistance_ArenaGrows(true, 1, false), "1P, option on: arenas grow");
	CHECK(!NativeDrawDistance_ArenaGrows(false, 0, false), "attract path, option off: arenas keep retail size");
	CHECK(!NativeDrawDistance_ArenaGrows(false, 4, true), "main menu (loads with 4 players), option off: arenas keep retail size");
	CHECK(NativeDrawDistance_ArenaGrows(true, 4, true), "main menu, option on: arenas grow as in A5-4");
	for (int p = 2; p <= 4; p++)
	{
		CHECK(NativeDrawDistance_ArenaGrows(false, p, false), "split screen, option off: arenas still grow");
		CHECK(NativeDrawDistance_ArenaGrows(true, p, false), "split screen, option on: arenas grow");
		CHECK(NativeDrawDistance_ArenaBytes(v, NativeDrawDistance_ArenaGrows(false, p, false)) == NativeDrawDistance_ArenaBytes(v, NativeDrawDistance_ArenaGrows(true, p, false)),
		      "split screen: the arena size does not depend on the option");
	}
	static const struct { int proj; struct V r; } race[] = {
	    {128, {4160, 3328, 3072, 1536, 896, 448}},
	    {256, {8320, 6656, 6144, 3072, 1792, 896}},
	    {257, {8352, 6682, 6168, 3084, 1799, 899}},
	    {320, {10400, 8320, 7680, 3840, 2240, 1120}},
	    {0x1c2, {14625, 11700, 10800, 5400, 3150, 1575}},
	};
	struct MainRenderLevelGeometryScratch s;
	memset(&s, 0xa5, sizeof s);
	for (unsigned i = 0; i < sizeof race / sizeof race[0]; i++)
		for (int pass = 0; pass < 3; pass++) /* off, on, off: no factor accumulates across a live toggle */
		{
			RenderLevelLod_Init1P(&s, CRASH_COVE, 0, race[i].proj, pass == 1);
			onep(&s, &race[i].r, pass == 1);
		}
	static const struct { int level; u32 mode; bool isSpecial; } scenes[] = {
	    {ADVENTURE_GARAGE, 0, true}, {ADVENTURE_GARAGE, GAME_CUTSCENE, true}, {CRASH_COVE, GAME_CUTSCENE, true},
	    {INTRO_CRASH, GAME_CUTSCENE, false}, {INTRO_CRASH, 0, false},
	};
	for (unsigned i = 0; i < sizeof scenes / sizeof scenes[0]; i++)
		for (int pass = 0; pass < 3; pass++)
		{
			RenderLevelLod_Init1P(&s, scenes[i].level, scenes[i].mode, 257, pass == 1);
			onep(&s, scenes[i].isSpecial ? &special : &race[2].r, pass == 1);
		}
	for (int players = 2; players <= 4; players++)
	{
		memset(&s, 0xa5, sizeof s);
		s.bspLodDistanceThreshold = 123;
		s.fullDynamicFadeDepthStart = 456;
		for (int pass = 0; pass < 3; pass++)
		{
			if (players == 2)
				RenderLevelLod_Init2P(&s, pass == 1);
			else
				RenderLevelLod_InitSplitGround(&s, pass == 1);
			textures(&s, players == 2 ? &twoPlayer : &splitGround, pass == 1);
			CHECK(RenderLevelLod_BspThreshold(&s, (u8)players, pass == 1) == 0x1540 * (pass == 1 ? NATIVE_DRAW_DISTANCE_SCALE : 1),
			      "split-screen BSP cutoff is retail 0x1540, scaled on its own");
			CHECK(s.bspLodDistanceThreshold == 123 && s.fullDynamicFadeDepthStart == 456, "split-screen setup leaves the 1P fields alone");
		}
	}
	printf("%%s  compiled LOD and draw distance helpers\n", fails ? "FAIL" : "ok  ");
	return fails != 0;
}
'''

source = fixture % {
    "crash_cove": enum_index("include/namespace_Level.h", "CRASH_COVE"),
    "intro_crash": enum_index("include/namespace_Level.h", "INTRO_CRASH"),
    "garage": enum_index("include/namespace_Level.h", "ADVENTURE_GARAGE"),
    "cutscene": enum_value("include/namespace_Main.h", "GAME_CUTSCENE"),
    "fade": enum_value("include/namespace_Main.h", "MAIN_RENDER_LEVEL_GEOMETRY_FULL_DYNAMIC_FADE_OFFSET"),
    "dd": dd.replace("#include <stdbool.h>", ""),
    "lod": lod_body,
}

with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "dd.c"
    exe = Path(tmp) / "dd"
    c.write_text(source)
    build = subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(c)], capture_output=True, text=True)
    check(build.returncode == 0, "LOD helpers compile cleanly against the stubs" + ("" if build.returncode == 0 else ": " + build.stderr[:2000]))
    if build.returncode == 0:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(run.stdout)
        check(run.returncode == 0, "compiled helper checks pass")

# --- Every renderer path asks the helpers ---------------------------------------------
frame = read("game/MAIN/MainFrame_RenderFrame.c")
check("RenderLevelLod_Init1P(scratch, gGT->levelID, gGT->gameMode1, pushBuffer->distanceToScreen_PREV, g_config.increaseDrawDistance);" in frame,
      "1P level render sets its thresholds through RenderLevelLod_Init1P")
check("bspLodDistanceThreshold * 3" not in frame and "bspLodDistanceThreshold = 0x640" not in frame,
      "the old x3 BSP-only scaling and inline thresholds are gone")
check(frame.index("RenderLevelLod_Init1P(") < frame.index("RenderLists_PreInit();") < frame.index("CustomTrackDiag_BeginFrame("),
      "custom-track frame accounting still opens after the LOD setup and before the BSP walk")
check("RenderLevelLod_Init2P(DrawLevelOvr1P_RenderScratch(), g_config.increaseDrawDistance);" in read("game/227/227_00_DrawLevelOvr2P.c"),
      "2P seeds the shared helper thresholds through RenderLevelLod_Init2P")
check("RenderLevelLod_InitSplitGround(DrawLevelOvr1P_RenderScratch(), g_config.increaseDrawDistance);" in read("game/226/226_00_DrawLevelOvr1P.c"),
      "3P and 4P ground thresholds go through RenderLevelLod_InitSplitGround")
check("RenderLevelLod_BspThreshold(" in read("game/RenderLevel/RenderLists.c") and ": 0x1540;" not in read("game/RenderLevel/RenderLists.c"),
      "the BSP walk takes its split-screen cutoff from RenderLevelLod_BspThreshold")
bucket = read("game/RenderBucket/RenderBucket_QueueExecute.c")
check("NativeDrawDistance_GeomScreenHalf(geomScreenHalf, g_config.increaseDrawDistance)" in bucket, "instance far-Z reject uses the shared factor")
check("NativeDrawDistance_ProjectedLodDistance(" in bucket and "* 100) / 400" not in bucket, "model LOD distance uses the shared factor")
check("NativeDrawDistance_FarClip(g_config.increaseDrawDistance)" in read("game/PushBuffer.c"), "frustum far clip uses the shared factor")
# A5-13c: every near-subdivision threshold goes through the retail-keeping helper, every texture threshold through the scaled one.
lod_flat = re.sub(r"\s+", " ", lod)
for field in ("topLevelNearDepthThreshold", "recursiveNearDepthThreshold"):
    assigns = re.findall(r"scratch->" + field + r" = (\w+)\(", lod_flat)
    check(len(assigns) == 4 and all(a == "NativeDrawDistance_SubdivisionThreshold" for a in assigns),
          "all four %s assignments use NativeDrawDistance_SubdivisionThreshold (%s)" % (field, ", ".join(assigns)))
# The renderer reads the two subdivision thresholds by scratch offset: 0x24 top-level, 0x28 recursive.
main_h = read("include/namespace_Main.h")
check("offsetof(struct MainRenderLevelGeometryScratch, topLevelNearDepthThreshold) == 0x24" in main_h and
      "offsetof(struct MainRenderLevelGeometryScratch, recursiveNearDepthThreshold) == 0x28" in main_h,
      "the subdivision thresholds sit at scratch 0x24 and 0x28")
ovr1p = read("game/226/226_00_DrawLevelOvr1P.c")
check("return DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, 0x24);" in ovr1p and
      "return DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, 0x28);" in ovr1p,
      "the top-level and recursive near masks read scratch 0x24 and 0x28")
# Every near-mask read, whatever its vertex and index arguments: the two subdivision thresholds, a forwarded
# offset (checked below at the dispatchers), or 0x14, the full-dynamic low-LOD depth scale, which never scales.
near_calls = re.findall(r"DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset\(\s*\w+\s*,\s*\w+\s*,\s*(\w+)\s*\)", ovr1p)
near_args = set(near_calls)
check(len(near_calls) == ovr1p.count("DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(") - 1,
      "every near-mask call has a parsed offset argument (%d calls)" % len(near_calls))
check(near_args <= {"0x14", "0x24", "0x28", "scratchOffset", "thresholdScratchOffset"}, "near masks only read the subdivision thresholds or the unscaled depth scale (%s)" % ", ".join(sorted(near_args)))
check("offsetof(struct MainRenderLevelGeometryScratch, depthScale) == 0x14" in main_h, "scratch 0x14 is the depth scale")
low_calls = re.findall(r"DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset\(\s*\w+\s*,\s*(\w+)\s*,\s*0x14\s*\)", ovr1p)
check(low_calls == ["sDrawLevelOvr1PFullDynamicLowIndices"], "only the full-dynamic low-LOD near mask reads scratch 0x14 (%s)" % ", ".join(low_calls))
offset_args = set(re.findall(r"(0x[0-9a-fA-F]+), (?:inheritedOtIndex|otIndexOverride)\)", ovr1p)) | set(re.findall(r"directMask, (0x[0-9a-fA-F]+)\)", ovr1p))
check(offset_args <= {"0x24", "0x28"}, "every threshold offset handed to a near-subdivision dispatcher is 0x24 or 0x28 (%s)" % ", ".join(sorted(offset_args)))
for field in ("textureLodDepthThreshold0", "textureLodDepthThreshold1"):
    assigns = re.findall(r"scratch->" + field + r" = (\w+)\(", lod_flat)
    check(len(assigns) == 4 and all(a == "NativeDrawDistance_TextureLodThreshold" for a in assigns),
          "all four %s assignments use NativeDrawDistance_TextureLodThreshold (%s)" % (field, ", ".join(assigns)))
for path in sorted((root / "game").rglob("*.c")):
    for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if re.search(r"increaseDrawDistance\s*\?", line):
            check(False, "no bare increaseDrawDistance ternary outside the helpers: %s:%d" % (path.relative_to(root), n))

# --- Clip records: one function sizes allocation, checkpoint rebind and the writers' end --
callers = []
for sub in ("game", "platform", "ap"):
    for path in sorted((root / sub).rglob("*.c")):
        for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if "MainDB_GetClipSize(" in line and not line.lstrip().startswith("//"):
                callers.append("%s:%d" % (path.relative_to(root), n))
check(all(c.startswith("game/MAIN/MainDB.c") for c in callers), "only MainDB.c reads the retail clip table directly (" + ", ".join(callers) + ")")
check("MainDB_GetClipEntries(gGT->levelID, gGT->numPlyrCurrGame)" in read("game/MAIN/MainInit.c"), "clip slices are allocated with the native entry count")
check("MainDB_GetClipEntries(sdata_static.gameTracker.levelID, clipPlayers)" in read("platform/native_checkpoint.c"), "checkpoint rebinds clip slices with the native entry count")
check("MainDB_GetClipEntries(gGT->levelID, gGT->numPlyrCurrGame) << 2" in read("game/226/226_00_DrawLevelOvr1P.c"), "the writers' clip end uses the native entry count")
clip_h = read("platform/native_clip_scratch.h")
check("#define CTR_NATIVE_CLIP_SCRATCH_SCALE ((uint32_t)NATIVE_DRAW_DISTANCE_SCALE)" in clip_h, "clip scratch scale is the draw-distance factor")

# --- Prim and OT arenas: 1P with the option, split screen always, 8 MiB build, before the 1P floor
init = read("game/MAIN/MainInit.c")
prim = init[init.index("void MainInit_PrimMem("):init.index("void MainInit_JitPoolsReset(")]
ot = init[init.index("void MainInit_OTMem("):init.index("void MainInit_JitPoolsNew(")]
grow_prim = ("size = NativeDrawDistance_ArenaBytes(size, NativeDrawDistance_ArenaGrows(g_config.increaseDrawDistance, gGT->numPlyrCurrGame,", "(gGT->gameMode1 & (MAIN_MENU | GAME_CUTSCENE)) != 0));")
grow_ot = ("size = NativeDrawDistance_ArenaBytes(size, NativeDrawDistance_ArenaGrows(g_config.increaseDrawDistance, gGT->numPlyrCurrGame,", "(gameMode & (MAIN_MENU | GAME_CUTSCENE)) != 0));")
def at(body, pair):
    i = body.find(pair[0])
    return i if i >= 0 and body.find(pair[1], i) >= 0 and body.find(pair[1], i) - i < 300 else -1
check(at(prim, grow_prim) >= 0 and prim.index("#ifdef CTR_CUSTOM_TRACKS") < at(prim, grow_prim) < prim.index("CustomTrackPolicy_PrimArenaBytes("),
      "prim arena scales inside CTR_CUSTOM_TRACKS, by the option, the player count and the menu/cutscene test, before the custom-track floor")
check(at(ot, grow_ot) >= 0 and ot.index("#ifdef CTR_CUSTOM_TRACKS") < at(ot, grow_ot) < ot.index("MainDB_OTMem(&gGT->db[0].otMem, size);"),
      "OT arena scales inside CTR_CUSTOM_TRACKS, by the same rule, before it is allocated")
check(prim.count("NativeDrawDistance_ArenaBytes(") == 1 and ot.count("NativeDrawDistance_ArenaBytes(") == 1,
      "each arena is scaled exactly once per load")
check("static int MainInit_GetPrimMemSize(struct GameTracker *gGT)" in init, "the ASM-verified retail prim size function is still there, unscaled")

# --- Budget arithmetic for the worst grown load (split screen grows even with the option off)
policy = read("include/platform/native_custom_tracks_policy.h")
floor = int(re.search(r"#define CTR_CT_PRIM_ARENA_BYTES (0x[0-9A-Fa-f]+)", policy).group(1), 16)
scale = int(re.search(r"#define NATIVE_DRAW_DISTANCE_SCALE (\d+)", dd).group(1))
retail_prim_max = 255 << 10  # u8 << 10, the per-level table ceiling, larger than every constant branch
retail_ot_max = 0x8000  # battle
check(retail_prim_max * scale <= floor, "scaled 1P retail budget never exceeds the 1P floor, so 1P allocates what it did before")
token_budget = 0x00F00000
worst = 2 * max(floor, retail_prim_max * scale) + 2 * retail_ot_max * scale + 2 * ((4 << 12) | 0x18)
check(worst < token_budget // 2, "worst prim plus OT pair stays under half the GPU link token budget (%d of %d)" % (worst, token_budget))
mempack_expanded = 8 * 1024 * 1024 - 0x800
retail_window = 0x144E10
check(retail_window + worst < mempack_expanded, "retail window plus the scaled arenas fit the 8 MiB MEMPACK")

if failures:
    print("%d failed" % len(failures))
    sys.exit(1)
print("all draw distance checks passed")
