#!/usr/bin/env python3
"""Validate the AP-owned crate model against the renderer's static-model contract."""

import re
import sys
from pathlib import Path


src = Path(sys.argv[1]).read_text(encoding="utf-8")
model_src = Path(sys.argv[2]).read_text(encoding="utf-8")


def macro(name: str) -> int:
    match = re.search(rf"#define {name} (\d+)", src)
    assert match, f"missing {name}"
    return int(match.group(1))


def array_body(name: str) -> str:
    start = src.index(name)
    body = src[start:]
    return body[body.index("{") + 1:body.index("};")]


num_verts = macro("AP_BOX_MODEL_NUM_VERTS")
num_tris = macro("AP_BOX_MODEL_NUM_TRIS")
num_colors = macro("AP_BOX_MODEL_NUM_COLORS")

verts_body = re.sub(r"//[^\n]*", "", array_body("s_apBoxModelVerts"))
vertex_bytes = [int(value) for value in re.findall(r"\b\d+\b", verts_body)]
colors = re.findall(r"0x[0-9a-fA-F]{8}", array_body("s_apBoxModelColors"))
tri_colors = [int(value) for value in re.findall(
    r"AP_BOX_TRI\((\d+)\)", array_body("s_apBoxModelCommands"))]

checks = {
    "vertex byte count": len(vertex_bytes) == num_verts * 3,
    "triangle vertex count": num_verts == num_tris * 3,
    "vertex byte range": all(0 <= value <= 255 for value in vertex_bytes),
    "colour count": len(colors) == num_colors,
    "one strip restart per triangle": len(tri_colors) == num_tris,
    "command colour range": all(0 <= value < num_colors for value in tri_colors),
    "two triangles per cube face": all(tri_colors.count(i) == 2
                                         for i in range(num_colors)),
    "single final terminator": array_body("s_apBoxModelCommands").count(
        "0xffffffffu") == 1,
    "fallback occupies only the missing crate slot":
        "gGT->modelPtr[PU_RANDOM_CRATE] = &s_apBoxModel" in model_src,
    "model identity matches the occupied slot":
        "s_apBoxModel.id = PU_RANDOM_CRATE" in model_src,
    "retail crate wins before fallback registration": model_src.index(
        "if (gGT->modelPtr[PU_RANDOM_CRATE] != 0)") < model_src.index(
            "gGT->modelPtr[PU_RANDOM_CRATE] = &s_apBoxModel"),
}

for label, ok in checks.items():
    print(f"{'ok' if ok else 'FAIL'}: {label}")

if not all(checks.values()):
    raise SystemExit(1)
print("AP box fallback model is structurally valid")
