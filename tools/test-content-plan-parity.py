#!/usr/bin/env python3
"""Compare the production native parser with the paired generator, without a server.

Run with the AP virtualenv and --apworld <paired checkout> --parser <host harness>.
The C++ harness is also a standalone CI test using the checked-in real rooms.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys

args = argparse.ArgumentParser()
args.add_argument("--apworld", type=Path, required=True)
args.add_argument("--parser", type=Path, required=True)
args = args.parse_args()
os.environ["SKIP_REQUIREMENTS_UPDATE"] = "1"
sys.path.insert(0, str(args.apworld.resolve()))
from worlds.ctr import content_plan as cp
from worlds.ctr.test.test_content_plan import make_world, profile_options

root = Path(__file__).resolve().parents[1]
registry = json.loads(re.search(r'R"CTRREG\((.*)\)CTRREG"', (root / "ap/ap_content_registry.hpp").read_text(), re.S)[1])
expected = dict(packages=list(cp.PACKAGES), retail=[dict(id=i, name=n, location=cp.CTR_LOCATION_IDS[n + ": Trophy Race"])
                                                 for i, n in sorted(cp.RETAIL.items())],
                pads=[dict(physical=p, hub=h, keys=k) for p, h, k in cp.pad_registry()],
                custom_locations=[cp.CTR_LOCATION_IDS[f"Custom Track {i}: Trophy Race"] for i in range(1, 33)])
assert registry == expected, "native frozen registry differs from paired apworld"
cases = []
for fixture in ("baseline", "starts-extras"):
    cases.append((fixture, json.loads((root / f"tools/fixtures/content-plan/{fixture}.json").read_text()), True))
for seed in range(50):
    options = profile_options()
    if seed % 3 == 0:
        options["content_pool"]["tracks"]["custom"] = list(copy.deepcopy(cp.PACKAGES))
    if seed % 3 == 1:
        options["content_pool"]["tracks"]["custom"] = []
        options["content_pool"]["required_entries"] = []
    if seed % 2:
        options["item_pool"]["base"]["gems"] = ["red", "blue"]
        options["gems_required_goal"] = 2
        options["item_pool"]["extra"] = {"keys": 2, "gems": {"green": 1}}
        options["start_inventory_from_pool"] = {"Key": 2}
        options["start_inventory"] = {"Trophy": 3}
    world = make_world(options, seed=seed)
    cases.append((f"generated-{seed}", world.fill_slot_data(), True))

base = cases[0][1]
def alter(name, change):
    case = copy.deepcopy(base)
    change(case["content_plan"])
    try:
        cp.validate(case["content_plan"])
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError):
        accepted = False
    cases.append((name, case, accepted))

alter("duplicate check", lambda p: p["checks"].append(copy.deepcopy(p["checks"][0])))
alter("duplicate package", lambda p: p["packages"].append(copy.deepcopy(p["packages"][0])))
alter("duplicate pad", lambda p: p["pads"].__setitem__(0, copy.deepcopy(p["pads"][1])))
alter("foreign goal Gem", lambda p: p["goal"]["items"].__setitem__(0, 35010000))
alter("changed immutable bytes", lambda p: p["packages"][0]["files"][0].__setitem__("bytes", 2558169))
alter("copied capability claim", lambda p: p["packages"][0]["evidence"][0].__setitem__("level", "runtime"))
alter("missing route", lambda p: p["checks"][0].__setitem__("routes", []))
alter("arbitrary entry alias", lambda p: None)
for row in range(15):
    for key in ("base", "extra", "start_from_pool", "start_additional", "remaining", "receipt_cap", "requirement_ceiling"):
        alter(f"ledger-{row}-{key}", lambda p, row=row, key=key: p["items"]["rows"][row].__setitem__(key, p["items"]["rows"][row][key] + 1))
for row in range(32):
    alter(f"hub-{row}", lambda p, row=row: p["pads"][row].__setitem__("hub_id", "hub:99"))
for key in ("tracks", "entries", "pads", "checks", "requirements", "items"):
    alter("null-" + key, lambda p, key=key: p.__setitem__(key, None))

run = subprocess.run([str(args.parser.resolve()), "--probe"], input="".join(json.dumps(v) + "\n" for _, v, _ in cases),
                     text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, cwd=root)
answers = run.stdout.splitlines()
assert len(answers) == len(cases), (len(answers), len(cases), run.stderr[-1000:])
disagreements = [name for (name, _, expected), actual in zip(cases, answers) if (actual == "ACCEPT") != expected]
assert not disagreements, disagreements
print(f"PASS registry parity and {len(cases)} native/generator admission cases ({sum(x[2] for x in cases)} accepted)")
