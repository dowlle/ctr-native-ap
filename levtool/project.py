"""Versioned editor project and AP candidate manifest helpers."""

from __future__ import annotations

from hashlib import sha256
import json
from pathlib import Path
from typing import Any

PROJECT_SCHEMA = "ctr-native-editor-project"
PROJECT_VERSION = 1
MANIFEST_SCHEMA = "ctr-native-content-manifest"
MANIFEST_VERSION = 1


def file_sha256(path: Path) -> str:
    digest = sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def new_project(lev: Path, vrm: Path, host_slot: int) -> dict[str, Any]:
    if not 0 <= host_slot < 18:
        raise ValueError("host slot must be an arcade track levelID from 0 through 17")
    for source in (lev, vrm):
        if not source.is_file():
            raise ValueError(f"source file does not exist: {source}")
    lev_hash = file_sha256(lev)
    return {
        "schema": PROJECT_SCHEMA, "version": PROJECT_VERSION,
        "metadata": {"content_id": f"ctrtrack-{lev_hash[:16]}", "title": lev.stem, "creator": "",
                     "content_version": "0.1.0", "minimum_editor_version": "0.1.0-editor-dev"},
        "sources": {"lev": {"path": str(lev.resolve()), "sha256": lev_hash},
                    "vrm": {"path": str(vrm.resolve()), "sha256": file_sha256(vrm)}},
        "host_slot": host_slot, "units": "ctr-world-s16",
        "camera": {"position": [0, 0, 0], "rotation": [0, 0, 0], "speed": 32},
        "vanilla_objects": [], "ap_candidates": [],
        "history": {"generation": 0, "clean_generation": 0},
        "last_clean_export_sha256": None,
        "last_clean_export_path": None,
    }


