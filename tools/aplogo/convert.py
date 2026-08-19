#!/usr/bin/env python3
"""Convert MMRecompRando's N64 F3DEX AP-logo mesh (GPL-3.0) into ctr-native
static model data: a u8 vertex blob, a shaded greyscale colour LUT and a
RenderBucket command stream.

Engine facts this encodes (all verified in
game/RenderBucket/RenderBucket_QueueExecute.c on native main 75b8436c6):

  * vertices are u8 triples read SEQUENTIALLY by ctx->vertexIndex
    (:2420-2424), so the mesh must be de-indexed into a flat draw-order list.
  * byte0 -> GTE VX (horizontal, :2238), byte1 -> GTE VZ (depth, :2264),
    byte2 -> GTE VY (vertical, :2238 high half).
  * a strip of N commands emits N-2 triangles (:4423-4460); flags bit 0x80
    restarts the strip (:4402).
  * command layout: flags<<24 | stackIndex<<16 | colorIndex<<9 | texIndex,
    where texIndex 0 selects the untextured POLY_G3 path (:2861-2871).
  * (command >> 16) == 0 is reinterpreted as a colour-only command (:4375),
    so stackIndex must never be 0 while flags are 0.
  * PackModelVertexXY does a single packed 32-bit add, so the low (X) half
    carries into the high (vertical) half (:2240-2244). With a negative
    frame origin every vertex with byte0 >= 128 carries; we pre-compensate
    by subtracting 1 from its vertical byte.
"""

import re
import sys

SRC = "aplogo.h"

# --- parse the Vtx arrays -------------------------------------------------
# {{ {x, y, z}, flag, {u, v}, {r, g, b, a} }}
vtx_arrays = {}
text = open(SRC).read()

for m in re.finditer(r"Vtx (\w+)\[(\d+)\] = \{(.*?)\n\};", text, re.S):
    name, count, body = m.group(1), int(m.group(2)), m.group(3)
    verts = []
    for vm in re.finditer(
        r"\{\{\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", body
    ):
        verts.append(tuple(int(g) for g in vm.groups()))
    assert len(verts) == count, f"{name}: parsed {len(verts)} of {count}"
    vtx_arrays[name] = verts

# --- walk the display list ------------------------------------------------
gfx = text[text.index("Gfx archilogo_archilogo_mesh[]"):]

prim_colors = []          # the 6 flat AP-logo region colours, in encounter order
tris = []                 # (v0, v1, v2, colorIndex) with v* as N64 (x, y, z)
slots = {}                # F3DEX vertex-buffer slot -> N64 vertex
cur_color = None

# The mesh opens with an 8-vertex cull volume + gsSPCullDisplayList; that is a
# bounding box for the N64's display-list cull, not geometry. Skip it.
for line in gfx.splitlines():
    line = line.strip()

    m = re.match(r"gsDPSetPrimColor\(\s*\d+,\s*\d+,\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+\)", line)
    if m:
        rgb = tuple(int(g) for g in m.groups())
        if rgb not in prim_colors:
            prim_colors.append(rgb)
        cur_color = prim_colors.index(rgb)
        continue

    m = re.match(r"gsSPVertex\((\w+)\s*\+\s*(\d+),\s*(\d+),\s*(\d+)\)", line)
    if m:
        arr, off, n, start = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        if arr.endswith("_cull"):
            continue
        for i in range(n):
            slots[start + i] = vtx_arrays[arr][off + i]
        continue

    m = re.match(r"gsSP2Triangles\(\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+,\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+\)", line)
    if m:
        a, b, c, d, e, f = (int(g) for g in m.groups())
        assert cur_color is not None, "triangle before any gsDPSetPrimColor"
        tris.append((slots[a], slots[b], slots[c], cur_color))
        tris.append((slots[d], slots[e], slots[f], cur_color))
        continue

    m = re.match(r"gsSP1Triangle\(\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+\)", line)
    if m:
        a, b, c = (int(g) for g in m.groups())
        tris.append((slots[a], slots[b], slots[c], cur_color))
        continue

print(f"parsed {len(tris)} triangles, {len(prim_colors)} region colours", file=sys.stderr)

# --- coordinate mapping ---------------------------------------------------
# N64 space is Y-up with X as the plate thickness. ctr-native model space is
# byte0 = horizontal, byte1 = depth, byte2 = vertical, so:
#   byte0 <- N64 z,  byte1 <- N64 x (thickness),  byte2 <- N64 y (up)
allv = [v for t in tris for v in t[:3]]
n64_x = [v[0] for v in allv]
n64_y = [v[1] for v in allv]
n64_z = [v[2] for v in allv]

