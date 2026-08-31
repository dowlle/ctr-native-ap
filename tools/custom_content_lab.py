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
import re
import shutil
import secrets
import subprocess
import sys
import tempfile
import time
import zlib
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

API_ROOT = "https://www.projectsaphi.com"
TRACKS_URL = f"{API_ROOT}/api/v3/tracks?per_page=100&include_standards=0&include_events=0&include_downloads=1"
MODES = ("time_trial", "relic_race", "ctr_challenge", "arcade", "crystal_challenge")
RESULTS = ("Yes", "No", "Not tested")
STATIC_RESULTS = ("Detected", "Not detected", "Indeterminate", "Error")
SCHEMA_VERSION = 2
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_TEXT = 512
MAX_URL = 2048
MAX_PAGE_BYTES = 16 * 1024 * 1024
MAX_PAGES = 1000
MAX_TRACKS = 100000
MAX_VERSIONS = 100


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
        directory = os.open(path.parent, os.O_RDONLY)
        try: os.fsync(directory)
        finally: os.close(directory)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def load_json(path: Path, default: Any = None) -> Any:
    if not path.exists():
        return default
    if path.stat().st_size > MAX_JSON_BYTES:
        raise ValueError(f"JSON exceeds {MAX_JSON_BYTES} byte limit: {path}")
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def fetch_json(url: str) -> Any:
    request = urllib.request.Request(url, headers={"User-Agent": "CTR-AP-Verification-Lab/0.1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        raw = response.read(MAX_PAGE_BYTES + 1)
        if len(raw) > MAX_PAGE_BYTES: raise ValueError("Saphi response exceeds page limit")
        return json.loads(raw)


def positive_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{field} must be a positive integer")
    return value


def bounded_text(value: Any, field: str) -> str:
    if not isinstance(value, str) or len(value) > MAX_TEXT or "\x00" in value:
        raise ValueError(f"{field} must be a string of at most {MAX_TEXT} characters")
    return value


def normalized_crc32(value: Any) -> str:
    if isinstance(value, bool): raise ValueError("CRC32 must be hexadecimal text or integer")
    if isinstance(value, int):
        if value < 0 or value > 0xffffffff: raise ValueError("CRC32 integer out of range")
        return f"{value:08x}"
    if isinstance(value, str):
        text = value.lower().removeprefix("0x")
        if len(text) == 8 and all(ch in "0123456789abcdef" for ch in text): return text
    raise ValueError("CRC32 must contain exactly eight hexadecimal digits")


def saphi_url(value: Any) -> str:
    value = bounded_text(value, "download_url")
    if len(value) > MAX_URL: raise ValueError("download_url exceeds limit")
    resolved = urllib.parse.urljoin(API_ROOT + "/", value)
    parsed = urllib.parse.urlsplit(resolved)
    expected = urllib.parse.urlsplit(API_ROOT)
    if parsed.scheme != "https" or parsed.hostname != expected.hostname or parsed.port not in (None, 443) or parsed.username or parsed.password:
        raise ValueError("download_url must stay on the configured Project Saphi HTTPS origin")
    return resolved


def media_versions(track: dict[str, Any]) -> list[dict[str, Any]]:
    grouped: dict[str, dict[str, list[dict[str, Any]]]] = {}
    downloads = track.get("downloads", [])
    if not isinstance(downloads, list) or len(downloads) > MAX_VERSIONS * 2:
        raise ValueError("downloads must be a bounded list")
    seen_media: set[int] = set()
    for media in downloads:
        if not isinstance(media, dict): raise ValueError("download must be an object")
        if media.get("type") not in {"lev", "vrm"} or not media.get("download_url"):
            continue
        mid = positive_int(media.get("id"), "media.id")
        if mid in seen_media: raise ValueError(f"duplicate media id {mid}")
        seen_media.add(mid)
        version = bounded_text(media.get("version"), "media.version")
        if not version: raise ValueError("media.version must not be empty")
        modes = media.get("modes", [])
        if not isinstance(modes, list) or any(mode not in MODES for mode in modes):
            raise ValueError("media.modes contains an unsupported tag")
        normalized = dict(media)
        normalized.update({"id": mid, "file_size": positive_int(media.get("file_size"), "media.file_size"),
                           "crc32_hash": normalized_crc32(media.get("crc32_hash")),
                           "download_url": saphi_url(media["download_url"]), "version": version,
                           "modes": list(dict.fromkeys(modes))})
        grouped.setdefault(version, {"lev": [], "vrm": []})[media["type"]].append(normalized)
    if len(grouped) > MAX_VERSIONS: raise ValueError("track has too many versions")
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
    if not isinstance(response, dict) or not isinstance(response.get("data"), list):
        raise ValueError("Saphi response must contain a data list")
    tracks = []
    seen_tracks: set[int] = set()
    seen_media_global: set[int] = set()
    for track in response.get("data", []):
        if not isinstance(track, dict): raise ValueError("track must be an object")
        if track.get("type") not in {"race", "battle"}:
            continue
        tid = positive_int(track.get("id"), "track.id")
        if tid in seen_tracks: raise ValueError(f"duplicate track id {tid}")
        seen_tracks.add(tid)
        versions = media_versions(track)
        for version in versions:
            for role in ("lev", "vrm"):
                media = version.get(role)
                if not media: continue
                if media["id"] in seen_media_global: raise ValueError(f"duplicate global media id {media['id']}")
                seen_media_global.add(media["id"])
        tracks.append({"id": tid, "name": bounded_text(track.get("name"), "track.name"),
                       "author_id": positive_int(track.get("author_id"), "track.author_id"),
                       "author": bounded_text(track.get("author"), "track.author"),
                       "collection_id": positive_int(track.get("collection_id"), "track.collection_id"),
                       "collection": bounded_text(track.get("collection"), "track.collection"),
                       "type_id": positive_int(track.get("type_id"), "track.type_id"),
                       "type": track["type"], "is_active": bool(track.get("is_active")),
                       "versions": versions})
        if len(tracks) > MAX_TRACKS: raise ValueError("normalized track limit exceeded")
    return {"schema_version": SCHEMA_VERSION,
            "source": TRACKS_URL,
            "fetched_at": datetime.now(timezone.utc).isoformat(),
            "tracks": tracks}


_last_download_request = 0.0


def file_crc32(path: Path) -> str:
    value = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(128 * 1024), b""):
            value = zlib.crc32(chunk, value)
    return f"{value & 0xffffffff:08x}"


def quarantine(path: Path, reason: str) -> None:
    if path.exists():
        stamp = int(time.time_ns())
        path.replace(path.with_name(f"{path.name}.quarantine-{stamp}-{reason}"))


def valid_cached_media(media: dict[str, Any], target: Path) -> bool:
    try:
        return (target.is_file() and not target.is_symlink() and
                target.stat().st_size == media["file_size"] and
                file_crc32(target) == media["crc32_hash"])
    except OSError:
        return False


def download_media(media: dict[str, Any], target: Path) -> None:
    expected = positive_int(media.get("file_size"), "media.file_size")
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and valid_cached_media(media, target):
        return
    if target.exists(): quarantine(target, "integrity")
    partial = target.with_suffix(target.suffix + ".part")
    start = partial.stat().st_size if partial.exists() else 0
    if start > expected:
        quarantine(partial, "oversized")
        start = 0
    headers = {"User-Agent": "CTR-AP-Verification-Lab/0.1"}
    if start:
        headers["Range"] = f"bytes={start}-"
    url = saphi_url(media["download_url"])
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
    if file_crc32(partial) != normalized_crc32(media.get("crc32_hash")):
        quarantine(partial, "crc32")
        raise IOError("downloaded media CRC32 does not match Saphi")
    os.replace(partial, target)


def cache_paths(root: Path, track_id: int, version: str, lev_id: int, vrm_id: int) -> tuple[Path, Path]:
    del version  # public semantic versions are metadata, never path components
    root = root.resolve()
    base = root / "media" / str(positive_int(track_id, "track_id"))
    lev = base / f"lev-{positive_int(lev_id, 'lev_media_id')}.lev"
    vrm = base / f"vrm-{positive_int(vrm_id, 'vrm_media_id')}.vrm"
    for candidate in (lev, vrm):
        if root not in candidate.resolve(strict=False).parents:
            raise ValueError("cache path escapes configured root")
        cursor = candidate.parent
        while cursor != root:
            if cursor.exists() and cursor.is_symlink(): raise ValueError("symlinked cache path refused")
            cursor = cursor.parent
    return lev, vrm


def identity_hash(parts: list[str]) -> str:
    if any(not isinstance(part, str) or "\x00" in part for part in parts):
        raise ValueError("identity fields must be canonical NUL-free strings")
    return hashlib.sha256("\0".join(parts).encode("utf-8")).hexdigest()


def content_id(report: dict[str, Any]) -> str:
    return identity_hash([report["lev_sha256"], report["vrm_sha256"]])


def variant_id(track_id: int, version: str, lev_id: int, vrm_id: int) -> str:
    return identity_hash([str(positive_int(track_id, "track_id")), bounded_text(version, "version"),
                          str(positive_int(lev_id, "lev_media_id")), str(positive_int(vrm_id, "vrm_media_id"))])


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
    if args.catalogue_file:
        response = load_json(args.catalogue_file)
    else:
        response = None
        combined: list[Any] = []
        url = TRACKS_URL
        seen_urls: set[str] = set()
        for _ in range(MAX_PAGES):
            if url in seen_urls: raise ValueError("Saphi pagination loop detected")
            seen_urls.add(url)
            page = fetch_json(url)
            if not isinstance(page, dict) or not isinstance(page.get("data"), list):
                raise ValueError("invalid Saphi page")
            combined.extend(page["data"])
            if len(combined) > MAX_TRACKS: raise ValueError("Saphi total track limit exceeded")
            next_url = page.get("links", {}).get("next") if isinstance(page.get("links"), dict) else None
            if next_url:
                url = saphi_url(next_url)
                continue
            meta = page.get("meta", {}) if isinstance(page.get("meta"), dict) else {}
            current, last = meta.get("current_page"), meta.get("last_page")
            if isinstance(current, int) and isinstance(last, int) and current < last:
                parsed = urllib.parse.urlsplit(url)
                query = urllib.parse.parse_qs(parsed.query)
                query["page"] = [str(current + 1)]
                url = urllib.parse.urlunsplit(parsed._replace(query=urllib.parse.urlencode(query, doseq=True)))
                continue
            response = dict(page, data=combined)
            break
        else: raise ValueError("Saphi page limit exceeded")
    catalogue = catalogue_from_response(response)
    atomic_json(args.root / "catalogue.json", catalogue)
    print(f"catalogued {len(catalogue['tracks'])} race/battle entries")


def command_analyze(args: argparse.Namespace) -> None:
    catalogue = load_json(args.root / "catalogue.json")
    if not catalogue: raise SystemExit("run sync first")
    existing = {entry["variant_id"]: entry for entry in inventory(args.root)["entries"] if entry.get("variant_id")}
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
                vid = variant_id(track["id"], version["version"], lev_media["id"], vrm_media["id"])
                base.update({"variant_id": vid, "content_id": content_id(report), "status": "verified", "lev_path": str(lev.relative_to(args.root)),
                             "vrm_path": str(vrm.relative_to(args.root)), "verification": report})
                prior = existing.get(vid)
                if prior and prior.get("notes") and not base["notes"]:
                    base["notes"] = prior["notes"]
            except Exception as exc:  # retained as evidence; one bad track must not erase the sweep
                base.update({"status": "error", "notes": str(exc)})
            entries.append(base)
            atomic_json(args.root / "inventory.json", {"schema_version": SCHEMA_VERSION, "entries": entries})
    print(f"analysed {len(entries)} exact versions")


def command_queue(args: argparse.Namespace) -> None:
    ledger = verdicts(args.root)["verdicts"]
    rows = []
    for entry in inventory(args.root)["entries"]:
        if entry.get("status") != "verified": continue
        if args.author and entry.get("author") != args.author: continue
        if args.track_type and entry.get("track_type") != args.track_type: continue
        if args.collection and entry.get("collection") != args.collection: continue
        if args.mode and args.mode not in entry.get("saphi_modes", []): continue
        values = ledger.get(entry["variant_id"], {}).get("modes", {})
        pending = [mode for mode in MODES if values.get(mode, {}).get("result", "Not tested") == "Not tested"]
        if args.pending and not pending: continue
        report = entry.get("verification", {}).get("file_analysis", {})
        differences = [mode for mode in MODES if
                       (report.get(mode, {}).get("result") == "Detected") != (mode in entry.get("saphi_modes", []))]
        if args.difference and not differences: continue
        rows.append(dict(entry, pending=pending, differences=differences))

    def version_key(value: Any) -> tuple[Any, ...]:
        match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(?:[-+]([0-9A-Za-z.-]+))?", str(value))
        return (0, *(int(match.group(i)) for i in range(1, 4)), match.group(4) or "") if match else (1, str(value))
    rows.sort(key=lambda row: (row["track_id"], version_key(row["version"]),
                               row["lev_media_id"], row["vrm_media_id"], row["variant_id"]))
    if args.newest_only:
        newest = {}
        for row in rows: newest[row["track_id"]] = row
        rows = sorted(newest.values(), key=lambda row: (row["track_id"], row["variant_id"]))
    start = args.cursor
    page = rows[start:start + args.page_size]
    if args.json:
        print(json.dumps({"rows": page, "next_cursor": start + len(page) if start + len(page) < len(rows) else None}, sort_keys=True))
    else:
        for entry in page:
            print(f"{entry['variant_id'][:12]}  Saphi {entry['track_id']} media {entry['lev_media_id']}/{entry['vrm_media_id']} {entry['name']} {entry['version']}  {','.join(entry['pending'])}")


def verified_entry(root: Path, key: str) -> dict[str, Any]:
    entry = next((row for row in inventory(root)["entries"]
                  if row.get("variant_id") == key and row.get("status") == "verified"), None)
    if not entry: raise SystemExit("unknown or unverified exact-version key")
    return entry


def command_prepare_session(args: argparse.Namespace) -> None:
    entry = verified_entry(args.root, args.variant_id)
    if entry["track_type"] != "race": raise SystemExit("battle runtime review is Unsupported")
    if args.mode == "crystal_challenge": raise SystemExit("crystal_challenge battle review is Unsupported")
    if args.host < 0 or args.host > 17: raise SystemExit("host must be an arcade level ID from 0 through 17")
    report = entry["verification"]
    manifest = {"schema_version": SCHEMA_VERSION, "nonce": secrets.token_hex(32),
                "variant_id": entry["variant_id"], "content_id": entry["content_id"],
                "track_id": entry["track_id"], "version": entry["version"],
                "lev_media_id": entry["lev_media_id"], "vrm_media_id": entry["vrm_media_id"],
                "lev_path": str((args.root / entry["lev_path"]).resolve()),
                "vrm_path": str((args.root / entry["vrm_path"]).resolve()),
                "lev_sha256": report["lev_sha256"], "vrm_sha256": report["vrm_sha256"],
                "lev_bytes": report["lev_bytes"], "vrm_bytes": report["vrm_bytes"],
                "host": args.host, "mode": args.mode,
                "created_at": datetime.now(timezone.utc).isoformat(),
                "expires_at": (datetime.now(timezone.utc) + timedelta(minutes=args.minutes)).isoformat()}
    atomic_json(args.manifest, manifest)
    print(f"Prepared one-use Steam review session {manifest['nonce']} at {args.manifest}.")
    print("Start the configured development build through Steam. The lab did not launch CTR.")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(128 * 1024), b""): digest.update(chunk)
    return digest.hexdigest()


