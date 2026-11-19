"""Run the engine's grid, lane rebuild and weapon crate entry points headlessly."""

import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
with TemporaryDirectory(prefix="ctr-recorded-runtime-") as temp:
    temp = Path(temp)
    binary = temp / "recorded-race"
    bots = (root / "game/BOTS.c").read_text()
    tick = "void BOTS_ThTick_Drive(struct Thread *botThread)"
    start = bots.index(tick)
    movement = bots.index("\tbotDriver->turbo_MeterRoomLeft = 0;", start)
    end = bots.index("\n", movement)
    # Run the actual tick entry through its launch gate. The last assignment
    # tells the fixture when the rest of the normal drive tick would run.
    (temp / "recorded-race-tick-prefix.h").write_text(
        bots[start:end].replace(tick, "static void ExerciseRecordedTick(struct Thread *botThread)", 1) + "\n}\n"
    )
    subprocess.run([
        os.environ.get("CC", "cc"), "-m32", "-O0", "-ffunction-sections", "-fdata-sections",
        "-DCTR_NATIVE", "-DCTR_CUSTOM_TRACKS", "-DCTR_CUSTOM_PACKAGES", "-DCTR_AI_LAP_RECORDER",
        "-DCTR_RECORDED_RACE", "-DBUILD=926", "-Iinclude", "-I.", "-I", temp,
        "tools/fixtures/recorded-race-runtime.c", "-Wl,--gc-sections", "-lm", "-o", binary,
    ], cwd=root, check=True)
    for operation in ("race", "write", "present"):
        subprocess.run([binary, operation], cwd=temp, check=True)
    (temp / "ap-navpaths" / "navpath-6-001.navlap").unlink()
    subprocess.run([binary, "removed"], cwd=temp, check=True)
