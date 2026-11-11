#!/usr/bin/env python3
"""Generate the A4-52 admission fixtures from the paired apworld."""
import argparse
import json
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--apworld", type=Path, required=True)
args = parser.parse_args()
os.environ["SKIP_REQUIREMENTS_UPDATE"] = "1"
sys.path.insert(0, str(args.apworld.resolve()))
from worlds.ctr.test.test_object_rulings import options
from worlds.ctr.test.test_content_plan import make_world

rows = []
for difficulty in ("easy", "medium", "hard"):
    for blue in (False, True):
        for boost in ("off", "shared_global", "per_character"):
            world = make_world(options(difficulty, blue, boost), seed=52, stop_before="pre_fill")
            rows.append(dict(difficulty=difficulty, blue=blue, boost=boost, slot_data=world.fill_slot_data()))
target = Path(__file__).resolve().parent / "fixtures/content-plan/object-rulings.jsonl"
target.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows))
print("Wrote", len(rows), "object ruling rooms")
