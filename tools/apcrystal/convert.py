#!/usr/bin/env python3
"""Generate the AP stand-in crystal: an ORIGINAL faceted-gem mesh emitted as
ctr-native static model data (u8 vertex blob, shaded grey LUT, RenderBucket
command stream).

Why a generated mesh and not the arena crystal
----------------------------------------------
The battle-arena crystal (STATIC_CRYSTAL, id 0x60) lives in battle-arena LEV
data, and gGT->modelPtr[] is refilled per level, so whether it is resident on
the adventure hub is a property of the level that is loaded -- not something a
display policy may assume. The previous attempt on this branch DID assume it,
the assumption was wrong, and Stef's own progression rewards rendered as
fallback bars on a real hub pad (in-game, 2026-08-12).

Shipping the retail crystal's geometry is not an option either: it is retail
game data, and this repo is public.

So the crystal this project draws is its own: a low-poly faceted gem authored
here, registered into the crystal model slot ONLY when the slot is empty. Where
the real arena crystal is loaded it keeps the slot and nothing changes; where it
is not, the pad still shows a spinning purple crystal instead of falling back to
a marker. Residency stops being a premise and becomes a construction.

Engine facts this encodes (verified in
game/RenderBucket/RenderBucket_QueueExecute.c, same set tools/aplogo/convert.py
documents):

  * vertices are u8 triples read SEQUENTIALLY by ctx->vertexIndex (:2420-2424),
    so the mesh is de-indexed into a flat draw-order list.
  * byte0 -> GTE VX (horizontal, :2238), byte1 -> GTE VZ (depth, :2264),
    byte2 -> GTE VY (vertical, :2238 high half).
  * a strip of N commands emits N-2 triangles (:4423-4460); flags bit 0x80
    restarts the strip (:4402), so one restart per triangle keeps them
    independent.
  * command layout: flags<<24 | stackIndex<<16 | colorIndex<<9 | texIndex,
    where texIndex 0 selects the untextured POLY_G3 path (:2861-2871).
  * (command >> 16) == 0 is reinterpreted as a colour-only command (:4375), so
    stackIndex is never 0.
  * PackModelVertexXY does a single packed 32-bit add, so the low (X) half
    carries into the high (vertical) half (:2240-2244). With the negative frame
    origin every vertex with byte0 >= 128 carries; pre-compensate by subtracting
    1 from its vertical byte.

DOUBLE-SIDED ON PURPOSE. RenderBucket_CheckProjectedPrim runs gte_nclip and
rejects by winding (:2691-2702), so a mesh wound the wrong way renders as
nothing at all -- and nothing at all is indistinguishable, from the build
machine, from the mesh being correct. This build machine has no display and no
disc, so winding cannot be checked here. Emitting each facet in both windings
costs 24 extra triangles and makes the question unaskable. It is the same class
of unverifiable premise that cost this lane its first attempt.

The mesh is untextured, which is a constraint on the RENDER SITE, not just here:
the writers selected by DRAW_TRANSPARENT and USE_SPECULAR_LIGHT both bail out at
tex == 0 and draw nothing (:2924, :3063), so this model must stay on the plain
prim path and take its colour from the colorRGBA lerp, exactly like the #124
marker. AH_WarpPad's STATIC_CRYSTAL arm branches on AP_CrystalModel_IsStandIn
for precisely that reason. The ghost writer is safe: it has an explicit tex == 0
flat-packet path (:3296), so a peer's crystal still ghosts.
"""

import math
import sys

# --- geometry -------------------------------------------------------------
# A hexagonal bipyramid with a girdle: top apex, an upper ring, a lower ring and
# a bottom apex. Six sides read as a cut gem at PSX draw distance while keeping
# the facet count low enough that every face gets its own flat-shaded grey.
SIDES = 6
TOP_APEX = 1.00
UPPER_RING_Y = 0.34
LOWER_RING_Y = -0.30
BOTTOM_APEX = -1.00
UPPER_RING_R = 1.00
LOWER_RING_R = 0.86
# Half a facet of twist between the rings, so the girdle band reads as its own
# row of facets rather than as a straight extrusion of the crown.
TWIST = math.pi / SIDES


