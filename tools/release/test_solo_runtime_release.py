#!/usr/bin/env python3
"""Solo runtime (#89) in the release: assembler entries, archive modes and the
verifier's --require-solo-runtime check. Uses small fake runtimes; the real
runtime build is tools/solo/build-runtime.py."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import os
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ASSEMBLER = load("assemble_release_solo", HERE / "assemble-release.py")
VERIFIER = load("verify_release_solo", HERE.parent / "verify-release.py")
BUILDER = load("build_runtime_solo", HERE.parent / "solo" / "build-runtime.py")
INTERPRETER = {"windows": "python/python.exe", "linux": "python/bin/python3.12"}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class SoloRuntimeReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.apworld = self.root / "ctr.apworld"
        self.apworld.write_bytes(b"apworld-bytes")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def runtime(self, platform: str, apworld_sha: str | None = None, name: str | None = None) -> Path:
        folder = self.root / (name or f"rt-{platform}")
        # Names whose string order differs from path order ("a.b" vs "a/b").
        for rel in (INTERPRETER[platform], "ctr_solo.py", "archipelago/MultiServer.py", "presets/standard.yaml",
                    "python/lib-dynload/x.so", "python/lib.txt", "python-x/y"):
            path = folder / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(rel.encode())
        files = [(p.relative_to(folder).as_posix(), sha(p.read_bytes())) for p in folder.rglob("*") if p.is_file()]
        (folder / "RUNTIME.txt").write_text(
            "# test\nschema: 1\n"
            f"platform: {platform}\ninterpreter: {INTERPRETER[platform]}\n"
            f"ctr.apworld sha256: {apworld_sha or sha(self.apworld.read_bytes())}\n"
            f"ctr_solo.py sha256: {sha((folder / 'ctr_solo.py').read_bytes())}\n"
            f"tree sha256: {ASSEMBLER.runtime_tree_sha256(files)}\n", encoding="utf-8")
        return folder

    def test_entries_accept_a_matching_runtime(self) -> None:
        entries = ASSEMBLER.solo_runtime_entries(self.runtime("linux"), "linux", self.apworld)
        names = {name for _, name in entries}
        self.assertIn("solo-runtime/RUNTIME.txt", names)
        self.assertIn("solo-runtime/python/bin/python3.12", names)
        self.assertTrue(all(name.startswith("solo-runtime/") for name in names))

    def test_entries_refuse_another_apworld(self) -> None:
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.solo_runtime_entries(self.runtime("windows", apworld_sha="0" * 64), "windows", self.apworld)

    def test_entries_refuse_the_wrong_platform(self) -> None:
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.solo_runtime_entries(self.runtime("linux"), "windows", self.apworld)

    def test_entries_refuse_a_symlink(self) -> None:
        runtime = self.runtime("linux")
        os.symlink("python3.12", runtime / "python" / "bin" / "python3")
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.solo_runtime_entries(runtime, "linux", self.apworld)

    def test_entries_refuse_a_missing_interpreter(self) -> None:
        runtime = self.runtime("windows")
        (runtime / INTERPRETER["windows"]).unlink()
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.solo_runtime_entries(runtime, "windows", self.apworld)

    def test_entries_refuse_a_changed_file(self) -> None:
        runtime = self.runtime("windows")
        (runtime / "archipelago" / "MultiServer.py").write_text("changed")
        with self.assertRaises(ASSEMBLER.AssemblyError):
            ASSEMBLER.solo_runtime_entries(runtime, "windows", self.apworld)

    def test_tree_digest_matches_the_builder(self) -> None:
        runtime = self.runtime("linux")
        self.assertEqual(BUILDER.manifest_hashes(runtime),
                         ASSEMBLER.read_runtime_manifest((runtime / "RUNTIME.txt").read_text())["tree sha256"])

    def test_required_from_0_3_0(self) -> None:
        self.assertFalse(ASSEMBLER.solo_runtime_required("0.2.4"))
        self.assertTrue(ASSEMBLER.solo_runtime_required("0.3.0"))
        self.assertTrue(ASSEMBLER.solo_runtime_required("0.3.0-rc1"))
        self.assertTrue(VERIFIER.solo_runtime_required("v1.0.0"))
        self.assertFalse(VERIFIER.solo_runtime_required("v0.2.9-alpha1"))

    def test_linux_interpreter_is_executable_in_the_tarball(self) -> None:
        entries = ASSEMBLER.solo_runtime_entries(self.runtime("linux"), "linux", self.apworld)
        tarball = self.root / "client.tar.gz"
        ASSEMBLER.create_archive(tarball, "root", entries, linux=True)
        with tarfile.open(tarball) as bundle:
            modes = {m.name: m.mode for m in bundle.getmembers()}
        self.assertEqual(modes["root/solo-runtime/python/bin/python3.12"], 0o755)
        self.assertEqual(modes["root/solo-runtime/ctr_solo.py"], 0o644)

    def write_release(self, tag: str, windows_runtime: Path | None, linux_runtime: Path | None,
                      bundled_apworld: bytes | None = None) -> Path:
        release = self.root / "release"
        release.mkdir()
        (release / "ctr.apworld").write_bytes(self.apworld.read_bytes())
        apworld_entry = self.root / "bundled.apworld"
        apworld_entry.write_bytes(bundled_apworld if bundled_apworld is not None else self.apworld.read_bytes())
        for platform, runtime in (("windows", windows_runtime), ("linux", linux_runtime)):
            entries = [(apworld_entry, "ctr.apworld")]
            if runtime is not None:
                entries += [(p, f"solo-runtime/{p.relative_to(runtime).as_posix()}")
                            for p in sorted(runtime.rglob("*")) if p.is_file()]
            suffix = ".zip" if platform == "windows" else ".tar.gz"
            ASSEMBLER.create_archive(release / f"ctr-archipelago-{tag}-{platform}-x86{suffix}",
                                     f"ctr-archipelago-{tag}", entries, linux=platform == "linux")
        return release

    def test_verifier_accepts_matching_runtimes(self) -> None:
        release = self.write_release("v0.3.0", self.runtime("windows"), self.runtime("linux"))
        VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_requires_the_runtime(self) -> None:
        release = self.write_release("v0.3.0", self.runtime("windows"), None)
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_refuses_a_runtime_for_another_apworld(self) -> None:
        release = self.write_release("v0.3.0", self.runtime("windows"),
                                     self.runtime("linux", apworld_sha="1" * 64))
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_refuses_a_changed_runtime_file(self) -> None:
        windows = self.runtime("windows")
        linux = self.runtime("linux")
        (linux / "ctr_solo.py").write_text("tampered")
        release = self.write_release("v0.3.0", windows, linux)
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_refuses_a_non_executable_interpreter_and_links(self) -> None:
        import tarfile
        release = self.write_release("v0.3.0", self.runtime("windows"), self.runtime("linux"))
        tarball = release / "ctr-archipelago-v0.3.0-linux-x86.tar.gz"
        repacked = self.root / "repacked.tar.gz"
        with tarfile.open(tarball) as src, tarfile.open(repacked, "w:gz") as out:
            for member in src.getmembers():
                data = src.extractfile(member).read() if member.isfile() else None
                if member.name.endswith("python/bin/python3.12"):
                    member.mode = 0o644
                out.addfile(member, io.BytesIO(data) if data is not None else None)
        repacked.replace(tarball)
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")
        with tarfile.open(tarball) as src, \
                tarfile.open(self.root / "linked.tar.gz", "w:gz") as out:
            for member in src.getmembers():
                data = src.extractfile(member).read() if member.isfile() else None
                if member.name.endswith("python/bin/python3.12"):
                    member.mode = 0o755
                out.addfile(member, io.BytesIO(data) if data is not None else None)
            link = tarfile.TarInfo("ctr-archipelago-v0.3.0/solo-runtime/python/bin/python3")
            link.type = tarfile.SYMTYPE
            link.linkname = "python3.12"
            out.addfile(link)
        (self.root / "linked.tar.gz").replace(tarball)
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_refuses_special_zip_members(self) -> None:
        release = self.write_release("v0.3.0", self.runtime("windows"), self.runtime("linux"))
        archive = release / "ctr-archipelago-v0.3.0-windows-x86.zip"
        with zipfile.ZipFile(archive, "a") as bundle:
            fifo = zipfile.ZipInfo("ctr-archipelago-v0.3.0/solo-runtime/pipe")
            fifo.external_attr = (0o010644) << 16  # a FIFO
            bundle.writestr(fifo, b"")
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_refuses_swapped_platforms(self) -> None:
        release = self.write_release("v0.3.0", self.runtime("linux", name="a"), self.runtime("windows", name="b"))
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_verifier_refuses_a_different_bundled_apworld(self) -> None:
        release = self.write_release("v0.3.0", self.runtime("windows"), self.runtime("linux"),
                                     bundled_apworld=b"other")
        with self.assertRaises(VERIFIER.PolicyError):
            VERIFIER.verify_solo_runtime(release, "v0.3.0")

    def test_builder_refuses_an_apworld_without_solo_files(self) -> None:
        old = self.root / "old.apworld"
        with zipfile.ZipFile(old, "w") as bundle:
            bundle.writestr("ctr/__init__.py", "")
        with self.assertRaises(BUILDER.BuildError):
            BUILDER.extract_solo(old, self.root / "solo")

    def test_builder_extracts_solo_files(self) -> None:
        apworld = self.root / "new.apworld"
        with zipfile.ZipFile(apworld, "w") as bundle:
            bundle.writestr("ctr/__init__.py", "")
            bundle.writestr("ctr/LICENSE", "MIT")
            bundle.writestr("ctr/solo/ctr_solo.py", "print()")
            bundle.writestr("ctr/solo/presets/standard.yaml", "name: Player")
        BUILDER.extract_solo(apworld, self.root / "solo")
        self.assertTrue((self.root / "solo" / "ctr_solo.py").is_file())
        self.assertTrue((self.root / "solo" / "presets" / "standard.yaml").is_file())
        self.assertEqual((self.root / "solo" / "LICENSE").read_text(), "MIT")

    def test_lock_pins_every_dependency_for_both_platforms(self) -> None:
        import json
        lock = json.loads((HERE.parent / "solo" / "runtime-lock.json").read_text(encoding="utf-8"))
        for platform in ("windows", "linux"):
            self.assertRegex(lock["python"][platform]["sha256"], r"^[0-9a-f]{64}$")
        self.assertRegex(lock["archipelago"]["commit"], r"^[0-9a-f]{40}$")
        for name, entry in lock["wheels"].items():
            self.assertTrue(entry.get("license"), name)
            for platform in ("windows", "linux"):
                pin = entry.get("any") or entry.get(platform)
                self.assertIsNotNone(pin, f"{name} {platform}")
                self.assertRegex(pin["sha256"], r"^[0-9a-f]{64}$")
                self.assertTrue(pin["url"].startswith("https://"))


if __name__ == "__main__":
    unittest.main()
