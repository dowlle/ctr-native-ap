"""Read-only parser and reference-graph validator for CTR LEV files.

LEV pointer fields are data-relative offsets before load. The first signed word
in the file points into the payload that starts at file offset 4. The pointer
map stored there lists every payload slot relocated by LOAD_RunPtrMap.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path
import struct
from typing import Iterable


LEVEL_MIN_SIZE = 0x194
INSTDEF_SIZE = 0x40
MESH_INFO_SIZE = 0x20
QUADBLOCK_SIZE = 0x5C
PVS_SIZE = 0x10
BSP_SIZE = 0x20
LEV_VERTEX_SIZE = 0x10
CHECKPOINT_SIZE = 0x0C
SPAWN_TYPE2_SIZE = 0x08
SPAWN_POSROT_SIZE = 0x0C


class LevFormatError(ValueError):
    pass


@dataclass(frozen=True)
class Diagnostic:
    severity: str
    code: str
    message: str
    offset: int | None = None


@dataclass(frozen=True)
class InstDef:
    index: int
    offset: int
    name: str
    model_offset: int
    model_id: int
    position: tuple[int, int, int]
    rotation: tuple[int, int, int]
    global_references: int = 0
    pvs_references: int = 0
    bsp_references: int = 0
    other_references: int = 0

    @property
    def total_references(self) -> int:
        return self.global_references + self.pvs_references + self.bsp_references + self.other_references


LEVEL_FIELDS = {
    "mesh_offset": 0x00,
    "skybox_offset": 0x04,
    "anim_tex_offset": 0x08,
    "num_instances": 0x0C,
    "instdefs_offset": 0x10,
    "num_models": 0x14,
    "models_array_offset": 0x18,
    "global_instances_offset": 0x24,
    "num_water_vertices": 0x34,
    "water_offset": 0x38,
    "texture_lookup_offset": 0x3C,
    "named_textures_offset": 0x40,
    "config_flags": 0xDC,
    "build_start_offset": 0xE0,
    "build_end_offset": 0xE4,
    "build_type_offset": 0xE8,
    "spawn_type_1_offset": 0x134,
    "num_spawn_type_2": 0x138,
    "spawn_type_2_offset": 0x13C,
    "num_spawn_type_2_posrot": 0x140,
    "spawn_type_2_posrot_offset": 0x144,
    "num_restart_points": 0x148,
    "restart_points_offset": 0x14C,
    "nav_table_offset": 0x188,
    "vismem_offset": 0x190,
}

LEVEL_POINTER_FIELDS = {name for name in LEVEL_FIELDS if name.endswith("_offset")}


class LevFile:
    def __init__(self, raw: bytes, path: str | Path | None = None):
        self.raw = raw
        self.path = Path(path) if path is not None else None
        self.diagnostics: list[Diagnostic] = []
        self.ptr_map_offset = -1
        self.data = b""
        self.pointer_slots: tuple[int, ...] = ()
        self.pointer_slot_set: set[int] = set()
        self.level: dict[str, int] = {}
        self.pvs_lists: dict[int, tuple[int, ...]] = {}
        self.pvs_list_pointer_slots: dict[int, set[int]] = {}
        self.bsp_hitbox_lists: dict[int, tuple[tuple[int, int], ...]] = {}
        self.bsp_list_pointer_slots: dict[int, set[int]] = {}
        self.bsp_list_leaf_boxes: dict[int, list[tuple[int, int, int, int, int, int]]] = {}
        self.global_instance_offsets: tuple[int, ...] = ()
        self.instances: tuple[InstDef, ...] = ()
        self.vertices: tuple[dict[str, object], ...] = ()
        self.quadblocks: tuple[dict[str, object], ...] = ()
        self.checkpoints: tuple[dict[str, object], ...] = ()
        self.driver_spawns: tuple[dict[str, object], ...] = ()
        self.spawn_groups: tuple[dict[str, object], ...] = ()
        self._parse()

    @classmethod
    def from_path(cls, path: str | Path) -> "LevFile":
        source = Path(path)
        return cls(source.read_bytes(), source)

    def _diag(self, severity: str, code: str, message: str, offset: int | None = None) -> None:
        self.diagnostics.append(Diagnostic(severity, code, message, offset))

    def _in_data(self, offset: int, size: int = 1) -> bool:
        return offset >= 0 and size >= 0 and offset + size <= len(self.data)

    def _u16(self, offset: int) -> int:
        return struct.unpack_from("<H", self.data, offset)[0]

    def _s16(self, offset: int) -> int:
        return struct.unpack_from("<h", self.data, offset)[0]

    def _u32(self, offset: int) -> int:
        return struct.unpack_from("<I", self.data, offset)[0]

    def _s32(self, offset: int) -> int:
        return struct.unpack_from("<i", self.data, offset)[0]

    def _cstring(self, offset: int, limit: int = 96) -> str | None:
        if offset == 0 or not self._in_data(offset):
            return None
        end = self.data.find(b"\0", offset, min(len(self.data), offset + limit))
        if end < 0:
            end = min(len(self.data), offset + limit)
        return self.data[offset:end].decode("ascii", "replace")

    def _parse(self) -> None:
        if len(self.raw) < 8:
            self._diag("error", "file-too-small", "LEV is smaller than its pointer-map header")
            return
        self.ptr_map_offset = struct.unpack_from("<i", self.raw, 0)[0]
        self.data = self.raw[4:]
        self._parse_pointer_map()
        self._parse_level()
        self._parse_reference_graph()
        self._parse_geometry()
        self._parse_checkpoints_and_spawns()

    def _parse_pointer_map(self) -> None:
        if self.ptr_map_offset < LEVEL_MIN_SIZE or not self._in_data(self.ptr_map_offset, 4):
            self._diag("error", "pointer-map-offset", f"pointer map offset {self.ptr_map_offset} is outside the payload", 0)
            return
        num_bytes = self._s32(self.ptr_map_offset)
        if num_bytes < 0 or num_bytes % 4 != 0:
            self._diag("error", "pointer-map-size", f"pointer map byte count {num_bytes} is not a non-negative multiple of four", self.ptr_map_offset)
            return
        begin = self.ptr_map_offset + 4
        end = begin + num_bytes
        if end > len(self.data):
            self._diag("error", "pointer-map-truncated", "pointer map extends beyond end of file", self.ptr_map_offset)
            return
        if end != len(self.data):
            self._diag("warning", "pointer-map-not-at-eof", f"{len(self.data) - end} payload bytes follow the pointer map", end)
        slots = struct.unpack_from(f"<{num_bytes // 4}i", self.data, begin) if num_bytes else ()
        self.pointer_slots = tuple(slots)
        self.pointer_slot_set = set(slots)
        if len(self.pointer_slot_set) != len(slots):
            self._diag("error", "pointer-map-duplicate", "pointer map contains duplicate patch slots")
        for slot in slots:
            if slot % 4:
                self._diag("error", "pointer-slot-unaligned", f"patch slot 0x{slot:X} is not four-byte aligned", slot)
                continue
            if slot < 0 or slot + 4 > self.ptr_map_offset:
                self._diag("error", "pointer-slot-range", f"patch slot 0x{slot:X} is outside relocatable payload", slot)
                continue
            target = self._u32(slot)
            if target >= self.ptr_map_offset:
                self._diag("error", "pointer-target-range", f"slot 0x{slot:X} targets 0x{target:X}, outside relocatable payload", slot)

    def _parse_level(self) -> None:
        if len(self.data) < LEVEL_MIN_SIZE:
            self._diag("error", "level-header-truncated", "payload does not contain a complete struct Level")
            return
        self.level = {name: self._u32(offset) for name, offset in LEVEL_FIELDS.items()}
        for name in LEVEL_POINTER_FIELDS:
            field_offset = LEVEL_FIELDS[name]
            value = self.level[name]
            if value and field_offset not in self.pointer_slot_set:
                self._diag("error", "level-pointer-unmapped", f"Level.{name} is non-null but its slot is absent from the pointer map", field_offset)

    def _read_null_terminated_pointer_list(self, start: int, code: str, max_items: int = 100000) -> tuple[int, ...]:
        if start == 0:
            return ()
        if start % 4 or not self._in_data(start, 4):
            self._diag("error", f"{code}-range", f"pointer list starts at invalid offset 0x{start:X}", start)
            return ()
        result: list[int] = []
        for index in range(max_items):
            slot = start + index * 4
            if not self._in_data(slot, 4) or slot >= self.ptr_map_offset:
                self._diag("error", f"{code}-unterminated", "pointer list has no null terminator before the pointer map", start)
                break
            target = self._u32(slot)
            if target == 0:
                break
            if slot not in self.pointer_slot_set:
                self._diag("error", f"{code}-entry-unmapped", f"non-null list slot 0x{slot:X} is absent from the pointer map", slot)
            result.append(target)
        return tuple(result)

    def _parse_reference_graph(self) -> None:
        if not self.level:
            return
        num_instances = self.level["num_instances"]
        instdefs_offset = self.level["instdefs_offset"]
        global_offset = self.level["global_instances_offset"]
        if num_instances > 100000:
            self._diag("error", "instance-count", f"unreasonable instance count {num_instances}", LEVEL_FIELDS["num_instances"])
            return
        if num_instances and (instdefs_offset % 4 or not self._in_data(instdefs_offset, num_instances * INSTDEF_SIZE)):
            self._diag("error", "instdef-table-range", "InstDef table is outside the payload", instdefs_offset)
            return
        self.global_instance_offsets = self._read_null_terminated_pointer_list(global_offset, "global-instance-list")
        if len(self.global_instance_offsets) != num_instances:
            self._diag("error", "global-instance-count", f"global instance list has {len(self.global_instance_offsets)} entries, expected {num_instances}", global_offset)
        inst_offsets = {instdefs_offset + index * INSTDEF_SIZE for index in range(num_instances)}
        global_set = set(self.global_instance_offsets)
        missing = sorted(inst_offsets - global_set)
        extra = sorted(global_set - inst_offsets)
        if missing:
            self._diag("error", "global-instance-missing", f"global instance list omits {len(missing)} InstDef records", global_offset)
        if extra:
            self._diag("error", "global-instance-extra", f"global instance list contains {len(extra)} non-InstDef targets", global_offset)
        self._parse_pvs_lists()
        self._parse_bsp_hitboxes()
        global_slots = set(range(global_offset, global_offset + len(self.global_instance_offsets) * 4, 4)) if global_offset else set()
        pvs_slots = {slot for start, values in self.pvs_lists.items() for slot in range(start, start + len(values) * 4, 4)}
        bsp_slots = {record + 0x1C for values in self.bsp_hitbox_lists.values() for record, target in values if target != 0}
        parsed: list[InstDef] = []
        for index in range(num_instances):
            offset = instdefs_offset + index * INSTDEF_SIZE
            name = self.data[offset:offset + 0x10].split(b"\0", 1)[0].decode("ascii", "replace")
            position = tuple(self._s16(offset + 0x30 + lane * 2) for lane in range(3))
            rotation = tuple(self._s16(offset + 0x36 + lane * 2) for lane in range(3))
            refs = [slot for slot in self.pointer_slots if self._in_data(slot, 4) and self._u32(slot) == offset]
            if index == 0 and LEVEL_FIELDS["instdefs_offset"] in refs:
                refs.remove(LEVEL_FIELDS["instdefs_offset"])
            global_refs = sum(slot in global_slots for slot in refs)
            pvs_refs = sum(slot in pvs_slots for slot in refs)
            bsp_refs = sum(slot in bsp_slots for slot in refs)
            parsed.append(InstDef(index, offset, name, self._u32(offset + 0x10), self._s32(offset + 0x3C), position, rotation,
                                  global_refs, pvs_refs, bsp_refs, len(refs) - global_refs - pvs_refs - bsp_refs))
        self.instances = tuple(parsed)

    def _parse_pvs_lists(self) -> None:
        mesh = self.level.get("mesh_offset", 0)
        if not mesh or not self._in_data(mesh, MESH_INFO_SIZE):
            return
        num_quads = self._s32(mesh)
        quad_array = self._u32(mesh + 0x0C)
        if num_quads < 0 or num_quads > 100000 or not self._in_data(quad_array, num_quads * QUADBLOCK_SIZE):
            self._diag("error", "quadblock-table-range", "mesh quadblock table is outside the payload", quad_array)
            return
        seen: set[int] = set()
        for index in range(num_quads):
            pvs = self._u32(quad_array + index * QUADBLOCK_SIZE + 0x44)
            if pvs == 0:
                continue
            if not self._in_data(pvs, PVS_SIZE):
                self._diag("error", "pvs-range", f"quadblock {index} points outside the payload", pvs)
                continue
            list_start = self._u32(pvs + 0x08)
            if list_start:
                self.pvs_list_pointer_slots.setdefault(list_start, set()).add(pvs + 0x08)
            if pvs in seen:
                continue
            seen.add(pvs)
            if list_start and list_start not in self.pvs_lists:
                self.pvs_lists[list_start] = self._read_null_terminated_pointer_list(list_start, "pvs-instance-list")

    def _parse_bsp_hitboxes(self) -> None:
        mesh = self.level.get("mesh_offset", 0)
        if not mesh or not self._in_data(mesh, MESH_INFO_SIZE):
            return
        root = self._u32(mesh + 0x18)
        count = self._s32(mesh + 0x1C)
        if count < 0 or count > 100000 or not self._in_data(root, count * BSP_SIZE):
            self._diag("error", "bsp-table-range", "mesh BSP node table is outside the payload", root)
            return
        for index in range(count):
            node = root + index * BSP_SIZE
            if self._u16(node) & 1 == 0:
                continue
            array = self._u32(node + 0x14)
            if array == 0:
                continue
            self.bsp_list_pointer_slots.setdefault(array, set()).add(node + 0x14)
            self.bsp_list_leaf_boxes.setdefault(array, []).append(tuple(self._s16(node + 4 + lane * 2) for lane in range(6)))
            if array in self.bsp_hitbox_lists:
                continue
            records: list[tuple[int, int]] = []
            for record_index in range(100000):
                record = array + record_index * BSP_SIZE
                if not self._in_data(record, BSP_SIZE) or record >= self.ptr_map_offset:
                    self._diag("error", "bsp-hitbox-unterminated", f"BSP leaf {index} hitbox list has no zero record", array)
                    break
                flags = self._u16(record)
                if flags == 0:
                    break
                target = self._u32(record + 0x1C)
                if target and record + 0x1C not in self.pointer_slot_set:
                    self._diag("error", "bsp-instdef-unmapped", "BSP hitbox InstDef pointer is absent from the pointer map", record + 0x1C)
                records.append((record, target))
            self.bsp_hitbox_lists[array] = tuple(records)

    def _parse_geometry(self) -> None:
        mesh = self.level.get("mesh_offset", 0)
        if not mesh or not self._in_data(mesh, MESH_INFO_SIZE):
            return
        num_vertices = self._s32(mesh + 0x04)
        vertex_array = self._u32(mesh + 0x10)
        if num_vertices < 0 or num_vertices > 1000000:
            self._diag("error", "vertex-count", f"unreasonable vertex count {num_vertices}", mesh + 0x04)
        elif num_vertices and not self._in_data(vertex_array, num_vertices * LEV_VERTEX_SIZE):
            self._diag("error", "vertex-table-range", "mesh vertex table is outside the payload", vertex_array)
        else:
            self.vertices = tuple({
                "index": index,
                "offset": vertex_array + index * LEV_VERTEX_SIZE,
                "position": tuple(self._s16(vertex_array + index * LEV_VERTEX_SIZE + lane * 2) for lane in range(3)),
                "flags": self._u16(vertex_array + index * LEV_VERTEX_SIZE + 0x06),
                "color_high": list(self.data[vertex_array + index * LEV_VERTEX_SIZE + 0x08:
                                             vertex_array + index * LEV_VERTEX_SIZE + 0x0C]),
                "color_low": list(self.data[vertex_array + index * LEV_VERTEX_SIZE + 0x0C:
                                            vertex_array + index * LEV_VERTEX_SIZE + 0x10]),
            } for index in range(num_vertices))
        num_quads = self._s32(mesh)
        quad_array = self._u32(mesh + 0x0C)
        if num_quads < 0 or num_quads > 100000 or (num_quads and not self._in_data(quad_array, num_quads * QUADBLOCK_SIZE)):
            return
        quads: list[dict[str, object]] = []
        for index in range(num_quads):
            offset = quad_array + index * QUADBLOCK_SIZE
            vertex_indices = tuple(self._u16(offset + lane * 2) for lane in range(9))
            invalid_indices = [value for value in vertex_indices if value >= num_vertices]
            if invalid_indices:
                self._diag("error", "quadblock-vertex-index", f"quadblock {index} has {len(invalid_indices)} out-of-range vertex indices", offset)
            pvs_offset = self._u32(offset + 0x44)
            quads.append({
                "index": index,
                "offset": offset,
                "vertex_indices": vertex_indices,
                "flags": self._u16(offset + 0x12),
                "draw_order_low": self._u32(offset + 0x14),
                "draw_order_high": self._u32(offset + 0x18),
                "bounding_box": tuple(self._s16(offset + 0x2C + lane * 2) for lane in range(6)),
                "terrain": self.data[offset + 0x38],
                "weather_intensity": self.data[offset + 0x39],
                "weather_vanish_rate": self.data[offset + 0x3A],
                "normal_y_multiplier": struct.unpack_from("<b", self.data, offset + 0x3B)[0],
                "block_id": self._s16(offset + 0x3C),
                "checkpoint_index": self.data[offset + 0x3E],
                "pvs_offset": pvs_offset,
                "pvs_instance_list_offset": self._u32(pvs_offset + 0x08) if pvs_offset and self._in_data(pvs_offset, PVS_SIZE) else 0,
            })
        self.quadblocks = tuple(quads)

    def _parse_checkpoints_and_spawns(self) -> None:
        if not self.level:
            return
        count = self.level.get("num_restart_points", 0)
        start = self.level.get("restart_points_offset", 0)
        if count > 100000:
            self._diag("error", "checkpoint-count", f"unreasonable checkpoint count {count}", LEVEL_FIELDS["num_restart_points"])
        elif count and not self._in_data(start, count * CHECKPOINT_SIZE):
            self._diag("error", "checkpoint-table-range", "checkpoint table is outside the payload", start)
        else:
            checkpoints = []
            for index in range(count):
                offset = start + index * CHECKPOINT_SIZE
                links = [self.data[offset + 0x08 + lane] for lane in range(4)]
                for link in links:
                    if link != 0xFF and link >= count:
                        self._diag("error", "checkpoint-link-range", f"checkpoint {index} links to invalid index {link}", offset + 0x08)
                checkpoints.append({"index": index, "offset": offset,
                                    "position": tuple(self._s16(offset + lane * 2) for lane in range(3)),
                                    "distance_to_finish": self._u16(offset + 0x06),
                                    "links": {"forward": links[0], "left": links[1], "backward": links[2], "right": links[3]}})
            self.checkpoints = tuple(checkpoints)
        self.driver_spawns = tuple({
            "index": index,
            "position": tuple(self._s16(0x6C + index * SPAWN_POSROT_SIZE + lane * 2) for lane in range(3)),
            "rotation": tuple(self._s16(0x72 + index * SPAWN_POSROT_SIZE + lane * 2) for lane in range(3)),
        } for index in range(8))
        groups = []
        for kind, group_count_field, table_field, stride in (
            ("position", "num_spawn_type_2", "spawn_type_2_offset", 6),
            ("position-rotation", "num_spawn_type_2_posrot", "spawn_type_2_posrot_offset", SPAWN_POSROT_SIZE),
        ):
            group_count = self.level.get(group_count_field, 0)
            table = self.level.get(table_field, 0)
            if group_count > 100000 or (group_count and not self._in_data(table, group_count * SPAWN_TYPE2_SIZE)):
                self._diag("error", "spawn-group-table-range", f"{kind} spawn-group table is outside the payload", table)
                continue
            for index in range(group_count):
                header = table + index * SPAWN_TYPE2_SIZE
                coord_count = self._s32(header)
                coords = self._u32(header + 4)
                if coord_count < 0 or coord_count > 100000 or (coord_count and not self._in_data(coords, coord_count * stride)):
                    self._diag("error", "spawn-coordinates-range", f"{kind} spawn group {index} is outside the payload", coords)
                    continue
                entries = []
                for coord_index in range(coord_count):
                    offset = coords + coord_index * stride
                    entry: dict[str, object] = {"index": coord_index,
                                                "position": tuple(self._s16(offset + lane * 2) for lane in range(3))}
                    if kind == "position-rotation":
                        entry["rotation"] = tuple(self._s16(offset + 6 + lane * 2) for lane in range(3))
                    entries.append(entry)
                groups.append({"kind": kind, "index": index, "offset": header, "entries": entries})
        self.spawn_groups = tuple(groups)

    @property
    def errors(self) -> tuple[Diagnostic, ...]:
        return tuple(item for item in self.diagnostics if item.severity == "error")

    @property
    def warnings(self) -> tuple[Diagnostic, ...]:
        return tuple(item for item in self.diagnostics if item.severity == "warning")

    @property
    def valid(self) -> bool:
        return not self.errors

    def summary(self) -> dict[str, object]:
        return {
            "path": str(self.path) if self.path else None,
            "file_bytes": len(self.raw),
            "payload_bytes": len(self.data),
            "pointer_map_offset": self.ptr_map_offset,
            "pointer_count": len(self.pointer_slots),
            "level": self.level,
            "build": {"start": self._cstring(self.level.get("build_start_offset", 0)), "end": self._cstring(self.level.get("build_end_offset", 0)),
                      "type": self._cstring(self.level.get("build_type_offset", 0))} if self.level else {},
            "pvs_unique_instance_lists": len(self.pvs_lists),
            "bsp_unique_hitbox_lists": len(self.bsp_hitbox_lists),
            "instances": [asdict(instance) | {"total_references": instance.total_references} for instance in self.instances],
            "valid": self.valid,
            "diagnostics": [asdict(item) for item in self.diagnostics],
        }

    def simulate_fixups(self, base: int = 0x10000000) -> list[tuple[int, int, int]]:
        result: list[tuple[int, int, int]] = []
        for slot in self.pointer_slots:
            if not self._in_data(slot, 4):
                continue
            relative = self._u32(slot)
            absolute = base + relative
            if absolute > 0xFFFFFFFF:
                self._diag("error", "fixup-overflow", f"relocation at 0x{slot:X} overflows 32-bit address space", slot)
            result.append((slot, relative, absolute & 0xFFFFFFFF))
        return result


def contiguous_byte_differences(left: bytes, right: bytes) -> Iterable[tuple[int, bytes, bytes]]:
    limit = max(len(left), len(right))
    start: int | None = None
    for offset in range(limit):
        a = left[offset] if offset < len(left) else None
        b = right[offset] if offset < len(right) else None
        if a != b and start is None:
            start = offset
        elif a == b and start is not None:
            yield start, left[start:offset], right[start:offset]
            start = None
    if start is not None:
        yield start, left[start:limit], right[start:limit]
