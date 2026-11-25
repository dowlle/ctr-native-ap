"""Locate the release inputs in a matching apworld source checkout."""

import argparse
from contextlib import contextmanager
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def archive_members(path: Path) -> dict[str, bytes]:
    with zipfile.ZipFile(path) as archive:
        entries = [info for info in archive.infolist() if not info.is_dir()]
        members = {info.filename: archive.read(info) for info in entries}
    if len(members) != len(entries):
        raise ValueError(f"duplicate apworld member in {path}")
    return members


@contextmanager
def paired_checkout(apworld: Path, checkout: Path, ref: str):
    """Read the named Git tree and check all packed members before using it."""
    git = ["git", "-C", str(checkout)]
    commit = subprocess.check_output(
        [*git, "rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}"], text=True
    ).strip()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        source_tar = root / "source.tar"
        subprocess.run([*git, "archive", f"--output={source_tar}", commit, "worlds/ctr"], check=True)
        with tarfile.open(source_tar) as archive:
            archive.extractall(root, filter="data")
        rebuilt = root / "ctr.apworld"
        subprocess.run([sys.executable, str(root / "worlds/ctr/tools/build_apworld.py"), str(rebuilt)],
                       check=True, stdout=subprocess.DEVNULL)
        packed = archive_members(apworld)
        if packed != archive_members(rebuilt):
            raise ValueError(f"packed apworld differs from the repack of {commit}")
        print(f"Apworld {commit}: all {len(packed)} packed files match the source")
        yield root


def solo_source_files(checkout: Path) -> tuple[Path, dict[str, Path]]:
    world = checkout / "worlds" / "ctr"
    solo = world / "tools" / "solo"
    if not solo.exists():
        # Older paired tags and native-only rehearsals still use this layout.
        solo = world / "solo"

    files = {
        "ctr_solo.py": solo / "ctr_solo.py",
        "options-overlay.json": solo / "options-overlay.json",
        "LICENSES/ctr-apworld-LICENSE.txt": world / "LICENSE",
    }
    if not (solo / "presets" / "presets.json").is_file():
        raise FileNotFoundError(f"missing Solo preset index in {solo}")
    files.update((path.relative_to(solo).as_posix(), path)
                 for path in sorted((solo / "presets").rglob("*")) if path.is_file())
    for path in (*files.values(), solo / "trim_core.py"):
        if not path.is_file():
            raise FileNotFoundError(f"missing Solo source file: {path}")
    return solo, files


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apworld", type=Path)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("commit", help="exact apworld commit or paired tag")
    args = parser.parse_args()
    with paired_checkout(args.apworld, args.checkout, args.commit) as source:
        solo, _ = solo_source_files(source)
        print(f"Play Solo source: {solo.relative_to(source).as_posix()}")
