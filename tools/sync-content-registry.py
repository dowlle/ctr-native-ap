#!/usr/bin/env python3
"""Check or rewrite the named-block table and the cup pool in ap/ap_content_registry.hpp.

The table maps each registry track key that has a named id block to its block
index and frozen room name (`ap_name`). It is copied from the paired apworld's
worlds/ctr/data/named_tracks.json, so native can require a content plan's
custom track to use exactly the block its key owns (#133 PR 2 review). The `cups`
pool is copied the same way from worlds/ctr/cup_checks.py (CUP_NAMES in code
order with each name's Win and Win All Legs codes), so a cup name appended in
the apworld reaches native through this tool and not by hand.

  tools/sync-content-registry.py --apworld <paired checkout>          check, exit 1 on drift
  tools/sync-content-registry.py --apworld <paired checkout> --write  rewrite the table

tools/test-content-plan-parity.py runs the same comparison through the
apworld's own loader.
"""
import argparse
import ast
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


def cup_pool(apworld: Path):
    """The apworld's CUP_NAMES with their location codes, read from worlds/ctr/cup_checks.py
    as source (no import, so no Archipelago environment is needed). Index i < 27 owns
    base + i; a later name takes the growth range, base + 100 + (i - 27), because
    35027027 is Cortex Vortex's Cup Leg Victory (cup_checks._pool_code).
    tools/test-content-plan-parity.py checks the result against the live world."""
    tree = ast.parse((apworld / "worlds/ctr/cup_checks.py").read_text(encoding="utf-8"))
    consts = {}
    for node in tree.body:
        target = node.target if isinstance(node, ast.AnnAssign) else node.targets[0] if isinstance(node, ast.Assign) else None
        if isinstance(target, ast.Name) and target.id in ("CUP_NAMES", "CUP_WIN_CODE_BASE", "CUP_WIN_GROWTH_BASE",
                                                          "CUP_SWEEP_CODE_BASE", "CUP_SWEEP_GROWTH_BASE"):
            consts[target.id] = ast.literal_eval(node.value)

    def code(base, growth, i):
        return base + i if i < 27 else growth + i - 27
    return [dict(name=n, win=code(consts["CUP_WIN_CODE_BASE"], consts["CUP_WIN_GROWTH_BASE"], i),
                 sweep=code(consts["CUP_SWEEP_CODE_BASE"], consts["CUP_SWEEP_GROWTH_BASE"], i))
            for i, n in enumerate(consts["CUP_NAMES"])]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apworld", type=Path, required=True)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    text = HEADER.read_text(encoding="utf-8")
    match = PATTERN.search(text)
    registry = json.loads(match[2])
    expected = named_blocks(args.apworld)
    cups = cup_pool(args.apworld)
    sanity = json.loads((args.apworld / "worlds/ctr/data/sanity_needs.json").read_text())
    exclusions = json.loads((args.apworld / "worlds/ctr/data/crate_exclusions.json").read_text())
    object_rulings = json.loads((args.apworld / "worlds/ctr/data/object_rulings.json").read_text())
    data = json.loads((args.apworld / "worlds/ctr/data/locations.json").read_text())
    box_tracks = [n for _, n in sorted((r["code"], r["region"]) for r in data if r["name"].endswith(": Sapphire Time Trial") and r["code"] is not None)]
    bases = {name: [35029000 + 32 * i, 35029600 + 16 * i, 35026300 + 16 * i] for i, name in enumerate(box_tracks)}
    bases["Cortex Vortex"] = [35026100, 35026180, 35026200]
    bases.update({t["name"]: [37200000 + 128 * t["block"] + off for off in (0, 80, 96)] for t in expected})
    if (registry.get("named_blocks") == expected and registry.get("cups") == cups and registry.get("sanity") == sanity
            and registry.get("sanity_exclusions") == exclusions and registry.get("sanity_locations") == bases
            and registry.get("object_rulings") == object_rulings):
        print(f"PASS named-block table and cup pool match the apworld ({len(expected)} blocks, {len(cups)} cups)")
        return 0
    if not args.write:
        print("FAIL named-block table or cup pool differs from the apworld; rerun with --write", file=sys.stderr)
        return 1
    registry["named_blocks"] = expected
    registry["cups"] = cups
    registry["sanity"] = sanity
    registry["sanity_exclusions"] = exclusions
    registry["object_rulings"] = object_rulings
    registry["sanity_locations"] = bases
    body = "\n" + json.dumps(registry, indent=2) + "\n"
    HEADER.write_text(text[:match.start(2)] + body + text[match.end(2):], encoding="utf-8")
    print(f"WROTE {len(expected)} named blocks and {len(cups)} cups to {HEADER.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
