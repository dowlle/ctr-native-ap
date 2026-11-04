#!/usr/bin/env python3
"""Verify a CTR-AP release against the signed client manifest.

Default (published) mode fails closed: it requires the exact published release
file set, a real committed public key and minisign, and a valid signature over
the exact manifest bytes.  ``--pre-sign`` is a distinct mode that verifies a
complete pre-sign assembly (the eleven standard assets plus ``manifest.json``)
without requiring a signature; it never claims a signed release.

Usage:
    python3 tools/verify-release.py <release-directory> --version vX.Y.Z
    python3 tools/verify-release.py <assembly-directory> --version vX.Y.Z --pre-sign
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "release"))

import release_policy
from release_policy import (
    MANIFEST_NAME,
    PLACEHOLDER_PREFIX,
    PUBLIC_KEY_NAME,
    SIGNATURE_NAME,
    PolicyError,
    fail,
    load_manifest_bytes,
    published_release_files,
    prepared_release_files,
    require_regular_file,
    sha256,
    verify_standard_sidecars,
)


def require_exact_files(release: Path, permitted: set[str], optional: set[str] = frozenset()) -> None:
    """Reject unknown files, symlinks and non-regular entries in the release dir.

    ``optional`` names may be absent; they are never counted as missing.
    """
    actual = {path.name for path in release.iterdir()}
    missing = sorted(permitted - actual)
    extra = sorted(actual - permitted - optional)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing release files: {', '.join(missing)}")
        if extra:
            details.append(f"unexpected release files: {', '.join(extra)}")
        fail("; ".join(details))
    for name in sorted(actual):
        require_regular_file(release / name, "release file")


def read_public_key(public_key: Path) -> str:
    require_regular_file(public_key, "public key")
    text = public_key.read_text(encoding="utf-8")
    if text.startswith(PLACEHOLDER_PREFIX):
        fail(
            "release public key is still the placeholder; replace "
            f"{PUBLIC_KEY_NAME} with the real minisign public key before "
            "verifying a signed release"
        )
    return text


def verify_signature(manifest: Path, signature: Path, public_key: Path) -> None:
    read_public_key(public_key)
    minisign = shutil.which("minisign")
    if minisign is None:
        fail("minisign is required to verify a signed release but is not installed")
    result = subprocess.run(
        [
            minisign,
            "-Vm",
            str(manifest),
            "-x",
            str(signature),
            "-p",
            str(public_key),
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "minisign failed"
        fail(f"bad manifest signature: {detail}")


# The in-game updater (#301, kMaxArchive in ap/ap_update_core.h) refuses a
# larger client archive, so released clients could not update to it.
UPDATER_MAX_ARCHIVE = 64 * 1024 * 1024


def verify_archive_hashes(release: Path, artifacts: dict[str, str]) -> None:
    for name, expected in sorted(artifacts.items()):
        artifact = release / name
        require_regular_file(artifact, "covered archive")
        if artifact.stat().st_size > UPDATER_MAX_ARCHIVE:
            fail(f"{name} is larger than the in-game updater accepts ({UPDATER_MAX_ARCHIVE} bytes)")
        actual = sha256(artifact)
        if actual != expected:
            fail(
                f"SHA-256 mismatch for {name}: "
                f"manifest has {expected}, actual is {actual}"
            )


SOLO_RUNTIME_MANIFEST = "solo-runtime/RUNTIME.txt"


def _runtime_value(text: str, key: str) -> str | None:
    for line in text.splitlines():
        name, sep, value = line.partition(":")
        if sep and name.strip() == key:
            return value.strip()
    return None


SOLO_RUNTIME_FROM = (0, 3, 0)  # releases from 0.3.0 on must carry the solo runtime (#89)


def solo_runtime_required(tag: str) -> bool:
    import re
    numbers = re.match(r"^v?(\d+)\.(\d+)\.(\d+)", tag)
    return bool(numbers) and tuple(int(n) for n in numbers.groups()) >= SOLO_RUNTIME_FROM


def runtime_tree_from_archive(archive: Path) -> tuple[str, dict[str, str]]:
    """Recompute RUNTIME.txt's tree sha256 from the archive's solo-runtime/
    members. Refuses links, special files, duplicate paths and (Linux) an
    interpreter without execute permission. Returns the digest and the
    per-file hashes."""
    files: dict[str, str] = {}
    prefix = "solo-runtime/"

    def add(rel: str, data: bytes) -> None:
        if rel in files:
            fail(f"{archive.name}: duplicate solo runtime member {rel}")
        files[rel] = hashlib.sha256(data).hexdigest()

    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as bundle:
            for info in bundle.infolist():
                rel = info.filename.split("/", 1)[1] if "/" in info.filename else ""
                if not rel.startswith(prefix) or info.is_dir():
                    continue
                kind = (info.external_attr >> 16) & 0o170000
                if kind not in (0, 0o100000):  # no Unix type recorded, or a regular file
                    fail(f"{archive.name}: solo runtime member is not a regular file: {rel}")
                add(rel[len(prefix):], bundle.read(info))
    else:
        with tarfile.open(archive) as bundle:
            for member in bundle.getmembers():
                rel = member.name.split("/", 1)[1] if "/" in member.name else ""
                if not rel.startswith(prefix) or member.isdir():
                    continue
                if not member.isfile():
                    fail(f"{archive.name}: solo runtime member is not a regular file: {rel}")
                name = rel[len(prefix):]
                if name == "python/bin/python3.12" and not member.mode & 0o111:
                    fail(f"{archive.name}: the solo runtime interpreter is not executable")
                stream = bundle.extractfile(member)
                add(name, stream.read() if stream else b"")
    digest = hashlib.sha256()
    for rel, file_sha in sorted(files.items()):
        if rel == "RUNTIME.txt":
            continue
        digest.update(rel.encode() + b"\0" + file_sha.encode() + b"\n")
    return digest.hexdigest(), files


def read_archive_member(archive: Path, suffix: str) -> bytes | None:
    """Bytes of the one member `<root>/<suffix>`, or None."""
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as bundle:
            names = [n for n in bundle.namelist() if n.count("/") >= 1 and n.split("/", 1)[1] == suffix]
            return bundle.read(names[0]) if len(names) == 1 else None
    with tarfile.open(archive) as bundle:
        members = [m for m in bundle.getmembers()
                   if m.isfile() and m.name.count("/") >= 1 and m.name.split("/", 1)[1] == suffix]
        if len(members) != 1:
            return None
        stream = bundle.extractfile(members[0])
        return stream.read() if stream else None


def verify_solo_runtime(release: Path, tag: str) -> None:
    """Each client archive carries solo-runtime/RUNTIME.txt (#89), built for the
    exact ctr.apworld asset of this release, with its interpreter inside."""
    apworld = sha256(release / "ctr.apworld")
    for platform, interpreter in (("windows", "python/python.exe"), ("linux", "python/bin/python3.12")):
        suffix = ".zip" if platform == "windows" else ".tar.gz"
        archive = release / f"ctr-archipelago-{tag}-{platform}-x86{suffix}"
        text = read_archive_member(archive, SOLO_RUNTIME_MANIFEST)
        if text is None:
            fail(f"{archive.name} has no {SOLO_RUNTIME_MANIFEST}")
        manifest = text.decode("utf-8")
        if _runtime_value(manifest, "platform") != platform:
            fail(f"{archive.name}: solo runtime is not the {platform} runtime")
        if _runtime_value(manifest, "ctr.apworld sha256") != apworld:
            fail(f"{archive.name}: solo runtime ctr.apworld hash differs from the ctr.apworld asset")
        if read_archive_member(archive, f"solo-runtime/{interpreter}") is None:
            fail(f"{archive.name}: solo runtime has no {interpreter}")
        tree, files = runtime_tree_from_archive(archive)
        if _runtime_value(manifest, "tree sha256") != tree:
            fail(f"{archive.name}: solo runtime files differ from its RUNTIME.txt (tree sha256)")
        if _runtime_value(manifest, "ctr_solo.py sha256") != files.get("ctr_solo.py"):
            fail(f"{archive.name}: solo runtime ctr_solo.py differs from its RUNTIME.txt")
        bundled = read_archive_member(archive, "ctr.apworld")
        if bundled is None or hashlib.sha256(bundled).hexdigest() != apworld:
            fail(f"{archive.name}: bundled ctr.apworld differs from the ctr.apworld asset")


def verify(release: Path, version: str, public_key: Path, pre_sign: bool,
           require_solo_runtime: bool = False) -> None:
    if not release.is_dir():
        fail(f"release directory does not exist: {release}")
    tag = version if version.startswith("v") else f"v{version}"
    if not release_policy.TAGGED_VERSION.fullmatch(tag):
        fail(f"invalid expected release version: {version!r}")

    permitted = prepared_release_files(tag) if pre_sign else published_release_files(tag)
    require_exact_files(release, permitted, set(release_policy.authoring_asset_names(tag)))

    manifest = release / MANIFEST_NAME
    if pre_sign:
        require_regular_file(manifest, "manifest")
        version_seen, artifacts = load_manifest_bytes(manifest.read_bytes())
    else:
        require_regular_file(manifest, "manifest")
        signature = release / SIGNATURE_NAME
        require_regular_file(signature, "manifest signature")
        # Verify the signature over the exact on-disk manifest bytes before
        # trusting anything parsed out of them.
        verify_signature(manifest, signature, public_key)
        version_seen, artifacts = load_manifest_bytes(manifest.read_bytes())

    if version_seen != tag:
        fail(f"manifest version {version_seen!r} does not match expected {tag!r}")

    verify_archive_hashes(release, artifacts)
    verify_standard_sidecars(release, tag)
    if require_solo_runtime or solo_runtime_required(tag):
        verify_solo_runtime(release, tag)
    release_policy.verify_authoring_sidecars(release, tag)

    if pre_sign:
        print(
            f"unsigned assembly checks passed for {tag}: "
            f"{len(artifacts)} client archives, not a signed release"
        )
    else:
        print(f"verified signed release {tag}: {len(artifacts)} client archives")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("release_directory", type=Path)
    result.add_argument(
        "--version",
        required=True,
        help="expected release version, for example v0.2.1-alpha2",
    )
    result.add_argument(
        "--pre-sign",
        action="store_true",
        help="verify an unsigned pre-sign assembly instead of a signed release",
    )
    result.add_argument(
        "--require-solo-runtime",
        action="store_true",
        help="require the solo runtime (#89) for a release before 0.3.0 too; from 0.3.0 on it is always required",
    )
    result.add_argument(
        "--public-key",
        type=Path,
        default=Path(__file__).resolve().parents[1] / PUBLIC_KEY_NAME,
    )
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        verify(args.release_directory, args.version, args.public_key, args.pre_sign,
               args.require_solo_runtime)
    except (OSError, PolicyError, zipfile.BadZipFile, tarfile.TarError) as error:
        print(f"release verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
