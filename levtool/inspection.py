"""Stable, versioned inspection reports for LEV files and editor projects."""

from __future__ import annotations

from collections import Counter
from dataclasses import asdict
from hashlib import sha256
from pathlib import Path
import struct
from typing import Any

from .format import LevFile, contiguous_byte_differences
from .project import file_sha256, validate_project


LEV_INSPECTION_SCHEMA = "ctr-native-lev-inspection"
LEV_INSPECTION_VERSION = 1
PROJECT_INSPECTION_SCHEMA = "ctr-native-project-inspection"
PROJECT_INSPECTION_VERSION = 1
LEV_DIFF_SCHEMA = "ctr-native-lev-diff"
LEV_DIFF_VERSION = 1
GEOMETRY_QUERY_SCHEMA = "ctr-native-geometry-query"
GEOMETRY_QUERY_VERSION = 1


def _hex(value: int) -> str:
    return f"0x{value:X}"


def lev_inspection(lev: LevFile, *, include_path: bool = True) -> dict[str, Any]:
    """Return a deterministic, machine-consumable view of a parsed LEV graph."""
    offset_to_index = {instance.offset: instance.index for instance in lev.instances}
    instances = []
    for instance in lev.instances:
        pvs_lists = [start for start, targets in sorted(lev.pvs_lists.items()) if instance.offset in targets]
        bsp_lists = [start for start, records in sorted(lev.bsp_hitbox_lists.items())
                     if any(target == instance.offset for _, target in records)]
        instances.append(asdict(instance) | {
            "offset_hex": _hex(instance.offset),
            "model_offset_hex": _hex(instance.model_offset),
            "total_references": instance.total_references,
            "membership": {
                "global": instance.offset in lev.global_instance_offsets,
                "pvs_lists": pvs_lists,
                "bsp_hitbox_lists": bsp_lists,
            },
        })
    model_counts = Counter(instance.model_id for instance in lev.instances)
    report: dict[str, Any] = {
        "schema": LEV_INSPECTION_SCHEMA,
        "version": LEV_INSPECTION_VERSION,
        "source": {
            "sha256": sha256(lev.raw).hexdigest(),
            "file_bytes": len(lev.raw),
            "payload_bytes": len(lev.data),
        },
        "validation": {
            "valid": lev.valid,
            "error_count": len(lev.errors),
            "warning_count": len(lev.warnings),
            "diagnostics": [asdict(item) for item in lev.diagnostics],
        },
        "layout": {
            "pointer_map_offset": lev.ptr_map_offset,
            "pointer_count": len(lev.pointer_slots),
            "level": lev.level,
            "build": {
                "start": lev._cstring(lev.level.get("build_start_offset", 0)),
                "end": lev._cstring(lev.level.get("build_end_offset", 0)),
                "type": lev._cstring(lev.level.get("build_type_offset", 0)),
            } if lev.level else {},
        },
        "graph": {
            "instance_count": len(lev.instances),
            "global_instance_count": len(lev.global_instance_offsets),
            "pvs_unique_instance_list_count": len(lev.pvs_lists),
            "bsp_unique_hitbox_list_count": len(lev.bsp_hitbox_lists),
            "unreferenced_instance_count": sum(instance.total_references == 0 for instance in lev.instances),
            "reference_counts": {
                "global": sum(instance.global_references for instance in lev.instances),
                "pvs": sum(instance.pvs_references for instance in lev.instances),
                "bsp": sum(instance.bsp_references for instance in lev.instances),
                "other": sum(instance.other_references for instance in lev.instances),
            },
            "model_counts": [{"model_id": model_id, "count": count}
                             for model_id, count in sorted(model_counts.items())],
            "pvs_lists": [{
                "offset": start,
                "offset_hex": _hex(start),
                "owner_pointer_slots": sorted(lev.pvs_list_pointer_slots.get(start, set())),
                "instance_indices": [offset_to_index.get(target) for target in targets],
                "instance_offsets": list(targets),
            } for start, targets in sorted(lev.pvs_lists.items())],
            "bsp_hitbox_lists": [{
                "offset": start,
                "offset_hex": _hex(start),
                "owner_pointer_slots": sorted(lev.bsp_list_pointer_slots.get(start, set())),
                "leaf_boxes": sorted(lev.bsp_list_leaf_boxes.get(start, [])),
                "record_count": len(records),
                "instance_indices": [offset_to_index.get(target) if target else None for _, target in records],
                "instance_offsets": [target for _, target in records],
            } for start, records in sorted(lev.bsp_hitbox_lists.items())],
        },
        "geometry": {
            "vertex_count": len(lev.vertices),
            "quadblock_count": len(lev.quadblocks),
            "vertices": list(lev.vertices),
            "quadblocks": list(lev.quadblocks),
        },
        "checkpoints": list(lev.checkpoints),
        "spawns": {
            "driver_grid": list(lev.driver_spawns),
            "groups": list(lev.spawn_groups),
        },
        "instances": instances,
    }
    if include_path:
        report["source"]["path"] = str(lev.path) if lev.path else None
    return report


