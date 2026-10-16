#!/usr/bin/env python3
"""Check or rewrite the named-block table in ap/ap_content_registry.hpp.

The table maps each registry track key that has a named id block to its block
index and frozen room name (`ap_name`). It is copied from the paired apworld's
worlds/ctr/data/named_tracks.json, so native can require a content plan's
custom track to use exactly the block its key owns (#133 PR 2 review).

  tools/sync-content-registry.py --apworld <paired checkout>          check, exit 1 on drift
  tools/sync-content-registry.py --apworld <paired checkout> --write  rewrite the table

tools/test-content-plan-parity.py runs the same comparison through the
apworld's own loader.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "ap" / "ap_content_registry.hpp"
PATTERN = re.compile(r'(R"CTRREG\()(.*)(\)CTRREG")', re.S)


def named_blocks(apworld: Path):
    document = json.loads((apworld / "worlds/ctr/data/named_tracks.json").read_text(encoding="utf-8"))
    return [dict(key=t["track_key"], block=t["block_index"], name=t["ap_name"])
            for t in sorted(document["tracks"], key=lambda t: t["track_key"])]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apworld", type=Path, required=True)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    text = HEADER.read_text(encoding="utf-8")
    match = PATTERN.search(text)
    registry = json.loads(match[2])
    expected = named_blocks(args.apworld)
    if registry.get("named_blocks") == expected:
        print(f"PASS named-block table matches the apworld ({len(expected)} blocks)")
        return 0
    if not args.write:
        print("FAIL named-block table differs from the apworld; rerun with --write", file=sys.stderr)
        return 1
    registry["named_blocks"] = expected
    body = "\n" + json.dumps(registry, indent=2) + "\n"
    HEADER.write_text(text[:match.start(2)] + body + text[match.end(2):], encoding="utf-8")
    print(f"WROTE {len(expected)} named blocks to {HEADER.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
