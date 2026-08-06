#!/usr/bin/env python3
"""Convert MMRecompRando's N64 F3DEX AP-logo mesh (GPL-3.0) into ctr-native
static model data: a u8 vertex blob, a greyscale colour LUT and a
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


def to_bytes(v):
    n64x, n64y, n64z = v
    raw = [n64z, n64x, n64y]  # horizontal, depth, vertical
    out = []
    for i, r in enumerate(raw):
        b = int(round((r - centers[i]) * scale + 127.5))
        out.append(max(0, min(255, b)))
    # Pre-compensate the packed-add carry: byte0 >= 128 adds 1 to the vertical
    # half once the negative frame origin is applied (QueueExecute.c:2240-2244).
    if out[0] >= 128:
        out[2] = max(0, out[2] - 1)
    return tuple(out)


# --- de-index into draw order --------------------------------------------
verts = []
cmds = []
for v0, v1, v2, ci in tris:
    for j, v in enumerate((v0, v1, v2)):
        verts.append(to_bytes(v))
        flags = 0x80 if j == 0 else 0x00
        stack = j + 1  # never 0: (command >> 16) == 0 is a colour-only command
        cmds.append((flags << 24) | (stack << 16) | (ci << 9) | 0)

# --- greyscale colour LUT -------------------------------------------------
# The spec requires a near-neutral base so the class tint modulates cleanly.
# Take luminance, then stretch the (naturally narrow) spread for legibility.
lum = [0.299 * r + 0.587 * g + 0.114 * b for r, g, b in prim_colors]
lo, hi = min(lum), max(lum)
LUT_LO, LUT_HI = 96, 255
greys = [int(round((l - lo) / (hi - lo) * (LUT_HI - LUT_LO) + LUT_LO)) for l in lum]
# PSX colour word is 0x00BBGGRR.
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
    w("// Their mesh is an extruded 6-region silhouette drawn with flat prim")
    w("// colours and no texture (its combiner is SHADE x PRIMITIVE, no TEXEL),")
    w("// which is exactly what ctr-native's untextured POLY_G3 path wants. The")
    w("// region colours are stored here as LUMINANCE so the per-class tint")
    w("// modulates a neutral base, per the ruled spec.")
    w("")
    w(f"#define AP_MARKER_NUM_VERTS {len(verts)}")
    w(f"#define AP_MARKER_NUM_TRIS  {len(tris)}")
    w(f"#define AP_MARKER_NUM_COLORS {len(color_words)}")
    w("")
    w("// Neutral (luminance) versions of the logo's six region colours, 0x00BBGGRR.")
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
