from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import sys
import tempfile

from .format import LevFile
from .inspection import geometry_query, lev_diff, lev_inspection, project_inspection
from .project import atomic_json_write, content_manifest, new_project, validate_project
from .writer import Placement, apply_placements, atomic_binary_write, move_local


def _emit(value: object, json_output: bool) -> None:
    print(json.dumps(value, indent=2, sort_keys=True) if json_output or not isinstance(value, str) else value)


def command_inspect(args: argparse.Namespace) -> int:
    lev = LevFile.from_path(args.lev)
    _emit(lev_inspection(lev), args.json)
    return 0 if lev.valid else 2


def command_validate(args: argparse.Namespace) -> int:
    lev = LevFile.from_path(args.lev)
    report = {"path": str(Path(args.lev)), "valid": lev.valid, "errors": [item.__dict__ for item in lev.errors],
              "warnings": [item.__dict__ for item in lev.warnings], "instances": len(lev.instances),
              "pointer_count": len(lev.pointer_slots), "pvs_unique_instance_lists": len(lev.pvs_lists),
              "bsp_unique_hitbox_lists": len(lev.bsp_hitbox_lists)}
    text = ("VALID" if lev.valid else "INVALID") + f": {args.lev}\n" + "\n".join(
        f"{item.severity.upper()} {item.code}: {item.message}" for item in lev.diagnostics)
    _emit(report if args.json else text, args.json)
    return 0 if lev.valid else 2


def command_roundtrip(args: argparse.Namespace) -> int:
    source, destination = Path(args.source), Path(args.destination)
    lev = LevFile.from_path(source)
    if not lev.valid:
        print("source LEV failed validation", file=sys.stderr)
        return 2
    if source.resolve() == destination.resolve():
        print("roundtrip destination must differ from source", file=sys.stderr)
        return 2
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=destination.name + ".", suffix=".tmp", dir=destination.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(lev.raw)
            stream.flush()
            os.fsync(stream.fileno())
        if Path(temporary_name).read_bytes() != lev.raw:
            raise OSError("temporary output failed byte-identity check")
        os.replace(temporary_name, destination)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
    print(f"byte-identical roundtrip: {destination} sha256={sha256(lev.raw).hexdigest()}")
    return 0


def command_fixup(args: argparse.Namespace) -> int:
    lev = LevFile.from_path(args.lev)
    fixups = lev.simulate_fixups(args.base)
    report = {"valid": lev.valid, "base": args.base, "fixup_count": len(fixups),
              "first_fixups": [{"slot": slot, "relative": relative, "absolute": absolute} for slot, relative, absolute in fixups[:args.limit]],
              "diagnostics": [item.__dict__ for item in lev.diagnostics]}
    _emit(report, args.json)
    return 0 if lev.valid else 2


def command_query(args: argparse.Namespace) -> int:
    lev = LevFile.from_path(args.lev)
    _emit(geometry_query(lev, tuple(args.point)), True)
    return 0 if lev.valid else 2


def command_diff(args: argparse.Namespace) -> int:
    left, right = LevFile.from_path(args.left), LevFile.from_path(args.right)
    report = lev_diff(left, right, limit=args.limit)
    _emit(report, args.json)
    return 0 if report["identical"] else 1


def command_project_init(args: argparse.Namespace) -> int:
    lev = LevFile.from_path(args.lev)
    if not lev.valid:
        print("LEV validation failed; project not created", file=sys.stderr)
        return 2
    atomic_json_write(Path(args.output), new_project(Path(args.lev), Path(args.vrm), args.host_slot))
    print(f"created project: {args.output}")
    return 0


def command_project_validate(args: argparse.Namespace) -> int:
    project = json.loads(Path(args.project).read_text(encoding="utf-8"))
    errors = validate_project(project, verify_hashes=not args.no_hashes)
    _emit({"valid": not errors, "errors": errors}, args.json)
    return 0 if not errors else 2


def command_project_inspect(args: argparse.Namespace) -> int:
    project = json.loads(Path(args.project).read_text(encoding="utf-8"))
    report = project_inspection(project)
    _emit(report, True)
    source_lev = report.get("source_lev")
    lev_valid = isinstance(source_lev, dict) and source_lev.get("validation", {}).get("valid") is True
    return 0 if report["project"]["valid"] and lev_valid else 2


def command_manifest(args: argparse.Namespace) -> int:
    project = json.loads(Path(args.project).read_text(encoding="utf-8"))
    atomic_json_write(Path(args.output), content_manifest(project))
    print(f"wrote content manifest: {args.output}")
    return 0


