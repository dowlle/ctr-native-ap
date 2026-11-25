"""Locate the release inputs in a matching apworld source checkout."""

from pathlib import Path


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
