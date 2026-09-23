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
import shutil
import subprocess
import sys
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


def verify_archive_hashes(release: Path, artifacts: dict[str, str]) -> None:
    for name, expected in sorted(artifacts.items()):
        artifact = release / name
        require_regular_file(artifact, "covered archive")
        actual = sha256(artifact)
        if actual != expected:
            fail(
                f"SHA-256 mismatch for {name}: "
                f"manifest has {expected}, actual is {actual}"
            )


def verify(release: Path, version: str, public_key: Path, pre_sign: bool) -> None:
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
        "--public-key",
        type=Path,
        default=Path(__file__).resolve().parents[1] / PUBLIC_KEY_NAME,
    )
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        verify(args.release_directory, args.version, args.public_key, args.pre_sign)
    except (OSError, PolicyError) as error:
        print(f"release verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
