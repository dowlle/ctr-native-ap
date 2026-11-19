#!/usr/bin/env python3
"""Create a new local Wumpa World revision beside its source package."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

PINS = {
    "lev": "bbab52f2c65c9c1ad7b697e168013fd849d7fe6ae8f0911ae361999f7c37a132",
    "vrm": "27fe7959a7798c42ead15015a78c9a38f3212507a154f66bec9bc79b79c11ca9",
}
SIDECARS = {
    "recorded_ai": "recorded-ai.navset",
    "race_grid": "grid-spots.json",
    "weapon_crate_spots": "weapon-crate-spots.json",
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def canonical(manifest):
    manifest["files"].sort(key=lambda file: (file["role"], file["path"]))
    return json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def stage(source, sidecars, output):
    manifest = json.loads((source / "manifest.json").read_bytes())
    old_pin = sha256(canonical(manifest))
    if source.name != old_pin:
        raise ValueError("Source folder must be named for the verified package pin")
    files = {}
    pins = {}
    for file in manifest["files"]:
        path = PurePosixPath(file["path"])
        if path.is_absolute() or ".." in path.parts or file["role"] in SIDECARS or path in files:
            raise ValueError("Source needs a base Time Trial manifest with safe, unique relative paths")
        payload = source.joinpath(*path.parts).read_bytes()
        if len(payload) != file["bytes"] or sha256(payload) != file["sha256"]:
            raise ValueError(f"Base {file['role']} bytes do not match the manifest")
        files[path] = payload
        pins[file["role"]] = file["sha256"]
    if any(pins.get(role) != pin for role, pin in PINS.items()):
        raise ValueError("The LEV and VRM must match the Wumpa World Time Trial revision")
    for role, filename in SIDECARS.items():
        payload = (sidecars / filename).read_bytes()
        path = PurePosixPath("recorded-race") / filename
        if path in files:
            raise ValueError(f"Base package already uses {path}")
        files[path] = payload
        manifest["files"].append(dict(role=role, path=str(path), bytes=len(payload), sha256=sha256(payload)))
    body = canonical(manifest)
    destination = output / sha256(body)
    destination.mkdir(parents=False)
    for path, payload in files.items():
        target = destination.joinpath(*path.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    (destination / "manifest.json").write_bytes(body)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="installed Wumpa World Time Trial package folder")
    parser.add_argument("sidecars", type=Path, help="folder containing the navset and both placement JSON files")
    parser.add_argument("output", type=Path, help="existing local directory for the new package folder")
    args = parser.parse_args()
    try:
        destination = stage(args.source, args.sidecars, args.output)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.error(str(exc))
    print(destination)


if __name__ == "__main__":
    main()