def command_project_export(args: argparse.Namespace) -> int:
    project_path = Path(args.project)
    project = json.loads(project_path.read_text(encoding="utf-8"))
    errors = validate_project(project)
    if errors:
        raise ValueError("invalid project: " + "; ".join(errors))
    source_path = Path(project["sources"]["lev"]["path"])
    output_path = Path(args.output)
    if source_path.resolve() == output_path.resolve():
        raise ValueError("export destination must differ from the immutable source LEV")
    placements = []
    for index, entry in enumerate(project["vanilla_objects"]):
        kind = entry.get("kind")
        position = entry.get("position")
        rotation = entry.get("rotation")
        if kind not in {"item-crate", "wumpa-crate", "wumpa-fruit"}:
            raise ValueError(f"vanilla_objects[{index}] has unsupported kind {kind!r}")
        if (not isinstance(position, list) or len(position) != 3 or not all(isinstance(value, int) for value in position)
                or not isinstance(rotation, list) or len(rotation) != 3 or not all(isinstance(value, int) for value in rotation)):
            raise ValueError(f"vanilla_objects[{index}] position and rotation must be integer triples")
        placements.append(Placement(f"ed{index:04d}{kind[0]}", kind, tuple(position), tuple(rotation)))
    raw = apply_placements(LevFile.from_path(source_path), placements)
    atomic_binary_write(output_path, raw)
    verified = LevFile.from_path(output_path)
    if not verified.valid:
        raise ValueError("written LEV failed post-write validation")
    digest = sha256(raw).hexdigest()
    project["last_clean_export_sha256"] = digest
    project["last_clean_export_path"] = str(output_path.resolve())
    history = project.setdefault("history", {})
    history["clean_generation"] = history.get("generation", 0)
    atomic_json_write(project_path, project)
    with project_path.with_suffix(project_path.suffix + ".log").open("a", encoding="utf-8") as stream:
        stream.write(f"EXPORT output={output_path} sha256={digest} instances={len(verified.instances)}\n")
    print(f"exported derived LEV: {output_path} sha256={digest} instances={len(verified.instances)}")
    return 0


def command_move_local(args: argparse.Namespace) -> int:
    source = Path(args.source)
    destination = Path(args.output)
    if source.resolve() == destination.resolve():
        raise ValueError("local move output must differ from the source LEV")
    raw = move_local(LevFile.from_path(source), args.index, tuple(args.position), tuple(args.rotation))
    atomic_binary_write(destination, raw)
    print(f"moved instance {args.index} into derived LEV: {destination} sha256={sha256(raw).hexdigest()}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="levtool", description="CTR LEV graph inspector and editor project tool")
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name, function in (("inspect", command_inspect), ("validate", command_validate)):
        command = subparsers.add_parser(name); command.add_argument("lev"); command.add_argument("--json", action="store_true"); command.set_defaults(func=function)
    command = subparsers.add_parser("roundtrip"); command.add_argument("source"); command.add_argument("destination"); command.set_defaults(func=command_roundtrip)
    command = subparsers.add_parser("diff"); command.add_argument("left"); command.add_argument("right"); command.add_argument("--limit", type=int, default=32); command.add_argument("--json", action="store_true"); command.set_defaults(func=command_diff)
    command = subparsers.add_parser("fixup-check"); command.add_argument("lev"); command.add_argument("--base", type=lambda value: int(value, 0), default=0x10000000); command.add_argument("--limit", type=int, default=16); command.add_argument("--json", action="store_true"); command.set_defaults(func=command_fixup)
    command = subparsers.add_parser("geometry-query"); command.add_argument("lev"); command.add_argument("--point", type=int, nargs=3, required=True); command.set_defaults(func=command_query)
    command = subparsers.add_parser("project-init"); command.add_argument("--lev", required=True); command.add_argument("--vrm", required=True); command.add_argument("--host-slot", type=int, required=True); command.add_argument("--output", required=True); command.set_defaults(func=command_project_init)
    command = subparsers.add_parser("project-validate"); command.add_argument("project"); command.add_argument("--no-hashes", action="store_true"); command.add_argument("--json", action="store_true"); command.set_defaults(func=command_project_validate)
    command = subparsers.add_parser("project-inspect"); command.add_argument("project"); command.set_defaults(func=command_project_inspect)
    command = subparsers.add_parser("project-export"); command.add_argument("project"); command.add_argument("--output", required=True); command.set_defaults(func=command_project_export)
    command = subparsers.add_parser("move-local"); command.add_argument("source"); command.add_argument("--index", type=int, required=True); command.add_argument("--position", type=int, nargs=3, required=True); command.add_argument("--rotation", type=int, nargs=3, default=(0, 0, 0)); command.add_argument("--output", required=True); command.set_defaults(func=command_move_local)
    command = subparsers.add_parser("manifest"); command.add_argument("project"); command.add_argument("--output", required=True); command.set_defaults(func=command_manifest)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"levtool: {error}", file=sys.stderr)
        return 2
