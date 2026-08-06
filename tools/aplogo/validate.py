#!/usr/bin/env python3
"""Replay RenderBucket_DrawFunc_Normal's command-stream semantics over the
generated marker data and check the invariants the engine relies on.

This does not prove the model looks right on screen. It proves the data is
well-formed against the decoder as written in
game/RenderBucket/RenderBucket_QueueExecute.c, which is the class of mistake
that is otherwise invisible until the marker silently fails to draw in game.
"""

import re
import sys

src = open(sys.argv[1]).read()


def arr(name, end="};"):
    body = src[src.index(name):]
    body = body[body.index("{") + 1:body.index(end)]
    return body


verts_raw = [int(x) for x in re.findall(r"\d+", arr("s_apMarkerVerts[AP_MARKER_NUM_VERTS * 3] = {"))]
verts = [tuple(verts_raw[i:i + 3]) for i in range(0, len(verts_raw), 3)]
colors = [int(x, 16) for x in re.findall(r"0x([0-9a-f]{8})", arr("s_apMarkerColors[AP_MARKER_NUM_COLORS] = {"))]
cmd_body = arr("s_apMarkerCommands[] = {")
# commands[0] is emitted as the AP_MARKER_NUM_COLORS macro, not a hex literal.
cmds = [int(re.search(r"#define AP_MARKER_NUM_COLORS (\d+)", src).group(1))]
cmds += [int(x, 16) for x in re.findall(r"0x([0-9a-f]{8})u?", cmd_body)]

n_colors = int(re.search(r"#define AP_MARKER_NUM_COLORS (\d+)", src).group(1))
n_verts = int(re.search(r"#define AP_MARKER_NUM_VERTS (\d+)", src).group(1))
n_tris = int(re.search(r"#define AP_MARKER_NUM_TRIS  (\d+)", src).group(1))

fails = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


print("data shape")
check(len(verts) == n_verts, f"vertex count {len(verts)} == AP_MARKER_NUM_VERTS {n_verts}")
check(len(colors) == n_colors, f"colour count {len(colors)} == AP_MARKER_NUM_COLORS {n_colors}")
check(all(0 <= c <= 255 for v in verts for c in v), "all vertex bytes in 0..255")

# commands[0] is the colour count copied into the 1 KiB scratchpad colour cache
# at offset 0x140 (QueueExecute.c:2302-2316). Overrunning it corrupts scratchpad.
print("\nscratchpad safety")
check(cmds[0] == n_colors, f"commands[0] ({cmds[0]}) is the colour count")
check(cmds[0] <= (0x400 - 0x140) // 4, f"colour count {cmds[0]} fits the scratchpad colour cache")
# GetCommandColor reads (command >> 7) & 0x1fc as a BYTE offset (:2318-2328),
# so the colour index is a 7-bit field however much scratchpad is free.
check(n_colors <= 127, f"colour count {n_colors} fits the 7-bit colour-index field")

stream = cmds[1:]
print("\nterminator")
check(stream[-1] == 0xFFFFFFFF, "stream ends with the 0xffffffff terminator")
check(0xFFFFFFFF not in stream[:-1], "no early terminator")
stream = stream[:-1]

# --- replay the decoder ---------------------------------------------------
print("\ncommand decode")
bad_color, bad_tex, color_only, reuse_cached = [], [], [], []
max_stack = 0
for i, c in enumerate(stream):
    flags = (c >> 24) & 0xFF
    stack = (c >> 16) & 0xFF
    color_idx = (((c >> 7) & 0x1FC)) // 4
    tex = c & 0x1FF
    if (c >> 16) == 0:
        color_only.append(i)          # would be eaten as a colour-only command
    if color_idx >= n_colors:
        bad_color.append((i, color_idx))
    if tex != 0:
        bad_tex.append((i, tex))
    if flags & 0x04:
        reuse_cached.append(i)
    max_stack = max(max_stack, stack)

check(not color_only, "no command is misread as colour-only ((cmd >> 16) == 0)")
check(not bad_color, f"every colour index < {n_colors}" + (f" (bad: {bad_color[:3]})" if bad_color else ""))
check(not bad_tex, "every texture index is 0 (selects the untextured POLY_G3 path)")
# stackIndex indexes an 8-byte-per-slot scratch region; keep it well inside 1 KiB.
check(max_stack < 0x58, f"max stackIndex {max_stack} stays inside the packed-vertex scratch")

# vertexIndex advances on every command without flag bit 0x04.
consumed = len(stream) - len(reuse_cached)
print("\nvertex consumption")
check(consumed == n_verts, f"stream consumes {consumed} vertices == {n_verts} supplied")

# --- strip machine (QueueExecute.c:4402-4460) ------------------------------
print("\nstrip machine")
strip_len = 0
tris = 0
for c in stream:
    flags = (c >> 24) & 0xFF
    if flags & 0x80:
        strip_len = 0
    if strip_len < 2:
        strip_len += 1
        continue
    tris += 1
    strip_len += 1
check(tris == n_tris, f"decoder emits {tris} triangles == AP_MARKER_NUM_TRIS {n_tris}")

# --- geometry sanity ------------------------------------------------------
print("\ngeometry")
h = [v[0] for v in verts]
d = [v[1] for v in verts]
z = [v[2] for v in verts]
check(max(h) - min(h) > 200, f"horizontal span {max(h) - min(h)} fills the cube")
check(max(z) - min(z) > 200, f"vertical span {max(z) - min(z)} fills the cube")
check(max(d) - min(d) < 60, f"depth span {max(d) - min(d)} is a thin plate")
degenerate = sum(
    1 for i in range(0, len(verts), 3)
    if len({verts[i], verts[i + 1], verts[i + 2]}) < 3
)
check(degenerate == 0, f"no degenerate triangles (found {degenerate})")

# The packed add carries the low (X) half into the vertical half for byte0 >= 128
# (QueueExecute.c:2240-2244); convert.py pre-compensates. Spot-check that the
# compensation left no vertical byte below 0.
check(all(v[2] >= 0 for v in verts), "carry pre-compensation kept vertical bytes in range")

print("\ncolour LUT is neutral (0x00BBGGRR)")
neutral = all(((c) & 0xFF) == ((c >> 8) & 0xFF) == ((c >> 16) & 0xFF) for c in colors)
check(neutral, "every LUT entry is grey, so the class tint modulates cleanly")

# The flat look fixed on 2026-08-06 was a LUT with almost no usable spread whose
# top entry was 0xff (which is what read as a white sliver edge-on). Guard both.
print("\nshading has usable range")
levels = sorted((c & 0xFF) for c in colors)
check(len(colors) > 6, f"LUT carries per-vertex shading, not one entry per region ({len(colors)})")
check(levels[-1] - levels[0] >= 120, f"grey spread {levels[-1] - levels[0]} is wide enough to read as shading")
check(levels[-1] <= 232, f"brightest grey {levels[-1]} stays off the washed-out top end")
check(levels[0] <= 48, f"darkest grey {levels[0]} is dark enough to read as shadow")

print()
if fails:
    print(f"{len(fails)} FAILURE(S)")
    sys.exit(1)
print("all checks passed")
