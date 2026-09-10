#!/usr/bin/env python3
"""Assemble the complete CTR-AP release asset set from hosted build outputs.

This command only combines already-built bytes.  It does not build, publish,
tag, sign, or include game assets.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path, PurePosixPath


ASSET_NAMES = (
    "ctr-archipelago-{version}-windows-x86.zip",
    "ctr-archipelago-{version}-windows-x86.zip.sha256",
    "ctr_native_ap.exe.debug",
    "ctr_native_ap.exe.debug.sha256",
    "ctr.apworld",
    "ctr.apworld.sha256",
    "Crash.Team.Racing.yaml",
    "ctr-archipelago-{version}-linux-x86.tar.gz",
    "ctr-archipelago-{version}-linux-x86.tar.gz.sha256",
    "ctr_native_ap.debug",
    "ctr_native_ap.debug.sha256",
)

HEX40 = re.compile(r"^[0-9a-f]{40}$")
VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
ARTIFACT_ARCHIVE = {
    "windows": re.compile(r"^ctr-ap-windows-x86-([0-9a-f]{12,40})\.zip$"),
    "linux": re.compile(r"^ctr-ap-linux-x86-([0-9a-f]{12,40})\.tar\.gz$"),
}
NATIVE_COMPANIONS = (
    "ap/ap_version.h",
    "tools/release-versions.sh",
    "tools/ci/package-client.py",
    "tools/extract-assets/extract_assets.py",
    "SETUP.md",
    "ap-config.example.txt",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "support-bundle.bat",
    "support-bundle.ps1",
    "support-bundle.sh",
)


class AssemblyError(ValueError):
    """An input failed a release assembly gate."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fail(message: str) -> None:
    raise AssemblyError(message)


