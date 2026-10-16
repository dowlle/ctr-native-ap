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
from worlds.ctr.test.test_warp_pad_rules import pool as manual_pool

root = Path(__file__).resolve().parents[1]
registry = json.loads(re.search(r'R"CTRREG\((.*)\)CTRREG"', (root / "ap/ap_content_registry.hpp").read_text(), re.S)[1])
# Custom packages are not in the native registry: content plans admit any
# complete descriptor (#133 PR 2) and native checks the bytes on load.
expected = dict(retail=[dict(id=i, name=n, location=cp.CTR_LOCATION_IDS[n + ": Trophy Race"])
                                                 for i, n in sorted(cp.RETAIL.items())],
                pads=[dict(physical=p, hub=h, keys=k) for p, h, k in cp.pad_registry()],
                custom_locations=[cp.CTR_LOCATION_IDS[f"Custom Track {i}: Trophy Race"] for i in range(1, 33)])
assert registry == expected, "native frozen registry differs from paired apworld"
assert (root/'tools/fixtures/arcade-arenas-v1.json').read_bytes()==(args.apworld/'worlds/ctr/data/arcade_arenas_v1.json').read_bytes(), "arcade manifests differ across the pair"
cases = []
for fixture in ("baseline", "starts-extras"):
    cases.append((fixture, json.loads((root / f"tools/fixtures/content-plan/{fixture}.json").read_text()), True))
for seed in range(50):
    options = profile_options()
    if seed % 3 == 0:
        options["content_pool"]["tracks"]["custom"] = [copy.deepcopy(cp.PACKAGES[1])]  # both revisions share one named block since #306
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

# Manual pad requirements (#463): plans the paired generator emits for random
# rule maps (free, every concrete family, every Any family), at many seeds.
manual_maps = [
    {"Crash Cove": ("free", 0), "Red Cup": ("AnyGem", 2), "Dingo Canyon": ("AnyRelic", 4)},
    {"Roo's Tubes": ("Trophy", 5), "Skull Rock": ("Gold Relic", 2), "Turbo Track": ("Sapphire Relic", 1)},
    {"Tiger Temple": ("Key", 4), "Green Cup": ("Red Gem", 1), "Rocky Road": ("AnyGem", 5), "Sewer Speedway": ("free", 0)},
    {"Polar Pass": ("Trophy", 1)},
]
for seed in range(40):
    options = profile_options()
    options["item_pool"] = manual_pool()
    options["warp_pad_rules"] = {pad: dict(item=item, count=count)
                                 for pad, (item, count) in manual_maps[seed % len(manual_maps)].items()}
    if seed % 2:
        options["start_inventory"] = {"Trophy": 1}
    world = make_world(options, seed=seed)
    cases.append((f"manual-generated-{seed}", world.fill_slot_data(), True))
cases.append(("manual-fixture", json.loads((root / "tools/fixtures/content-plan/manual-rules.json").read_text()), True))

# All arena subsets and both Crystalsanity modes exercise the real parser.
from worlds.ctr.test.test_arcade_arenas import arena_options
for count in range(1,8):
    for pickups in (False,True):
        for seed in range(5):
            wire=make_world(arena_options(count,pickups),seed=seed).fill_slot_data()
            cases.append((f"arena-{count}-{pickups}-{seed}",wire,True))

# Open package admission (#133 PR 2): plans with packages outside any compiled
# registry, on a named block and on a generic slot, at many seeds.
from worlds.ctr.test.test_custom_packages import UNKNOWN, named_fixture, options_with
for seed in range(30):
    custom = [[named_fixture(), copy.deepcopy(UNKNOWN)], [copy.deepcopy(UNKNOWN)], [named_fixture()],
              [copy.deepcopy(cp.PACKAGES[0]), copy.deepcopy(UNKNOWN)]][seed % 4]
    cases.append((f"open-generated-{seed}", make_world(options_with(custom), seed=seed).fill_slot_data(), True))
cases.append(("open-fixture", json.loads((root / "tools/fixtures/content-plan/open-packages.json").read_text()), True))

base = cases[0][1]
def alter(name, change, source=None):
    case = copy.deepcopy(source or base)
    change(case["content_plan"])
    try:
        cp.validate(case["content_plan"])
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append((name, case, accepted))

manual_base = next(w for name,w,ok in cases if name=="manual-fixture")
def manual_req(plan, rid):
    return next(r for r in plan["requirements"] if r["id"] == rid)
def manual_alter(name, change):
    alter("manual-" + name, change, manual_base)
