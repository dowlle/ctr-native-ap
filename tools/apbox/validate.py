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
vertices = [tuple(vertex_bytes[i:i + 3])
            for i in range(0, len(vertex_bytes), 3)]
triangles = [vertices[i:i + 3] for i in range(0, len(vertices), 3)]
colors = re.findall(r"0x[0-9a-fA-F]{8}", array_body("s_apBoxModelColors"))
tri_colors = [int(value) for value in re.findall(
    r"AP_BOX_TRI\((\d+)\)", array_body("s_apBoxModelCommands"))]


def nondegenerate(triangle) -> bool:
    a, b, c = triangle
    ab = tuple(b[i] - a[i] for i in range(3))
    ac = tuple(c[i] - a[i] for i in range(3))
    cross = (
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    )
    return cross != (0, 0, 0)

checks = {
    "vertex byte count": len(vertex_bytes) == num_verts * 3,
    "triangle vertex count": num_verts == num_tris * 3,
    "vertex byte range": all(0 <= value <= 255 for value in vertex_bytes),
    "all triangles are nondegenerate": all(map(nondegenerate, triangles)),
    "geometry spans all three axes": all(
        max(vertex[axis] for vertex in vertices)
        - min(vertex[axis] for vertex in vertices) >= 190
        for axis in range(3)),
    "colour count": len(colors) == num_colors,
    "colour cache fits renderer scratch": num_colors <= (0x400 - 0x140) // 4,
    "one strip restart per triangle": len(tri_colors) == num_tris,
    "command colour range": all(0 <= value < num_colors for value in tri_colors),
    "two triangles per cube face": all(tri_colors.count(i) == 2
                                         for i in range(num_colors)),
    "single final terminator": array_body("s_apBoxModelCommands").count(
        "0xffffffffu") == 1,
    "triangle macro emits restart and stack slots 1, 2, 3": all(
        token in src for token in
        ("0x80010000u", "0x00020000u", "0x00030000u")),
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
