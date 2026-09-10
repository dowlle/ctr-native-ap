#!/usr/bin/env python3
"""Focused release assembler tests, including tamper and mismatch gates."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("assemble-release.py")
SPEC = importlib.util.spec_from_file_location("assemble_release", SCRIPT)
assert SPEC and SPEC.loader
ASSEMBLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSEMBLER)


class AssemblerFixtureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.source = self.root / "native"
        self.source.mkdir()
        (self.source / ".git").mkdir()
        (self.source / "ap").mkdir()
        (self.source / "ap" / "ap_version.h").write_text(
            '#define CTR_AP_COMPAT_VERSION "v0.2.0"\n#define CTR_AP_VERSION "v0.2.0-rc-prep1"\n'
        )
        self.commit = "a" * 40
        subprocess.run(["git", "-C", str(self.source), "init", "-q"], check=True)
        subprocess.run(["git", "-C", str(self.source), "config", "user.email", "test@example.invalid"], check=True)
        subprocess.run(["git", "-C", str(self.source), "config", "user.name", "Release Test"], check=True)
        subprocess.run(["git", "-C", str(self.source), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.source), "commit", "-qm", "fixture"], check=True)
        self.commit = subprocess.check_output(["git", "-C", str(self.source), "rev-parse", "HEAD"], text=True).strip()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write_sidecar(self, path: Path) -> None:
        path.with_name(path.name + ".sha256").write_text(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        )

    def write_build_archive(self, platform: str, source_commit: str | None = None) -> tuple[Path, Path]:
        artifact = self.root / platform
        artifact.mkdir()
        binary_name = "ctr_native_ap.exe" if platform == "windows" else "ctr_native_ap"
        debug_name = binary_name + ".debug"
        binary = (
            b"Crash Team Racing - CTR-AP v0.2.0-rc-prep1\0"
            b"[AP BOOT] ===== client run start ===== (v0.2.0-rc-prep1)\0"
            b'  "client": "v0.2.0-rc-prep1",'
        )
        debug = b"debug"
        metadata = {
            "source_commit": source_commit or self.commit,
            "platform": platform,
            "variant": "ap",
            "executable_sha256": hashlib.sha256(binary).hexdigest(),
            "debug_sha256": hashlib.sha256(debug).hexdigest(),
        }
        archive = artifact / f"ctr-ap-{platform}-x86-{self.commit[:12]}.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            root = f"ctr-ap-{platform}-x86-{self.commit[:12]}"
            bundle.writestr(f"{root}/{binary_name}", binary)
            bundle.writestr(f"{root}/BUILD.json", json.dumps(metadata))
        self.write_sidecar(archive)
        debug_path = artifact / debug_name
        debug_path.write_bytes(debug)
        self.write_sidecar(debug_path)
        return artifact, debug_path

    def test_tampered_artifact_sidecar_is_rejected(self) -> None:
        artifact, _ = self.write_build_archive("windows")
        (artifact / "ctr_native_ap.exe.debug.sha256").write_text("0" * 64 + "  ctr_native_ap.exe.debug\n")
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.check_checksum(artifact / "ctr_native_ap.exe.debug")

    def test_provenance_mismatch_is_rejected(self) -> None:
        artifact, debug = self.write_build_archive("linux", source_commit="b" * 40)
        archive = next(artifact.glob("ctr-ap-linux-x86-*.zip"))
        unpacked = self.root / "unpacked"
        binary, metadata = ASSEMBLER.unpack_artifact(archive, "linux", unpacked)
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.validate_build(self.source, "linux", archive, debug, binary, metadata)

    def test_traversal_member_is_rejected(self) -> None:
        archive = self.root / "bad.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("root/../../outside", b"x")
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.unpack_artifact(archive, "windows", self.root / "out")

    def test_tracked_source_change_is_rejected_before_packaging(self) -> None:
        header = self.source / "ap" / "ap_version.h"
        header.write_text(header.read_text() + "// changed\n")
        args = type("Args", (), {
            "version": "0.2.0-rc-prep1",
            "native_source": self.source,
            "windows_artifacts": self.root / "windows",
            "linux_artifacts": self.root / "linux",
            "apworld": self.root / "ctr.apworld",
            "template": self.root / "Crash.Team.Racing.yaml",
            "output_dir": self.root / "release",
        })()
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.assemble(args)


if __name__ == "__main__":
    unittest.main()