def _source_status(entry: object) -> dict[str, Any]:
    if not isinstance(entry, dict):
        return {"path": None, "pinned_sha256": None, "actual_sha256": None, "exists": False, "matches": False}
    path_text = entry.get("path")
    pinned = entry.get("sha256")
    path = Path(path_text) if isinstance(path_text, str) else None
    exists = path is not None and path.is_file()
    actual = file_sha256(path) if exists else None
    return {"path": path_text, "pinned_sha256": pinned, "actual_sha256": actual,
            "exists": exists, "matches": actual is not None and actual == pinned}


def project_inspection(project: dict[str, Any]) -> dict[str, Any]:
    """Return project health and its source LEV graph without mutating the project."""
    sources = project.get("sources")
    source_entries = sources if isinstance(sources, dict) else {}
    source_status = {kind: _source_status(source_entries.get(kind)) for kind in ("lev", "vrm")}
    clean_export_status = _source_status({"path": project.get("last_clean_export_path"),
                                          "sha256": project.get("last_clean_export_sha256")})
    project_errors = validate_project(project)
    history = project.get("history") if isinstance(project.get("history"), dict) else {}
    generation = history.get("generation")
    clean_generation = history.get("clean_generation")
    dirty = (generation != clean_generation) if isinstance(generation, int) and isinstance(clean_generation, int) else None
    vanilla = project.get("vanilla_objects") if isinstance(project.get("vanilla_objects"), list) else []
    candidates = project.get("ap_candidates") if isinstance(project.get("ap_candidates"), list) else []
    vanilla_counts = Counter(entry.get("kind") for entry in vanilla if isinstance(entry, dict))
    report: dict[str, Any] = {
        "schema": PROJECT_INSPECTION_SCHEMA,
        "version": PROJECT_INSPECTION_VERSION,
        "project": {
            "schema": project.get("schema"),
            "version": project.get("version"),
            "metadata": project.get("metadata"),
            "host_slot": project.get("host_slot"),
            "valid": not project_errors,
            "errors": project_errors,
        },
        "sources": source_status,
        "state": {
            "generation": generation,
            "clean_generation": clean_generation,
            "dirty": dirty,
            "last_clean_export_sha256": project.get("last_clean_export_sha256"),
            "last_clean_export_path": project.get("last_clean_export_path"),
            "has_clean_export": isinstance(project.get("last_clean_export_sha256"), str),
        },
        "authored_objects": {
            "vanilla_total": len(vanilla),
            "vanilla_by_kind": {str(kind): count for kind, count in sorted(vanilla_counts.items(), key=lambda item: str(item[0]))},
            "ap_candidate_total": len(candidates),
        },
        "source_lev": None,
        "clean_export": clean_export_status,
    }
    lev_status = source_status["lev"]
    if lev_status["exists"]:
        report["source_lev"] = lev_inspection(LevFile.from_path(lev_status["path"]), include_path=False)
    return report