def ring(y, r, phase):
    return [
        (r * math.cos(2 * math.pi * i / SIDES + phase), y,
         r * math.sin(2 * math.pi * i / SIDES + phase))
        for i in range(SIDES)
    ]


upper = ring(UPPER_RING_Y, UPPER_RING_R, 0.0)
lower = ring(LOWER_RING_Y, LOWER_RING_R, TWIST)
top = (0.0, TOP_APEX, 0.0)
bottom = (0.0, BOTTOM_APEX, 0.0)

tris = []
for i in range(SIDES):
    j = (i + 1) % SIDES
    # crown: apex to the upper ring
    tris.append((top, upper[i], upper[j]))
    # girdle band: upper ring to the twisted lower ring, two facets per side
    tris.append((upper[i], lower[i], upper[j]))
    tris.append((upper[j], lower[i], lower[j]))
    # pavilion: lower ring down to the bottom apex
    tris.append((lower[i], bottom, lower[j]))

# --- output space ---------------------------------------------------------
# (horizontal, depth, vertical) in a 0..255 cube, one uniform scale so the gem
# keeps its proportions.
allv = [v for t in tris for v in t]
xs = [v[0] for v in allv]
ys = [v[1] for v in allv]
zs = [v[2] for v in allv]
spans = [max(xs) - min(xs), max(zs) - min(zs), max(ys) - min(ys)]
scale = 255.0 / max(spans)
centers = [(max(xs) + min(xs)) / 2.0, (max(zs) + min(zs)) / 2.0,
           (max(ys) + min(ys)) / 2.0]


def to_float(v):
    raw = [v[0], v[2], v[1]]
    return [(r - centers[i]) * scale + 127.5 for i, r in enumerate(raw)]


def to_bytes(v):
    out = [max(0, min(255, int(round(c)))) for c in to_float(v)]
    # Pre-compensate the packed-add carry (QueueExecute.c:2240-2244).
    if out[0] >= 128:
        out[2] = max(0, out[2] - 1)
    return tuple(out)


# --- per-vertex shading ---------------------------------------------------
# Same three-factor recipe the marker uses, for the same reason: the tint is a
# lerp toward one colour, so all the shape a player reads has to be baked into
# the greys. A gem with no facet contrast lerps to a flat purple blob.
#
#   * a face term |normal . depth|, so facets turned toward the camera are
#     brighter than the ones seen edge-on;
#   * a light-from-above vertical gradient;
#   * a per-facet jitter, which is what makes a spinning gem sparkle: adjacent
#     facets must not resolve to the same grey or the silhouette is all there is.
FACE_AMBIENT = 0.28
FACE_TERM = 0.42
TOP_GRADIENT = 0.30
FACET_JITTER = 0.10
GREY_MAX = 220   # 255 washes out under the tint lerp, same as the marker
GREY_STEP = 4

allf = [to_float(v) for v in allv]
v_lo = min(p[2] for p in allf)
v_hi = max(p[2] for p in allf)


def face_term(t):
    a, b, c = (to_float(v) for v in t)
    u = [b[i] - a[i] for i in range(3)]
    w = [c[i] - a[i] for i in range(3)]
    n = [u[1] * w[2] - u[2] * w[1],
         u[2] * w[0] - u[0] * w[2],
         u[0] * w[1] - u[1] * w[0]]
    mag = sum(x * x for x in n) ** 0.5
    if mag == 0.0:
        return 1.0
    return abs(n[1]) / mag


def vertex_grey(v, face, facet):
    vnorm = (to_float(v)[2] - v_lo) / (v_hi - v_lo)
    s = (FACE_AMBIENT + FACE_TERM * face + TOP_GRADIENT * vnorm
         + FACET_JITTER * math.sin(facet * 2.399))
    s = max(0.0, min(1.0, s))
    q = int(round(GREY_MAX * s / GREY_STEP)) * GREY_STEP
    return max(0, min(255, q))


# --- de-index into draw order --------------------------------------------
verts = []
cmds = []
greys = []
grey_index = {}

