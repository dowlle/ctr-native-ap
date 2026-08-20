#!/usr/bin/env python3
"""Validate the generated AP box atlas against what the engine expects of it.

    python validate.py ../../ap/ap_box_texture_data.h

Checks the shape the renderer and ap/ap_box_texture.c depend on, and that the
baked pixels are still the source art. It cannot tell you the box looks right in
a race; it tells you the data is well-formed and faithful, which is the failure
mode that otherwise only shows up in game.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from convert import ATLAS_H, ATLAS_W, RECTS, read_png, resample_nearest  # noqa: E402

HERE = Path(__file__).resolve().parent
header = Path(sys.argv[1] if len(sys.argv) > 1 else HERE / ".." / ".." / "ap" / "ap_box_texture_data.h")
src = header.read_text(encoding="utf-8")


def macro(name):
    match = re.search(rf"#define {name} (\d+)\b", src)
    assert match, f"missing #define {name}"
    return int(match.group(1))


body = src[src.index("s_apBoxTextureAtlas"):]
body = body[body.index("{") + 1:body.index("};")]
atlas = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body)]

declared_w = macro("AP_BOX_TEXTURE_ATLAS_W")
declared_h = macro("AP_BOX_TEXTURE_ATLAS_H")

checks = {}
checks["declared atlas width matches the generator"] = declared_w == ATLAS_W
checks["declared atlas height matches the generator"] = declared_h == ATLAS_H
checks["array length is width * height * 4"] = len(atlas) == declared_w * declared_h * 4
checks["atlas is not blank"] = any(atlas)

# Rects: declared where the generator put them, inside the atlas, disjoint, and
# each one carrying actual pixels rather than a hole in the middle of the art.
covered = set()
for name, png, ax, ay, w, h in RECTS:
    checks[f"{name} rect origin/size are declared"] = (
        macro(f"AP_BOX_TEXTURE_{name}_X") == ax
        and macro(f"AP_BOX_TEXTURE_{name}_Y") == ay
        and macro(f"AP_BOX_TEXTURE_{name}_W") == w
        and macro(f"AP_BOX_TEXTURE_{name}_H") == h
    )
    checks[f"{name} rect fits inside the atlas"] = ax >= 0 and ay >= 0 and ax + w <= ATLAS_W and ay + h <= ATLAS_H

    cells = {(ax + x, ay + y) for y in range(h) for x in range(w)}
    checks[f"{name} rect does not overlap another rect"] = not (cells & covered)
    covered |= cells

    opaque = sum(1 for (x, y) in cells if atlas[((y * ATLAS_W) + x) * 4 + 3] != 0)
    checks[f"{name} rect has non-zero pixel data"] = opaque > 0

    sw, sh, pixels = read_png(HERE / png)
    if (sw, sh) != (w, h):
        pixels = resample_nearest(pixels, sw, sh, w, h)
    matches = all(
        bytes(atlas[(((ay + y) * ATLAS_W) + ax + x) * 4:(((ay + y) * ATLAS_W) + ax + x) * 4 + 4])
        == pixels[((y * w) + x) * 4:((y * w) + x) * 4 + 4]
        for y in range(h)
        for x in range(w)
    )
    checks[f"{name} rect still matches {png}"] = matches

# The face rect is the one the model samples; its layout corners must land on
# real pixels, so it may not sit flush against the atlas edge in a way that would
# make u/v exceed the 8-bit UV field the layout stores them in.
face = next(r for r in RECTS if r[0] == "FACE")
checks["face rect UV corners fit the layout's 8-bit fields"] = (
    face[2] + face[4] - 1 <= 255 and face[3] + face[5] - 1 <= 255
)

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"{'ok  ' if ok else 'FAIL'}  {name}")
opaque_total = sum(1 for i in range(3, len(atlas), 4) if atlas[i] != 0)
print(f"\n{declared_w}x{declared_h} atlas, {len(atlas)} bytes, {opaque_total} non-transparent texels")
if failed:
    sys.exit(f"\n{len(failed)} check(s) failed")
print("all checks passed")
