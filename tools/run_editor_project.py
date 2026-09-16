#!/usr/bin/env python3
"""Validate and launch one CTR Native Editor project."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from levtool.project import validate_project
from levtool.retail import TRACK_NAMES, prepare_project, export_coordinates
from levtool.disc_assets import ExtractError


KIND_NAMES = {"item-crate", "wumpa-crate", "wumpa-fruit", "ap-candidate"}


def _triple(value: object, label: str) -> list[int]:
    if not isinstance(value, list) or len(value) != 3 or not all(isinstance(item, int) for item in value):
        raise ValueError(f"{label} must be an array of three integers")
    return value


def _numeric_id(text: str) -> int:
    suffix = text.rsplit("-", 1)[-1]
    try:
        return int(suffix, 10)
    except ValueError as error:
        raise ValueError(f"object id {text!r} must end in a decimal number") from error


EDITOR_RELOAD_EXIT = 75
EDITOR_ROLLBACK_EXIT = 76


def choose_retail_track() -> str | None:
    import tkinter as tk
    from tkinter import ttk
    root = tk.Tk()
    root.title("CTR Native Editor")
    root.resizable(False, False)
    choice = tk.StringVar(value="Slide Coliseum")
    result = []
    frame = ttk.Frame(root, padding=24)
    frame.pack()
    ttk.Label(frame, text="Choose a retail track", font=("Segoe UI", 16)).pack(anchor="w")
    ttk.Label(frame, text="Each track keeps its own saved candidates and camera.").pack(pady=(8, 16))
    box = ttk.Combobox(frame, values=["Slide Coliseum", "Turbo Track"] + list(TRACK_NAMES[:16]),
                       textvariable=choice, state="readonly", width=32)
    box.pack(fill="x")
    def accept(event=None):
        result.append(choice.get())
        root.destroy()
    ttk.Button(frame, text="Open track", command=accept).pack(pady=(18, 0), fill="x")
    root.bind("<Return>", accept)
    root.bind("<Escape>", lambda event: root.destroy())
    box.focus_set()
    root.mainloop()
    return result[0] if result else None


def generation_output_path(base: Path, generation: int) -> Path:
    suffix = base.suffix or ".lev"
    candidate = base.with_name(f"{base.stem}-g{generation:04d}{suffix}")
    revision = 2
    while candidate.exists():
        candidate = base.with_name(f"{base.stem}-g{generation:04d}-r{revision}{suffix}")
        revision += 1
    return candidate


def run_editor_loop(binary: Path, project_path: Path, hot_reload_output: Path | None,
                    run_process=subprocess.run, export_process=subprocess.run) -> int:
    active_levs: list[tuple[Path, str] | None] = [None]
    while True:
        project = json.loads(project_path.read_text(encoding="utf-8"))
        errors = validate_project(project)
        if errors:
            raise ValueError("; ".join(errors))
        command = build_command(binary, project_path, project, active_levs[-1], hot_reload_output is not None)
        return_code = run_process(command, check=False).returncode
        if return_code == EDITOR_ROLLBACK_EXIT and hot_reload_output is not None:
            if len(active_levs) > 1:
                active_levs.pop()
            continue
        if return_code != EDITOR_RELOAD_EXIT or hot_reload_output is None:
            return return_code
        history = project.get("history", {})
        generation = history.get("generation", 0) if isinstance(history, dict) else 0
        destination = generation_output_path(hot_reload_output, int(generation))
        export = export_process([sys.executable, "-m", "levtool", "project-export", str(project_path),
                                 "--output", str(destination)], cwd=Path(__file__).resolve().parents[1], check=False)
        if export.returncode != 0:
            return export.returncode
        project = json.loads(project_path.read_text(encoding="utf-8"))
        digest = project.get("last_clean_export_sha256")
        if not isinstance(digest, str):
            raise ValueError("validated export did not record its SHA-256")
        active_levs.append((destination.resolve(), digest))


def build_command(binary: Path, project_path: Path, project: dict[str, object],
                  lev_override: tuple[Path, str] | None = None, hot_reload_enabled: bool = False) -> list[str]:
    sources = project["sources"]
    assert isinstance(sources, dict)
    lev = sources["lev"]
    vrm = sources["vrm"]
    assert isinstance(lev, dict) and isinstance(vrm, dict)
    active_lev_path = lev_override[0] if lev_override is not None else Path(str(lev["path"]))
    active_lev_hash = lev_override[1] if lev_override is not None else str(lev["sha256"])
    command = [str(binary.resolve()), "--editor-lev", str(active_lev_path), "--editor-vrm", str(vrm["path"]),
               "--editor-lev-sha256", active_lev_hash, "--editor-vrm-sha256", str(vrm["sha256"]),
               "--editor-source-lev", str(lev["path"]), "--editor-source-lev-sha256", str(lev["sha256"]),
               "--editor-host-slot", str(project["host_slot"]), "--editor-sidecar", str(project_path.resolve()),
               "--editor-log", str(project_path.with_suffix(project_path.suffix + ".log").resolve())]
    command.append("--editor-validation-ok")
    if hot_reload_enabled:
        command.append("--editor-hot-reload")
    metadata = project.get("metadata", {})
    if isinstance(metadata, dict):
        command.extend(["--editor-content-id", str(metadata.get("content_id", "unconfigured")),
                        "--editor-content-title", str(metadata.get("title", "Untitled track")),
                        "--editor-content-creator", str(metadata.get("creator", "")),
                        "--editor-content-version", str(metadata.get("content_version", "0.1.0")),
                        "--editor-minimum-version", str(metadata.get("minimum_editor_version", "0.1.0-editor-dev"))])
    camera = project.get("camera", {})
    if not isinstance(camera, dict):
        raise ValueError("camera must be an object")
    position = _triple(camera.get("position"), "camera.position")
    rotation = _triple(camera.get("rotation"), "camera.rotation")
    speed = camera.get("speed")
    if not isinstance(speed, int) or speed <= 0:
        raise ValueError("camera.speed must be a positive integer")
    command.extend(["--editor-camera", ",".join(map(str, position + rotation + [speed]))])
    history = project.get("history", {})
    if isinstance(history, dict):
        command.extend(["--editor-generation", str(history.get("generation", 0)),
                        "--editor-clean-generation", str(history.get("clean_generation", 0))])
    last_export = project.get("last_clean_export_sha256")
    if isinstance(last_export, str):
        command.extend(["--editor-last-export-sha256", last_export])
    last_export_path = project.get("last_clean_export_path")
    if isinstance(last_export_path, str):
        command.extend(["--editor-last-export-path", last_export_path])
    objects: list[tuple[int, str, list[int], list[int]]] = []
    for entry in project.get("vanilla_objects", []):
        if not isinstance(entry, dict):
            raise ValueError("vanilla object entry must be an object")
        kind = entry.get("kind")
        if kind not in KIND_NAMES - {"ap-candidate"}:
            raise ValueError(f"unsupported vanilla object kind {kind!r}")
        objects.append((_numeric_id(str(entry["id"])), str(kind), _triple(entry.get("position"), "object.position"),
                        _triple(entry.get("rotation"), "object.rotation")))
    for entry in project.get("ap_candidates", []):
        if not isinstance(entry, dict):
            raise ValueError("AP candidate entry must be an object")
        objects.append((_numeric_id(str(entry["id"])), "ap-candidate", _triple(entry.get("position"), "candidate.position"),
                        _triple(entry.get("rotation"), "candidate.rotation")))
    for object_id, kind, position, rotation in objects:
        command.extend(["--editor-object", ",".join(map(str, [object_id, kind, *position, *rotation]))])
    return command


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate and launch a CTR Native Editor project")
    parser.add_argument("project", type=Path, nargs="?")
    parser.add_argument("--binary", type=Path, default=Path("build-editor/ctr_native_editor"))
    parser.add_argument("--retail-track", help="retail name or LevelID; omitted project opens the chooser")
    parser.add_argument("--assets", type=Path, help="defaults to assets beside the binary")
    parser.add_argument("--projects", type=Path, help="defaults to the folder beside the binary")
    parser.add_argument("--coordinates-only", action="store_true", help="export coordinates without launching")
    parser.add_argument("--print-command", action="store_true", help="validate and print arguments without launching")
    parser.add_argument("--export-derived", type=Path,
                        help="after a clean editor exit, export vanilla placements to this new LEV path")
    parser.add_argument("--hot-reload-output", type=Path,
                        help="enable H reload and Shift H rollback using generation-stamped derived LEVs")
    args = parser.parse_args(argv)
    graphical = args.project is None and args.retail_track is None and not args.print_command
    try:
        retail = args.project is None
        if args.project is not None and args.retail_track is not None:
            raise ValueError("Choose either an existing project or a retail track")
        if retail:
            selection = args.retail_track
            if selection is None:
                if args.print_command or args.coordinates_only:
                    raise ValueError("Specify --retail-track for a headless command")
                selection = choose_retail_track()
                if selection is None:
                    return 0
            root = args.binary.resolve().parent
            args.project = prepare_project(selection, args.assets or root / "assets", args.projects or root)
        project = json.loads(args.project.read_text(encoding="utf-8"))
        errors = validate_project(project)
        if errors:
            raise ValueError("; ".join(errors))
        if args.coordinates_only:
            print(export_coordinates(args.project))
            return 0
        if not args.binary.is_file():
            raise ValueError(f"editor binary does not exist: {args.binary}")
        command = build_command(args.binary, args.project, project, hot_reload_enabled=args.hot_reload_output is not None)
        if args.print_command:
            print(json.dumps(command, indent=2))
            return 0
        return_code = run_editor_loop(args.binary, args.project, args.hot_reload_output)
        if retail:
            print(export_coordinates(args.project))
        if return_code == 0 and args.export_derived is not None:
            export = subprocess.run([sys.executable, "-m", "levtool", "project-export", str(args.project),
                                     "--output", str(args.export_derived)], cwd=Path(__file__).resolve().parents[1], check=False)
            return export.returncode
        return return_code
    except (OSError, ValueError, RuntimeError, ImportError, ExtractError) as error:
        print(f"run_editor_project: {error}", file=sys.stderr)
        if graphical:
            from tkinter import messagebox
            messagebox.showerror("CTR Native Editor", str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