def validate_project(project: dict[str, Any], verify_hashes: bool = True) -> list[str]:
    errors: list[str] = []
    if project.get("schema") != PROJECT_SCHEMA:
        errors.append(f"schema must be {PROJECT_SCHEMA!r}")
    if project.get("version") != PROJECT_VERSION:
        errors.append(f"unsupported project version {project.get('version')!r}")
    host_slot = project.get("host_slot")
    if not isinstance(host_slot, int) or not 0 <= host_slot < 18:
        errors.append("host_slot must be an integer from 0 through 17")
    metadata = project.get("metadata")
    if not isinstance(metadata, dict):
        errors.append("metadata must be an object")
    else:
        for key in ("content_id", "title", "creator", "content_version", "minimum_editor_version"):
            if not isinstance(metadata.get(key), str):
                errors.append(f"metadata.{key} must be a string")
    sources = project.get("sources")
    if not isinstance(sources, dict):
        errors.append("sources must be an object")
    else:
        for key in ("lev", "vrm"):
            entry = sources.get(key)
            if not isinstance(entry, dict) or not isinstance(entry.get("path"), str) or not isinstance(entry.get("sha256"), str):
                errors.append(f"sources.{key} must contain path and sha256 strings")
                continue
            if len(entry["sha256"]) != 64 or any(character not in "0123456789abcdef" for character in entry["sha256"]):
                errors.append(f"sources.{key}.sha256 must be 64 lowercase hexadecimal characters")
            if verify_hashes:
                path = Path(entry["path"])
                if not path.is_file():
                    errors.append(f"sources.{key}.path does not exist")
                elif file_sha256(path) != entry["sha256"]:
                    errors.append(f"sources.{key} hash no longer matches")
    all_ids: set[str] = set()
    for collection in ("vanilla_objects", "ap_candidates"):
        values = project.get(collection)
        if not isinstance(values, list):
            errors.append(f"{collection} must be an array")
            continue
        ids: set[str] = set()
        for index, value in enumerate(values):
            if not isinstance(value, dict) or not isinstance(value.get("id"), str):
                errors.append(f"{collection}[{index}] must have a string id")
                continue
            if value["id"] in ids:
                errors.append(f"duplicate {collection} id {value['id']!r}")
            ids.add(value["id"])
            if value["id"] in all_ids:
                errors.append(f"object id {value['id']!r} is not globally unique")
            all_ids.add(value["id"])
            for field in ("position", "rotation"):
                triple = value.get(field)
                if not isinstance(triple, list) or len(triple) != 3 or not all(isinstance(item, int) and -32768 <= item <= 32767 for item in triple):
                    errors.append(f"{collection}[{index}].{field} must be a signed 16-bit integer triple")
            if collection == "vanilla_objects" and value.get("kind") not in {"item-crate", "wumpa-crate", "wumpa-fruit"}:
                errors.append(f"vanilla_objects[{index}].kind is unsupported")
    ordinals = [entry.get("ordinal") for entry in project.get("ap_candidates", []) if isinstance(entry, dict)]
    if ordinals and ordinals != list(range(len(ordinals))):
        errors.append("AP candidate ordinals must be contiguous and match list order")
    camera = project.get("camera")
    if not isinstance(camera, dict):
        errors.append("camera must be an object")
    else:
        for field in ("position", "rotation"):
            triple = camera.get(field)
            if not isinstance(triple, list) or len(triple) != 3 or not all(isinstance(item, int) and -32768 <= item <= 32767 for item in triple):
                errors.append(f"camera.{field} must be a signed 16-bit integer triple")
        if not isinstance(camera.get("speed"), int) or camera["speed"] <= 0:
            errors.append("camera.speed must be a positive integer")
    history = project.get("history")
    if not isinstance(history, dict) or not isinstance(history.get("generation"), int) or not isinstance(history.get("clean_generation"), int):
        errors.append("history must contain integer generation and clean_generation")
    elif not 0 <= history["clean_generation"] <= history["generation"]:
        errors.append("history generations must satisfy 0 <= clean_generation <= generation")
    export_hash = project.get("last_clean_export_sha256")
    if export_hash is not None and (not isinstance(export_hash, str) or len(export_hash) != 64
                                    or any(character not in "0123456789abcdef" for character in export_hash)):
        errors.append("last_clean_export_sha256 must be null or 64 lowercase hexadecimal characters")
    export_path = project.get("last_clean_export_path")
    if export_path is not None and not isinstance(export_path, str):
        errors.append("last_clean_export_path must be null or a string")
    if export_path is not None and export_hash is None:
        errors.append("last_clean_export_path requires last_clean_export_sha256")
    if verify_hashes and isinstance(export_path, str) and isinstance(export_hash, str):
        path = Path(export_path)
        if not path.is_file():
            errors.append("last_clean_export_path does not exist")
        elif file_sha256(path) != export_hash:
            errors.append("last_clean_export_path hash no longer matches")
    return errors


def content_manifest(project: dict[str, Any]) -> dict[str, Any]:
    errors = validate_project(project)
    if errors:
        raise ValueError("invalid project: " + "; ".join(errors))
    counts = {kind: sum(entry.get("kind") == kind for entry in project["vanilla_objects"])
              for kind in ("item-crate", "wumpa-crate", "wumpa-fruit")}
    return {
        "schema": MANIFEST_SCHEMA, "version": MANIFEST_VERSION,
        "content": project["metadata"],
        "track": {"lev_sha256": project["sources"]["lev"]["sha256"], "vrm_sha256": project["sources"]["vrm"]["sha256"],
                  "host_slot": project["host_slot"]},
        "derived_lev_sha256": project.get("last_clean_export_sha256"),
        "derived_lev_path": project.get("last_clean_export_path"),
        "capabilities": {"vanilla_lev_writeback": project.get("last_clean_export_sha256") is not None,
                         "ap_candidate_sidecar": True},
        "measured_counts": {"vanilla": counts, "ap_candidates": len(project["ap_candidates"])},
        "ap_candidates": project["ap_candidates"],
    }


def atomic_json_write(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)