def command_collect_session(args: argparse.Namespace) -> None:
    if not args.steam_attested: raise SystemExit("reviewer Steam-launch attestation is required")
    manifest = load_json(args.manifest)
    receipt = load_json(args.receipt)
    if not isinstance(manifest, dict) or not isinstance(receipt, dict): raise SystemExit("session manifest and receipt are required")
    expires = datetime.fromisoformat(manifest["expires_at"])
    if datetime.now(timezone.utc) > expires: raise SystemExit("session manifest expired")
    fields = ("nonce", "variant_id", "content_id", "lev_sha256", "vrm_sha256", "host", "mode")
    if any(receipt.get(field) != manifest.get(field) for field in fields):
        raise SystemExit("session receipt does not match manifest")
    executable_hash = sha256_file(args.executable.resolve())
    if receipt.get("executable_sha256") != executable_hash: raise SystemExit("receipt executable hash mismatch")
    if receipt.get("complete") is not True or not receipt.get("git_commit"):
        raise SystemExit("receipt is incomplete")
    entry = verified_entry(args.root, manifest["variant_id"])
    if entry["content_id"] != manifest["content_id"]: raise SystemExit("inventory content identity changed")
    data = verdicts(args.root)
    record = data["verdicts"].setdefault(entry["variant_id"], {
        "variant_id": entry["variant_id"], "content_id": entry["content_id"],
        "track_id": entry["track_id"], "version": entry["version"],
        "lev_media_id": entry["lev_media_id"], "vrm_media_id": entry["vrm_media_id"],
        "lev_sha256": manifest["lev_sha256"], "vrm_sha256": manifest["vrm_sha256"], "modes": {}})
    record["modes"][manifest["mode"]] = {"result": args.result, "notes": args.notes or "",
        "launch_method": "user-steam-attested", "reviewer": args.reviewer,
        "git_commit": receipt["git_commit"], "dirty": bool(receipt.get("dirty")),
        "executable_sha256": executable_hash, "nonce": manifest["nonce"],
        "recorded_at": datetime.now(timezone.utc).isoformat()}
    atomic_json(args.root / "verdicts.json", data)
    consumed = args.manifest.with_suffix(args.manifest.suffix + ".consumed")
    os.replace(args.manifest, consumed)
    print(f"Collected Steam-attested {manifest['mode']}={args.result} for {entry['variant_id']}.")