def lev_diff(left: LevFile, right: LevFile, *, limit: int = 32, include_paths: bool = True) -> dict[str, Any]:
    """Return a stable semantic and byte-level comparison of two LEV files."""
    ranges = list(contiguous_byte_differences(left.raw, right.raw))
    def instance_record(lev: LevFile, index: int) -> dict[str, object] | None:
        if index >= len(lev.instances):
            return None
        record = asdict(lev.instances[index]) | {"total_references": lev.instances[index].total_references}
        record.pop("offset")
        return record

    instance_changes = []
    for index in range(max(len(left.instances), len(right.instances))):
        before = instance_record(left, index)
        after = instance_record(right, index)
        if before != after:
            instance_changes.append({"index": index, "before": before, "after": after})

    def changed_records(before: tuple[dict[str, object], ...], after: tuple[dict[str, object], ...],
                        ignored: tuple[str, ...] = ()) -> list[dict[str, object]]:
        changes = []
        for index in range(max(len(before), len(after))):
            old = ({key: value for key, value in before[index].items() if key not in ignored} if index < len(before) else None)
            new = ({key: value for key, value in after[index].items() if key not in ignored} if index < len(after) else None)
            if old != new:
                changes.append({"index": index, "before": old, "after": new})
        return changes

    quad_ignored = ("offset", "pvs_offset", "pvs_instance_list_offset")
    record_ignored = ("offset",)
    quadblock_changes = changed_records(left.quadblocks, right.quadblocks, quad_ignored)
    checkpoint_changes = changed_records(left.checkpoints, right.checkpoints, record_ignored)
    spawn_group_changes = changed_records(left.spawn_groups, right.spawn_groups, record_ignored)

    def pvs_memberships(lev: LevFile) -> tuple[dict[str, object], ...]:
        offset_to_index = {instance.offset: instance.index for instance in lev.instances}
        return tuple({"ordinal": ordinal, "instance_indices": tuple(offset_to_index.get(target) for target in targets)}
                     for ordinal, (_, targets) in enumerate(sorted(lev.pvs_lists.items())))

    def bsp_memberships(lev: LevFile) -> tuple[dict[str, object], ...]:
        offset_to_index = {instance.offset: instance.index for instance in lev.instances}
        result = []
        for ordinal, (start, records) in enumerate(sorted(lev.bsp_hitbox_lists.items())):
            semantic_records = []
            for record, target in records:
                semantic_records.append({
                    "flags": struct.unpack_from("<H", lev.data, record)[0],
                    "bounding_box": struct.unpack_from("<hhhhhh", lev.data, record + 4),
                    "center": struct.unpack_from("<hhh", lev.data, record + 0x10),
                    "radius": struct.unpack_from("<h", lev.data, record + 0x16)[0],
                    "instance_index": offset_to_index.get(target) if target else None,
                })
            result.append({"ordinal": ordinal, "leaf_boxes": tuple(sorted(lev.bsp_list_leaf_boxes.get(start, []))),
                           "records": tuple(semantic_records)})
        return tuple(result)

    pvs_membership_changes = changed_records(pvs_memberships(left), pvs_memberships(right))
    bsp_membership_changes = changed_records(bsp_memberships(left), bsp_memberships(right))
    left_source: dict[str, Any] = {"sha256": sha256(left.raw).hexdigest(), "bytes": len(left.raw), "valid": left.valid}
    right_source: dict[str, Any] = {"sha256": sha256(right.raw).hexdigest(), "bytes": len(right.raw), "valid": right.valid}
    if include_paths:
        left_source["path"] = str(left.path) if left.path else None
        right_source["path"] = str(right.path) if right.path else None
    return {
        "schema": LEV_DIFF_SCHEMA,
        "version": LEV_DIFF_VERSION,
        "identical": not ranges,
        "left": left_source,
        "right": right_source,
        "summary": {
            "changed_range_count": len(ranges),
            "instance_change_count": len(instance_changes),
            "quadblock_change_count": len(quadblock_changes),
            "checkpoint_change_count": len(checkpoint_changes),
            "spawn_group_change_count": len(spawn_group_changes),
            "pvs_membership_change_count": len(pvs_membership_changes),
            "bsp_membership_change_count": len(bsp_membership_changes),
            "pointer_count": {"before": len(left.pointer_slots), "after": len(right.pointer_slots)},
            "pvs_list_count": {"before": len(left.pvs_lists), "after": len(right.pvs_lists)},
            "bsp_hitbox_list_count": {"before": len(left.bsp_hitbox_lists), "after": len(right.bsp_hitbox_lists)},
        },
        "changed_ranges": [{"offset": offset, "left_bytes": len(a), "right_bytes": len(b),
                            "left_hex": a[:32].hex(), "right_hex": b[:32].hex()}
                           for offset, a, b in ranges[:limit]],
        "instance_changes": instance_changes,
        "quadblock_changes": quadblock_changes,
        "checkpoint_changes": checkpoint_changes,
        "spawn_group_changes": spawn_group_changes,
        "pvs_membership_changes": pvs_membership_changes,
        "bsp_membership_changes": bsp_membership_changes,
    }