# One uniform scale across all three axes so the logo keeps its proportions.
spans = [max(n64_z) - min(n64_z), max(n64_x) - min(n64_x), max(n64_y) - min(n64_y)]
scale = 255.0 / max(spans)
centers = [
    (max(n64_z) + min(n64_z)) / 2.0,
    (max(n64_x) + min(n64_x)) / 2.0,
    (max(n64_y) + min(n64_y)) / 2.0,
]


def to_float(v):
    """Output-space (horizontal, depth, vertical) coords, unrounded."""
    n64x, n64y, n64z = v
    raw = [n64z, n64x, n64y]
    return [(r - centers[i]) * scale + 127.5 for i, r in enumerate(raw)]


def to_bytes(v):
    out = [max(0, min(255, int(round(c)))) for c in to_float(v)]
    # Pre-compensate the packed-add carry: byte0 >= 128 adds 1 to the vertical
    # half once the negative frame origin is applied (QueueExecute.c:2240-2244).
    if out[0] >= 128:
        out[2] = max(0, out[2] - 1)
    return tuple(out)


# --- per-vertex shading ---------------------------------------------------
# The marker read FLAT in its first in-game look (2026-08-06 rc3 playtest): six
# unshaded regions lerped most of the way to a single class colour leave almost
# no variation, and seen edge-on the plate collapsed to a bright near-white
# sliver (its brightest region colour was 0xff).
#
# This is fixed in DATA, not in the renderer. The untextured path emits POLY_G3
# with three INDEPENDENT per-vertex colours taken from tempColor[1..3]
# (QueueExecute.c:2861-2871), and every vertex command carries its own colour
# index (:4397-4400), so a Gouraud gradient is already supported and costs
# nothing at runtime.
#
# Three factors combine into one grey per vertex:
#   * the region's own luminance, so the logo's internal regions stay distinct;
#   * a face term |normal . depth|, which darkens the extruded RIM relative to
#     the flat faces -- this is what stops the edge-on view reading as a white
#     sliver, since edge-on shows only rim;
#   * a vertical term, a plain light-from-above gradient.
# Vertices are already de-indexed per triangle, so a geometric per-triangle
# normal can be baked straight in.
RIM_AMBIENT = 0.15   # light on a surface turned fully edge-on
RIM_FACE = 0.55      # extra light on the flat faces
TOP_GRADIENT = 0.30  # extra light toward the top of the logo
REGION_FLOOR = 0.45  # how far a dark region may pull a vertex down
GREY_MAX = 220       # brightest LUT entry (255 was the washout)
GREY_STEP = 4        # quantisation; keeps the LUT far inside the 127-index field

lum = [0.299 * r + 0.587 * g + 0.114 * b for r, g, b in prim_colors]
lo, hi = min(lum), max(lum)
region_w = [REGION_FLOOR + (1 - REGION_FLOOR) * (l - lo) / (hi - lo) for l in lum]

allf = [to_float(v) for v in allv]
v_lo = min(p[2] for p in allf)
v_hi = max(p[2] for p in allf)


def tri_face_term(v0, v1, v2):
    """|normal . depth axis|: 1.0 on the logo's flat faces, 0.0 on the rim."""
    a, b, c = to_float(v0), to_float(v1), to_float(v2)
    u = [b[i] - a[i] for i in range(3)]
    w = [c[i] - a[i] for i in range(3)]
    n = [
        u[1] * w[2] - u[2] * w[1],
        u[2] * w[0] - u[0] * w[2],
        u[0] * w[1] - u[1] * w[0],
    ]
    mag = sum(x * x for x in n) ** 0.5
    if mag == 0.0:
        return 1.0
    return abs(n[1]) / mag


flat_tris = [t for t in tris if tri_face_term(t[0], t[1], t[2]) > 0.99]
print(f"triangle shape: keeping {len(flat_tris)} flat faces, dropping "
      f"{len(tris) - len(flat_tris)} extrusion-rim faces", file=sys.stderr)

# A reward pad can show three AP markers at once. The original fully extruded
# source costs 216 POLY_G3 packets per marker: 6,048 primitive-memory bytes, or
# 18,144 bytes for one pad before nearby hub geometry has finished rendering.
# Live testing showed the same black/flashing floor failure here that many
# author-mode marker instances produced earlier. Keep the source mesh's two
# flat faces (front + back, so the spinning logo remains two-sided) and drop its
# 120 thin extrusion-rim triangles. The silhouette and all six logo regions are
# unchanged, while a three-marker pad falls from 648 to 288 triangles.
tris = flat_tris


def vertex_grey(v, ci, face):
    vnorm = (to_float(v)[2] - v_lo) / (v_hi - v_lo)
    s = region_w[ci] * (RIM_AMBIENT + RIM_FACE * face + TOP_GRADIENT * vnorm)
    s = max(0.0, min(1.0, s))
    q = int(round(GREY_MAX * s / GREY_STEP)) * GREY_STEP
    return max(0, min(255, q))