for facet, t in enumerate(tris):
    face = face_term(t)
    # Both windings: nclip rejects one of them and this machine cannot tell
    # which. See the module docstring.
    for wound in (t, (t[0], t[2], t[1])):
        for j, v in enumerate(wound):
            verts.append(to_bytes(v))
            g = vertex_grey(v, face, facet)
            if g not in grey_index:
                grey_index[g] = len(greys)
                greys.append(g)
            gi = grey_index[g]
            flags = 0x80 if j == 0 else 0x00
            stack = j + 1  # never 0: (command >> 16) == 0 is colour-only
            cmds.append((flags << 24) | (stack << 16) | (gi << 9) | 0)

# RenderBucket_GetCommandColor reads (command >> 7) & 0x1fc as a BYTE offset
# (QueueExecute.c:2318-2328), so 127 is the hard ceiling on the colour index.
assert len(greys) <= 127, f"colour LUT overflowed the 7-bit index field: {len(greys)}"

# PSX colour word is 0x00BBGGRR; every entry is grey so the purple tint
# modulates a neutral base.
color_words = [(g << 16) | (g << 8) | g for g in greys]

NUM_TRIS = len(tris) * 2


def emit(path):
    o = []
    w = o.append
    w("// GENERATED by tools/apcrystal/convert.py -- do not edit by hand.")
    w("//")
    w("// An ORIGINAL faceted-gem mesh authored by this project. It is NOT the")
    w("// retail battle-arena crystal and carries none of its data: the retail")
    w("// model lives in arena LEV data, which this public repo cannot ship, and")
    w("// whose residency on the adventure hub cannot be assumed -- assuming it")
    w("// is exactly what failed Stef's in-game gate on 2026-08-12.")
    w("//")
    w("// This mesh stands in ONLY where the real crystal is not loaded, so a CTR")
    w("// progression reward reads as a spinning purple crystal on every surface")
    w("// rather than as a marker on some of them. See ap/ap_crystal_model.c.")
    w("//")
    w("// Untextured, like the #124 marker: its greys are lerped toward the")
    w("// crystal purple by colorRGBA + alphaScale on the plain prim path. The")
    w("// textured writers (DRAW_TRANSPARENT, USE_SPECULAR_LIGHT) bail at")
    w("// tex == 0 and would draw nothing at all.")
    w("//")
    w("// Every facet is emitted in BOTH windings: gte_nclip rejects by winding")
    w("// (QueueExecute.c:2691-2702) and the build machine has no display, so a")
    w("// wrong guess would be invisible and indistinguishable from success.")
    w("")
    w(f"#define AP_CRYSTAL_NUM_VERTS {len(verts)}")
    w(f"#define AP_CRYSTAL_NUM_TRIS  {NUM_TRIS}")
    w(f"#define AP_CRYSTAL_NUM_COLORS {len(color_words)}")
    w("")
    w("// Deduplicated per-vertex shaded greys, 0x00BBGGRR.")
    w("static const u32 s_apCrystalColors[AP_CRYSTAL_NUM_COLORS] = {")
    w("    " + ", ".join(f"0x{c:08x}" for c in color_words) + ",")
    w("};")
    w("")
    w("// Draw-order vertex bytes: byte0 = horizontal, byte1 = depth, byte2 = vertical.")
    w("static const u8 s_apCrystalVerts[AP_CRYSTAL_NUM_VERTS * 3] = {")
    for i in range(0, len(verts), 8):
        chunk = verts[i:i + 8]
        w("    " + " ".join(f"{v[0]:3d},{v[1]:3d},{v[2]:3d}," for v in chunk))
    w("};")
    w("")
    w("// [0] = colour count (copied to the scratchpad colour cache), then one")
    w("// command per vertex, then the 0xffffffff terminator.")
    w("static const u32 s_apCrystalCommands[] = {")
    w("    AP_CRYSTAL_NUM_COLORS,")
    for i in range(0, len(cmds), 6):
        chunk = cmds[i:i + 6]
        w("    " + " ".join(f"0x{c:08x}," for c in chunk))
    w("    0xffffffffu,")
    w("};")
    w("")
    open(path, "w").write("\n".join(o))


emit(sys.argv[1] if len(sys.argv) > 1 else "ap_crystal_model_data.h")
print(f"emitted {len(verts)} verts, {NUM_TRIS} tris, {len(cmds)} commands, "
      f"greys={greys}", file=sys.stderr)