def geometry_query(lev: LevFile, point: tuple[int, int, int]) -> dict[str, Any]:
    """Query conservative offline geometry memberships at one world-space point."""
    def contains(box: object) -> bool:
        return (isinstance(box, tuple) and len(box) == 6 and box[0] <= point[0] <= box[3]
                and box[1] <= point[1] <= box[4] and box[2] <= point[2] <= box[5])

    def box_distance(box: tuple[int, ...]) -> int:
        total = 0
        for lane in range(3):
            delta = box[lane] - point[lane] if point[lane] < box[lane] else (point[lane] - box[lane + 3] if point[lane] > box[lane + 3] else 0)
            total += delta * delta
        return total

    containing_quads = [quad for quad in lev.quadblocks if contains(quad["bounding_box"])]
    nearest_quad = min(lev.quadblocks, key=lambda quad: (box_distance(quad["bounding_box"]), quad["index"]), default=None)
    containing_leaves = []
    predicted_lists: set[int] = set()
    for start, boxes in sorted(lev.bsp_list_leaf_boxes.items()):
        matching = [box for box in boxes if contains(tuple(box))]
        if matching:
            predicted_lists.add(start)
            containing_leaves.append({"hitbox_list_offset": start, "boxes": matching})

    def nearest_record(records: tuple[dict[str, object], ...]) -> dict[str, object] | None:
        if not records:
            return None
        record = min(records, key=lambda item: (sum((item["position"][lane] - point[lane]) ** 2 for lane in range(3)), item["index"]))
        return {"index": record["index"], "position": record["position"],
                "distance_squared": sum((record["position"][lane] - point[lane]) ** 2 for lane in range(3))}

    quad = containing_quads[0] if containing_quads else nearest_quad
    return {
        "schema": GEOMETRY_QUERY_SCHEMA,
        "version": GEOMETRY_QUERY_VERSION,
        "source_sha256": sha256(lev.raw).hexdigest(),
        "valid": lev.valid,
        "point": point,
        "quadblocks": {
            "containing_indices": [item["index"] for item in containing_quads],
            "nearest_index": nearest_quad["index"] if nearest_quad else None,
            "selected": quad,
        },
        "visibility": {
            "pvs_instance_list_offset": quad["pvs_instance_list_offset"] if quad else None,
            "pvs_instance_indices": next((entry["instance_indices"] for entry in lev_inspection(lev, include_path=False)["graph"]["pvs_lists"]
                                          if quad and entry["offset"] == quad["pvs_instance_list_offset"]), []),
        },
        "collision": {
            "containing_bsp_leaves": containing_leaves,
            "predicted_hitbox_list_offsets": sorted(predicted_lists),
        },
        "nearest_checkpoint": nearest_record(lev.checkpoints),
        "nearest_instance": nearest_record(tuple({"index": item.index, "position": item.position} for item in lev.instances)),
    }