def safe_member(name: str) -> PurePosixPath:
    if "\\" in name:
        fail(f"archive member uses a backslash: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        fail(f"unsafe archive member path: {name!r}")
    return path


def check_checksum(path: Path) -> None:
    sidecar = Path(str(path) + ".sha256")
    if not sidecar.is_file():
        fail(f"missing checksum sidecar: {sidecar}")
    lines = sidecar.read_text(encoding="utf-8").splitlines()
    if len(lines) != 1:
        fail(f"checksum sidecar must contain one line: {sidecar}")
    fields = lines[0].split()
    if len(fields) != 2 or fields[1] != path.name or not re.fullmatch(r"[0-9a-f]{64}", fields[0]):
        fail(f"malformed checksum sidecar: {sidecar}")
    actual = sha256(path)
    if fields[0] != actual:
        fail(f"checksum mismatch for {path.name}: sidecar has {fields[0]}, actual is {actual}")


def find_one(root: Path, predicate, label: str) -> Path:
    candidates = sorted(path for path in root.rglob("*") if path.is_file() and predicate(path))
    if len(candidates) != 1:
        names = ", ".join(str(path) for path in candidates) or "none"
        fail(f"expected exactly one {label} in {root}, found {len(candidates)}: {names}")
    return candidates[0]


def unpack_artifact(archive: Path, platform: str, destination: Path) -> tuple[Path, dict]:
    """Extract a package-client archive after checking its complete shape."""
    binary_name = "ctr_native_ap.exe" if platform == "windows" else "ctr_native_ap"
    helper_names = {"support-bundle.bat", "support-bundle.ps1"} if platform == "windows" else {"support-bundle.sh"}
    allowed = {
        binary_name,
        "versions.txt",
        "extract_assets.py",
        "SETUP.md",
        "ap-config.example.txt",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "BUILD.json",
        "BUILD-NOTICE.txt",
        *helper_names,
    }
    members: list[tuple[str, bytes]] = []
    root_name: str | None = None
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as bundle:
            infos = bundle.infolist()
            for info in infos:
                path = safe_member(info.filename)
                if info.is_dir():
                    continue
                if (info.external_attr >> 16) & 0o170000 == 0o120000:
                    fail(f"symlink in build archive: {info.filename!r}")
                if len(path.parts) != 2:
                    fail(f"build archive member must be directly under its root: {info.filename!r}")
                root_name = root_name or path.parts[0]
                if root_name != path.parts[0] or path.parts[1] not in allowed:
                    fail(f"unexpected build archive member: {info.filename!r}")
                members.append((path.parts[1], bundle.read(info)))
    else:
        with tarfile.open(archive, "r:gz") as bundle:
            for info in bundle.getmembers():
                path = safe_member(info.name)
                if info.isdir():
                    continue
                if not info.isfile() or info.issym() or info.islnk():
                    fail(f"non-regular build archive member: {info.name!r}")
                if len(path.parts) != 2:
                    fail(f"build archive member must be directly under its root: {info.name!r}")
                root_name = root_name or path.parts[0]
                if root_name != path.parts[0] or path.parts[1] not in allowed:
                    fail(f"unexpected build archive member: {info.name!r}")
                stream = bundle.extractfile(info)
                if stream is None:
                    fail(f"cannot read build archive member: {info.name!r}")
                members.append((path.parts[1], stream.read()))
    if root_name is None:
        fail(f"empty build archive: {archive}")
    names = [name for name, _ in members]
    if len(names) != len(set(names)):
        fail(f"duplicate build archive member in {archive.name}")
    required = {binary_name, "BUILD.json"}
    missing = sorted(required - set(names))
    if missing:
        fail(f"build archive lacks required member(s): {', '.join(missing)}")
    destination.mkdir(parents=True)
    for name, data in members:
        (destination / name).write_bytes(data)
    if platform == "linux":
        os.chmod(destination / binary_name, 0o755)
    try:
        metadata = json.loads((destination / "BUILD.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"invalid BUILD.json in {archive.name}: {error}")
    if not isinstance(metadata, dict):
        fail(f"BUILD.json must contain an object: {archive.name}")
    return destination / binary_name, metadata


def load_package_client(source: Path):
    script = source / "tools" / "ci" / "package-client.py"
    if not script.is_file():
        return None
    spec = importlib.util.spec_from_file_location("ctr_package_client", script)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_build(source: Path, platform: str, archive: Path, debug: Path, binary: Path, metadata: dict) -> None:
    expected_commit = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if not HEX40.fullmatch(expected_commit):
        fail(f"native source HEAD is not a full commit id: {expected_commit!r}")
    if metadata.get("source_commit") != expected_commit:
        fail(f"{platform} artifact source_commit does not match native source HEAD")
    if metadata.get("platform") != platform or metadata.get("variant") != "ap":
        fail(f"{platform} artifact is not an AP {platform} build")
    if metadata.get("executable_sha256") != sha256(binary):
        fail(f"{platform} BUILD.json executable hash does not match archive bytes")
    if metadata.get("debug_sha256") != sha256(debug):
        fail(f"{platform} BUILD.json debug hash does not match sidecar bytes")
    match = ARTIFACT_ARCHIVE[platform].match(archive.name)
    if match is None or not expected_commit.startswith(match.group(1)):
        fail(f"{platform} archive name does not identify native source commit")
    # package-client performed the platform ABI, split-debug and debuglink gates
    # while creating this artifact. Repeat them here whenever the required host
    # inspection tools can parse the extracted native binary.
    package_client = load_package_client(source)
    if package_client is not None:
        try:
            package_client.verify(platform, binary)
        except Exception as error:
            fail(f"{platform} package-client validation failed: {error}")


def read_source_identity(source: Path) -> tuple[str, str]:
    header = source / "ap" / "ap_version.h"
    text = header.read_text(encoding="utf-8")
    compat = re.search(r'^#define\s+CTR_AP_COMPAT_VERSION\s+"([^"]+)"', text, re.MULTILINE)
    build = re.search(r'^#define\s+CTR_AP_VERSION\s+"([^"]+)"', text, re.MULTILINE)
    if compat is None or build is None:
        fail(f"could not read release identities from {header}")
    return compat.group(1).removeprefix("v"), build.group(1).removeprefix("v")


def validate_tracked_companions(source: Path) -> None:
    result = subprocess.run(
        ["git", "-C", str(source), "ls-files", "--error-unmatch", "--", *NATIVE_COMPANIONS],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or "a release companion is not tracked"
        fail(f"native source has an untracked or missing release companion: {detail}")


def validate_apworld(path: Path, version: str, source_compat: str) -> None:
    if path.name != "ctr.apworld":
        fail(f"apworld must be named ctr.apworld, got {path.name}")
    if not path.is_file():
        fail(f"missing apworld: {path}")
    with zipfile.ZipFile(path) as bundle:
        names: set[str] = set()
        for info in bundle.infolist():
            member = safe_member(info.filename)
            if member.parts and member.parts[0] != "ctr":
                fail(f"apworld contains an unexpected top-level path: {info.filename!r}")
            if info.is_dir():
                continue
            if info.filename in names:
                fail(f"apworld contains duplicate member: {info.filename!r}")
            names.add(info.filename)
        required = {"ctr/archipelago.json", "ctr/version.py"}
        if not required <= names:
            fail(f"apworld lacks required identity files: {sorted(required - names)}")
        try:
            manifest = json.loads(bundle.read("ctr/archipelago.json"))
            version_py = bundle.read("ctr/version.py").decode("utf-8")
        except (json.JSONDecodeError, UnicodeDecodeError, KeyError) as error:
            fail(f"cannot read apworld identity: {error}")
    manifest_version = manifest.get("world_version")
    compat = re.search(r'^COMPAT_VERSION\s*=\s*"([^"]+)"', version_py, re.MULTILINE)
    build = re.search(r'^BUILD_VERSION\s*=\s*"([^"]+)"', version_py, re.MULTILINE)
    expected_compat = version.split("-", 1)[0]
    if manifest.get("game") != "Crash Team Racing":
        fail("apworld manifest is not Crash Team Racing")
    if manifest_version != expected_compat or manifest_version != source_compat:
        fail(f"apworld compatibility {manifest_version!r} does not match release {expected_compat}")
    if compat is None or compat.group(1) != expected_compat:
        fail("apworld COMPAT_VERSION does not match its manifest/release")
    if build is None or build.group(1).removeprefix("v") != version:
        fail("apworld BUILD_VERSION does not match the requested release version")


def validate_template(path: Path, version: str, source_compat: str) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        fail(f"missing or empty generated template: {path}")
    text = path.read_text(encoding="utf-8-sig")
    if "game: Crash Team Racing" not in text or "Crash Team Racing:" not in text:
        fail("template does not describe Crash Team Racing")
    expected = f"Crash Team Racing: {source_compat}"
    if expected not in text:
        fail(f"template does not require apworld version {source_compat}")
    if version.split("-", 1)[0] != source_compat:
        fail("release build version and compatibility version disagree")


def write_checksum(path: Path) -> None:
    path.with_name(path.name + ".sha256").write_text(f"{sha256(path)}  {path.name}\n", encoding="utf-8")


def create_archive(destination: Path, root_name: str, files: list[Path], linux: bool) -> None:
    if linux:
        with tarfile.open(destination, "w:gz") as bundle:
            directory = tarfile.TarInfo(root_name)
            directory.type = tarfile.DIRTYPE
            directory.mode = 0o755
            bundle.addfile(directory)
            for path in sorted(files, key=lambda item: item.name):
                info = bundle.gettarinfo(str(path), arcname=f"{root_name}/{path.name}")
                if not info.isfile():
                    fail(f"release input is not a regular file: {path}")
                with path.open("rb") as stream:
                    bundle.addfile(info, stream)
    else:
        with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(files, key=lambda item: item.name):
                bundle.write(path, f"{root_name}/{path.name}")


def assemble(args: argparse.Namespace) -> list[Path]:
    version = args.version.removeprefix("v")
    if not VERSION.fullmatch(version):
        fail(f"invalid release version: {args.version!r}")
    source = args.native_source.resolve()
    output = args.output_dir.resolve()
    if not (source / ".git").exists():
        fail(f"native source is not a git checkout: {source}")
    clean = subprocess.run(
        ["git", "-C", str(source), "diff", "--quiet", "HEAD", "--"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if clean.returncode != 0:
        detail = clean.stderr.strip() or "tracked changes are present"
        fail(f"native source checkout is not clean: {detail}")
    validate_tracked_companions(source)
    source_compat, source_build = read_source_identity(source)
    if source_build != version:
        fail(f"native source CTR_AP_VERSION is {source_build!r}, requested {version!r}")
    if source_compat != version.split("-", 1)[0]:
        fail("native source compatibility version does not match requested release")
    validate_apworld(args.apworld.resolve(), version, source_compat)
    validate_template(args.template.resolve(), version, source_compat)
    output.mkdir(parents=True, exist_ok=True)
    expected_assets = [name.format(version=f"v{version}") for name in ASSET_NAMES]
    existing = [name for name in expected_assets if (output / name).exists()]
    if existing:
        fail(f"refusing to overwrite existing release asset(s): {', '.join(existing)}")
    with tempfile.TemporaryDirectory(prefix="ctr-release-") as temporary:
        temp = Path(temporary)
        staged: dict[str, tuple[Path, dict]] = {}
        artifact_inputs = (
            ("windows", args.windows_artifacts.resolve()),
            ("linux", args.linux_artifacts.resolve()),
        )
        for platform, artifact_dir in artifact_inputs:
            archive = find_one(
                artifact_dir,
                lambda p, platform=platform: ARTIFACT_ARCHIVE[platform].fullmatch(p.name) is not None,
                f"{platform} AP archive",
            )
            check_checksum(archive)
            debug_name = "ctr_native_ap.exe.debug" if platform == "windows" else "ctr_native_ap.debug"
            debug = find_one(
                artifact_dir,
                lambda p, debug_name=debug_name: p.name == debug_name,
                f"{platform} debug sidecar",
            )
            check_checksum(debug)
            unpacked, metadata = unpack_artifact(archive, platform, temp / platform)
            staged[platform] = (unpacked, metadata)
            # package-client.verify resolves the sidecar as <binary>.debug.
            shutil.copy2(debug, temp / platform / debug_name)
            validate_build(source, platform, archive, debug, unpacked, metadata)
        root_name = f"ctr-archipelago-v{version}"
        common = [
            source / "tools" / "extract-assets" / "extract_assets.py",
            source / "SETUP.md",
            source / "ap-config.example.txt",
            source / "LICENSE",
            source / "THIRD_PARTY_NOTICES.md",
            args.apworld.resolve(),
        ]
        helper = [source / "support-bundle.bat", source / "support-bundle.ps1"]
        for path in common + helper:
            if not path.is_file():
                fail(f"missing release companion file: {path}")
        versions = subprocess.check_output(
            ["bash", str(source / "tools" / "release-versions.sh")], cwd=source, text=True
        )
        if f"CTR-AP build: v{version}" not in versions and f"CTR-AP build: {version}" not in versions:
            fail("versions.txt does not carry the requested build identity")
        (temp / "versions.txt").write_text(versions, encoding="utf-8")
        bundle_common = [*common, temp / "versions.txt"]
        windows_files = [staged["windows"][0], *bundle_common, *helper]
        linux_files = [staged["linux"][0], *bundle_common, source / "support-bundle.sh"]
        if not (source / "support-bundle.sh").is_file():
            fail("missing release companion file: support-bundle.sh")
        windows_archive = output / f"ctr-archipelago-v{version}-windows-x86.zip"
        linux_archive = output / f"ctr-archipelago-v{version}-linux-x86.tar.gz"
        create_archive(windows_archive, root_name, windows_files, linux=False)
        create_archive(linux_archive, root_name, linux_files, linux=True)
        shutil.copy2(temp / "windows" / "ctr_native_ap.exe.debug", output / "ctr_native_ap.exe.debug")
        shutil.copy2(temp / "linux" / "ctr_native_ap.debug", output / "ctr_native_ap.debug")
    shutil.copy2(args.apworld, output / "ctr.apworld")
    shutil.copy2(args.template, output / "Crash.Team.Racing.yaml")
    for name in (
        f"ctr-archipelago-v{version}-windows-x86.zip",
        "ctr_native_ap.exe.debug",
        "ctr.apworld",
        f"ctr-archipelago-v{version}-linux-x86.tar.gz",
        "ctr_native_ap.debug",
    ):
        write_checksum(output / name)
    assets = [output / name for name in expected_assets]
    if any(not path.is_file() for path in assets):
        fail("assembly did not produce the complete eleven-asset set")
    return assets


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--native-source", type=Path, required=True)
    result.add_argument("--windows-artifacts", type=Path, required=True)
    result.add_argument("--linux-artifacts", type=Path, required=True)
    result.add_argument("--apworld", type=Path, required=True)
    result.add_argument("--template", type=Path, required=True)
    result.add_argument(
        "--version",
        required=True,
        help="numeric semver, with optional prerelease and optional leading v",
    )
    result.add_argument("--output-dir", type=Path, required=True)
    return result


def main() -> int:
    try:
        assets = assemble(parser().parse_args())
    except (AssemblyError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile, tarfile.TarError) as error:
        print(f"release assembly failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"assets": [str(path) for path in assets]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
