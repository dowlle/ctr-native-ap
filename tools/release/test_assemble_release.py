#!/usr/bin/env python3
"""Focused release assembler tests, including tamper and mismatch gates."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import io
import subprocess
import tarfile
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

    ASSET_MEMBERS = (
        "assets/tracks/cortex-vortex/CVortex Arcade All.lev",
        "assets/tracks/cortex-vortex/CVortex Arcade All.vrm",
    )

    def write_build_archive(
        self,
        platform: str,
        source_commit: str | None = None,
        assets: bool = False,
        extra: dict[str, bytes] | None = None,
    ) -> tuple[Path, Path]:
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
            if assets:
                bundle.writestr(f"{root}/BUILD-NOTICE.txt", b"internal build, not a release\n")
                for member in self.ASSET_MEMBERS:
                    bundle.writestr(f"{root}/{member}", member.encode())
            for member, payload in (extra or {}).items():
                bundle.writestr(f"{root}/{member}", payload)
        self.write_sidecar(archive)
        debug_path = artifact / debug_name
        debug_path.write_bytes(debug)
        self.write_sidecar(debug_path)
        return artifact, debug_path


    def write_build_tarball(self, platform: str = "linux") -> Path:
        """Mirror write_build_archive as a tar.gz, the real Linux CI shape."""
        artifact, _ = self.write_build_archive(platform, assets=True)
        source_zip = next(artifact.glob(f"ctr-ap-{platform}-x86-*.zip"))
        tarball = artifact / f"ctr-ap-{platform}-x86-{self.commit[:12]}.tar.gz"
        root = f"ctr-ap-{platform}-x86-{self.commit[:12]}"
        with zipfile.ZipFile(source_zip) as bundle, tarfile.open(tarball, "w:gz") as out:
            directory = tarfile.TarInfo(root)
            directory.type = tarfile.DIRTYPE
            directory.mode = 0o755
            out.addfile(directory)
            for name in bundle.namelist():
                data = bundle.read(name)
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o644
                out.addfile(info, io.BytesIO(data))
        source_zip.unlink()
        (artifact / (source_zip.name + ".sha256")).unlink()
        self.write_sidecar(tarball)
        return tarball

    def test_nested_assets_are_carried_and_build_notice_is_dropped(self) -> None:
        artifact, _ = self.write_build_archive("windows", assets=True)
        archive = next(artifact.glob("ctr-ap-windows-x86-*.zip"))
        unpacked = self.root / "unpacked-windows"
        binary, _, assets = ASSEMBLER.unpack_artifact(archive, "windows", unpacked)
        self.assertEqual([name for _, name in assets], list(self.ASSET_MEMBERS))
        for path, name in assets:
            self.assertTrue(path.is_file())
            self.assertEqual(path.read_bytes(), name.encode())
        self.assertFalse((unpacked / "BUILD-NOTICE.txt").exists())
        self.assertTrue(binary.is_file())

    def test_nested_assets_in_tarball_are_carried(self) -> None:
        tarball = self.write_build_tarball()
        unpacked = self.root / "unpacked-linux"
        binary, _, assets = ASSEMBLER.unpack_artifact(tarball, "linux", unpacked)
        self.assertEqual([name for _, name in assets], list(self.ASSET_MEMBERS))
        self.assertFalse((unpacked / "BUILD-NOTICE.txt").exists())
        self.assertEqual(binary.stat().st_mode & 0o777, 0o755)

    def test_unexpected_nested_directory_is_rejected(self) -> None:
        artifact, _ = self.write_build_archive("windows", extra={"secrets/key.txt": b"nope"})
        archive = next(artifact.glob("ctr-ap-windows-x86-*.zip"))
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.unpack_artifact(archive, "windows", self.root / "out-nested")

    def test_unexpected_root_member_is_still_rejected(self) -> None:
        artifact, _ = self.write_build_archive("windows", extra={"surprise.txt": b"nope"})
        archive = next(artifact.glob("ctr-ap-windows-x86-*.zip"))
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.unpack_artifact(archive, "windows", self.root / "out-root")

    def test_bundles_carry_assets_with_relative_paths_and_modes(self) -> None:
        artifact, _ = self.write_build_archive("linux", assets=True)
        archive = next(artifact.glob("ctr-ap-linux-x86-*.zip"))
        unpacked = self.root / "bundle-src"
        binary, _, assets = ASSEMBLER.unpack_artifact(archive, "linux", unpacked)
        helper = self.root / "support-bundle.sh"
        helper.write_text("#!/bin/sh\n")
        helper.chmod(0o755)
        entries = [(binary, "ctr_native_ap"), (helper, "support-bundle.sh"), *assets]
        root_name = "ctr-archipelago-v0.2.1-alpha1"

        tarball = self.root / "bundle.tar.gz"
        ASSEMBLER.create_archive(tarball, root_name, entries, linux=True)
        with tarfile.open(tarball) as bundle:
            modes = {info.name: (info.mode, info.isdir()) for info in bundle.getmembers()}
        for member in self.ASSET_MEMBERS:
            self.assertIn(f"{root_name}/{member}", modes)
            self.assertEqual(modes[f"{root_name}/{member}"][0], 0o644)
        self.assertEqual(modes[f"{root_name}/ctr_native_ap"][0], 0o755)
        self.assertEqual(modes[f"{root_name}/support-bundle.sh"][0], 0o755)
        for directory in ("assets", "assets/tracks", "assets/tracks/cortex-vortex"):
            self.assertEqual(modes[f"{root_name}/{directory}"], (0o755, True))

        zip_path = self.root / "bundle.zip"
        ASSEMBLER.create_archive(zip_path, root_name, entries, linux=False)
        with zipfile.ZipFile(zip_path) as bundle:
            names = bundle.namelist()
            for member in self.ASSET_MEMBERS:
                self.assertIn(f"{root_name}/{member}", names)
                self.assertEqual(bundle.read(f"{root_name}/{member}"), member.encode())

    def test_tampered_artifact_sidecar_is_rejected(self) -> None:
        artifact, _ = self.write_build_archive("windows")
        (artifact / "ctr_native_ap.exe.debug.sha256").write_text("0" * 64 + "  ctr_native_ap.exe.debug\n")
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.check_checksum(artifact / "ctr_native_ap.exe.debug")

    def test_provenance_mismatch_is_rejected(self) -> None:
        artifact, debug = self.write_build_archive("linux", source_commit="b" * 40)
        archive = next(artifact.glob("ctr-ap-linux-x86-*.zip"))
        unpacked = self.root / "unpacked"
        binary, metadata, _ = ASSEMBLER.unpack_artifact(archive, "linux", unpacked)
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
