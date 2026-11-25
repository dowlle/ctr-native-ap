#!/usr/bin/env python3
"""Exercise the packaged Solo runtime through the client's Python launch paths."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(args: list[str], commands: str = "") -> list[dict]:
    result = subprocess.run(args, input=commands, stdout=subprocess.PIPE, text=True, timeout=120)
    print(result.stdout, end="")
    result.check_returncode()
    return [json.loads(line) for line in result.stdout.splitlines()]


def smoke(archive: Path, interpreter: str) -> None:
    header = Path(__file__).resolve().parents[2] / "ap" / "ap_solo_track_visibility.hpp"
    adapter = header.read_text(encoding="utf-8").split('R"PY(', 1)[1].split(')PY";', 1)[0]
    with tempfile.TemporaryDirectory() as folder:
        shutil.unpack_archive(archive, folder)
        (client,) = Path(folder).iterdir()
        runtime = client / "solo-runtime"
        digest = hashlib.sha256((client / "ctr.apworld").read_bytes()).hexdigest()
        python = [str(runtime / interpreter), "-I", "-B"]
        script = str(runtime / "ctr_solo.py")
        room = run([
            *python, "-c", adapter, script, "", "--apworld-sha256", digest,
            "generate", "--yaml", str(runtime / "presets" / "quick.yaml"),
            "--preset", "quick", "--seed", "10725", "--name", "AppieCTR",
        ])[-1]
        if (room["event"] != "result" or room["slot"] != "AppieCTR"
                or room["locations_total"] <= 0 or room["apworld_sha256"] != digest):
            raise SystemExit(f"unexpected Solo generation result: {room}")
        events = run([
            *python, script, "serve", "--room", room["room_id"], "--game-pid", str(os.getpid()),
        ], "/save\n/exit\n")
        ready, saved, stop = events
        if (ready["event"] != "ready" or ready["room_id"] != room["room_id"]
                or ready["slot"] != room["slot"] or not 38301 <= ready["port"] <= 38320
                or saved != {"event": "saved", "ok": True} or stop != {"event": "stopped", "reason": "exit"}):
            raise SystemExit(f"unexpected Solo server events: {events}")


if __name__ == "__main__":
    smoke(Path(sys.argv[1]), sys.argv[2])