manual_alter("count above supply", lambda p: manual_req(p, "manual:3")["predicate"].__setitem__("count", 6))
manual_alter("count zero", lambda p: manual_req(p, "manual:3")["predicate"].__setitem__("count", 0))
manual_alter("item outside counted", lambda p: manual_req(p, "manual:3")["predicate"].__setitem__("item", 35010015))
manual_alter("item without supply", lambda p: manual_req(p, "manual:3")["predicate"].__setitem__("item", 35010003))
manual_alter("sum drops a colour", lambda p: manual_req(p, "manual:100")["predicate"]["items"].pop(4))
manual_alter("sum reordered", lambda p: manual_req(p, "manual:100")["predicate"]["items"].reverse())
manual_alter("sum lists unsupplied item", lambda p: manual_req(p, "manual:0")["predicate"]["items"].append(35010003))
manual_alter("sum mixes families", lambda p: manual_req(p, "manual:0")["predicate"].__setitem__("items", [35010001, 35010009]))
manual_alter("sum count above supply", lambda p: manual_req(p, "manual:0")["predicate"].__setitem__("count", 5))
manual_alter("unsupported predicate", lambda p: manual_req(p, "manual:3").__setitem__("predicate", dict(op="all", terms=[dict(op="always")])))
manual_alter("feature missing", lambda p: p.__setitem__("required_features", ["content_plan_v1"]))
manual_alter("feature unknown", lambda p: p.__setitem__("required_features", ["content_plan_v1", "manual_pad_requirements_v2"]))
manual_alter("gate to free", lambda p: next(x for x in p["pads"] if x["id"] == "pad:3")["mode_gates"][0].__setitem__("requirement_id", "free"))
manual_alter("gate to another pad", lambda p: next(x for x in p["pads"] if x["id"] == "pad:3")["mode_gates"][0].__setitem__("requirement_id", "manual:17"))
manual_alter("record without pad", lambda p: manual_req(p, "manual:3").__setitem__("id", "manual:4"))
manual_alter("record for unknown pad", lambda p: manual_req(p, "manual:3").__setitem__("id", "manual:99"))
manual_alter("stray record", lambda p: p["requirements"].append(dict(id="extra", predicate=dict(op="always"))))

open_base = next(w for name, w, ok in cases if name == "open-fixture")
def open_alter(name, change):
    alter("open-" + name, lambda p: change(p["packages"]), open_base)
def setter(path, value):
    def change(packages):
        target = packages
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
    return change
