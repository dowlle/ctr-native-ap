"""Read retail 1P pairs and maintain independent, source-pinned authoring projects."""
from __future__ import annotations

from contextlib import contextmanager
from hashlib import sha256
import json
import os
from pathlib import Path
import struct

from .disc_assets import Disc, build_index, boot_serial_from_system_cnf, normalize_serial
from .format import LevFile
from .project import file_sha256, new_project, validate_project

TRACK_NAMES = (
    "Dingo Canyon", "Dragon Mines", "Blizzard Bluff", "Crash Cove", "Tiger Temple",
    "Papu's Pyramid", "Roo's Tubes", "Hot Air Skyway", "Sewer Speedway", "Mystery Caves",
    "Cortex Castle", "N. Gin Labs", "Polar Pass", "Oxide Station", "Coco Park",
    "Tiny Arena", "Slide Coliseum", "Turbo Track",
)


def track_info(selection: str | int) -> dict:
    for level, name in enumerate(TRACK_NAMES):
        slug = name.lower().replace("'", "").replace(".", "").replace(" ", "-")
        if str(selection).lower() in (str(level), slug, name.lower()):
            return {"level_id": level, "title": name, "slug": slug,
                    "vrm_entry": level * 8, "lev_entry": level * 8 + 1}
    raise ValueError(f"Unknown retail track {selection!r}; use a track name or LevelID 0 through 17")


