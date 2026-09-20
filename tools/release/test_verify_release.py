#!/usr/bin/env python3
"""Production-boundary tests for the signed release verifier.

The key integration tests run the production manifest writer and the production
verifier in-process and through the CLI, so a mistake shared between generation
and verification cannot hide.  Disposable minisign keys are generated inside a
temporary directory and never touch the committed placeholder.
"""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


RELEASE_DIR = Path(__file__).resolve().parent
NATIVE_ROOT = RELEASE_DIR.parents[1]
sys.path.insert(0, str(RELEASE_DIR))

import release_policy  # noqa: E402
from release_policy import (  # noqa: E402
    MANIFEST_NAME,
    SIGNATURE_NAME,
    prepared_release_files,
    sha256,
)


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ASSEMBLER = load_module("assemble_release", RELEASE_DIR / "assemble-release.py")
VERIFY_SCRIPT = NATIVE_ROOT / "tools" / "verify-release.py"
VERIFIER = load_module("verify_release", VERIFY_SCRIPT)


def minisign_path() -> str | None:
    import shutil as _shutil

    return _shutil.which("minisign")


# The read-only CI trust job installs minisign and sets this so a broken
# install cannot turn the required signing tests into silent skips.
_REQUIRE_MINISIGN = os.environ.get("CTR_REQUIRE_MINISIGN") == "1"


def _minisign_gate() -> bool:
    if minisign_path():
        return True
    if _REQUIRE_MINISIGN:
        raise AssertionError(
            "CTR_REQUIRE_MINISIGN=1 but minisign is not installed; the signing "
            "tests must not be skipped in a trusted environment"
        )
    return False