for name, change in [
        ("unsorted", lambda p: p.reverse()),
        ("id", setter([0, "id"], "package/fixture-loop/9")),
        ("key case", setter([0, "content_id"], "Fixture-loop")),
        ("key underscore", setter([0, "content_id"], "fixture_loop")),
        ("key double hyphen", setter([0, "content_id"], "fixture--loop")),
        ("key trailing hyphen", setter([0, "content_id"], "fixture-loop-")),
        ("key too long", setter([0, "content_id"], "k" * 56)),
        ("revision dot", setter([0, "revision"], ".1")),
        ("revision number", setter([0, "revision"], 21)),
        ("revision space", setter([0, "revision"], "2 1")),
        ("uuid case", setter([0, "uuid"], UNKNOWN["uuid"].upper())),
        ("uuid short", setter([0, "uuid"], UNKNOWN["uuid"][:-1])),
        ("title control", setter([0, "display_name"], "Fixture\x01Loop")),
        ("title delete", setter([0, "display_name"], "Fixture\x7fLoop")),
        ("title 129 bytes", setter([0, "display_name"], "x" * 129)),
        ("title 128 bytes utf8", setter([0, "display_name"], "\u00e9" * 64)),
        ("author empty", setter([0, "author"], "")),
        ("laps 0", setter([0, "laps"], 0)),
        ("laps 8", setter([0, "laps"], 8)),
        ("laps text", setter([0, "laps"], "3")),
        ("cap number", setter([0, "capabilities", "arcade"], 1)),
        ("cap extra", setter([0, "capabilities", "boxes"], True)),
        ("cap missing", lambda p: p[0]["capabilities"].pop("wumpa")),
        ("cap list", setter([0, "capabilities"], ["arcade"])),
        ("relic without targets", lambda p: p[0].pop("relic_targets")),
        ("targets without relic", lambda p: p[1].__setitem__("relic_targets", copy.deepcopy(p[0]["relic_targets"]))),
        ("targets equal", setter([0, "relic_targets", "gold"], 96000)),
        ("targets zero", setter([0, "relic_targets", "platinum"], 0)),
        ("targets hour", setter([0, "relic_targets", "sapphire"], 3456000)),
        ("targets over hour", setter([0, "relic_targets", "sapphire"], 3456001)),
        ("targets extra", setter([0, "relic_targets", "bronze"], 1)),
        ("files reversed", lambda p: p[0]["files"].reverse()),
        ("files three", lambda p: p[0]["files"].append(copy.deepcopy(p[0]["files"][1]))),
        ("files one", lambda p: p[0]["files"].pop()),
        ("hash case", setter([0, "files", 0, "sha256"], "A1" * 32)),
        ("hash short", setter([0, "files", 1, "sha256"], "b2" * 31)),
        ("lev max", setter([0, "files", 0, "bytes"], 16 * 1024 * 1024)),
        ("lev over", setter([0, "files", 0, "bytes"], 16 * 1024 * 1024 + 1)),
        ("vrm max", setter([0, "files", 1, "bytes"], 4 * 1024 * 1024)),
        ("vrm over", setter([0, "files", 1, "bytes"], 4 * 1024 * 1024 + 1)),
        ("bytes zero", setter([0, "files", 1, "bytes"], 0)),
        ("format", setter([0, "files", 1, "format"], "ctr-lev")),
        ("format version", setter([0, "files", 0, "format_version"], 2)),
        ("file extra", setter([0, "files", 0, "path"], "x.lev")),
        ("evidence ok", setter([0, "evidence"], [dict(capability="arcade", file_hashes=["a1" * 32, "b2" * 32],
                                                      verifier="fixture", revision=1, level="runtime")])),
        ("evidence unpinned", setter([0, "evidence"], [dict(capability="arcade", file_hashes=["e5" * 32],
                                                             verifier="fixture", revision=1, level="runtime")])),
        ("evidence repeated hash", setter([0, "evidence"], [dict(capability="arcade", file_hashes=["a1" * 32] * 2,
                                                                  verifier="fixture", revision=1, level="runtime")])),
        ("evidence level", setter([0, "evidence"], [dict(capability="arcade", file_hashes=["a1" * 32],
                                                          verifier="fixture", revision=1, level="measured")])),
        ("evidence 16", setter([0, "evidence"], [dict(capability="arcade", file_hashes=["a1" * 32],
                                                       verifier="fixture", revision=1, level="runtime")] * 16)),
        ("evidence 17", setter([0, "evidence"], [dict(capability="arcade", file_hashes=["a1" * 32],
                                                       verifier="fixture", revision=1, level="runtime")] * 17)),
        ("repeated key", lambda p: p[1].update(content_id="fixture-loop", id="package/fixture-loop/3", revision="3")),
        ("repeated uuid", lambda p: p[1].__setitem__("uuid", p[0]["uuid"])),
        ("repeated files", lambda p: p[1].__setitem__("files", copy.deepcopy(p[0]["files"]))),
        ("no arcade", setter([1, "capabilities", "arcade"], False)),
        ("claims less", setter([0, "capabilities", "time_trial"], False)),
        ("extra field", setter([1, "source_url"], "https://example.invalid")),
        ]:
    open_alter(name, change)
alter("open-track laps differ", lambda p: next(t for t in p["tracks"] if t.get("package_id") == "package/fixture-loop/2.1").__setitem__("laps", 4), open_base)

alter("duplicate check", lambda p: p["checks"].append(copy.deepcopy(p["checks"][0])))
alter("duplicate package", lambda p: p["packages"].append(copy.deepcopy(p["packages"][0])))
alter("duplicate pad", lambda p: p["pads"].__setitem__(0, copy.deepcopy(p["pads"][1])))
alter("foreign goal Gem", lambda p: p["goal"]["items"].__setitem__(0, 35010000))
# Since open admission (#133 PR 2) other pins or evidence are simply another
# package; both sides accept them and native checks the bytes on load. The
# checked-in baseline predates the named block, so these use the open fixture.
alter("changed pinned size", lambda p: p["packages"][0]["files"][0].__setitem__("bytes", 2558169), open_base)
alter("runtime evidence row", lambda p: p["packages"][0]["evidence"].append(
    dict(capability="trophy", file_hashes=[p["packages"][0]["files"][0]["sha256"]], verifier="x", revision=1, level="runtime")), open_base)
alter("missing route", lambda p: p["checks"][0].__setitem__("routes", []))
cases.append(("unchanged baseline copy", copy.deepcopy(base), True))  # the checked-in fixture predates the named block for baby-t-park, so the paired validator no longer accepts it
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
