#!/usr/bin/env python3
"""Assemble the solo runtime folder for one platform (#89).

    build-runtime.py --platform windows|linux --apworld ctr.apworld --out DIR [--cache DIR]

The result is the `solo-runtime/` folder that goes into the client archive
beside ctr_native_ap and ctr.apworld:

    python/          the pinned interpreter (python.org embeddable on Windows,
                     python-build-standalone on Linux), trimmed
    site-packages/   the pinned dependency wheels, unpacked
    archipelago/     the trimmed Archipelago core at the pinned commit
    ctr_solo.py      the wrapper, presets/ and options-overlay.json, taken
                     from ctr/solo/ inside the given ctr.apworld
    LICENSES/        license texts of every bundled part
    RUNTIME.txt      versions and SHA-256 of every pinned part

Every download is checked against tools/solo/runtime-lock.json. Nothing here
runs the interpreter it ships. Bytecode is precompiled with the build host's
CPython 3.12 using unchecked hashes, so the runtime never writes .pyc files.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import py_compile
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOCK = HERE / "runtime-lock.json"
INTERPRETER = {"windows": "python/python.exe", "linux": "python/bin/python3.12"}
# Parts of the Linux interpreter the runtime never uses.
LINUX_DROP = ("include", "share", "bin/2to3", "bin/2to3-3.12", "bin/idle3", "bin/idle3.12", "bin/pydoc3",
              "bin/pydoc3.12", "bin/python3-config", "bin/python3.12-config", "lib/pkgconfig")
# Tcl/Tk support folders (tkinter is not shipped).
LINUX_DROP_GLOBS = ("lib/itcl*", "lib/tcl*", "lib/tk*", "lib/Tix*", "lib/thread*")
STDLIB_DROP = ("test", "idlelib", "tkinter", "turtledemo", "ensurepip", "lib2to3", "pydoc_data",
               "site-packages", "venv", "config-3.12-x86_64-linux-gnu", "__phello__", "turtle.py")
SOLO_MEMBERS = ("ctr_solo.py", "trim_core.py", "options-overlay.json")


class BuildError(Exception):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(url: str, expected: str, cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / f"{expected[:16]}-{url.rsplit('/', 1)[-1].replace('%2B', '+')}"
    if not target.is_file() or sha256(target) != expected:
        tmp = target.with_suffix(target.suffix + ".part")
        with urllib.request.urlopen(url, timeout=120) as response, open(tmp, "wb") as out:
            shutil.copyfileobj(response, out)
        actual = sha256(tmp)
        if actual != expected:
            tmp.unlink()
            raise BuildError(f"{url}: SHA-256 {actual}, lock says {expected}")
        tmp.replace(target)
    return target


def safe_extract_zip(archive: Path, dest: Path) -> None:
    with zipfile.ZipFile(archive) as bundle:
        for info in bundle.infolist():
            name = Path(info.filename)
            if name.is_absolute() or ".." in name.parts:
                raise BuildError(f"unsafe member in {archive.name}: {info.filename}")
        bundle.extractall(dest)


def safe_extract_tar(archive: Path, dest: Path) -> None:
    with tarfile.open(archive) as bundle:
        bundle.extractall(dest, filter="data")


def remove(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def build_python(platform: str, entry: dict, cache: Path, runtime: Path, licenses: Path) -> None:
    archive = fetch(entry["url"], entry["sha256"], cache)
    target = runtime / "python"
    if platform == "windows":
        target.mkdir()
        safe_extract_zip(archive, target)
        # The embeddable package reads sys.path from python312._pth. Keep the
        # stdlib zip and the folder; the wrapper adds its own paths.
        pth = target / "python312._pth"
        if not pth.is_file():
            raise BuildError("embeddable package has no python312._pth")
        pth.write_text("python312.zip\n.\n", encoding="ascii")
        shutil.copyfile(target / "LICENSE.txt", licenses / "CPython-LICENSE.txt")
    else:
        with tempfile.TemporaryDirectory() as tmp:
            safe_extract_tar(archive, Path(tmp))
            shutil.move(str(Path(tmp) / "python"), target)
        for rel in LINUX_DROP:
            remove(target / rel)
        stdlib = target / "lib" / "python3.12"
        for rel in STDLIB_DROP:
            remove(stdlib / rel)
        for dynload in (stdlib / "lib-dynload").glob("_tkinter*"):
            dynload.unlink()
        for pattern in LINUX_DROP_GLOBS:
            for path in target.glob(pattern):
                remove(path)
        for lib in (target / "lib").glob("lib*t[ck]*8.6*"):
            remove(lib)
        shutil.copyfile(stdlib / "LICENSE.txt", licenses / "CPython-LICENSE.txt")
        # Release archives carry regular files only. The interpreter is
        # python3.12 and loads libpython3.12.so.1.0; the aliases are links.
        for link in [p for p in target.rglob("*") if p.is_symlink()]:
            link.unlink()
    if not (runtime / INTERPRETER[platform]).is_file():
        raise BuildError(f"interpreter missing after extraction: {INTERPRETER[platform]}")


def install_wheels(platform: str, wheels: dict, cache: Path, runtime: Path, licenses: Path) -> list[str]:
    site = runtime / "site-packages"
    site.mkdir()
    lines = []
    for name, entry in sorted(wheels.items()):
        pin = entry.get("any") or entry.get(platform)
        if not pin:
            raise BuildError(f"no {platform} wheel pinned for {name}")
        wheel = fetch(pin["url"], pin["sha256"], cache)
        safe_extract_zip(wheel, site)
        lines.append(f"wheel {name}: {entry['version']} {entry['license']} sha256 {pin['sha256']}")
    for dist in sorted(site.glob("*.dist-info")):
        for lic in sorted(p for p in dist.rglob("*") if p.is_file() and
                          p.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE"))):
            shutil.copyfile(lic, licenses / f"{dist.name.split('-')[0]}-{lic.name}")
    for script in ("bin", "Scripts"):
        remove(site / script)
    return lines


def build_core(commit: str, repository: str, runtime: Path, solo: Path, licenses: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "archipelago"
        subprocess.run(["git", "init", "-q", str(source)], check=True)
        subprocess.run(["git", "-C", str(source), "fetch", "-q", "--depth", "1", repository, commit], check=True)
        subprocess.run(["git", "-C", str(source), "checkout", "-q", "FETCH_HEAD"], check=True)
        head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if head != commit:
            raise BuildError(f"Archipelago checkout is {head}, lock says {commit}")
        subprocess.run([sys.executable, str(solo / "trim_core.py"), str(source), str(runtime / "archipelago")],
                       check=True, stdout=subprocess.DEVNULL)
    shutil.copyfile(runtime / "archipelago" / "LICENSE", licenses / "Archipelago-LICENSE.txt")


def extract_solo(apworld: Path, dest: Path) -> None:
    """Copy ctr/solo/ out of the release apworld. It is the only source."""
    with zipfile.ZipFile(apworld) as bundle:
        members = [m for m in bundle.namelist() if m.startswith("ctr/solo/") and not m.endswith("/")]
        if not members:
            raise BuildError("ctr.apworld has no ctr/solo/ folder: it predates solo mode")
        for member in members:
            rel = Path(member).relative_to("ctr/solo")
            if ".." in rel.parts:
                raise BuildError(f"unsafe member {member}")
            target = dest / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(bundle.read(member))
        lic = "ctr/LICENSE"
        if lic in bundle.namelist():
            (dest / "LICENSE").write_bytes(bundle.read(lic))


def precompile(runtime: Path) -> int:
    if sys.version_info[:2] != (3, 12):
        raise BuildError("precompile needs a CPython 3.12 build host (same bytecode as the runtime)")
    count = 0
    folders = [("archipelago", True), ("site-packages", True)]
    if (runtime / "python" / "lib" / "python3.12").is_dir():
        # Linux stdlib. Windows ships python312.zip with its bytecode.
        folders.append(("python/lib/python3.12", False))
    for folder, strict in folders:
        for path in sorted((runtime / folder).rglob("*.py")):
            try:
                py_compile.compile(str(path), doraise=True,
                                   invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
            except py_compile.PyCompileError:
                if strict:
                    raise
                continue  # a few stdlib test fixtures are invalid on purpose
            count += 1
    return count


def manifest_hashes(runtime: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(p for p in runtime.rglob("*") if p.is_file() and p.name != "RUNTIME.txt"):
        rel = path.relative_to(runtime).as_posix()
        digest.update(rel.encode() + b"\0" + sha256(path).encode() + b"\n")
    return digest.hexdigest()


def build(platform: str, apworld: Path, out: Path, cache: Path) -> Path:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    if out.exists():
        raise BuildError(f"{out} exists; use a fresh output folder")
    runtime = out
    runtime.mkdir(parents=True)
    licenses = runtime / "LICENSES"
    licenses.mkdir()
    with tempfile.TemporaryDirectory() as tmp:
        solo = Path(tmp) / "solo"
        extract_solo(apworld, solo)
        for name in ("ctr_solo.py", "options-overlay.json"):
            shutil.copyfile(solo / name, runtime / name)
        shutil.copytree(solo / "presets", runtime / "presets")
        if (solo / "LICENSE").is_file():
            shutil.copyfile(solo / "LICENSE", licenses / "ctr-apworld-LICENSE.txt")
        python = lock["python"]
        build_python(platform, python[platform], cache, runtime, licenses)
        wheel_lines = install_wheels(platform, lock["wheels"], cache, runtime, licenses)
        core = lock["archipelago"]
        build_core(core["commit"], core["repository"], runtime, solo, licenses)
    compiled = precompile(runtime)
    apworld_sha = sha256(apworld)
    lines = [
        "# CTR Archipelago solo runtime. Do not edit; the client checks these values.",
        "schema: 1",
        f"platform: {platform}",
        f"interpreter: {INTERPRETER[platform]}",
        f"python: {python['version']} {python[platform]['kind']} sha256 {python[platform]['sha256']}",
        f"archipelago: {core['version']} {core['commit']}",
        f"ctr.apworld sha256: {apworld_sha}",
        f"ctr_solo.py sha256: {sha256(runtime / 'ctr_solo.py')}",
        f"runtime-lock sha256: {sha256(LOCK)}",
        *wheel_lines,
        f"precompiled: {compiled}",
    ]
    lines.append(f"tree sha256: {manifest_hashes(runtime)}")
    (runtime / "RUNTIME.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return runtime


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--apworld", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=Path(tempfile.gettempdir()) / "ctr-solo-runtime-cache")
    args = parser.parse_args()
    try:
        runtime = build(args.platform, args.apworld.resolve(), args.out.resolve(), args.cache.resolve())
    except (BuildError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile, tarfile.TarError) as error:
        print(f"solo runtime build failed: {error}", file=sys.stderr)
        return 1
    print((runtime / "RUNTIME.txt").read_text(encoding="utf-8"), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