class BoundedDisc(Disc):
    """Retain the established ISO reader, with bounds on every requested extent."""
    def read_extent(self, lba, size):
        if lba < 0 or not 0 <= size <= 16 * 1024 * 1024:
            raise ValueError("Invalid disc extent")
        if (lba + (size + 2047) // 2048) * self.raw > os.fstat(self.file.fileno()).st_size:
            raise ValueError("Disc extent is truncated")
        data = super().read_extent(lba, size)
        if len(data) != size:
            raise ValueError("Short disc read")
        return data


@contextmanager
def bigfile_reader(source: Path):
    if source.name.lower() == "bigfile.big":
        with source.open("rb") as stream:
            size = os.fstat(stream.fileno()).st_size
            def read(offset, count):
                if offset < 0 or count < 0 or offset + count > size:
                    raise ValueError("BIGFILE entry is out of bounds")
                stream.seek(offset)
                data = stream.read(count)
                if len(data) != count:
                    raise ValueError("Short BIGFILE read")
                return data
            yield read, size
    else:
        disc = BoundedDisc(source)
        try:
            files, _, _ = build_index(disc)
            if normalize_serial(boot_serial_from_system_cnf(disc, files) or "") != "SCUS94426":
                raise ValueError("Retail editor requires the NTSC-U CTR disc (SCUS_944.26)")
            if "/BIGFILE.BIG" not in files:
                raise ValueError("Disc has no BIGFILE.BIG")
            lba, size = files["/BIGFILE.BIG"]
            def read(offset, count):
                if offset < 0 or count < 0 or offset + count > size:
                    raise ValueError("BIGFILE entry is out of bounds")
                skip = offset % 2048
                return disc.read_extent(lba + offset // 2048, count + skip)[skip:]
            yield read, size
        finally:
            disc.close()


def validate_vrm(data: bytes) -> None:
    """Check the TIM rectangles/pixel extents consumed by LOAD_VramFileCallback."""
    def tim(offset, end):
        if offset + 20 > end:
            raise ValueError("VRM TIM header is truncated")
        magic, flags, block = struct.unpack_from("<III", data, offset)
        x, y, width, height = struct.unpack_from("<HHHH", data, offset + 12)
        if magic != 0x10 or flags != 2 or not width or not height or x + width > 1024 or y + height > 512:
            raise ValueError("Unsupported VRM TIM format or rectangle")
        if block != 12 + width * height * 2 or offset + 8 + block != end:
            raise ValueError("VRM pixel size does not match rectangle")
    if len(data) < 4:
        raise ValueError("VRM is truncated")
    if struct.unpack_from("<I", data)[0] != 0x20:
        tim(0, len(data))
        return
    cursor, count = 4, 0
    while cursor + 4 <= len(data):
        size = struct.unpack_from("<I", data, cursor)[0]
        cursor += 4
        if size == 0:
            if cursor != len(data) or not count:
                raise ValueError("VRM has empty pack or trailing data")
            return
        size &= ~3
        if size < 20 or cursor + size > len(data):
            raise ValueError("VRM packed TIM is out of bounds")
        tim(cursor, cursor + size)
        cursor += size
        count += 1
    raise ValueError("VRM pack has no terminator")


def source_file(assets: Path) -> Path:
    if not assets.is_dir():
        raise ValueError(f"Retail assets folder does not exist: {assets}")
    children = {p.name.lower(): p for p in assets.iterdir() if p.is_file()}
    # Match native_assets.c: an extracted BIGFILE takes precedence over the image.
    for name in ("bigfile.big", "ctr-u.bin"):
        if name in children:
            return children[name].resolve()
    raise ValueError(f"Expected BIGFILE.BIG or ctr-u.bin in {assets}")


def write_new(path: Path, data: bytes) -> None:
    """Exclusive creation; existing files and symlinks are never replaced."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def prepare_project(selection: str | int, assets: Path, projects: Path) -> Path:
    track = track_info(selection)
    source = source_file(assets)
    source_hash = file_sha256(source)
    pairs = {}
    with bigfile_reader(source) as (read, total):
        _, count = struct.unpack("<ii", read(0, 8))
        if not 138 <= count <= (0x4000 - 8) // 8:
            raise ValueError("Invalid retail BIGFILE entry count")
        table = read(8, count * 8)
        for kind in ("lev", "vrm"):
            sector, size = struct.unpack_from("<ii", table, track[kind + "_entry"] * 8)
            offset = sector * 2048
            if offset < 8 + count * 8 or not 0 < size <= 16 * 1024 * 1024 or offset + size > total:
                raise ValueError(f"Invalid {kind.upper()} entry bounds")
            pairs[kind] = read(offset, size)
    lev = LevFile(pairs["lev"])
    if not lev.valid:
        raise ValueError("Retail LEV failed validation: " + "; ".join(d.message for d in lev.errors))
    validate_vrm(pairs["vrm"])
    if file_sha256(source) != source_hash:
        raise ValueError("Retail source changed during extraction")
    cache = projects.resolve() / "retail-cache" / source_hash / track["slug"]
    for parent in (projects.resolve() / "retail-cache", cache.parent, cache):
        if parent.is_symlink():
            raise ValueError("Retail cache directories must not be symlinks")
    paths = {kind: cache / f"{track['slug']}-1p.{kind}" for kind in pairs}
    project_path = projects.resolve() / f"{track['slug']}-ctr-letters.editor.json"
    # Compare before any writes, including cross-track identity and source path.
    expected_sources = {kind: {"path": str(paths[kind]), "sha256": sha256(data).hexdigest()}
                        for kind, data in pairs.items()}
    existing = None
    if project_path.exists() or project_path.is_symlink():
        if project_path.is_symlink():
            raise ValueError("Project sidecar must not be a symlink")
        existing = json.loads(project_path.read_text(encoding="utf-8"))
        if (existing.get("host_slot") != track["level_id"] or existing.get("sources") != expected_sources
                or existing.get("metadata", {}).get("title") != track["title"]):
            raise ValueError("Existing project has a different track/source identity; preserved unchanged")
        errors = validate_project(existing)
        if errors:
            raise ValueError("; ".join(errors))
    for kind, path in paths.items():
        if path.exists() or path.is_symlink():
            if path.is_symlink() or file_sha256(path) != expected_sources[kind]["sha256"]:
                raise ValueError("Cached extraction hash mismatch; preserved unchanged")
    for kind, path in paths.items():
        if not path.exists():
            write_new(path, pairs[kind])
    receipt = cache / "extraction.json"
    provenance = {"source_path": str(source), "source_sha256": source_hash,
                  "track": track, "sources": expected_sources}
    if not receipt.exists():
        write_new(receipt, json_bytes(provenance))
    if existing is None:
        project = new_project(paths["lev"], paths["vrm"], track["level_id"])
        project["metadata"].update(title=track["title"], content_id=f"retail-{track['slug']}-ctr-letters")
        write_new(project_path, json_bytes(project))
    return project_path


def coordinate_record(project_path: Path) -> dict:
    project = json.loads(project_path.read_text(encoding="utf-8"))
    errors = validate_project(project)
    if errors or project.get("units") != "ctr-world-s16":
        raise ValueError("; ".join(errors) or "Expected ctr-world-s16 coordinates")
    track = track_info(project["host_slot"])
    if project["metadata"]["title"] != track["title"]:
        raise ValueError("Coordinate export requires matching retail track identity")
    return {"schema": "ctr-retail-candidate-coordinates", "version": 1,
            "track": track["title"], "level_id": track["level_id"], "units": "ctr-world-s16",
            "project_sha256": file_sha256(project_path), "sources": project["sources"],
            "ap_candidates": project["ap_candidates"]}


def export_coordinates(project_path: Path) -> Path:
    record = coordinate_record(project_path)
    destination = project_path.with_name(project_path.stem + ".coordinates-" + record["project_sha256"] + ".json")
    data = json_bytes(record)
    if destination.exists():
        if destination.read_bytes() != data:
            raise ValueError("Coordinate receipt exists with different contents")
    else:
        write_new(destination, data)
    return destination
