#!/usr/bin/env python3
"""Read-only LEV C/T/R collision identity audit, using native struct offsets.

This proves authored TOUCH boxes reference each letter through mesh leaves.
It does not certify driving reachability, live relocation or pickup rendering.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path


def audit(path):
    raw = Path(path).read_bytes()
    if len(raw) > 16 * 1024 * 1024 or len(raw) < 8:
        raise ValueError("LEV size out of bounds")
    payload = raw[4:]
    end = struct.unpack_from("<I", raw)[0]

    def read(fmt, offset, limit=None):
        limit = end if limit is None else limit
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > min(limit, len(payload)):
            raise ValueError("read outside retained LEV payload")
        return struct.unpack_from(fmt, payload, offset)

    if end < 0x190 or end % 4:
        raise ValueError("invalid pointer-map offset")
    count = read("<I", end, len(payload))[0]
    if count % 4 or end + 4 + count != len(payload):
        raise ValueError("invalid pointer-map extent")
    slots = read(f"<{count // 4}I", end + 4, len(payload))
    if len(set(slots)) != len(slots):
        raise ValueError("duplicate pointer slots")
    for slot in slots:
        if slot % 4 or read("<I", slot)[0] >= end:
            raise ValueError("invalid pointer slot/target")
    slots = set(slots)

    def pointer(offset):
        if offset not in slots:
            raise ValueError("unregistered relocation pointer")
        return read("<I", offset)[0]

    instance_count = read("<I", 0x0C)[0]
    if instance_count > 65536:
        raise ValueError("instance count out of bounds")
    instances = pointer(0x10)
    letters = {}
    for index in range(instance_count):
        address = instances + index * 0x40
        read("<64s", address)
        model = read("<H", pointer(address + 0x10) + 0x10)[0]
        if model in (0x93, 0x94, 0x95):
            letters[address] = {"letter": "CTR"[model - 0x93],
                                "instance": address,
                                "position": read("<3h", address + 0x30), "boxes": []}
    if sorted(row["letter"] for row in letters.values()) != ["C", "R", "T"]:
        raise ValueError("expected exactly one C/T/R instance")
    mesh = pointer(0)
    root = pointer(mesh + 0x18)
    nodes = read("<I", mesh + 0x1C)[0]
    if nodes > 65536:
        raise ValueError("BSP node count out of bounds")
    for index in range(nodes):
        node = root + index * 0x20
        read("<32s", node)
        if not read("<H", node)[0] & 1:
            continue
        offset = node + 0x14
        if offset not in slots or read("<I", offset)[0] == 0:
            continue
        hitbox = pointer(offset)
        for _ in range(65536):
            if read("<I", hitbox)[0] == 0:
                break
            read("<32s", hitbox)
            if hitbox + 0x1C in slots:
                owner = pointer(hitbox + 0x1C)
                if owner in letters:
                    flag = read("<H", hitbox)[0]
                    bounds = read("<6h", hitbox + 4)
                    if flag >> 8 != 4 or any(bounds[i] >= bounds[i + 3] for i in range(3)):
                        raise ValueError("letter has non-TOUCH or degenerate box")
                    box = {"offset": hitbox, "flag": flag, "bounds": bounds}
                    if box not in letters[owner]["boxes"]:
                        letters[owner]["boxes"].append(box)
            hitbox += 0x20
        else:
            raise ValueError("unterminated hitbox array")
    if any(not row["boxes"] for row in letters.values()):
        raise ValueError("letter missing referenced mesh TOUCH box")
    return {"sha256": hashlib.sha256(raw).hexdigest(), "mesh_nodes": nodes,
            "letters": sorted(letters.values(), key=lambda row: row["letter"])}


if __name__ == "__main__":
    print(json.dumps(audit(sys.argv[1]), indent=2))