def rows_for_export(root: Path) -> list[dict[str, Any]]:
    ledger = verdicts(root)["verdicts"]
    rows = []
    for entry in inventory(root)["entries"]:
        row = {key: entry.get(key) for key in ("track_id", "name", "author", "track_type", "collection",
                                                "version", "lev_media_id", "vrm_media_id", "lev_crc32",
                                                "vrm_crc32", "status", "notes", "variant_id", "content_id")}
        report = entry.get("verification", {})
        row.update({"lev_sha256": report.get("lev_sha256"), "vrm_sha256": report.get("vrm_sha256"),
                    "existing_saphi_tags": ", ".join(entry.get("saphi_modes", []))})
        runtime = ledger.get(entry.get("variant_id", ""), {}).get("modes", {})
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
    escaped = False
    for row in rows:
        sheet.append([row.get(header) for header in headers])
        for cell in sheet[sheet.max_row]:
            if isinstance(cell.value, str) and cell.value.lstrip(" \t")[:1] in {"=", "+", "-", "@"}:
                cell.value = "'" + cell.value
                cell.number_format = "@"
                escaped = True
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
    queue.add_argument("--difference", action="store_true")
    queue.add_argument("--author")
    queue.add_argument("--track-type", choices=("race", "battle"))
    queue.add_argument("--mode", choices=MODES)
    queue.add_argument("--collection")
    queue.add_argument("--pending", action="store_true")
    queue.add_argument("--newest-only", action="store_true")
    queue.add_argument("--page-size", type=int, default=50, choices=range(1, 201))
    queue.add_argument("--cursor", type=int, default=0)
    queue.add_argument("--json", action="store_true")
    queue.set_defaults(func=command_queue)
    prepare = commands.add_parser("prepare-session")
    prepare.add_argument("variant_id")
    prepare.add_argument("mode", choices=MODES)
    prepare.add_argument("--manifest", type=Path, required=True)
    prepare.add_argument("--host", type=int, required=True)
    prepare.add_argument("--minutes", type=int, default=30)
    prepare.set_defaults(func=command_prepare_session)
    collect = commands.add_parser("collect-session")
    collect.add_argument("--manifest", type=Path, required=True)
    collect.add_argument("--receipt", type=Path, required=True)
    collect.add_argument("--executable", type=Path, required=True)
    collect.add_argument("--reviewer", required=True)
    collect.add_argument("--steam-attested", action="store_true")
    collect.add_argument("--result", choices=("Yes", "No"), required=True)
    collect.add_argument("--notes")
    collect.set_defaults(func=command_collect_session)
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
