"""Conservative derived-file writer for supported CTR pickup instances."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import struct
import tempfile

from .format import BSP_SIZE, INSTDEF_SIZE, LEVEL_FIELDS, LevFile


MODEL_IDS = {"wumpa-fruit": 0x02, "wumpa-crate": 0x07, "item-crate": 0x08}


@dataclass(frozen=True)
class Placement:
    name: str
    kind: str
    position: tuple[int, int, int]
    rotation: tuple[int, int, int]


def _align(data: bytearray, alignment: int = 4) -> None:
    while len(data) % alignment:
        data.append(0)


def _append(data: bytearray, payload: bytes | bytearray, alignment: int = 4) -> int:
    _align(data, alignment)
    offset = len(data)
    data.extend(payload)
    return offset


def _put_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def _in_s16(values: tuple[int, int, int]) -> bool:
    return all(-32768 <= value <= 32767 for value in values)


def _boxes_overlap(left: tuple[int, int, int, int, int, int], right: tuple[int, int, int, int, int, int]) -> bool:
    return (left[0] <= right[3] and right[0] <= left[3] and left[1] <= right[4] and right[1] <= left[4]
            and left[2] <= right[5] and right[2] <= left[5])


def add_placement(source: LevFile, placement: Placement) -> bytes:
    if not source.valid:
        raise ValueError("source LEV is invalid")
    if placement.kind not in MODEL_IDS:
        raise ValueError(f"unsupported vanilla placement kind {placement.kind!r}")
    if not _in_s16(placement.position) or not _in_s16(placement.rotation):
        raise ValueError("placement position and rotation must fit signed 16-bit CTR coordinates")
    encoded_name = placement.name.encode("ascii")
    if not encoded_name or len(encoded_name) > 15 or b"\0" in encoded_name:
        raise ValueError("instance name must be 1 through 15 ASCII bytes")

    templates = [item for item in source.instances if item.model_id == MODEL_IDS[placement.kind] and item.bsp_references]
    if not templates:
        templates = [item for item in source.instances if item.model_id == MODEL_IDS[placement.kind]]
    if not templates:
        raise ValueError(f"track does not contain a model template for {placement.kind}")
    template = templates[0]
    template_hitboxes = [(record, target) for values in source.bsp_hitbox_lists.values() for record, target in values
                         if target == template.offset]
    if not template_hitboxes:
        raise ValueError(f"template {template.name!r} has no BSP hitbox record")
    template_record = template_hitboxes[0][0]

    data = bytearray(source.data[:source.ptr_map_offset])
    pointer_slots = set(source.pointer_slots)
    old_table = source.level["instdefs_offset"]
    old_count = source.level["num_instances"]

    table_bytes = bytearray(source.data[old_table:old_table + old_count * INSTDEF_SIZE])
    table_bytes.extend(source.data[template.offset:template.offset + INSTDEF_SIZE])
    new_table = _append(data, table_bytes)
    new_offsets = {old_table + index * INSTDEF_SIZE: new_table + index * INSTDEF_SIZE for index in range(old_count)}
    new_instance = new_table + old_count * INSTDEF_SIZE
    new_offsets[template.offset] = new_offsets[template.offset]
    data[new_instance:new_instance + 0x10] = encoded_name.ljust(0x10, b"\0")
    struct.pack_into("<hhh", data, new_instance + 0x30, *placement.position)
    struct.pack_into("<hhh", data, new_instance + 0x36, *placement.rotation)
    _put_u32(data, new_instance + 0x2C, 0)

    for index, old_instance in enumerate(source.instances):
        for slot in source.pointer_slots:
            if old_instance.offset <= slot < old_instance.offset + INSTDEF_SIZE:
                relative = slot - old_instance.offset
                if relative != 0x2C and struct.unpack_from("<I", data, new_table + index * INSTDEF_SIZE + relative)[0]:
                    pointer_slots.add(new_table + index * INSTDEF_SIZE + relative)
    if struct.unpack_from("<I", data, new_instance + 0x10)[0]:
        pointer_slots.add(new_instance + 0x10)

    old_list_slots = set(range(source.level["global_instances_offset"],
                               source.level["global_instances_offset"] + len(source.global_instance_offsets) * 4, 4))
    old_list_slots.update(slot for start, values in source.pvs_lists.items()
                          for slot in range(start, start + len(values) * 4, 4))
    old_list_slots.update(record + 0x1C for values in source.bsp_hitbox_lists.values() for record, target in values if target)
    for slot in tuple(pointer_slots):
        if slot in old_list_slots:
            continue
        if slot + 4 <= len(data):
            target = struct.unpack_from("<I", data, slot)[0]
            if target in new_offsets:
                _put_u32(data, slot, new_offsets[target])

    global_values = [new_offsets[value] for value in source.global_instance_offsets] + [new_instance]
    global_list = _append(data, struct.pack(f"<{len(global_values) + 1}I", *global_values, 0))
    for lane in range(len(global_values)):
        pointer_slots.add(global_list + lane * 4)
    _put_u32(data, LEVEL_FIELDS["num_instances"], old_count + 1)
    _put_u32(data, LEVEL_FIELDS["instdefs_offset"], new_table)
    _put_u32(data, LEVEL_FIELDS["global_instances_offset"], global_list)

    for old_list, values in source.pvs_lists.items():
        remapped = [new_offsets[value] for value in values]
        remapped.append(new_instance)
        new_list = _append(data, struct.pack(f"<{len(remapped) + 1}I", *remapped, 0))
        for lane in range(len(remapped)):
            pointer_slots.add(new_list + lane * 4)
        for field_slot in source.pvs_list_pointer_slots.get(old_list, ()):
            _put_u32(data, field_slot, new_list)

    old_center = tuple(struct.unpack_from("<hhh", source.data, template_record + 0x10))
    delta = tuple(placement.position[lane] - template.position[lane] for lane in range(3))
    new_center = tuple(old_center[lane] + delta[lane] for lane in range(3))
    if not _in_s16(new_center):
        raise ValueError("translated hitbox center exceeds signed 16-bit coordinates")
    original_box = tuple(struct.unpack_from("<hhhhhh", source.data, template_record + 4))
    translated_box = tuple(original_box[lane] + delta[lane % 3] for lane in range(6))
    if not all(-32768 <= value <= 32767 for value in translated_box):
        raise ValueError("translated hitbox bounds exceed signed 16-bit coordinates")
    new_hitbox = bytearray(source.data[template_record:template_record + BSP_SIZE])
    struct.pack_into("<hhhhhh", new_hitbox, 4, *translated_box)
    struct.pack_into("<hhh", new_hitbox, 0x10, *new_center)
    struct.pack_into("<I", new_hitbox, 0x1C, new_instance)

    inserted_hitbox = False
    for old_list, records in source.bsp_hitbox_lists.items():
        array = bytearray()
        pointer_lanes: list[int] = []
        for record, target in records:
            record_bytes = bytearray(source.data[record:record + BSP_SIZE])
            if target in new_offsets:
                struct.pack_into("<I", record_bytes, 0x1C, new_offsets[target])
            pointer_lanes.append(len(array) + 0x1C)
            array.extend(record_bytes)
        include = any(_boxes_overlap(box, translated_box) for box in source.bsp_list_leaf_boxes.get(old_list, ()))
        if include:
            pointer_lanes.append(len(array) + 0x1C)
            array.extend(new_hitbox)
            inserted_hitbox = True
        array.extend(b"\0" * BSP_SIZE)
        new_list = _append(data, array)
        for lane in pointer_lanes:
            pointer_slots.add(new_list + lane)
        for field_slot in source.bsp_list_pointer_slots.get(old_list, ()):
            _put_u32(data, field_slot, new_list)
    if not inserted_hitbox:
        raise ValueError("placement hitbox does not overlap any BSP leaf")

    _align(data)
    pointer_map_offset = len(data)
    ordered_slots = sorted(pointer_slots)
    for slot in ordered_slots:
        if slot % 4 or slot + 4 > pointer_map_offset:
            raise ValueError(f"writer produced invalid pointer slot 0x{slot:X}")
        target = struct.unpack_from("<I", data, slot)[0]
        if target >= pointer_map_offset:
            raise ValueError(f"writer produced out-of-range pointer target 0x{target:X}")
    data.extend(struct.pack("<i", len(ordered_slots) * 4))
    data.extend(struct.pack(f"<{len(ordered_slots)}i", *ordered_slots))
    result = struct.pack("<i", pointer_map_offset) + data
    reparsed = LevFile(result)
    if not reparsed.valid:
        raise ValueError("derived LEV failed validation: " + "; ".join(item.message for item in reparsed.errors))
    return result


def move_local(source: LevFile, index: int, position: tuple[int, int, int], rotation: tuple[int, int, int]) -> bytes:
    """Move one supported instance while retaining its proven PVS and BSP memberships."""
    if not source.valid:
        raise ValueError("source LEV is invalid")
    if not 0 <= index < len(source.instances):
        raise ValueError("instance index is out of range")
    if not _in_s16(position) or not _in_s16(rotation):
        raise ValueError("position and rotation must fit signed 16-bit CTR coordinates")
    instance = source.instances[index]
    if instance.model_id not in MODEL_IDS.values() or not instance.bsp_references or not instance.pvs_references:
        raise ValueError("instance is not a supported portable object with both PVS and BSP membership")
    delta = tuple(position[lane] - instance.position[lane] for lane in range(3))
    data = bytearray(source.data)
    changed_hitboxes = 0
    for list_start, records in source.bsp_hitbox_lists.items():
        for record, target in records:
            if target != instance.offset:
                continue
            old_box = tuple(struct.unpack_from("<hhhhhh", source.data, record + 4))
            new_box = tuple(old_box[lane] + delta[lane % 3] for lane in range(6))
            old_center = tuple(struct.unpack_from("<hhh", source.data, record + 0x10))
            new_center = tuple(old_center[lane] + delta[lane] for lane in range(3))
            if not all(-32768 <= value <= 32767 for value in new_box) or not _in_s16(new_center):
                raise ValueError("translated hitbox exceeds signed 16-bit coordinates")
            leaf_boxes = source.bsp_list_leaf_boxes.get(list_start, ())
            if leaf_boxes and not any(_boxes_overlap(leaf, new_box) for leaf in leaf_boxes):
                raise ValueError("move leaves the instance's existing BSP leaf membership; use a cross-region operation")
            struct.pack_into("<hhhhhh", data, record + 4, *new_box)
            struct.pack_into("<hhh", data, record + 0x10, *new_center)
            changed_hitboxes += 1
    if not changed_hitboxes:
        raise ValueError("supported instance has no writable BSP hitbox")
    struct.pack_into("<hhh", data, instance.offset + 0x30, *position)
    struct.pack_into("<hhh", data, instance.offset + 0x36, *rotation)
    result = struct.pack("<i", source.ptr_map_offset) + data
    verified = LevFile(result)
    if not verified.valid or verified.pointer_slots != source.pointer_slots:
        raise ValueError("local move failed graph validation")
    return result


def apply_placements(source: LevFile, placements: list[Placement]) -> bytes:
    raw = source.raw
    for placement in placements:
        raw = add_placement(LevFile(raw), placement)
    return raw


def atomic_binary_write(path: Path, raw: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
