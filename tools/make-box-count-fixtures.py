"""Regenerate synthetic capacity rooms with a paired apworld checkout.

Run with the AP Python environment: python tools/make-box-count-fixtures.py PATH
The synthetic spot files are never included in a release.
"""
import json
import os
from pathlib import Path
import sys

os.environ["SKIP_REQUIREMENTS_UPDATE"] = "1"
sys.path.insert(0, str(Path(sys.argv[1]).resolve()))
from worlds.ctr.test.test_item_box_count import fifty_spots, count_world

out = Path(__file__).resolve().parent / "fixtures/content-plan"
with fifty_spots(custom=True):
    for count in (0, 1, 15, 50):
        (out / f"box-count-{count}.json").write_text(
            json.dumps(count_world(count).fill_slot_data(), separators=(",", ":")) + "\n")
