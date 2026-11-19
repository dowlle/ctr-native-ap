#!/usr/bin/env python3
"""Propose an eight-kart grid and four weapon crate rows for a three-lap LEV."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def ground_height(x, z, triangle):
    a, b, c = triangle
    determinant = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
    if not determinant:
        return None
    u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / determinant
    v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / determinant
    if min(u, v, 1 - u - v) < -1e-8:
        return None
    return round(u * a[1] + v * b[1] + (1 - u - v) * c[1])


def read_track(raw):
    map_offset, = struct.unpack_from("<I", raw)
    data = raw[4:4 + map_offset]

    def u32(offset):
        return struct.unpack_from("<I", data, offset)[0]

    mesh = u32(0)
    quad_count, vertex_count = u32(mesh), u32(mesh + 4)
    quads, verts = u32(mesh + 12), u32(mesh + 16)
    if quads + quad_count * 0x5c > len(data) or verts + vertex_count * 16 > len(data):
        raise ValueError("LEV mesh extends beyond the data segment")
    vertices = [struct.unpack_from("<3h", data, verts + i * 16) for i in range(vertex_count)]
    triangles = []
    for i in range(quad_count):
        at = quads + i * 0x5c
        ids = struct.unpack_from("<9H", data, at)
        flags, = struct.unpack_from("<H", data, at + 0x12)
        if not flags & 0x1000 or flags & 0x0210:  # Ground, excluding kill planes and no-response quads.
            continue
        # The eight high-LOD triangles used by COLL_FIXED_QUADBLK_TestTriangles.
        cells = [(0, 4, 5), (4, 6, 5), (6, 4, 1), (5, 6, 2)]
        if ids[2] != ids[3]:
            cells += [(8, 6, 7), (7, 3, 8), (1, 7, 6), (2, 6, 8)]
        for cell in cells:
            triangle = tuple(vertices[ids[j]] for j in cell)
            if len(set(triangle)) == 3:
                triangles.append(triangle)
    count, start = u32(0x148), u32(0x14c)
    if not 4 <= count <= 255 or start + count * 12 > len(data):
        raise ValueError("Invalid checkpoint table")
    checkpoints = [struct.unpack_from("<hhhH4B", data, start + i * 12) for i in range(count)]
    anchor = struct.unpack_from("<3h", data, 0x6c + 12)  # Time Trial uses slot 1.
    return triangles, checkpoints, anchor


def place(triangles, x, y, z, tolerance):
    x, z = round(x), round(z)
    heights = [h for tri in triangles if (h := ground_height(x, z, tri)) is not None]
    if not heights:
        raise ValueError("No ground under placement")
    height = min(heights, key=lambda h: abs(h - y))
    if abs(height - y) > tolerance or not all(-32768 <= v <= 32767 for v in (x, height, z)):
        raise ValueError("Placement outside the expected ground or coordinate range")
    return x, height, z


def direction(a, b):
    x, z = b[0] - a[0], b[2] - a[2]
    length = math.hypot(x, z)
    if not length:
        raise ValueError("Cannot determine a direction from coincident anchors")
    return x / length, z / length


def propose_grid(triangles, first_checkpoint, anchor):
    fx, fz = direction(anchor, first_checkpoint)
    yaw = (round(math.atan2(fx, fz) * 2048 / math.pi) - 0x400) & 4095
    grid = []
    for seat in range(8):
        lateral, back = (seat % 4 - 1) * 192, (seat // 4) * 260
        x = anchor[0] + fz * lateral - fx * back
        z = anchor[2] - fx * lateral - fz * back
        grid.append({"pos": place(triangles, x, anchor[1], z, 128), "rot": [0, yaw, 0]})
    return grid


def propose_crates(triangles, checkpoints):
    # Follow forward checkpoints from the start and reject broken cycles.
    order, seen, next_index = [], set(), 0
    while next_index not in seen and next_index < len(checkpoints):
        order.append(next_index)
        seen.add(next_index)
        next_index = checkpoints[next_index][4]
    if next_index != 0 or len(order) < 6:
        raise ValueError("Checkpoint path does not close")

    lap_distance = checkpoints[0][3]
    rows, used = [], set()
    for row in range(1, 5):
        target = lap_distance * (1 - row / 5)
        candidates = sorted(order[1:-1], key=lambda i: abs(checkpoints[i][3] - target))
        for index in candidates:
            x, y, z, distance, ahead, _, behind, _ = checkpoints[index]
            if index in used or abs(distance - target) > 300 or ahead not in seen or behind not in seen:
                continue
            prev, nxt = checkpoints[behind], checkpoints[ahead]
            if max(abs(y - prev[1]), abs(y - nxt[1])) > 256:
                continue
            try:
                fx, fz = direction(prev, nxt)
                crates = [{"pos": place(triangles, x + fz * off, y, z - fx * off, 100), "rot_y": 0}
                          for off in (-288, -96, 96, 288)]
            except ValueError:
                continue
            used.add(index)
            rows.append({"row": row, "crates": crates, "source": "proposed", "credit": "", "review": ""})
            break
        else:
            raise ValueError(f"No grounded placement for crate row {row}")
    return rows


def write_proposals(output, track, grid, rows):
    output.mkdir(parents=True)
    for filename, format_name, key, spots in (
        ("grid-spots.json", "ctr-ap-grid-spots", "spawns", grid),
        ("weapon-crate-spots.json", "ctr-ap-weapon-crate-spots", "rows", rows),
    ):
        document = {"format": format_name, "version": 1, "track": track, key: spots}
        (output / filename).write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lev", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("track_key")
    parser.add_argument("track_name")
    args = parser.parse_args()
    raw = args.lev.read_bytes()
    try:
        triangles, checkpoints, anchor = read_track(raw)
        grid = propose_grid(triangles, checkpoints[0], anchor)
        rows = propose_crates(triangles, checkpoints)
    except (IndexError, ValueError, struct.error) as exc:
        parser.error(str(exc))
    track = {"kind": "custom", "track_key": args.track_key, "name": args.track_name,
             "lev_sha256": [hashlib.sha256(raw).hexdigest()]}
    try:
        write_proposals(args.output, track, grid, rows)
    except OSError as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