class ReleaseFixture:
    """Build a complete twelve-file pre-sign assembly plus a signed variant."""

    VERSION = "v0.2.0-rc-prep1"

    def __init__(self, root: Path):
        self.root = root
        self.assembly = root / "assembly"
        self.assembly.mkdir()
        self.public_key = root / "disposable.pub"
        self.secret_key = root / "disposable.key"
        self.placeholder = root / "placeholder.pub"
        self.placeholder.write_text(
            f"{release_policy.PLACEHOLDER_PREFIX} no release key yet\n",
            encoding="utf-8",
        )
        self.populate()

    def populate(self) -> None:
        version = self.VERSION
        permitted = prepared_release_files(version)
        names = permitted - {MANIFEST_NAME}
        # Client archives get real, distinct bytes so tampering is meaningful.
        archives = {
            f"ctr-archipelago-{version}-windows-x86.zip": b"windows-client-bytes",
            f"ctr-archipelago-{version}-linux-x86.tar.gz": b"linux-client-bytes",
        }
        for name in names:
            if name.endswith(".sha256"):
                continue
            path = self.assembly / name
            path.write_bytes(archives.get(name, f"content of {name}".encode()))
        # Only the five checksummed assets get a sidecar; the template does not.
        for name in names:
            if name.endswith(".sha256"):
                asset = self.assembly / name[: -len(".sha256")]
                (self.assembly / name).write_text(
                    f"{sha256(asset)}  {asset.name}\n", encoding="utf-8"
                )

    def write_manifest(self) -> Path:
        return ASSEMBLER.write_manifest(self.assembly, self.VERSION)

    def make_disposable_key(self) -> None:
        subprocess.run(
            [
                minisign_path(),
                "-G",
                "-W",
                "-p",
                str(self.public_key),
                "-s",
                str(self.secret_key),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

    def sign(self) -> None:
        signature = self.assembly / SIGNATURE_NAME
        signature.unlink(missing_ok=True)
        subprocess.run(
            [
                minisign_path(),
                "-S",
                "-s",
                str(self.secret_key),
                "-m",
                str(self.assembly / MANIFEST_NAME),
                "-x",
                str(signature),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

    def run_verifier(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(VERIFY_SCRIPT),
                str(self.assembly),
                "--version",
                self.VERSION,
                *extra,
            ],
            capture_output=True,
            text=True,
        )


@unittest.skipUnless(_minisign_gate(), "minisign is required for signing tests")
class ProductionBoundaryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.fixture = ReleaseFixture(self.root)
        self.fixture.write_manifest()
        self.fixture.make_disposable_key()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    # ---- generation -------------------------------------------------------

    def test_manifest_bytes_are_exact_and_trailing_newline(self) -> None:
        manifest = self.fixture.assembly / MANIFEST_NAME
        text = manifest.read_text(encoding="utf-8")
        self.assertTrue(text.endswith("}\n"))
        self.assertFalse(text.endswith("}\n\n"))
        parsed = json.loads(text)
        self.assertEqual(parsed["version"], self.fixture.VERSION)
        self.assertEqual(
            list(parsed["artifacts"]),
            release_policy.signed_asset_names(self.fixture.VERSION),
        )
        for name, digest in parsed["artifacts"].items():
            self.assertEqual(digest, sha256(self.fixture.assembly / name))

    def test_writer_refuses_existing_manifest(self) -> None:
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.write_manifest(self.fixture.assembly, self.fixture.VERSION)

    # ---- pre-sign mode ----------------------------------------------------

    def test_pre_sign_mode_accepts_twelve_file_assembly(self) -> None:
        result = self.fixture.run_verifier("--pre-sign")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("not a signed release", result.stdout)

    def test_pre_sign_mode_rejects_signature_file(self) -> None:
        (self.fixture.assembly / SIGNATURE_NAME).write_text("x", encoding="utf-8")
        result = self.fixture.run_verifier("--pre-sign")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected release files", result.stderr)

    # ---- signed mode, good and bad ---------------------------------------

    def test_good_signed_release_passes(self) -> None:
        self.fixture.sign()
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("verified signed release", result.stdout)

    def test_missing_signature_fails_closed(self) -> None:
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(SIGNATURE_NAME, result.stderr)

    def test_modified_archive_fails(self) -> None:
        self.fixture.sign()
        target = (
            self.fixture.assembly
            / f"ctr-archipelago-{self.fixture.VERSION}-windows-x86.zip"
        )
        target.write_bytes(b"tampered")
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256 mismatch", result.stderr)

    def test_replaced_archive_with_matching_sidecar_fails(self) -> None:
        # An attacker who replaces an archive and its sidecar still fails the
        # signed manifest, because the manifest is authenticated separately.
        self.fixture.sign()
        target = (
            self.fixture.assembly
            / f"ctr-archipelago-{self.fixture.VERSION}-linux-x86.tar.gz"
        )
        target.write_bytes(b"replaced")
        (self.fixture.assembly / (target.name + ".sha256")).write_text(
            f"{sha256(target)}  {target.name}\n", encoding="utf-8"
        )
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256 mismatch", result.stderr)

    def test_missing_archive_fails(self) -> None:
        self.fixture.sign()
        (
            self.fixture.assembly
            / f"ctr-archipelago-{self.fixture.VERSION}-linux-x86.tar.gz"
        ).unlink()
        (
            self.fixture.assembly
            / f"ctr-archipelago-{self.fixture.VERSION}-linux-x86.tar.gz.sha256"
        ).unlink()
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)

    def test_reformatted_signed_manifest_fails(self) -> None:
        # Reformatting preserves JSON meaning but changes exact bytes.
        self.fixture.sign()
        manifest = self.fixture.assembly / MANIFEST_NAME
        data = json.loads(manifest.read_text(encoding="utf-8"))
        manifest.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        # The failure is the signature, not the archive hashes.
        self.assertNotIn("SHA-256 mismatch", result.stderr)

    def test_bad_signature_fails(self) -> None:
        self.fixture.sign()
        (self.fixture.assembly / SIGNATURE_NAME).write_text(
            "not a minisign signature\n", encoding="utf-8"
        )
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)

    def test_wrong_valid_public_key_fails(self) -> None:
        self.fixture.sign()
        other_key = self.root / "other.pub"
        other_secret = self.root / "other.key"
        subprocess.run(
            [minisign_path(), "-G", "-W", "-p", str(other_key), "-s", str(other_secret)],
            check=True,
            capture_output=True,
            text=True,
        )
        result = self.fixture.run_verifier("--public-key", str(other_key))
        self.assertNotEqual(result.returncode, 0)

    def test_placeholder_key_fails_loudly(self) -> None:
        self.fixture.sign()
        result = self.fixture.run_verifier("--public-key", str(self.fixture.placeholder))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("placeholder", result.stderr.lower())

    def test_placeholder_key_fails_when_signature_missing(self) -> None:
        result = self.fixture.run_verifier("--public-key", str(self.fixture.placeholder))
        self.assertNotEqual(result.returncode, 0)

    def test_external_version_mismatch_fails(self) -> None:
        # The same valid signed manifest must fail when the operator names a
        # different expected version, proving the external version binding.
        self.fixture.sign()
        result = subprocess.run(
            [
                sys.executable,
                str(VERIFY_SCRIPT),
                str(self.fixture.assembly),
                "--version",
                "v9.9.9",
                "--public-key",
                str(self.fixture.public_key),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing release files", result.stderr)

    def test_manifest_version_field_mismatch_fails(self) -> None:
        # A signed manifest whose version disagrees with its artifact names is
        # rejected by the strict parser; re-signing cannot launder it.
        manifest = self.fixture.assembly / MANIFEST_NAME
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["version"] = "v9.9.9"
        manifest.write_text(json.dumps(data) + "\n", encoding="utf-8")
        self.fixture.sign()
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)

    def test_unknown_release_file_fails(self) -> None:
        self.fixture.sign()
        (self.fixture.assembly / "surprise.txt").write_text("x", encoding="utf-8")
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected release files", result.stderr)

    def test_missing_standard_asset_fails(self) -> None:
        self.fixture.sign()
        (self.fixture.assembly / "ctr.apworld").unlink()
        (self.fixture.assembly / "ctr.apworld.sha256").unlink()
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing release files", result.stderr)

    def test_corrupt_standard_sidecar_fails(self) -> None:
        self.fixture.sign()
        sidecar = self.fixture.assembly / "ctr.apworld.sha256"
        sidecar.write_text("0" * 64 + "  ctr.apworld\n", encoding="utf-8")
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum mismatch", result.stderr)

    def test_symlinked_archive_fails(self) -> None:
        self.fixture.sign()
        target = (
            self.fixture.assembly
            / f"ctr-archipelago-{self.fixture.VERSION}-windows-x86.zip"
        )
        real = self.root / "real.zip"
        target.replace(real)
        os.symlink(real, target)
        result = self.fixture.run_verifier("--public-key", str(self.fixture.public_key))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink", result.stderr)


class MalformedManifestTests(unittest.TestCase):
    """Direct parser tests for malformed inputs the CLI cannot easily express."""

    VERSION = "v0.2.0-rc-prep1"

    def parse(self, text: str):
        return release_policy.load_manifest_bytes(text.encode("utf-8"))

    def good(self) -> str:
        return json.dumps(
            {
                "version": self.VERSION,
                "artifacts": {
                    f"ctr-archipelago-{self.VERSION}-windows-x86.zip": "a" * 64,
                    f"ctr-archipelago-{self.VERSION}-linux-x86.tar.gz": "b" * 64,
                },
            }
        )

    def test_duplicate_top_level_key_is_rejected(self) -> None:
        text = (
            '{"version": "v0.2.0-rc-prep1", '
            '"version": "v0.2.0-rc-prep1", "artifacts": {}}'
        )
        with self.assertRaises(release_policy.PolicyError):
            self.parse(text)

    def test_duplicate_artifacts_key_is_rejected(self) -> None:
        text = '{"version": "v0.2.0-rc-prep1", "artifacts": {}, "artifacts": {}}'
        with self.assertRaises(release_policy.PolicyError):
            self.parse(text)

    def test_duplicate_artifact_filename_is_rejected(self) -> None:
        name = f"ctr-archipelago-{self.VERSION}-windows-x86.zip"
        text = (
            '{"version": "v0.2.0-rc-prep1", "artifacts": {'
            f'"{name}": "{"a" * 64}", "{name}": "{"b" * 64}"'
            "}}"
        )
        with self.assertRaises(release_policy.PolicyError):
            self.parse(text)

    def test_nonstandard_nan_constant_is_rejected(self) -> None:
        with self.assertRaises(release_policy.PolicyError):
            self.parse('{"version": NaN, "artifacts": {}}')

    def test_nan_inside_artifact_is_rejected(self) -> None:
        name = f"ctr-archipelago-{self.VERSION}-windows-x86.zip"
        text = '{"version": "v0.2.0-rc-prep1", "artifacts": {"' + name + '": NaN}}'
        with self.assertRaises(release_policy.PolicyError):
            self.parse(text)

    def test_wrong_top_level_keys_rejected(self) -> None:
        with self.assertRaises(release_policy.PolicyError):
            self.parse('{"version": "v0.2.0-rc-prep1"}')

    def test_third_artifact_entry_rejected(self) -> None:
        data = json.loads(self.good())
        data["artifacts"]["extra.bin"] = "c" * 64
        with self.assertRaises(release_policy.PolicyError):
            self.parse(json.dumps(data))

    def test_traversal_artifact_name_rejected(self) -> None:
        data = json.loads(self.good())
        data["artifacts"] = {"../x": "c" * 64}
        with self.assertRaises(release_policy.PolicyError):
            self.parse(json.dumps(data))

    def test_absolute_artifact_path_rejected(self) -> None:
        data = json.loads(self.good())
        data["artifacts"] = {"/etc/passwd": "c" * 64}
        with self.assertRaises(release_policy.PolicyError):
            self.parse(json.dumps(data))

    def test_uppercase_sha_rejected(self) -> None:
        data = json.loads(self.good())
        data["artifacts"][f"ctr-archipelago-{self.VERSION}-windows-x86.zip"] = "A" * 64
        with self.assertRaises(release_policy.PolicyError):
            self.parse(json.dumps(data))

    def test_short_sha_rejected(self) -> None:
        data = json.loads(self.good())
        data["artifacts"][f"ctr-archipelago-{self.VERSION}-windows-x86.zip"] = "a" * 63
        with self.assertRaises(release_policy.PolicyError):
            self.parse(json.dumps(data))

    def test_wrong_platform_name_rejected(self) -> None:
        data = json.loads(self.good())
        data["artifacts"] = {
            f"ctr-archipelago-{self.VERSION}-macos-x86.zip": "a" * 64,
            f"ctr-archipelago-{self.VERSION}-linux-x86.tar.gz": "b" * 64,
        }
        with self.assertRaises(release_policy.PolicyError):
            self.parse(json.dumps(data))


if __name__ == "__main__":
    unittest.main()
