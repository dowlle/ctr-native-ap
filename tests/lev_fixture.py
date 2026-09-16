from __future__ import annotations

import struct


def _align(data: bytearray, alignment: int = 4) -> None:
    while len(data) % alignment:
        data.append(0)


def build_minimal_lev() -> bytes:
    data = bytearray(0x194)
    def alloc(size: int) -> int:
        _align(data); offset = len(data); data.extend(b"\0" * size); return offset
    mesh, quad, vertex, pvs, instdef = alloc(0x20), alloc(0x5C), alloc(0x10), alloc(0x10), alloc(0x40)
    global_list, pvs_list, bsp, hitboxes = alloc(8), alloc(8), alloc(0x20), alloc(0x40)
    pointer_slots: list[int] = []
    def pointer(slot: int, target: int) -> None:
        struct.pack_into("<I", data, slot, target); pointer_slots.append(slot)
    pointer(0x00, mesh); struct.pack_into("<I", data, 0x0C, 1); pointer(0x10, instdef); pointer(0x24, global_list)
    struct.pack_into("<ii", data, mesh, 1, 1); pointer(mesh + 0x0C, quad); pointer(mesh + 0x10, vertex)
    pointer(mesh + 0x18, bsp); struct.pack_into("<i", data, mesh + 0x1C, 1)
    struct.pack_into("<hhhH", data, vertex, 100, 200, 300, 1)
    struct.pack_into("<hhhhhh", data, quad + 0x2C, 50, 150, 250, 150, 250, 350)
    struct.pack_into("<H", data, quad + 0x12, 0x3000); data[quad + 0x38] = 2; data[quad + 0x3E] = 0xFF
    pointer(quad + 0x44, pvs); pointer(pvs + 0x08, pvs_list)
    data[instdef:instdef + 8] = b"crate_0\0"; struct.pack_into("<hhh", data, instdef + 0x14, 0x1000, 0x1000, 0x1000)
    struct.pack_into("<I", data, instdef + 0x1C, 0x80808000); struct.pack_into("<hhh", data, instdef + 0x30, 100, 200, 300)
    struct.pack_into("<hhh", data, instdef + 0x36, 0, 0x400, 0); struct.pack_into("<i", data, instdef + 0x3C, 8)
    pointer(global_list, instdef); pointer(pvs_list, instdef)
    struct.pack_into("<H", data, bsp, 1); struct.pack_into("<hhhhhh", data, bsp + 4, -1000, -1000, -1000, 1000, 1000, 1000)
    pointer(bsp + 0x14, hitboxes); struct.pack_into("<i", data, bsp + 0x18, 1); pointer(bsp + 0x1C, quad)
    struct.pack_into("<H", data, hitboxes, 0x4C0); struct.pack_into("<hhhhhh", data, hitboxes + 4, 70, 150, 270, 130, 250, 330)
    struct.pack_into("<hhhh", data, hitboxes + 0x10, 100, 200, 300, 32); pointer(hitboxes + 0x1C, instdef)
    _align(data); pointer_map_offset = len(data); ordered_slots = sorted(pointer_slots)
    data.extend(struct.pack("<i", len(ordered_slots) * 4)); data.extend(struct.pack(f"<{len(ordered_slots)}i", *ordered_slots))
    return struct.pack("<i", pointer_map_offset) + data
