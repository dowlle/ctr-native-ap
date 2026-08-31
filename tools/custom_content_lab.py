#!/usr/bin/env python3
"""Project Saphi v3 catalogue, cache, verification ledger and exporters.

This is the orchestration layer around the native verifier. It never mutates a
downloaded creator file. State changes are atomic and every runtime verdict is
bound to the exact LEV and VRM SHA-256 pair reported by the native core.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

API_ROOT = "https://www.projectsaphi.com"
TRACKS_URL = f"{API_ROOT}/api/v3/tracks?per_page=100&include_standards=0&include_events=0&include_downloads=1"
MODES = ("time_trial", "relic_race", "ctr_challenge", "arcade", "crystal_challenge")
RESULTS = ("Yes", "No", "Not tested")
SCHEMA_VERSION = 1


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, ensure_ascii=False, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def load_json(path: Path, default: Any = None) -> Any:
    if not path.exists():
        return default
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def fetch_json(url: str) -> Any:
    request = urllib.request.Request(url, headers={"User-Agent": "CTR-AP-Verification-Lab/0.1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def safe_component(value: Any) -> str:
    text = str(value)
    if not text or any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for ch in text):
        raise ValueError(f"unsafe cache component: {text!r}")
    return text


def media_versions(track: dict[str, Any]) -> list[dict[str, Any]]:
    grouped: dict[str, dict[str, list[dict[str, Any]]]] = {}
    for media in track.get("downloads", []):
        if media.get("type") not in {"lev", "vrm"} or not media.get("download_url"):
            continue
        version = media.get("version")
        if not isinstance(version, str) or not version:
            continue
        grouped.setdefault(version, {"lev": [], "vrm": []})[media["type"]].append(media)
    rows = []
    for version, pair in sorted(grouped.items()):
        signatures = sorted({tuple(sorted(row.get("modes", []))) for row in pair["lev"] + pair["vrm"]})
        for signature in signatures:
            levs = [row for row in pair["lev"] if tuple(sorted(row.get("modes", []))) == signature]
            vrms = [row for row in pair["vrm"] if tuple(sorted(row.get("modes", []))) == signature]
            reasons = []
            if len(levs) != 1: reasons.append(f"expected one LEV for modes {signature!r}, found {len(levs)}")
            if len(vrms) != 1: reasons.append(f"expected one VRM for modes {signature!r}, found {len(vrms)}")
            rows.append({"version": version, "lev": levs[0] if len(levs) == 1 else None,
                         "vrm": vrms[0] if len(vrms) == 1 else None,
                         "saphi_modes": list(signature), "consistent": not reasons,
                         "inconsistency": "; ".join(reasons)})
    return rows


def catalogue_from_response(response: dict[str, Any]) -> dict[str, Any]:
    tracks = []
    for track in response.get("data", []):
        if track.get("type") not in {"race", "battle"}:
            continue
        versions = media_versions(track)
        tracks.append({key: track.get(key) for key in
                       ("id", "name", "author_id", "author", "collection_id", "collection",
                        "type_id", "type", "is_active") } | {"versions": versions})
    return {"schema_version": SCHEMA_VERSION,
            "source": TRACKS_URL,
            "fetched_at": datetime.now(timezone.utc).isoformat(),
            "tracks": tracks}


_last_download_request = 0.0


def download_media(media: dict[str, Any], target: Path) -> None:
    expected = int(media.get("file_size") or 0)
    if expected <= 0:
        raise ValueError("download has no positive file size")
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and target.stat().st_size == expected:
        return
    partial = target.with_suffix(target.suffix + ".part")
    start = partial.stat().st_size if partial.exists() else 0
    headers = {"User-Agent": "CTR-AP-Verification-Lab/0.1"}
    if start:
        headers["Range"] = f"bytes={start}-"
    url = urllib.parse.urljoin(API_ROOT, media["download_url"])
    request = urllib.request.Request(url, headers=headers)
    global _last_download_request
    for attempt in range(7):
        delay = 0.55 - (time.monotonic() - _last_download_request)
        if delay > 0: time.sleep(delay)
        try:
            _last_download_request = time.monotonic()
            with urllib.request.urlopen(request, timeout=60) as response:
                resumed = start > 0 and getattr(response, "status", None) == 206
                if start and not resumed:
                    start = 0
                mode = "ab" if resumed else "wb"
                with partial.open(mode) as stream:
                    shutil.copyfileobj(response, stream, length=128 * 1024)
                    stream.flush()
                    os.fsync(stream.fileno())
            break
        except urllib.error.HTTPError as exc:
            if exc.code != 429 or attempt == 6: raise
            retry_after = exc.headers.get("Retry-After")
            try: wait = max(1.0, float(retry_after))
            except (TypeError, ValueError): wait = min(60.0, 2.0 ** attempt)
            time.sleep(wait)
    if partial.stat().st_size != expected:
        raise IOError(f"incomplete download: got {partial.stat().st_size}, expected {expected}")
    os.replace(partial, target)


def cache_paths(root: Path, track_id: int, version: str, lev_id: int, vrm_id: int) -> tuple[Path, Path]:
    base = root / "media" / safe_component(track_id) / safe_component(version)
    return base / f"lev-{safe_component(lev_id)}.lev", base / f"vrm-{safe_component(vrm_id)}.vrm"


def exact_key(track_id: int, version: str, report: dict[str, Any]) -> str:
    material = f"{track_id}\0{version}\0{report['lev_sha256']}\0{report['vrm_sha256']}".encode()
    return hashlib.sha256(material).hexdigest()


def run_verifier(executable: Path, lev: Path, vrm: Path) -> dict[str, Any]:
    result = subprocess.run([str(executable), str(lev), str(vrm)], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"verifier exited {result.returncode}")
    return json.loads(result.stdout)


def inventory(root: Path) -> dict[str, Any]:
    return load_json(root / "inventory.json", {"schema_version": SCHEMA_VERSION, "entries": []})


def verdicts(root: Path) -> dict[str, Any]:
    return load_json(root / "verdicts.json", {"schema_version": SCHEMA_VERSION, "verdicts": {}})


def command_sync(args: argparse.Namespace) -> None:
    response = load_json(args.catalogue_file) if args.catalogue_file else fetch_json(TRACKS_URL)
    catalogue = catalogue_from_response(response)
    atomic_json(args.root / "catalogue.json", catalogue)
    print(f"catalogued {len(catalogue['tracks'])} race/battle entries")


def command_analyze(args: argparse.Namespace) -> None:
    catalogue = load_json(args.root / "catalogue.json")
    if not catalogue: raise SystemExit("run sync first")
    existing = {entry["key"]: entry for entry in inventory(args.root)["entries"] if entry.get("key")}
    entries = []
    for track in catalogue["tracks"]:
        for version in track["versions"]:
            base = {"track_id": track["id"], "name": track["name"], "author": track["author"],
                    "track_type": track["type"], "collection": track["collection"],
                    "version": version["version"], "saphi_modes": version["saphi_modes"],
                    "consistent": version["consistent"], "notes": version["inconsistency"]}
            if not version["consistent"]:
                base["status"] = "inconsistent"
                entries.append(base)
                atomic_json(args.root / "inventory.json", {"schema_version": SCHEMA_VERSION, "entries": entries})
                continue
            lev_media, vrm_media = version["lev"], version["vrm"]
            base.update({"lev_media_id": lev_media["id"], "vrm_media_id": vrm_media["id"],
                         "lev_crc32": lev_media.get("crc32_hash"), "vrm_crc32": vrm_media.get("crc32_hash")})
            lev, vrm = cache_paths(args.root, track["id"], version["version"], lev_media["id"], vrm_media["id"])
            try:
                if not args.offline:
                    download_media(lev_media, lev)
                    download_media(vrm_media, vrm)
                report = run_verifier(args.verifier, lev, vrm)
                key = exact_key(track["id"], version["version"], report)
                base.update({"key": key, "status": "verified", "lev_path": str(lev.relative_to(args.root)),
                             "vrm_path": str(vrm.relative_to(args.root)), "verification": report})
                prior = existing.get(key)
                if prior and prior.get("notes") and not base["notes"]:
                    base["notes"] = prior["notes"]
            except Exception as exc:  # retained as evidence; one bad track must not erase the sweep
                base.update({"status": "error", "notes": str(exc)})
            entries.append(base)
            atomic_json(args.root / "inventory.json", {"schema_version": SCHEMA_VERSION, "entries": entries})
    print(f"analysed {len(entries)} exact versions")


def command_queue(args: argparse.Namespace) -> None:
    ledger = verdicts(args.root)["verdicts"]
    for entry in inventory(args.root)["entries"]:
        if entry.get("status") != "verified": continue
        values = ledger.get(entry["key"], {}).get("modes", {})
        pending = [mode for mode in MODES if values.get(mode, {}).get("result", "Not tested") == "Not tested"]
        if pending:
            print(f"{entry['key']}  Saphi {entry['track_id']} {entry['name']} {entry['version']}  {','.join(pending)}")


def verified_entry(root: Path, key: str) -> dict[str, Any]:
    entry = next((row for row in inventory(root)["entries"]
                  if row.get("key") == key and row.get("status") == "verified"), None)
    if not entry: raise SystemExit("unknown or unverified exact-version key")
    return entry


def native_launch_command(root: Path, executable: Path, entry: dict[str, Any], host: int) -> list[str]:
    if host < 0 or host > 17: raise SystemExit("host must be an arcade level ID from 0 through 17")
    report = entry["verification"]
    return [str(executable.resolve()),
            "--verify-lev", str((root / entry["lev_path"]).resolve()),
            "--verify-vrm", str((root / entry["vrm_path"]).resolve()),
            "--verify-lev-sha256", report["lev_sha256"],
            "--verify-vrm-sha256", report["vrm_sha256"],
            "--verify-host", str(host), "--verify-title", entry["name"]]


def command_launch(args: argparse.Namespace) -> None:
    entry = verified_entry(args.root, args.key)
    command = native_launch_command(args.root, args.client, entry, args.host)
    print(f"Launching exact Saphi {entry['track_id']} {entry['name']} {entry['version']}.")
    print(f"Select arcade host slot {args.host}; only that slot is overridden for this process.")
    completed = subprocess.run(command, check=False)
    if completed.returncode: raise SystemExit(f"native client exited {completed.returncode}")


def command_review(args: argparse.Namespace) -> None:
    """One-at-a-time development review: browse, launch, then bind a verdict."""
    entries = []
    ledger = verdicts(args.root)["verdicts"]
    for entry in inventory(args.root)["entries"]:
        if entry.get("status") != "verified": continue
        tested = ledger.get(entry["key"], {}).get("modes", {}).get(args.mode, {}).get("result", "Not tested")
        if tested == "Not tested": entries.append(entry)
    if not entries:
        print(f"No pending {args.mode} reviews.")
        return
    for index, entry in enumerate(entries, 1):
        print(f"{index:3}  Saphi {entry['track_id']}  {entry['name']}  {entry['version']}  tags={','.join(entry['saphi_modes']) or '-'}")
    selected = int(input("Selection: "))
    if selected < 1 or selected > len(entries): raise SystemExit("selection out of range")
    entry = entries[selected - 1]
    completed = subprocess.run(native_launch_command(args.root, args.client, entry, args.host), check=False)
    if completed.returncode: raise SystemExit(f"native client exited {completed.returncode}")
    result = input("In-game result [Yes/No/Not tested]: ").strip()
    if result not in RESULTS: raise SystemExit(f"result must be one of {RESULTS}")
    notes = input("Notes (optional): ").strip()
    args.key, args.result, args.notes = entry["key"], result, notes
    args.client_build = args.client_build or args.client.name
    command_verdict(args)


def command_verdict(args: argparse.Namespace) -> None:
    if args.result not in RESULTS: raise SystemExit(f"result must be one of {RESULTS}")
    if args.mode not in MODES: raise SystemExit(f"mode must be one of {MODES}")
    entry = next((row for row in inventory(args.root)["entries"] if row.get("key") == args.key), None)
    if not entry: raise SystemExit("unknown exact-version key")
    data = verdicts(args.root)
    record = data["verdicts"].setdefault(args.key, {
        "track_id": entry["track_id"], "version": entry["version"],
        "lev_sha256": entry["verification"]["lev_sha256"],
        "vrm_sha256": entry["verification"]["vrm_sha256"], "modes": {}})
    record["modes"][args.mode] = {"result": args.result, "notes": args.notes or "",
                                   "client_build": args.client_build or "",
                                   "recorded_at": datetime.now(timezone.utc).isoformat()}
    atomic_json(args.root / "verdicts.json", data)
    print(f"recorded {args.mode}={args.result} for {args.key}")


def rows_for_export(root: Path) -> list[dict[str, Any]]:
    ledger = verdicts(root)["verdicts"]
    rows = []
    for entry in inventory(root)["entries"]:
        row = {key: entry.get(key) for key in ("track_id", "name", "author", "track_type", "collection",
                                                "version", "lev_media_id", "vrm_media_id", "lev_crc32",
                                                "vrm_crc32", "status", "notes", "key")}
        report = entry.get("verification", {})
        row.update({"lev_sha256": report.get("lev_sha256"), "vrm_sha256": report.get("vrm_sha256"),
                    "existing_saphi_tags": ", ".join(entry.get("saphi_modes", []))})
        runtime = ledger.get(entry.get("key", ""), {}).get("modes", {})
        differences = []
        for mode in MODES:
            analysis = report.get("file_analysis", {}).get(mode, {}).get("result", "Not tested")
            tested = runtime.get(mode, {}).get("result", "Not tested")
            row[f"{mode} File analysis"] = analysis
            row[f"{mode} In-game test"] = tested
            tagged = mode in entry.get("saphi_modes", [])
            if analysis == "Yes" and not tagged: differences.append(f"{mode}: file Yes, Saphi absent")
            if analysis == "No" and tagged: differences.append(f"{mode}: file No, Saphi present")
            if tested != "Not tested" and tested != analysis: differences.append(f"{mode}: file {analysis}, game {tested}")
        row["differences"] = "; ".join(differences)
        rows.append(row)
    return rows


def command_export(args: argparse.Namespace) -> None:
    rows = rows_for_export(args.root)
    output = {"schema_version": SCHEMA_VERSION, "generated_at": datetime.now(timezone.utc).isoformat(),
              "source": TRACKS_URL, "modes": list(MODES), "entries": rows}
    atomic_json(args.json, output)
    try:
        from openpyxl import Workbook
        from openpyxl.styles import Font, PatternFill
        from openpyxl.worksheet.table import Table, TableStyleInfo
    except ImportError as exc:
        raise SystemExit("openpyxl is required for spreadsheet export") from exc
    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "Verification"
    headers = list(rows[0]) if rows else ["track_id"]
    sheet.append(headers)
    for row in rows: sheet.append([row.get(header) for header in headers])
    for cell in sheet[1]:
        cell.font = Font(bold=True, color="FFFFFF")
        cell.fill = PatternFill("solid", fgColor="1F4E78")
    sheet.freeze_panes = "A2"
    sheet.auto_filter.ref = sheet.dimensions
    for column in sheet.columns:
        width = min(55, max(10, max(len(str(cell.value or "")) for cell in column) + 2))
        sheet.column_dimensions[column[0].column_letter].width = width
    if rows:
        table = Table(displayName="SaphiVerification", ref=sheet.dimensions)
        table.tableStyleInfo = TableStyleInfo(name="TableStyleMedium2", showRowStripes=True)
        sheet.add_table(table)
    args.xlsx.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{args.xlsx.stem}.", suffix=".xlsx", dir=args.xlsx.parent)
    os.close(fd)
    try:
        workbook.save(temporary_name)
        os.replace(temporary_name, args.xlsx)
    except BaseException:
        try: os.unlink(temporary_name)
        except FileNotFoundError: pass
        raise
    print(f"exported {len(rows)} rows to {args.json} and {args.xlsx}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    result.add_argument("--root", type=Path, default=Path("custom-content-lab"))
    commands = result.add_subparsers(dest="command", required=True)
    sync = commands.add_parser("sync")
    sync.add_argument("--catalogue-file", type=Path)
    sync.set_defaults(func=command_sync)
    analyze = commands.add_parser("analyze")
    analyze.add_argument("--verifier", type=Path, required=True)
    analyze.add_argument("--offline", action="store_true")
    analyze.set_defaults(func=command_analyze)
    queue = commands.add_parser("queue")
    queue.set_defaults(func=command_queue)
    launch = commands.add_parser("launch")
    launch.add_argument("key")
    launch.add_argument("--client", type=Path, required=True)
    launch.add_argument("--host", type=int, default=0)
    launch.set_defaults(func=command_launch)
    review = commands.add_parser("review")
    review.add_argument("mode", choices=MODES)
    review.add_argument("--client", type=Path, required=True)
    review.add_argument("--host", type=int, default=0)
    review.add_argument("--client-build")
    review.set_defaults(func=command_review)
    verdict = commands.add_parser("verdict")
    verdict.add_argument("key")
    verdict.add_argument("mode", choices=MODES)
    verdict.add_argument("result", choices=RESULTS)
    verdict.add_argument("--notes")
    verdict.add_argument("--client-build")
    verdict.set_defaults(func=command_verdict)
    export = commands.add_parser("export")
    export.add_argument("--json", type=Path, required=True)
    export.add_argument("--xlsx", type=Path, required=True)
    export.set_defaults(func=command_export)
    return result


def main() -> int:
    args = parser().parse_args()
    args.root = args.root.resolve()
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