# --- de-index into draw order --------------------------------------------
# The colour LUT is now built from the shaded greys actually used, deduplicated,
# rather than being the six region colours.
verts = []
cmds = []
greys = []          # LUT, in first-use order
grey_index = {}
for v0, v1, v2, ci in tris:
    face = tri_face_term(v0, v1, v2)
    for j, v in enumerate((v0, v1, v2)):
        verts.append(to_bytes(v))
        g = vertex_grey(v, ci, face)
        if g not in grey_index:
            grey_index[g] = len(greys)
            greys.append(g)
        gi = grey_index[g]
        flags = 0x80 if j == 0 else 0x00
        stack = j + 1  # never 0: (command >> 16) == 0 is a colour-only command
        cmds.append((flags << 24) | (stack << 16) | (gi << 9) | 0)

# The colour index is a 7-bit field: RenderBucket_GetCommandColor reads
# (command >> 7) & 0x1fc as a BYTE offset (QueueExecute.c:2318-2328), so index
# 127 is the hard ceiling. The scratchpad colour cache is the looser limit at
# (0x400 - 0x140) / 4 = 176 entries (:2302-2316).
assert len(greys) <= 127, f"colour LUT overflowed the 7-bit index field: {len(greys)}"

# PSX colour word is 0x00BBGGRR; every entry stays grey so the class tint
# modulates a neutral base, per the ruled spec.
color_words = [(g << 16) | (g << 8) | g for g in greys]


def emit(path):
    o = []
    w = o.append
    w("// GENERATED by tools/aplogo/convert.py -- do not edit by hand.")
    w("//")
    w("// Geometry converted from MMRecompRando's Archipelago-logo mesh")
    w("// (github.com/RecompRando/MMRecompRando, GPL-3.0), which is itself a")
    w("// depiction of the Archipelago logo (c) 2022 Krista Corkos and")
    w("// Christopher Wilson, CC BY-NC 4.0. Both credits are permanent and owed")
    w("// in THIRD_PARTY_NOTICES before this ships.")
    w("//")
    w("// Their mesh is a 6-region silhouette drawn with flat prim")
    w("// colours and no texture (its combiner is SHADE x PRIMITIVE, no TEXEL),")
    w("// which is exactly what ctr-native's untextured POLY_G3 path wants. The")
    w("// colours are stored here as LUMINANCE so the per-class tint modulates a")
    w("// neutral base, per the ruled spec.")
    w("//")
    w("// The source mesh's front and back faces are kept, while its costly thin")
    w("// extrusion rim is omitted to protect the hub's primitive-memory budget")
    w("// when three reward markers render together. The greys remain shaded per")
    w("// vertex (region luminance + a light-from-above gradient).")
    w("")
    w(f"#define AP_MARKER_NUM_VERTS {len(verts)}")
    w(f"#define AP_MARKER_NUM_TRIS  {len(tris)}")
    w(f"#define AP_MARKER_NUM_COLORS {len(color_words)}")
    w("")
    w("// Deduplicated per-vertex shaded greys, 0x00BBGGRR.")
    w("static const u32 s_apMarkerColors[AP_MARKER_NUM_COLORS] = {")
    w("    " + ", ".join(f"0x{c:08x}" for c in color_words) + ",")
    w("};")
    w("")
    w("// Draw-order vertex bytes: byte0 = horizontal, byte1 = depth, byte2 = vertical.")
    w("static const u8 s_apMarkerVerts[AP_MARKER_NUM_VERTS * 3] = {")
    for i in range(0, len(verts), 8):
        chunk = verts[i:i + 8]
        w("    " + " ".join(f"{v[0]:3d},{v[1]:3d},{v[2]:3d}," for v in chunk))
    w("};")
    w("")
    w("// [0] = colour count (copied to the scratchpad colour cache), then one")
    w("// command per vertex, then the 0xffffffff terminator.")
    w("static const u32 s_apMarkerCommands[] = {")
    w(f"    AP_MARKER_NUM_COLORS,")
    for i in range(0, len(cmds), 6):
        chunk = cmds[i:i + 6]
        w("    " + " ".join(f"0x{c:08x}," for c in chunk))
    w("    0xffffffffu,")
    w("};")
    w("")
    open(path, "w").write("\n".join(o))


emit(sys.argv[1] if len(sys.argv) > 1 else "ap_marker_model_data.h")
print(
    f"emitted {len(verts)} verts, {len(cmds)} commands, "
    f"greys={greys}",
    file=sys.stderr,
)
