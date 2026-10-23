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
L = cp.CTR_LOCATION_IDS
expected = dict(retail=[dict(id=i, name=n, location=L[n + ": Trophy Race"], ctr=L[n + ": CTR Token Challenge"],
                             relic=[L[f"{n}: {t} Time Trial"] for t in cp.RELIC_TIERS])
                        for i, n in sorted(cp.RETAIL.items())],
                pads=[dict(physical=p, hub=h, keys=k) for p, h, k in cp.pad_registry()],
                custom_locations=[L[f"Custom Track {i}: Trophy Race"] for i in range(1, 33)],
                # Track modes (#133 PR 3): generic-slot CTR and relic tier codes.
                custom_ctr_locations=[L[f"Custom Track {i}: CTR Token Challenge"] for i in range(1, 33)],
                custom_relic_locations=[[L[f"Custom Track {i}: {t} Time Trial"] for t in cp.RELIC_TIERS]
                                        for i in range(1, 33)],
                # The named-block table native binds track keys to (#133 PR 2
                # review); tools/sync-content-registry.py rewrites it.
                named_blocks=[dict(key=t.track_key, block=t.block_index, name=t.ap_name)
                              for t in sorted(cp.destinations.NAMED_TRACKS, key=lambda t: t.track_key)])
# Per-track check families (#133 PR 3b): rung, Wumpa and letter codes per
# retail track and generic slot, and the one global Wumpa check.
RUNGS = [cp.RUNG_SUFFIX[k] for k in cp.podium.SLOT_ORDER]
for row in expected["retail"]:
    n = row["name"]
    row.update(podium=[L[f"{n}: {s}"] for s in RUNGS], wumpa=L[f"{n}: Reach 10 Wumpa"],
               letters=[L[f"{n}: Letter {x}"] for x in cp.LETTERS],
               letter_items=[cp.content_item_pool.ITEMS[f"Letter {x} ({n})"] for x in cp.LETTERS],
               perfect=L[f"{n}: Relic Race Perfect"])
expected.update(custom_podium_locations=[[L[f"Custom Track {i}: {s}"] for s in RUNGS] for i in range(1, 33)],
                custom_wumpa_locations=[L[f"Custom Track {i}: Reach 10 Wumpa"] for i in range(1, 33)],
                custom_letter_locations=[[L[f"Custom Track {i}: Letter {x}"] for x in cp.LETTERS] for i in range(1, 33)],
                custom_letter_items=[[cp.custom_check_code("letter_item", i, j) for j in range(3)] for i in range(1, 33)],
                # Relic Race Perfect per generic slot and Cortex Vortex as a
                # track (#133 PR 3c).
                custom_perfect_locations=[L[f"Custom Track {i}: Relic Race Perfect"] for i in range(1, 33)],
                wumpa_global=L["Wumpa: Reach 10 Wumpa"])
CV = cp.destinations.CORTEX_VORTEX
expected["cortex_vortex"] = dict(
    id=cp.CORTEX_TRACK_ID, package_id=cp.CORTEX_PACKAGE_ID, name=CV, laps=cp.CORTEX_LAPS,
    location=L[CV + ": Trophy Race"], ctr=L[CV + ": CTR Token Challenge"],
    relic=[L[f"{CV}: {t} Time Trial"] for t in cp.RELIC_TIERS], perfect=L[CV + ": Relic Race Perfect"],
    podium=[L[f"{CV}: {s}"] for s in RUNGS], wumpa=L[CV + ": Reach 10 Wumpa"],
    letters=[L[f"{CV}: Letter {x}"] for x in cp.LETTERS], letter_items=list(cp.cortex_vortex_track.LETTER_ITEM_CODES))
# Global check families (#133 PR 4d): codes and frozen names in class order.
from worlds.ctr import itemsanity as cits, hit_character as chit
expected["itemsanity"] = [dict(location=c, name=n) for n, c, _r in cits.ITEMSANITY_CLASS.all_locations()]
expected["hit_character"] = [dict(location=c, name=n) for n, c, _r in chit.HIT_CHARACTER_CLASS.all_locations()]
# Item Box Locations (#133 PR 4e): each retail row's placed slots as
# [code, Shortcut Knowledge tier], in slot order.
from worlds.ctr.item_boxes import ITEM_BOX_CLASS as CBOX, PLACED_COUNTS as CBOX_PLACED, SK_REQUIRED_TIER as CBOX_TIER
for row in expected["retail"]:
    row["boxes"] = [[CBOX.slot_code(row["name"], s), CBOX_TIER.get((row["name"], s), 0)]
                    for s in range(1, CBOX_PLACED[row["name"]] + 1)]
# Cups (#133 PR 5): the 27-name pool with its Win and Win All Legs codes, and
# every track's Cup Leg Victory code (retail rows, Cortex Vortex, generic slots).
from worlds.ctr import cup_checks as ccup
for row in expected["retail"]:
    row["cup_leg"] = L[ccup.cup_leg_name(row["name"])]
expected["cortex_vortex"]["cup_leg"] = ccup.CORTEX_VORTEX_CUP_LEG_CODE
expected["cups"] = [dict(name=n, win=L[ccup.cup_win_name(n)], sweep=L[ccup.cup_sweep_name(n)]) for n in ccup.CUP_NAMES]
expected["custom_cup_leg_locations"] = [L[ccup.cup_leg_name(f"Custom Track {i}")] for i in range(1, 33)]
expected["named_blocks"] = expected.pop("named_blocks")  # registry key order
assert registry == expected, "native frozen registry differs from paired apworld; run tools/sync-content-registry.py"
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

# Track identity (#133 PR 2 review): both sides bind a track key to its named
# block and recompute or check generic room titles.
def open_track(plan, key):
    return next(t for t in plan["tracks"] if t.get("package_id", "").startswith(f"package/{key}/"))
def open_check(plan, track):
    return next(c for c in plan["checks"] if c["owner"] == track["id"])
def drop_named(p):
    t = open_track(p, "tv-world"); t.pop("named_block")
    open_check(p, t)["location"] = cp.CTR_LOCATION_IDS["Custom Track 2: Trophy Race"]
    p["checks"].sort(key=lambda c: c["location"])
def claim_north_pole(p):
    north = cp.destinations.named_track("north-pole")
    t = open_track(p, "fixture-loop"); t.update(named_block=north.block_index, room_title=north.ap_name)
    open_check(p, t).update(location=cp.named_tracks.location_code(north.block_index, "trophy"),
                            display_name=north.ap_name + ": Trophy Race")
    p["checks"].sort(key=lambda c: c["location"])
def retitle(title):
    def change(p):
        t = open_track(p, "fixture-loop"); t["room_title"] = title
        open_check(p, t)["display_name"] = title + ": Trophy Race"
    return change
alter("identity: named key moved to a generic slot", drop_named, open_base)
alter("identity: unnamed key claims North Pole's block, name and check", claim_north_pole, open_base)
alter("identity: named key under another title", lambda p: (open_track(p, "tv-world").__setitem__("room_title", "TV World 2"),
      open_check(p, open_track(p, "tv-world")).__setitem__("display_name", "TV World 2: Trophy Race")), open_base)
# Clashes only: the apworld also refuses any other title that is not the one it
# derives (for example a changed case), which native does not recompute.
for title in ("Crash Cove", "TV World", "North Pole", "tv world", "Fixture Loop"):
    alter(f"identity: generic room title {title}", retitle(title), open_base)
def two_generic(title):
    def change(p):
        text = json.dumps(p).replace("tv-world", "tv-park")
        p.clear(); p.update(json.loads(text))
        t = open_track(p, "tv-park"); t.pop("named_block"); t.update(room_title=title, display_name=title)
        next(x for x in p["packages"] if x["content_id"] == "tv-park")["display_name"] = title
        open_check(p, t).update(location=cp.CTR_LOCATION_IDS["Custom Track 2: Trophy Race"], display_name=title + ": Trophy Race")
        p["checks"].sort(key=lambda c: c["location"])
    return change
alter("identity: two generic tracks, distinct titles", two_generic("TV Park"), open_base)
alter("identity: two generic tracks, one title", two_generic("Fixture Loop"), open_base)
for seed in range(8):
    # Generated clashes: a retail title and two tracks with one title.
    twin = copy.deepcopy(UNKNOWN)
    twin.update(id="package/fixture-twin/1", content_id="fixture-twin", revision="1", author="Other Author",
                uuid="1f0e0d0c-0b0a-4908-8706-050403020100", display_name=["Crash Cove", "Fixture Loop", "TV World", "Baby T Park"][seed % 4])
    twin["files"] = [dict(f, sha256=h * 32) for f, h in zip(twin["files"], ("e5", "f6"))]
    custom = [copy.deepcopy(UNKNOWN), twin] + ([named_fixture()] if seed % 2 else [])
    cases.append((f"identity-generated-{seed}", make_world(options_with(custom), seed=seed).fill_slot_data(), True))

# Track modes, stages and mode orders (#133 PR 3): every nonempty mode subset
# with both stage settings and all three orders, on retail tracks and on custom
# packages whose capabilities allow different modes (a CTR-capable named
# track, a Trophy and Relic B+ track, a Relic-only B+ track).
import itertools
from worlds.ctr.test.test_track_modes import mode_options, relic_only
from worlds.ctr.test.test_content_item_pool import small_pool
def mode_custom():
    tv = named_fixture()
    tv["capabilities"].update(ctr_tokens=True, relic=True)
    tv["relic_targets"] = dict(sapphire=90000, gold=80000, platinum=70000)
    return [tv, copy.deepcopy(UNKNOWN), relic_only()]
subsets = [list(c) for n in (1, 2, 3) for c in itertools.combinations(cp.MODES, n)]
for index, (modes, stages, order) in enumerate(itertools.product(subsets, ("single", "two"), ("fixed", "per_seed", "per_pad"))):
    custom = mode_custom() if index % 2 == 0 else [copy.deepcopy(cp.PACKAGES[1]), copy.deepcopy(UNKNOWN)]
    # Keep only the custom tracks that can run one of the selected modes.
    custom = [d for d in custom if set(modes) & set(cp.supported_modes("custom", d))]
    for seed in range(2):
        options = mode_options(modes, stages, order, custom=custom)
        if modes in (["trophy"], ["ctr"]):
            options["item_pool"] = small_pool()  # about 20 checks cannot hold the larger pool
        if seed:
            options["warp_pad_rules"] = {"Crash Cove": dict(item="Trophy", count=2)}
            options["warppad_unlock_requirements"] = "randomized"
        cases.append((f"modes-{'+'.join(modes)}-{stages}-{order}-{seed}", make_world(options, seed=seed + index).fill_slot_data(), True))
modes_base = make_world(mode_options(list(cp.MODES), "two", "per_pad", custom=mode_custom()), seed=3).fill_slot_data()
cases.append(("modes-base", modes_base, True))
def multi_pad(p):
    return next(x for x in p["pads"] if len(x["mode_gates"]) == 3)
def stage2_of(p, pad):
    return next(r for r in p["requirements"] if r["id"] == pad["mode_gates"][1]["requirement_id"])
def custom_track(p, key):
    return next(t for t in p["tracks"] if t["id"].startswith(f"custom/{key}/"))
def entry_for(p, track):
    return next(e for e in p["entries"] if e.get("track_id") == track["id"])
def pad_for(p, entry):
    return next(x for x in p["pads"] if x["entry_id"] == entry["id"])
def claim_mode(key, mode, location, suffix, award):
    def change(p):
        t = custom_track(p, key)
        t["modes"] = t["capabilities"] = [m for m in cp.MODES if m in t["modes"] + [mode]]
        e = entry_for(p, t); e["modes"].append(mode); pad = pad_for(p, e)
        stage = pad["mode_gates"][-1]
        pad["mode_gates"].append(dict(mode=mode, stage=stage["stage"] if len(pad["mode_gates"]) > 1 else 1,
                                      requirement_id=stage["requirement_id"]))
        p["checks"].append(dict(location=location, owner=t["id"], kind=mode, display_name=t.get("room_title", t["display_name"]) + ": " + suffix,
                                routes=[dict(entry_id=e["id"], mode=mode, leg_id=None, encounter_id=None, own_pad_id=pad["id"], award=award)]))
        p["items"]["coded_checks"] += 1; p["items"]["filler_count"] += 1
    return change
def modes_alter(name, change):
    alter("modes-" + name, change, modes_base)
modes_alter("later gate as stage one", lambda p: multi_pad(p)["mode_gates"][1].__setitem__("stage", 1))
modes_alter("later gate on the entrance record", lambda p: multi_pad(p)["mode_gates"][2].__setitem__("requirement_id", "free"))
modes_alter("later gate on another pad's record", lambda p: multi_pad(p)["mode_gates"][2].__setitem__(
    "requirement_id", next(r["id"] for r in p["requirements"] if r["id"].startswith("stage2:") and r["id"] != multi_pad(p)["mode_gates"][2]["requirement_id"])))
modes_alter("first gate as stage two", lambda p: multi_pad(p)["mode_gates"][0].__setitem__("stage", 2))
modes_alter("stage two record dropped", lambda p: p["requirements"].remove(stage2_of(p, multi_pad(p))))
modes_alter("stage two count above supply", lambda p: stage2_of(p, multi_pad(p))["predicate"].__setitem__("count", 99))
modes_alter("stage two count zero", lambda p: stage2_of(p, multi_pad(p))["predicate"].__setitem__("count", 0))
modes_alter("stage two item outside counted", lambda p: stage2_of(p, multi_pad(p)).__setitem__("predicate", dict(op="count", item=35010015, count=1)))
modes_alter("stage two sum of mixed families", lambda p: stage2_of(p, multi_pad(p)).__setitem__("predicate", dict(op="sum", items=[35010001, 35010004], count=1)))
modes_alter("stage two always", lambda p: stage2_of(p, multi_pad(p)).__setitem__("predicate", dict(op="always")))
modes_alter("stage two record on an empty pad", lambda p: p["requirements"].append(
    dict(id="stage2:" + next(x for x in p["pads"] if x["entry_id"] is None and x["kind"] == "normal")["id"].split(":")[1],
         predicate=dict(op="count", item=35010000, count=1))))
modes_alter("stage two record with a leading zero", lambda p: stage2_of(p, multi_pad(p)).__setitem__("id", "stage2:0" + multi_pad(p)["id"].split(":")[1]))
modes_alter("entry mode missing", lambda p: next(e for e in p["entries"] if len(e["modes"]) == 3)["modes"].pop())
def repeat_mode(p):
    e = next(e for e in p["entries"] if len(e["modes"]) == 3)
    e["modes"][2] = e["modes"][0]
modes_alter("entry mode repeated", repeat_mode)
# Allowed on both sides, as in manual rules; the generator never draws a Key.
modes_alter("stage two Key gate within supply", lambda p: stage2_of(p, multi_pad(p)).__setitem__("predicate", dict(op="count", item=35010014, count=2)))
modes_alter("entry mode unknown", lambda p: next(e for e in p["entries"] if len(e["modes"]) == 3)["modes"].__setitem__(2, "merged"))
modes_alter("gate order differs from entry order", lambda p: multi_pad(p)["mode_gates"].reverse())
modes_alter("track modes out of order", lambda p: next(t for t in p["tracks"] if t["origin"] == "retail").__setitem__("modes", ["ctr", "trophy", "relic"]))
modes_alter("capabilities differ from modes", lambda p: next(t for t in p["tracks"] if t["origin"] == "retail").__setitem__("capabilities", ["trophy"]))
modes_alter("feature dropped", lambda p: p["required_features"].remove("track_modes_v1"))
modes_alter("feature repeated", lambda p: p["required_features"].append("track_modes_v1"))
modes_alter("relic award swapped", lambda p: next(c for c in p["checks"] if c["kind"] == "relic" and c["routes"][0]["award"] == "gold")["routes"][0].__setitem__("award", "platinum"))
modes_alter("ctr award as win", lambda p: next(c for c in p["checks"] if c["kind"] == "ctr")["routes"][0].__setitem__("award", "win"))
modes_alter("relic check removed", lambda p: (p["checks"].remove(next(c for c in p["checks"] if c["kind"] == "relic")),
                                                p["items"].__setitem__("coded_checks", p["items"]["coded_checks"] - 1),
                                                p["items"].__setitem__("filler_count", p["items"]["filler_count"] - 1)))
modes_alter("merged entry", lambda p: next(e for e in p["entries"] if e["kind"] == "track").__setitem__("merged", True))
modes_alter("B+ track claims CTR without letters", claim_mode("fixture-loop", "ctr", L["Custom Track 1: CTR Token Challenge"], "CTR Token Challenge", "win_letters"))
modes_alter("relic-only track claims Trophy without arcade", claim_mode("relic-loop", "trophy", L["Custom Track 2: Trophy Race"], "Trophy Race", "win"))
def relic_without_lap_graph(p):
    t = custom_track(p, "relic-loop")
    next(x for x in p["packages"] if x["id"] == t["package_id"])["capabilities"]["time_trial"] = False
modes_alter("relic track without a lap graph", relic_without_lap_graph)
def relic_without_targets(p):
    t = custom_track(p, "fixture-loop")
    pkg = next(x for x in p["packages"] if x["id"] == t["package_id"])
    pkg["capabilities"]["relic"] = False; pkg.pop("relic_targets")
modes_alter("relic track whose package lost relic", relic_without_targets)
modes_alter("unchanged modes copy", lambda p: None)

# Per-track check families (#133 PR 3b): podium rung sets, both Wumpa modes and
# every Lettersanity shape, on retail tracks, a named block and a B+ track with
# letters and Wumpa, across the three mode orders; then forged family records.
from Options import OptionError
from worlds.ctr.test.test_track_checks import check_options
fam_bplus = copy.deepcopy(UNKNOWN)
fam_bplus["capabilities"].update(ctr_tokens=True, wumpa=True)
fam_custom = [copy.deepcopy(cp.PACKAGES[1]), fam_bplus]
rung_toggles = [dict(), dict(podium_held_fifth_rung=True), dict(podium_held_rungs=False),
                dict(podium_finish_rungs=False), dict(podium_any_position_rung=False)]
family_cases = 0
for index, (wumpa, letters, order) in enumerate(itertools.product(("off", "global", "per_track"), (0, 1, 2, 3),
                                                                  ("fixed", "per_seed", "per_pad"))):
    for seed in range(2):
        options = check_options(wumpa=wumpa, letters=letters, per_track=1 + (index + seed) % 3, order=order,
                                custom=copy.deepcopy(fam_custom) if (index + seed) % 2 == 0 else [fam_bplus],
                                stages=("two", "single")[seed])
        options.update(rung_toggles[(index + seed) % len(rung_toggles)])
        try:
            wire = make_world(options, seed=seed + 7 * index).fill_slot_data()
        except OptionError:
            continue  # a narrow start (open question for PR 3b), not a plan
        cases.append((f"families-{wumpa}-{letters}-{order}-{seed}", wire, True))
        family_cases += 1
assert family_cases >= 60, family_cases
fam_base = make_world(check_options(wumpa="per_track", letters=2, per_track=2, custom=copy.deepcopy(fam_custom)),
                      seed=20261017).fill_slot_data()
cases.append(("families-base", fam_base, True))
fam_global = next(w for name, w, ok in cases if name.startswith("families-global-3-"))
def fam_alter(name, change, source=None):
    alter("families-" + name, change, source or fam_base)
def first_check(p, kind, owner=None):
    return next(c for c in p["checks"] if c["kind"] == kind and (owner is None or c["owner"] == owner))
def letter_row(p):
    return next(r for r in p["items"]["rows"] if r["item"] > 35010014)
def drop_letter_row(p):
    p["items"]["rows"].remove(letter_row(p))
def drop_feature(name):
    return lambda p: p["required_features"].remove(name)
def move_letter(p):
    have = {c["location"] for c in p["checks"] if c["owner"] == "retail:3" and c["kind"] == "letter"}
    c = first_check(p, "letter", "retail:3")
    i = next(i for i in range(3) if 35012500 + i not in have)
    c.update(location=35012500 + i, display_name=f"Crash Cove: Letter {cp.LETTERS[i]}")
    c["routes"][0]["award"] = cp.LETTER_KEYS[i]
def move_letter_and_item(p):
    have = sorted(c["location"] for c in p["checks"] if c["owner"] == "retail:3" and c["kind"] == "letter")
    c = first_check(p, "letter", "retail:3")
    old = c["location"] - 35012500
    i = next(i for i in range(3) if 35012500 + i not in have)
    c.update(location=35012500 + i, display_name=f"Crash Cove: Letter {cp.LETTERS[i]}")
    c["routes"][0]["award"] = cp.LETTER_KEYS[i]
    next(r for r in p["items"]["rows"] if r["item"] == 35010139 + old)["item"] = 35010139 + i
    p["items"]["rows"].sort(key=lambda r: r["item"])
for name, change in [
        ("letter check dropped", lambda p: p["checks"].remove(first_check(p, "letter"))),
        ("rung dropped", lambda p: p["checks"].remove(first_check(p, "podium"))),
        ("Wumpa dropped", lambda p: p["checks"].remove(first_check(p, "wumpa"))),
        ("letter item row dropped", drop_letter_row),
        ("letter item pooled twice", lambda p: letter_row(p).update(base=2, remaining=2, receipt_cap=2, requirement_ceiling=2)),
        ("letter item with an extra copy", lambda p: letter_row(p).update(extra=1, remaining=2, receipt_cap=2)),
        ("letter item started from the pool", lambda p: letter_row(p).update(start_from_pool=1, remaining=0)),
        ("unknown item row", lambda p: letter_row(p).__setitem__("item", 35010187)),
        ("rung set never created", lambda p: p["track_checks"].__setitem__("podium_rungs", ["held_3rd"])),
        ("rung set held 5th alone", lambda p: p["track_checks"].__setitem__("podium_rungs", ["held_5th"])),
        ("rungs out of slot order", lambda p: p["track_checks"].__setitem__("podium_rungs", ["held_3rd", "held_1st", "finish_podium", "finish_any"])),
        ("rung unknown", lambda p: p["track_checks"]["podium_rungs"].__setitem__(0, "held_2nd")),
        ("rung repeated", lambda p: p["track_checks"]["podium_rungs"].__setitem__(1, "held_1st")),
        ("wumpa mode unknown", lambda p: p["track_checks"].__setitem__("wumpa", "per_pad")),
        ("per-track checks under global", lambda p: p["track_checks"].__setitem__("wumpa", "global")),
        ("letters_per_track missing", lambda p: p["track_checks"].pop("letters_per_track")),
        ("letters_per_track above the drawn letters", lambda p: p["track_checks"].__setitem__("letters_per_track", 3)),
        ("letters_per_track zero", lambda p: p["track_checks"].__setitem__("letters_per_track", 0)),
        ("mode 2 read as mode 1", lambda p: p["track_checks"].__setitem__("lettersanity", 1)),
        ("lettersanity as Boolean", lambda p: p["track_checks"].__setitem__("lettersanity", True)),
        ("lettersanity 4", lambda p: p["track_checks"].__setitem__("lettersanity", 4)),
        ("empty record", lambda p: p.__setitem__("track_checks", {})),
        ("unknown setting", lambda p: p["track_checks"].__setitem__("box_locations", True)),
        ("record removed", lambda p: p.pop("track_checks")),
        ("podium feature dropped", drop_feature("podium_rungs_v1")),
        ("Wumpa feature dropped", drop_feature("wumpa_checks_v1")),
        ("Lettersanity feature dropped", drop_feature("lettersanity_v1")),
        ("Wumpa route from a Relic Race", lambda p: first_check(p, "wumpa")["routes"][0].__setitem__("mode", "relic")),
        ("Wumpa routes reordered", lambda p: next(c for c in p["checks"] if c["kind"] == "wumpa" and len(c["routes"]) == 2)["routes"].reverse()),
        ("Wumpa award changed", lambda p: first_check(p, "wumpa")["routes"][0].__setitem__("award", "win")),
        ("rung on a CTR route", lambda p: first_check(p, "podium")["routes"][0].__setitem__("mode", "ctr")),
        ("letter award swapped", lambda p: first_check(p, "letter")["routes"][0].__setitem__(
            "award", "letter_t" if first_check(p, "letter")["routes"][0]["award"] == "letter_c" else "letter_c")),
        ("rung display name changed", lambda p: first_check(p, "podium").__setitem__("display_name", "Crash Cove: Held 2nd")),
        ("letter kind changed", lambda p: first_check(p, "letter").__setitem__("kind", "ctr")),
        ("B+ package loses Wumpa", lambda p: next(x for x in p["packages"] if x["content_id"] == "fixture-loop")["capabilities"].__setitem__("wumpa", False)),
        ("letter moved to an undrawn letter", move_letter),
        ("unchanged copy", lambda p: None),
        ]:
    fam_alter(name, change)
# Another draw for one track: both sides accept a letter that moves with its item.
fam_alter("letter moved with its item", move_letter_and_item)
for name, change in [
        ("global Wumpa dropped", lambda p: p["checks"].remove(first_check(p, "wumpa"))),
        ("global routes reversed", lambda p: first_check(p, "wumpa")["routes"].reverse()),
        ("global route dropped", lambda p: first_check(p, "wumpa")["routes"].pop()),
        ("global owner changed", lambda p: first_check(p, "wumpa").__setitem__("owner", "retail:3")),
        ("global read as per-track", lambda p: p["track_checks"].__setitem__("wumpa", "per_track")),
        ("mode 3 with letters_per_track", lambda p: p["track_checks"].__setitem__("letters_per_track", 3)),
        ("mode 3 read as mode 2", lambda p: p["track_checks"].update(lettersanity=2, letters_per_track=3)),
        ("global unchanged copy", lambda p: None),
        ]:
    fam_alter(name, change, fam_global)

# Cortex Vortex as a track and Relic Race Perfect checks (#133 PR 3c): plans
# with and without Cortex Vortex, every mode subset it can take, the perfect on
# retail, Cortex Vortex, a named block and a generic slot, with the 3b families
# and the three mode orders; then forged records.
from worlds.ctr.test.test_cortex_relic_perfect import cv_options, relic_named
cv_cases = 0
for index, (perfect, letters, order) in enumerate(itertools.product((False, True), (0, 2, 3), ("fixed", "per_seed", "per_pad"))):
    for seed in range(2):
        options = cv_options(perfect=perfect, order=order, required=(index + seed) % 3 != 2,
                             lettersanity=letters, letters_per_track=2,
                             wumpa_check=("off", "global", "per_track")[(index + seed) % 3],
                             podium_placement_checks=bool(seed))
        if index % 4 == 1:
            options["content_pool"]["tracks"]["overrides"] = {"cortex_vortex": [["relic"], ["trophy", "relic"], ["ctr"]][seed]}
        try:
            wire = make_world(options, seed=seed + 5 * index).fill_slot_data()
        except OptionError:
            continue
        cases.append((f"cortex-{perfect}-{letters}-{order}-{seed}", wire, True))
        cv_cases += 1
for seed in range(6):
    options = cv_options(cortex=False, order=("fixed", "per_pad")[seed % 2])
    cases.append((f"perfect-no-cortex-{seed}", make_world(options, seed=seed).fill_slot_data(), True))
    cv_cases += 1
assert cv_cases >= 36, cv_cases
cv_base = make_world(cv_options(lettersanity=2, letters_per_track=1, wumpa_check="per_track"), seed=20261017).fill_slot_data()
cases.append(("cortex-base", cv_base, True))
def cv_alter(name, change):
    alter("cortex-" + name, change, cv_base)
def cv_track(p):
    return next(t for t in p["tracks"] if t["origin"] == "cortex_vortex")
def cv_check(p, kind):
    return next(c for c in p["checks"] if c["owner"] == "cortex_vortex" and c["kind"] == kind)
def perfect_of(p, owner):
    return next(c for c in p["checks"] if c["owner"] == owner and c["kind"] == "perfect")
def drop_check(code_of):
    def change(p):
        p["checks"].remove(code_of(p))
        p["items"]["coded_checks"] -= 1
        p["items"]["filler_count"] -= 1
    return change
def perfect_on_btp(p):
    entry = next(e for e in p["entries"] if e.get("track_id") == "custom/baby-t-park/1.0.2")
    pad = next(x for x in p["pads"] if x["entry_id"] == entry["id"])
    p["checks"].append(dict(location=35201610, owner=entry["track_id"], kind="perfect",
                            display_name="Baby T Park: Relic Race Perfect",
                            routes=[dict(entry_id=entry["id"], mode="relic", leg_id=None, encounter_id=None,
                                         own_pad_id=pad["id"], award="perfect")]))
    p["items"]["coded_checks"] += 1
    p["items"]["filler_count"] += 1
def cv_with_slot(p):
    cv_track(p)["custom_slot"] = 9
def cv_as_package(p):
    t = cv_track(p)
    t["origin"] = "custom"
def cv_letter_item_swapped(p):
    row = next(r for r in p["items"]["rows"] if r["item"] in (35010200, 35010201, 35010202))
    row["item"] = 35010186
    p["items"]["rows"].sort(key=lambda r: r["item"])
def bplus_cortex_title(p):
    t = next(t for t in p["tracks"] if t["id"] == "custom/fixture-loop/2.1")
    old = t["room_title"]
    t["room_title"] = "Cortex Vortex"
    for c in p["checks"]:
        if c["owner"] == t["id"]:
            c["display_name"] = c["display_name"].replace(old, "Cortex Vortex", 1)
for name, change in [
        ("perfect dropped", drop_check(lambda p: perfect_of(p, "custom/fixture-loop/2.1"))),
        ("Cortex perfect dropped", drop_check(lambda p: cv_check(p, "perfect"))),
        ("perfect on a track without a Relic Race", perfect_on_btp),
        ("perfect takes Oxide Station's code", lambda p: cv_check(p, "perfect").__setitem__("location", 35012415)),
        ("perfect takes the generic slot of another", lambda p: perfect_of(p, "custom/fixture-loop/2.1").__setitem__("location", 35024031)),
        ("named perfect off its block", lambda p: perfect_of(p, "custom/north-pole/1.1.0").__setitem__("location", 35024001)),
        ("perfect award changed", lambda p: cv_check(p, "perfect")["routes"][0].__setitem__("award", "sapphire")),
        ("perfect on the Trophy route", lambda p: cv_check(p, "perfect")["routes"][0].__setitem__("mode", "trophy")),
        ("perfect kind changed", lambda p: cv_check(p, "perfect").__setitem__("kind", "relic")),
        ("perfect name changed", lambda p: cv_check(p, "perfect").__setitem__("display_name", "Cortex Vortex: Perfect")),
        ("perfect feature dropped", drop_feature("relic_perfect_v1")),
        ("perfect setting false", lambda p: p["track_checks"].__setitem__("relic_perfect", False)),
        ("perfect setting as integer", lambda p: p["track_checks"].__setitem__("relic_perfect", 1)),
        ("perfect setting dropped", lambda p: p["track_checks"].pop("relic_perfect")),
        ("Cortex laps", lambda p: cv_track(p).__setitem__("laps", 5)),
        ("Cortex package", lambda p: cv_track(p).__setitem__("package_id", "package/cortex-vortex/1.1.2")),
        ("Cortex name", lambda p: cv_track(p).__setitem__("display_name", "Oxide Station")),
        ("Cortex id", lambda p: cv_track(p).__setitem__("id", "cortex-vortex")),
        ("Cortex with a custom slot", cv_with_slot),
        ("Cortex as a custom origin", cv_as_package),
        ("Cortex Trophy takes Oxide Station's code", lambda p: cv_check(p, "trophy").__setitem__("location", 35011013)),
        ("Cortex Wumpa takes Oxide Station's code", lambda p: cv_check(p, "wumpa").__setitem__("location", 35016114)),
        ("Cortex letter item swapped", cv_letter_item_swapped),
        ("B+ title takes Cortex Vortex", bplus_cortex_title),
        ("unchanged copy", lambda p: None),
        ]:
    cv_alter(name, change)

# Pool-only packs and the envelope (#133 PR 4a): generated plans with every
# pack combination and envelope setting, then forged records, rows and
# envelope values. The envelope's apworld side is content_plan.restore's check.
from worlds.ctr.test.test_item_packs import pack_options
pack_cases = 0
for seed in range(24):
    send = [None, [], ["mask_grab"], ["weapon_hit", "race_loss"]][seed % 4]
    extra = dict(death_link=("off", "mask_reset", "any_hit", "race_loss")[seed % 4],
                 warp_pad_item_display=("one_pile", "by_reward_type")[seed % 2], ap_item_type_colors=seed % 3 != 0)
    if send is not None:
        extra["death_link_send"] = send
    options = pack_options(psw=seed % 11, turbo=bool(seed & 1), tizi=bool(seed & 2), **extra)
    cases.append((f"packs-generated-{seed}", make_world(options, seed=seed).fill_slot_data(), True))
    pack_cases += 1
pack_base = make_world(pack_options(psw=3, death_link="race_loss", death_link_send=["weapon_hit"],
                                    warp_pad_item_display="by_reward_type", ap_item_type_colors=False),
                       seed=20261017).fill_slot_data()
cases.append(("packs-base", pack_base, True))
def pack_row(p, code):
    return next(r for r in p["items"]["rows"] if r["item"] == code)
def pack_more(p):
    pack_row(p, 35010122).update(base=4, remaining=4, receipt_cap=4, requirement_ceiling=4)
    p["items"]["filler_count"] -= 1
def pack_drop_row(p):
    p["items"]["rows"].remove(pack_row(p, 35010189))
    p["items"]["filler_count"] += 1
def pack_drop_record(p):
    p.pop("item_packs")
    p["required_features"].remove("item_packs_v1")
for name, change in [
        ("count disagrees with row", lambda p: p["item_packs"].__setitem__("progressive_starting_wumpa", 2)),
        ("row disagrees with count", pack_more),
        ("count as Boolean", lambda p: p["item_packs"].__setitem__("progressive_starting_wumpa", True)),
        ("count 11", lambda p: p["item_packs"].__setitem__("progressive_starting_wumpa", 11)),
        ("count 0", lambda p: p["item_packs"].__setitem__("progressive_starting_wumpa", 0)),
        ("grant false", lambda p: p["item_packs"].__setitem__("turbo_grant", False)),
        ("grant as integer", lambda p: p["item_packs"].__setitem__("tizi_helper", 1)),
        ("unknown pack", lambda p: p["item_packs"].__setitem__("itemsanity", True)),
        ("empty record", lambda p: p.__setitem__("item_packs", {})),
        ("rows without record", pack_drop_record),
        ("record without its row", pack_drop_row),
        ("feature dropped", lambda p: p["required_features"].remove("item_packs_v1")),
        ("extra copy", lambda p: pack_row(p, 35010188).update(extra=1, remaining=2, receipt_cap=2)),
        ("reversed key order", lambda p: p.__setitem__("item_packs", dict(reversed(list(p["item_packs"].items()))))),
        ("unchanged copy", lambda p: None),
        ]:
    alter("packs-" + name, change, pack_base)
def env_alter(name, change):
    case = copy.deepcopy(pack_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cp.check_envelope_options(case["ctr_options"])
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append(("envelope-" + name, case, accepted))
for name, key, value in [("send 8", "death_link_send", 8), ("send -1", "death_link_send", -1),
                         ("send Boolean", "death_link_send", True), ("send 0", "death_link_send", 0),
                         ("send 7", "death_link_send", 7), ("display 2", "warp_pad_item_display", 2),
                         ("display Boolean", "warp_pad_item_display", True), ("display 0", "warp_pad_item_display", 0),
                         ("colours integer", "ap_item_type_colors", 1), ("colours true", "ap_item_type_colors", True)]:
    env_alter(name, lambda c, key=key, value=value: c["ctr_options"].__setitem__(key, value))
for key in ("death_link_send", "warp_pad_item_display", "ap_item_type_colors"):
    env_alter("absent " + key, lambda c, key=key: c["ctr_options"].pop(key))
env_alter("no trap block", lambda c: c.pop("trap_identities"))

# Character packs (#133 PR 4b): the roster table native maps unlock items
# with, generated plans with Character Unlocks, Remove Playable Oxide, racer
# locks (also with manual rules met by starting items) and the stat keys, then
# forged records, locks, rows and envelope values.
impl = (root / "ap/ap_content_plan_impl.hpp").read_text()
native_roster = [int(x) for x in re.search(r"kRosterCharacter\[16\] = \{([^}]*)\}", impl)[1].split(",")]
assert native_roster == list(cp.characters.ROSTER_CHARACTER_ID.values()), "native roster table differs from the apworld's"
assert list(cp.characters.ROSTER_CHARACTER_ID) == list(cp.characters.ROSTER), "apworld roster orders differ"
assert [cp.content_item_pool.ITEMS[n] for n in cp.characters.ROSTER] == list(range(35010123, 35010139)), \
    "unlock item codes are not 35010123 + roster slot"
from worlds.ctr.test.test_character_packs import racer_options
for seed in range(36):
    options = racer_options(locks=(0, 1, 5, 12, 27, True)[seed % 6], oxide=seed % 4 == 1,
                            start=("random_starter", "random_any", "penta_penguin")[seed % 3],
                            starting_stat_class=("vanilla", "speed", "turning")[seed % 3],
                            penta_stats=("ntsc", "pal")[seed % 2],
                            editable_stats=("off", "global", "per_character")[seed % 3],
                            wumpa_check=("off", "global", "per_track")[seed % 3])
    if seed % 9 == 8:
        options["character_unlocks"] = False
    cases.append((f"racers-generated-{seed}", make_world(options, seed=seed).fill_slot_data(), True))
for seed in range(12):
    options = racer_options(locks=27)
    options["warp_pad_rules"] = {pad: dict(item=item, count=count)
                                 for pad, (item, count) in manual_maps[seed % len(manual_maps)].items()}
    if seed % 2:
        options["start_inventory"] = {"Trophy": 1}
    cases.append((f"racers-manual-{seed}", make_world(options, seed=seed).fill_slot_data(), True))
racer_base = make_world(racer_options(locks=6, oxide=True, starting_stat_class="speed", penta_stats="pal",
                                      editable_stats="per_character"), seed=20261017).fill_slot_data()
cases.append(("racers-base", racer_base, True))
racer_plan = racer_base["content_plan"]
racer_locks = {p["id"]: p["racer_lock"] for p in racer_plan["pads"] if p["racer_lock"] is not None}
racer_locked = next(iter(racer_locks))
racer_start = racer_plan["racers"]["starting_character"]
racer_free = next(p["id"] for p in racer_plan["pads"] if p["entry_id"] and p["entrance_req"] == "free")
racer_empty = next(p["id"] for p in racer_plan["pads"] if p["kind"] == "normal" and p["entry_id"] is None)
racer_spare = next(p for p in cp.lock_eligible(racer_plan) if p not in racer_locks)
racer_code = next(r["item"] for r in racer_plan["items"]["rows"] if 35010123 <= r["item"] <= 35010138)
def rpad(p, pad_id):
    return next(x for x in p["pads"] if x["id"] == pad_id)
def rmove(p, target):
    rpad(p, target)["racer_lock"] = rpad(p, racer_locked)["racer_lock"]
    rpad(p, racer_locked)["racer_lock"] = None
def rdrop_record(p):
    p.pop("racers")
    p["required_features"].remove("character_unlocks_v1")
def rdrop_rows(p):
    rdrop_record(p)
    rows = [r for r in p["items"]["rows"] if not 35010123 <= r["item"] <= 35010138]
    p["items"]["filler_count"] += len(p["items"]["rows"]) - len(rows)
    p["items"]["rows"] = rows
    for pad in p["pads"]:
        pad["racer_lock"] = None
def rdrop_row(p):
    p["items"]["rows"] = [r for r in p["items"]["rows"] if r["item"] != racer_code]
    p["items"]["filler_count"] += 1
def rstart_row(p):
    row = copy.deepcopy(next(r for r in p["items"]["rows"] if r["item"] == racer_code))
    row["item"] = 35010123 + native_roster.index(racer_start)
    p["items"]["rows"].append(row)
    p["items"]["rows"].sort(key=lambda r: r["item"])
    p["items"]["filler_count"] -= 1
def rstart_open(p):
    # Four Keys started from the pool open every hub at the start, so no pad
    # may carry a lock any more.
    row = next(r for r in p["items"]["rows"] if r["item"] == 35010014)
    row.update(start_from_pool=4, remaining=0)
    p["items"]["filler_count"] += 4
for name, change in [
        ("unchanged copy", lambda p: None),
        ("lock without the record", rdrop_record),
        ("lock on a free starting pad", lambda p: rmove(p, racer_free)),
        ("lock on an empty pad", lambda p: rmove(p, racer_empty)),
        ("lock on a garage", lambda p: rmove(p, "garage:0")),
        ("lock moved to another eligible pad", lambda p: rmove(p, racer_spare)),
        ("lock names the starting racer", lambda p: rpad(p, racer_locked).__setitem__("racer_lock", racer_start)),
        ("lock names removed Oxide", lambda p: rpad(p, racer_locked).__setitem__("racer_lock", 15)),
        ("lock names 16", lambda p: rpad(p, racer_locked).__setitem__("racer_lock", 16)),
        ("lock as Boolean", lambda p: rpad(p, racer_locked).__setitem__("racer_lock", True)),
        ("lock negative", lambda p: rpad(p, racer_locked).__setitem__("racer_lock", -1)),
        ("one lock too many", lambda p: rpad(p, racer_spare).__setitem__("racer_lock", racer_locks[racer_locked])),
        ("one lock dropped", lambda p: rpad(p, racer_locked).__setitem__("racer_lock", None)),
        ("requested count 5", lambda p: p["racers"].__setitem__("requested_locks", 5)),
        ("requested count 28", lambda p: p["racers"].__setitem__("requested_locks", 28)),
        ("requested count as Boolean", lambda p: p["racers"].__setitem__("requested_locks", True)),
        ("start changed", lambda p: p["racers"].__setitem__("starting_character", (racer_start + 1) % 15)),
        ("start 16", lambda p: p["racers"].__setitem__("starting_character", 16)),
        ("Oxide start with Oxide removed", lambda p: p["racers"].__setitem__("starting_character", 15)),
        ("Oxide flag flipped", lambda p: p["racers"].__setitem__("remove_playable_oxide", False)),
        ("Oxide flag as integer", lambda p: p["racers"].__setitem__("remove_playable_oxide", 1)),
        ("unknown record field", lambda p: p["racers"].__setitem__("character_unlocks", True)),
        ("record field missing", lambda p: p["racers"].pop("requested_locks")),
        ("record not an object", lambda p: p.__setitem__("racers", [])),
        ("feature dropped", lambda p: p["required_features"].remove("character_unlocks_v1")),
        ("racer row missing", rdrop_row),
        ("row for the starting racer", rstart_row),
        ("racer row with an extra copy", lambda p: next(r for r in p["items"]["rows"] if r["item"] == racer_code).update(
            extra=1, remaining=2, receipt_cap=2)),
        ("racer row swapped for another item", lambda p: next(r for r in p["items"]["rows"] if r["item"] == racer_code).__setitem__(
            "item", 35010187)),
        ("Keys started from the pool open every lock", rstart_open),
        ]:
    alter("racers-" + name, change, racer_base)
def racer_env(name, change):
    case = copy.deepcopy(racer_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cp.check_envelope_options(case["ctr_options"])
        cp.check_racer_envelope(case["content_plan"], case["ctr_options"])
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append(("racer-envelope-" + name, case, accepted))
for name, key, value in [("unlocks false", "character_unlocks", False), ("unlocks integer", "character_unlocks", 1),
                         ("start differs", "starting_character", (racer_start + 1) % 15),
                         ("class 0", "starting_stat_class", 0), ("class 5", "starting_stat_class", 5),
                         ("class Boolean", "starting_stat_class", True), ("class 1", "starting_stat_class", 1),
                         ("penta 0", "penta_stats", 0), ("penta 2", "penta_stats", 2),
                         ("source 1", "stat_source", 1), ("source 0", "stat_source", 0), ("owner 0", "stat_owner", 0),
                         ("owner 3", "stat_owner", 3), ("owner 1", "stat_owner", 1),
                         ("editing false", "stat_editing_allowed", False), ("editing integer", "stat_editing_allowed", 1)]:
    racer_env(name, lambda c, key=key, value=value: c["ctr_options"].__setitem__(key, value))
for key in ("starting_stat_class", "penta_stats", "stat_source", "stat_owner", "stat_editing_allowed"):
    racer_env("absent " + key, lambda c, key=key: c["ctr_options"].pop(key))
racer_env("record, rows and locks dropped, unlocks off", lambda c: (rdrop_rows(c["content_plan"]),
                                                                     c["ctr_options"].__setitem__("character_unlocks", False)))
racer_env("record, rows and locks dropped, unlocks still on", lambda c: rdrop_rows(c["content_plan"]))
racer_env("absent stat trio", lambda c: [c["ctr_options"].pop(k) for k in ("stat_source", "stat_owner", "stat_editing_allowed")])
case = copy.deepcopy(pack_base)
case["ctr_options"]["character_unlocks"] = True
cases.append(("racer-envelope-unlocks true without the record", case, False))

# Capability packs (#133 PR 4c): the ledger rows native derives from the
# `capability` record, generated plans with Progressive Boost, Blue Fire,
# Progressive Stats, every Logic Difficulty and Shortcut Knowledge, custom
# tracks with and without super turbo pads, then forged records, ranks, rows
# and envelopes. A check's `boost` is the apworld's logic as data: native
# checks its range and that the plan has Progressive Boost on, and does not
# re-derive it (the win_logic rule), so a forged rank inside the chain is
# refused by the generator's validator only. Those cases are listed in
# `native_trusts` and must be refused by the apworld and accepted by native.
from worlds.ctr import content_capability as ccap
from worlds.ctr.test.test_capability_packs import cap_options, first_world
for blue in (False, True):
    for boost_mode in (1, 2):
        for stats_mode in (0, 1, 2):
            want = ccap.item_counts(dict(boost_mode=boost_mode, boost_blue_fire=blue, stats_mode=stats_mode))
            got = {}
            for slot in range(16):
                for chain in range(4):
                    mode = stats_mode if chain else boost_mode
                    if mode == 1 and slot == 0:
                        got[35010027 + chain] = 4 if chain else (3 if blue else 2)
                    if mode == 2:
                        got[35010031 + 4 * slot + chain] = 4 if chain else (3 if blue else 2)
            assert {cp.content_item_pool.ITEMS[n]: c for n, c in want.items()} == got, "capability rows differ"
for seed in range(36):
    pads = (None, True, False)[seed % 3]
    options = cap_options(boost=("shared_global", "per_character", "off")[seed % 3] if seed % 3 != 2 else "off",
                          stats=("off", "shared_global")[seed % 2] if seed % 3 != 2 else "shared_global",
                          difficulty=("easy", "medium", "hard")[(seed // 3) % 3], blue_fire=seed % 4 == 1,
                          knowledge=("easy", "medium", "hard")[seed % 3], loop_pads=pads,
                          relic_perfect_checks=seed % 2 == 0, lettersanity=(0, 2, 3)[seed % 3],
                          wumpa_check=("off", "global", "per_track")[(seed // 2) % 3])
    if seed % 6 == 1:
        options.update(character_unlocks=True, racer_locked_pads=6)
    cases.append((f"capability-generated-{seed}", first_world(options, seed=seed).fill_slot_data(), True))
# Per-racer stat chains (192 copies) fit only with every check family on.
for seed in range(4):
    options = cap_options(boost=("off", "shared_global")[seed % 2], stats="per_character", relic_perfect_checks=True,
                          lettersanity=1, letters_per_track=3, wumpa_check="per_track", podium_held_rungs=True,
                          podium_held_fifth_rung=True, podium_finish_rungs=True, podium_any_position_rung=True)
    cases.append((f"capability-per-racer-stats-{seed}", first_world(options, seed=seed).fill_slot_data(), True))
cap_base = first_world(cap_options(stats="shared_global", difficulty="medium", relic_perfect_checks=True,
                                   lettersanity=2, character_unlocks=True, racer_locked_pads=4), seed=20261018).fill_slot_data()
cases.append(("capability-base", cap_base, True))
cap_plan = cap_base["content_plan"]
ranked = next(i for i, c in enumerate(cap_plan["checks"]) if c.get("boost") == 2)
unranked = next(i for i, c in enumerate(cap_plan["checks"]) if "boost" not in c)
loop_index = next(i for i, p in enumerate(cap_plan["packages"]) if p["content_id"] == "fixture-loop")
def crow(p, item):
    return next(r for r in p["items"]["rows"] if r["item"] == item)
def cdrop_record(p):
    p.pop("capability")
    p["required_features"].remove("capability_packs_v1")
def cdrop_all(p):
    cdrop_record(p)
    rows = [r for r in p["items"]["rows"] if not 35010027 <= r["item"] <= 35010094]
    p["items"]["filler_count"] += sum(r["remaining"] for r in p["items"]["rows"]) - sum(r["remaining"] for r in rows)
    p["items"]["rows"] = rows
    for check in p["checks"]:
        check.pop("boost", None)
def cdrop_row(p):
    p["items"]["rows"] = [r for r in p["items"]["rows"] if r["item"] != 35010028]
    p["items"]["filler_count"] += 4
native_trusts = set()
for name, change, trusted in [
        ("unchanged copy", lambda p: None, False),
        ("record dropped, ranks kept", cdrop_record, False),
        ("feature dropped", lambda p: p["required_features"].remove("capability_packs_v1"), False),
        ("unknown record field", lambda p: p["capability"].__setitem__("itemsanity", 0), False),
        ("record field missing", lambda p: p["capability"].pop("shortcut_knowledge"), False),
        ("record not an object", lambda p: p.__setitem__("capability", []), False),
        ("boost mode 3", lambda p: p["capability"].__setitem__("boost_mode", 3), False),
        ("boost mode as Boolean", lambda p: p["capability"].__setitem__("boost_mode", True), False),
        ("both modes off", lambda p: p["capability"].update(boost_mode=0, stats_mode=0), False),
        ("boost mode per racer, shared rows", lambda p: p["capability"].__setitem__("boost_mode", 2), False),
        ("stats off, stat rows kept", lambda p: p["capability"].__setitem__("stats_mode", 0), False),
        ("Blue Fire on, two boost copies", lambda p: p["capability"].__setitem__("boost_blue_fire", True), False),
        ("Blue Fire as integer", lambda p: p["capability"].__setitem__("boost_blue_fire", 0), False),
        ("difficulty 3", lambda p: p["capability"].__setitem__("logic_difficulty", 3), False),
        ("knowledge -1", lambda p: p["capability"].__setitem__("shortcut_knowledge", -1), False),
        ("boost row with an extra copy", lambda p: crow(p, 35010027).update(base=3, remaining=3, receipt_cap=3,
                                                                           requirement_ceiling=3), False),
        ("stat row missing", cdrop_row, False),
        ("rank above the chain", lambda p: p["checks"][ranked].__setitem__("boost", 3), False),
        ("rank 0", lambda p: p["checks"][ranked].__setitem__("boost", 0), False),
        ("rank as Boolean", lambda p: p["checks"][ranked].__setitem__("boost", True), False),
        ("rank as text", lambda p: p["checks"][ranked].__setitem__("boost", "2"), False),
        ("super turbo pads as integer", lambda p: p["packages"][loop_index]["capabilities"].__setitem__("super_turbo_pads", 1), False),
        ("rank lowered", lambda p: p["checks"][ranked].__setitem__("boost", 1), True),
        ("rank dropped", lambda p: p["checks"][ranked].pop("boost"), True),
        ("rank added", lambda p: p["checks"][unranked].__setitem__("boost", 1), True),
        ("difficulty changed, ranks kept", lambda p: p["capability"].__setitem__("logic_difficulty", 2), True),
        ("knowledge changed, ranks kept", lambda p: p["capability"].__setitem__("shortcut_knowledge", 2), True),
        ("super turbo pads stated false, ranks kept",
         lambda p: p["packages"][loop_index]["capabilities"].__setitem__("super_turbo_pads", False), True),
        ]:
    alter("capability-" + name, change, cap_base)
    if trusted:
        native_trusts.add("capability-" + name)
        assert cases[-1][2] is False, name + ": the apworld must refuse a forged rank"
def cap_env(name, change, source=None):
    case = copy.deepcopy(source or cap_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cp.check_envelope_options(case["ctr_options"])
        cp.check_racer_envelope(case["content_plan"], case["ctr_options"])
        ccap.check_envelope(case["content_plan"].get("capability", {}), case["ctr_options"])
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append(("capability-envelope-" + name, case, accepted))
for name, key, value in [("boost 0", "progressive_boost", 0), ("boost 2", "progressive_boost", 2),
                         ("boost Boolean", "progressive_boost", True), ("stats 0", "progressive_stats", 0),
                         ("stats 2", "progressive_stats", 2), ("source 2", "stat_source", 2),
                         ("owner 2", "stat_owner", 2), ("editing true", "stat_editing_allowed", True),
                         ("editing integer", "stat_editing_allowed", 0)]:
    cap_env(name, lambda c, key=key, value=value: c["ctr_options"].__setitem__(key, value))
for key in ("stat_source", "stat_owner", "stat_editing_allowed"):
    cap_env("absent " + key, lambda c, key=key: c["ctr_options"].pop(key))
cap_env("editable stats trio on a Progressive Stats plan",
        lambda c: c["ctr_options"].update(stat_source=2, stat_owner=1, stat_editing_allowed=True))
cap_env("record, rows and ranks dropped, envelope off",
        lambda c: (cdrop_all(c["content_plan"]), c["ctr_options"].update(progressive_boost=0, progressive_stats=0),
                   [c["ctr_options"].pop(k) for k in ("stat_source", "stat_owner", "stat_editing_allowed")]))
cap_env("record, rows and ranks dropped, envelope still on", lambda c: cdrop_all(c["content_plan"]))
cap_env("Progressive Stats trio on a plan without the record", lambda c: c["ctr_options"].update(
    stat_source=1, stat_owner=1, stat_editing_allowed=False), pack_base)

# Itemsanity and Hit Character (#133 PR 4d): generated rooms over retail
# subsets, modes, stages, racer locks and boost; then forged records, checks,
# rows, envelopes and encounter blocks.
from worlds.ctr import content_global_checks as cgc
from worlds.ctr.test.test_global_checks import global_options
for seed in range(30):
    items, hits = ((True, True), (True, False), (False, True))[seed % 3]
    retail = (None, [3, 16, 4], [0, 1, 2, 12], [9])[(seed // 3) % 4]
    modes = (("trophy", "ctr", "relic"), ("trophy",), ("ctr", "relic"))[(seed // 2) % 3]
    options = global_options(items=items, hits=hits, retail=retail, modes=modes,
                             lettersanity=(0, 1, 2)[seed % 3], wumpa_check=("off", "global")[seed % 2],
                             progressive_boost=("off", "shared_global", "per_character")[(seed // 5) % 3])
    if seed % 4 == 1:
        options.update(character_unlocks=True, racer_locked_pads=5)
    if seed % 5 == 2:
        options["content_pool"]["tracks"]["cortex_vortex"] = True
    try:
        world = first_world(options, seed=seed)
    except OptionError as exc:
        print(f"global-generated-{seed} refused by the generator: {str(exc)[:120]}")
        continue
    cases.append((f"global-generated-{seed}", world.fill_slot_data(), True))
cases.append(("global-fixture", json.loads((root / "tools/fixtures/content-plan/global-checks.json").read_text()), True))
glob_base = first_world(global_options(retail=[3, 16, 4], character_unlocks=True, racer_locked_pads=4), seed=5).fill_slot_data()
cases.append(("global-base", glob_base, True))
def gcheck(p, code):
    return next(c for c in p["checks"] if c["location"] == code)
def grow(p, item):
    return next(r for r in p["items"]["rows"] if r["item"] == item)
def gdrop_record(p):
    p.pop("global_checks")
    p["required_features"].remove(cgc.FEATURE)
def gguest_custom(p):
    # Since #133 PR 5b a guest routes from custom tracks too.
    c = gcheck(p, 35025009)
    c["routes"] = [r for r in c["routes"] if r["entry_id"] != "entry/custom/baby-t-park/1.0.2"]
def gdefault_no_custom(p):
    c = gcheck(p, 35025002)
    c["routes"] = [r for r in c["routes"] if not r["entry_id"].startswith("entry/custom/")]
def gweapon_dropped(p):
    p["items"]["rows"] = [r for r in p["items"]["rows"] if r["item"] != 35010099]
    p["items"]["filler_count"] += 1
def gswap_order(p):
    c = gcheck(p, 35016004)
    c["routes"] = c["routes"][::-1]
for name, change in [
        ("unchanged copy", lambda p: None),
        ("record dropped", gdrop_record),
        ("feature dropped", lambda p: p["required_features"].remove(cgc.FEATURE)),
        ("unknown record field", lambda p: p["global_checks"].__setitem__("box_locations", True)),
        ("itemsanity false", lambda p: p["global_checks"].__setitem__("itemsanity", False)),
        ("itemsanity integer", lambda p: p["global_checks"].__setitem__("itemsanity", 1)),
        ("record not an object", lambda p: p.__setitem__("global_checks", [])),
        ("use route dropped", lambda p: gcheck(p, 35016006)["routes"].pop()),
        ("use routes reordered", gswap_order),
        ("use award", lambda p: gcheck(p, 35016001)["routes"][0].__setitem__("award", "use")),
        ("use owner", lambda p: gcheck(p, 35016001).__setitem__("owner", "retail:3")),
        ("use name", lambda p: gcheck(p, 35016001).__setitem__("display_name", "Itemsanity: Turbo")),
        ("use check with a boost rank", lambda p: gcheck(p, 35016000).__setitem__("boost", 1)),
        ("guest without its custom track route", gguest_custom),
        ("default racer without its custom routes", gdefault_no_custom),
        ("hit kind", lambda p: gcheck(p, 35025004).__setitem__("kind", "itemsanity")),
        ("weapon row dropped", gweapon_dropped),
        ("weapon row with two copies", lambda p: grow(p, 35010101).update(base=2, remaining=2, receipt_cap=2,
                                                                         requirement_ceiling=2)),
        ]:
    alter("global-" + name, change, glob_base)
def glob_env(name, change, source=None):
    case = copy.deepcopy(source or glob_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cgc.check_encounter_block(case["content_plan"], case)
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append(("global-envelope-" + name, case, accepted))
def gtrigger(c, guest):
    return c["hit_character_encounters"]["unlock_triggers"][str(guest)]
glob_env("unchanged copy", lambda c: None)
glob_env("Hit envelope off", lambda c: c["ctr_options"].__setitem__("hit_character", False))
glob_env("Hit envelope integer", lambda c: c["ctr_options"].__setitem__("hit_character", 1))
glob_env("Hit block dropped", lambda c: c.pop("hit_character_encounters"))
glob_env("fallback on a guest with a win in the plan", lambda c: gtrigger(c, 14).__setitem__("fallback_keys", 1))
glob_env("fallback missing on a guest without a win", lambda c: gtrigger(c, 13).pop("fallback_keys"))
glob_env("boss guest fallback missing", lambda c: gtrigger(c, 10).pop("fallback_keys"))
glob_env("boss guest fallback count", lambda c: gtrigger(c, 10).__setitem__("fallback_keys", 2))
glob_env("Oxide fallback count", lambda c: gtrigger(c, 15).__setitem__("fallback_keys", 3))
glob_env("block schema 2", lambda c: c["hit_character_encounters"].__setitem__("schema", 2))
glob_env("Hit block on a plan without Hit Character",
         lambda c: c.__setitem__("hit_character_encounters", copy.deepcopy(glob_base["hit_character_encounters"])),
         next(w for name, w, ok in cases if name == "global-generated-1"))

# Item Box Locations (#133 PR 4e): generated rooms over every tier, boost and
# stats, retail subsets, named tracks with several stated crate counts, a
# generic slot with crates, Cortex Vortex, racer locks and Itemsanity; then
# forged records, box checks, crate counts and envelopes. A box's `boost` and
# `stats` are read as given (like the 4c rank), so forged terms are refused by
# the generator alone (`native_trusts`).
from worlds.ctr import content_item_boxes as cbox
from worlds.ctr.test.test_content_item_boxes import box_options, btp, unruled, first_world as box_world
for seed in range(30):
    crates = (8, 1, 15, 40, 0, None)[seed % 6]
    loop = copy.deepcopy(UNKNOWN)
    if seed % 2:
        loop["capabilities"]["weapon_crates"] = 12
    custom = [[btp(crates), unruled(20 + seed), loop], [btp(crates)], [unruled(3), loop]][seed % 3]
    options = box_options(custom=custom, tier=("easy", "medium", "hard")[seed % 3],
                          progressive_boost=("off", "shared_global", "per_character")[(seed // 3) % 3],
                          progressive_stats=("off", "shared_global")[(seed // 2) % 2],
                          color_boxes_by_item=seed % 4 != 1, itemsanity=seed % 5 == 2)
    if seed % 4 == 3:
        options["content_pool"]["tracks"]["retail"] = [4, 5, 11, 13, 7]
    if seed % 5 == 1:
        options["content_pool"]["tracks"]["cortex_vortex"] = True
    if seed % 7 == 4:
        options.update(character_unlocks=True, racer_locked_pads=5)
    try:
        world = box_world(options, seed=seed)
    except OptionError as exc:
        print(f"boxes-generated-{seed} refused by the generator: {str(exc)[:120]}")
        continue
    cases.append((f"boxes-generated-{seed}", world.fill_slot_data(), True))
cases.append(("boxes-fixture", json.loads((root / "tools/fixtures/content-plan/item-boxes.json").read_text()), True))
box_base = cases[-1][1]
def bcheck(p, name):
    return next(c for c in p["checks"] if c["display_name"] == name)
def bpackage(p, key):
    return next(d for d in p["packages"] if d["content_id"] == key)
def bdrop_record(p):
    p.pop("item_boxes")
    p["required_features"].remove(cbox.FEATURE)
def bdrop_all(p):
    bdrop_record(p)
    count = sum(1 for c in p["checks"] if c["kind"] == "box")
    p["checks"] = [c for c in p["checks"] if c["kind"] != "box"]
    p["items"]["coded_checks"] -= count
    p["items"]["filler_count"] -= count
def bdrop_box(p, name):
    p["checks"].remove(bcheck(p, name))
    p["items"]["coded_checks"] -= 1
    p["items"]["filler_count"] -= 1
def bgeneric_box(p):
    extra = copy.deepcopy(bcheck(p, "Baby T Park: Item Box 1"))
    extra.update(owner="custom/fixture-loop/2.1", location=35017000, display_name="Fixture Loop: Item Box 1")
    extra["routes"] = [dict(r, entry_id="entry/custom/fixture-loop/2.1",
                            own_pad_id=next(q["id"] for q in p["pads"] if q["entry_id"] == "entry/custom/fixture-loop/2.1"))
                       for r in extra["routes"]]
    p["checks"].append(extra)
    p["items"]["coded_checks"] += 1
    p["items"]["filler_count"] += 1
for name, change, trusted in [
        ("unchanged copy", lambda p: None, False),
        ("record dropped, boxes kept", bdrop_record, False),
        ("record and boxes dropped", bdrop_all, False),
        ("feature dropped", lambda p: p["required_features"].remove(cbox.FEATURE), False),
        ("unknown record field", lambda p: p["item_boxes"].__setitem__("color_boxes_by_item", True), False),
        ("record not an object", lambda p: p.__setitem__("item_boxes", []), False),
        ("tier lowered", lambda p: p["item_boxes"].__setitem__("shortcut_knowledge", 1), False),
        ("tier 3", lambda p: p["item_boxes"].__setitem__("shortcut_knowledge", 3), False),
        ("tier as Boolean", lambda p: p["item_boxes"].__setitem__("shortcut_knowledge", True), False),
        ("tier disagrees with the capability record", lambda p: p["capability"].__setitem__("shortcut_knowledge", 1), False),
        ("hard-tier box dropped", lambda p: bdrop_box(p, "Oxide Station: Item Box 6"), False),
        ("named box dropped", lambda p: bdrop_box(p, "Baby T Park: Item Box 8"), False),
        ("box on the generic slot", bgeneric_box, False),
        ("box code", lambda p: bcheck(p, "N. Gin Labs: Item Box 1").__setitem__("location", 35014166), False),
        ("box route dropped", lambda p: bcheck(p, "N. Gin Labs: Item Box 1")["routes"].pop(), False),
        ("box routes reordered", lambda p: bcheck(p, "N. Gin Labs: Item Box 2")["routes"].reverse(), False),
        ("box award", lambda p: bcheck(p, "Tiger Temple: Item Box 5")["routes"][0].__setitem__("award", "win"), False),
        ("box kind", lambda p: bcheck(p, "Tiger Temple: Item Box 5").__setitem__("kind", "trophy"), False),
        ("box owner", lambda p: bcheck(p, "Tiger Temple: Item Box 5").__setitem__("owner", "retail:5"), False),
        ("box name", lambda p: bcheck(p, "Tiger Temple: Item Box 5").__setitem__("display_name", "Item Box 5"), False),
        ("weapon crates raised", lambda p: bpackage(p, "baby-t-park")["capabilities"].__setitem__("weapon_crates", 9), False),
        ("weapon crates lowered", lambda p: bpackage(p, "baby-t-park")["capabilities"].__setitem__("weapon_crates", 7), False),
        ("weapon crates unstated", lambda p: bpackage(p, "baby-t-park")["capabilities"].pop("weapon_crates"), False),
        ("weapon crates Boolean", lambda p: bpackage(p, "acid-lake")["capabilities"].__setitem__("weapon_crates", True), False),
        ("weapon crates text", lambda p: bpackage(p, "acid-lake")["capabilities"].__setitem__("weapon_crates", "20"), False),
        ("weapon crates past the ceiling", lambda p: bpackage(p, "acid-lake")["capabilities"].__setitem__("weapon_crates", 65537), False),
        ("weapon crates negative", lambda p: bpackage(p, "fixture-loop")["capabilities"].__setitem__("weapon_crates", -1), False),
        ("generic slot crates changed", lambda p: bpackage(p, "fixture-loop")["capabilities"].__setitem__("weapon_crates", 30), False),
        ("stats false", lambda p: bcheck(p, "Oxide Station: Item Box 6").__setitem__("stats", False), False),
        ("stats as integer", lambda p: bcheck(p, "Oxide Station: Item Box 6").__setitem__("stats", 1), False),
        ("stats on a race check", lambda p: bcheck(p, "Oxide Station: Trophy Race").__setitem__("stats", True), False),
        ("box rank above the chain", lambda p: bcheck(p, "N. Gin Labs: Item Box 4").__setitem__("boost", 3), False),
        ("stats dropped", lambda p: bcheck(p, "Oxide Station: Item Box 6").pop("stats"), True),
        ("stats added", lambda p: bcheck(p, "Oxide Station: Item Box 1").__setitem__("stats", True), True),
        ("box rank lowered", lambda p: bcheck(p, "N. Gin Labs: Item Box 4").__setitem__("boost", 1), True),
        ("custom box rank dropped", lambda p: bcheck(p, "Acid Lake: Item Box 1").pop("boost"), True),
        ]:
    alter("boxes-" + name, change, box_base)
    if trusted:
        native_trusts.add("boxes-" + name)
        assert cases[-1][2] is False, name + ": the apworld must refuse a forged box term"
def box_env(name, change, source=None):
    case = copy.deepcopy(source or box_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cp.check_box_envelope(case["content_plan"], case["ctr_options"])
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append(("boxes-envelope-" + name, case, accepted))
box_env("unchanged copy", lambda c: None)
box_env("box_locations false", lambda c: c["ctr_options"].__setitem__("box_locations", False))
box_env("box_locations dropped", lambda c: c["ctr_options"].pop("box_locations"))
box_env("box_locations integer", lambda c: c["ctr_options"].__setitem__("box_locations", 1))
box_env("colours dropped", lambda c: c["ctr_options"].pop("color_boxes_by_item"))
box_env("colours integer", lambda c: c["ctr_options"].__setitem__("color_boxes_by_item", 0))
box_env("colours on", lambda c: c["ctr_options"].__setitem__("color_boxes_by_item", True))
box_env("box keys on a plan without the record",
        lambda c: c["ctr_options"].update(box_locations=True, color_boxes_by_item=True), cap_base)
box_env("colours alone on a plan without the record",
        lambda c: c["ctr_options"].update(color_boxes_by_item=False), cap_base)
# A custom track whose package states no weapon crates is no Itemsanity or Hit
# route (#133 PR 4e); with its routes kept the plan is refused on both sides.
def gcrateless(p, keep_routes):
    bpackage(p, "baby-t-park")["capabilities"]["weapon_crates"] = 0
    if not keep_routes:
        for c in p["checks"]:
            if c["kind"] in ("itemsanity", "hit"):
                c["routes"] = [r for r in c["routes"] if r["entry_id"] != "entry/custom/baby-t-park/1.0.2"]
alter("crates-crateless track still routed", lambda p: gcrateless(p, True), glob_base)
alter("crates-crateless track without its routes", lambda p: gcrateless(p, False), glob_base)

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
cases.append(("unchanged baseline copy", copy.deepcopy(base), True))
for row in range(15):
    for key in ("base", "extra", "start_from_pool", "start_additional", "remaining", "receipt_cap", "requirement_ceiling"):
        alter(f"ledger-{row}-{key}", lambda p, row=row, key=key: p["items"]["rows"][row].__setitem__(key, p["items"]["rows"][row][key] + 1))
for row in range(32):
    alter(f"hub-{row}", lambda p, row=row: p["pads"][row].__setitem__("hub_id", "hub:99"))
for key in ("tracks", "entries", "pads", "checks", "requirements", "items"):
    alter("null-" + key, lambda p, key=key: p.__setitem__(key, None))

# Cups (#133 PR 5): generated plans over counts, sources, repeats, laps,
# sweeps, boost and racer locks, then forged cup records.
from worlds.ctr.test.test_content_cups import cup_options
cup_cases = []
from Options import OptionError as CupOptionError
for seed in range(80):
    try:
        wire = make_world(cup_options(seed), seed=seed).fill_slot_data()
    except CupOptionError:
        continue  # a narrow start the slot refuses before any plan is emitted
    cup_cases.append(wire)
    cases.append((f"cups-generated-{seed}", wire, True))
cup_base = next(w for w in cup_cases if any(c["kind"] == "cup_leg" for c in w["content_plan"]["checks"])
                and any(c["kind"] == "cup_sweep" for c in w["content_plan"]["checks"])
                and any(t["id"] not in {e.get("track_id") for e in w["content_plan"]["entries"]}
                        for t in w["content_plan"]["tracks"]))
def cup_entry(p):
    return next(e for e in p["entries"] if e["kind"] == "cup")
def cup_check(kind):
    return lambda p: next(c for c in p["checks"] if c["kind"] == kind)
for name, change in [
        ("name index", lambda p: cup_entry(p).__setitem__("id", "entry/cup:30")),
        ("display name", lambda p: cup_entry(p).__setitem__("display_name", "Cup")),
        ("three legs", lambda p: cup_entry(p)["legs"].pop()),
        ("leg id", lambda p: cup_entry(p)["legs"][1].__setitem__("id", "x")),
        ("leg unknown track", lambda p: cup_entry(p)["legs"][0].__setitem__("track_id", "retail:99")),
        ("leg trial track", lambda p: cup_entry(p)["legs"][0].__setitem__("track_id", "retail:16")),
        ("leg laps", lambda p: cup_entry(p)["legs"][0].__setitem__("laps", 2)),
        ("leg laps 0", lambda p: cup_entry(p)["legs"][0].__setitem__("laps", 0)),
        ("roster opponent", lambda p: cup_entry(p)["roster"].append(dict(role="opponent", slot=1, character_id=1,
                                                                        driving_profile="a", attack_profile="b"))),
        ("field size", lambda p: cup_entry(p).__setitem__("field_size", 4)),
        ("gate mode", lambda p: next(x for x in p["pads"] if x["entry_id"] == cup_entry(p)["id"])["mode_gates"][0].__setitem__("mode", "trophy")),
        ("gate stage", lambda p: next(x for x in p["pads"] if x["entry_id"] == cup_entry(p)["id"])["mode_gates"][0].__setitem__("stage", 2)),
        ("win code", lambda p: cup_check("cup")(p).__setitem__("location", 35027026)),
        ("win route award", lambda p: cup_check("cup")(p)["routes"][0].__setitem__("award", "win_letters")),
        ("win leg id", lambda p: cup_check("cup")(p)["routes"][0].__setitem__("leg_id", "x")),
        ("sweep dropped", lambda p: p["checks"].remove(cup_check("cup_sweep")(p))),
        ("leg victory dropped", lambda p: p["checks"].remove(cup_check("cup_leg")(p))),
        ("leg victory route dropped", lambda p: cup_check("cup_leg")(p)["routes"].pop()),
        ("leg victory name", lambda p: cup_check("cup_leg")(p).__setitem__("display_name", "Leg")),
        ("feature missing", lambda p: p["required_features"].remove("cup_entries_v1")),
        ("cup-only track mode", lambda p: next(t for t in p["tracks"] if t["id"] not in
                                              {e.get("track_id") for e in p["entries"]})["modes"].append("ctr")),
        ("unused cup-only track", lambda p: p["tracks"].append(dict(p["tracks"][0], id="retail:15", retail_id=15,
                                                                  display_name=cp.RETAIL[15]))),
        ]:
    alter("cup-" + name, change, cup_base)
# Hit Character with cups (#133 PR 5b): generated plans over the cup profiles
# with Hit Character on, then forged cup orders and cup-leg Hit routes.
from worlds.ctr.test.test_content_cups import hit_cup_options
hit_cup_cases = []
for seed in range(40):
    try:
        wire = make_world(hit_cup_options(seed, itemsanity=seed % 3 == 0), seed=seed).fill_slot_data()
    except CupOptionError:
        continue
    hit_cup_cases.append(wire)
    cases.append((f"cup-hits-generated-{seed}", wire, True))
cases.append(("cup-hits-fixture", json.loads((root / "tools/fixtures/content-plan/cup-hits.json").read_text()), True))
hit_cup_base = cases[-1][1]
def hit_cup_env(name, change, source=None):
    case = copy.deepcopy(source or hit_cup_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cgc.check_encounter_block(case["content_plan"], case)
        accepted = True
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
        accepted = False
    cases.append(("cup-hits-" + name, case, accepted))
def hc_orders(c):
    return c["hit_character_encounters"]["content_cups"]
def hc_one(c):
    return sorted(hc_orders(c), key=int)[0]
def hc_spare(c):
    return str(next(i for i in range(27) if str(i) not in hc_orders(c)))
def hc_hit(c, code):
    return next(x for x in c["content_plan"]["checks"] if x["location"] == code)
def hc_drop_cup_routes(c, code):
    x = hc_hit(c, code)
    x["routes"] = [r for r in x["routes"] if r["mode"] != "cup"]
def hc_swap_legs(c):
    routes = hc_hit(c, 35025002)["routes"]
    i = next(i for i in range(len(routes) - 1) if routes[i]["mode"] == "cup" == routes[i + 1]["mode"])
    routes[i], routes[i + 1] = routes[i + 1], routes[i]
for name, change in [
        ("unchanged copy", lambda c: None),
        ("content_cups dropped", lambda c: c["hit_character_encounters"].pop("content_cups")),
        ("content_cups empty", lambda c: c["hit_character_encounters"].__setitem__("content_cups", {})),
        ("cup order dropped", lambda c: hc_orders(c).pop(hc_one(c))),
        ("order for a missing cup", lambda c: hc_orders(c).__setitem__(hc_spare(c), copy.deepcopy(hc_orders(c)[hc_one(c)]))),
        ("order with a repeated id", lambda c: hc_orders(c)[hc_one(c)]["order"].__setitem__(1, hc_orders(c)[hc_one(c)]["order"][0])),
        ("order with fifteen ids", lambda c: hc_orders(c)[hc_one(c)]["order"].pop()),
        ("order with an extra key", lambda c: hc_orders(c)[hc_one(c)].__setitem__("seed", 1)),
        ("order shuffled", lambda c: hc_orders(c)[hc_one(c)]["order"].reverse()),
        ("guest without its cup leg routes", lambda c: hc_drop_cup_routes(c, 35025013)),
        ("default without its cup leg routes", lambda c: hc_drop_cup_routes(c, 35025004)),
        ("cup leg routes out of order", hc_swap_legs),
        ("cup leg route without its leg", lambda c: next(r for r in hc_hit(c, 35025006)["routes"] if r["mode"] == "cup").__setitem__("leg_id", None)),
        ("cup leg route award", lambda c: next(r for r in hc_hit(c, 35025006)["routes"] if r["mode"] == "cup").__setitem__("award", "leg_win")),
        ]:
    hit_cup_env(name, change)
hit_cup_env("content_cups on a Hit plan without cups",
            lambda c: c["hit_character_encounters"].__setitem__("content_cups", copy.deepcopy(hit_cup_base["hit_character_encounters"]["content_cups"])),
            glob_base)
# Boss garages (#133 PR 6): generated plans over the goal arms, venue draws,
# Final Challenge venues and relic modes, with cups, boost, racer locks,
# Itemsanity and Hit Character in some, then forged boss records. A forgery
# is judged by the generator's plan validation plus its envelope goal keys,
# which native reads from the same plan.
from worlds.ctr.test.test_content_bosses import varied as boss_varied
for seed in range(60):
    try:
        wire = make_world(boss_varied(seed), seed=seed).fill_slot_data()
    except CupOptionError:
        continue
    cases.append((f"bosses-generated-{seed}", wire, True))
for fixture in ("bosses", "bosses-final", "bosses-custom"):
    cases.append((fixture + "-fixture", json.loads((root / f"tools/fixtures/content-plan/{fixture}.json").read_text()), True))
boss_base = next(w for name, w, ok in cases if name == "bosses-fixture")
def boss_env(name, change, source=None):
    case = copy.deepcopy(source or boss_base)
    change(case)
    try:
        cp.validate(case["content_plan"])
        cgc.check_encounter_block(case["content_plan"], case)
        accepted = cp.goal_envelope(case["content_plan"]) == {k: case["ctr_options"][k] for k in
                                                              ("goal_oxide", "goal_bosses", "goal_gems")}
    except (ValueError, TypeError, KeyError, OverflowError, AttributeError, IndexError):
        accepted = False
    cases.append(("bosses-" + name, case, accepted))
def b_entry(c, g, index=0):
    return next(e for e in c["content_plan"]["entries"] if e["id"] == f"entry/boss:{g}/{index}")
def b_req(c, rid):
    return next(r for r in c["content_plan"]["requirements"] if r["id"] == rid)
def b_check(c, code):
    return next(x for x in c["content_plan"]["checks"] if x["location"] == code)
def b_row(c, item):
    return next(r for r in c["content_plan"]["items"]["rows"] if r["item"] == item)
def b_pad(c, pid):
    return next(x for x in c["content_plan"]["pads"] if x["id"] == pid)
for name, change in [
        ("unchanged copy", lambda c: None),
        ("venue repeated", lambda c: b_entry(c, 0).__setitem__("track_id", b_entry(c, 1)["track_id"])),
        ("venue uncertified", lambda c: b_entry(c, 1).__setitem__("track_id", "retail:18")),
        ("venue home track", lambda c: b_entry(c, 0).__setitem__("track_id", "retail:6")),
        ("Oxide 1 venue", lambda c: b_entry(c, 4).__setitem__("track_id", "cortex_vortex")),
        ("Final venue retail", lambda c: b_entry(c, 4, 1).__setitem__("track_id", "retail:3")),
        ("opponent", lambda c: b_entry(c, 0)["opponent"].__setitem__("character_id", 3)),
        ("opponent slot", lambda c: b_entry(c, 0)["opponent"].__setitem__("slot", 2)),
        ("driving profile", lambda c: b_entry(c, 1)["opponent"].__setitem__("driving_profile", "boss")),
        ("attack kit", lambda c: b_entry(c, 1)["opponent"].__setitem__("attack_profile", "mixed")),
        ("laps", lambda c: b_entry(c, 3).__setitem__("laps", 1)),
        ("Trophy door", lambda c: b_req(c, "boss:2")["predicate"].__setitem__("count", 11)),
        ("Trophy door item", lambda c: b_req(c, "boss:0")["predicate"].__setitem__("item", 35010014)),
        # The garage roll (#133 PR 6b): accepted shapes and refused ones agree.
        ("garage Trophy four", lambda c: b_req(c, "boss:1").__setitem__("predicate", dict(op="count", item=35010000, count=4))),
        ("garage Trophy eight", lambda c: b_req(c, "boss:1").__setitem__("predicate", dict(op="count", item=35010000, count=8))),
        ("garage Keys at hub gate", lambda c: b_req(c, "boss:3").__setitem__("predicate", dict(op="count", item=35010014, count=3))),
        ("garage Keys above hub gate", lambda c: b_req(c, "boss:0").__setitem__("predicate", dict(op="count", item=35010014, count=2))),
        ("garage free", lambda c: b_req(c, "boss:1").__setitem__("predicate", dict(op="always"))),
        ("garage one Gold Relic", lambda c: b_req(c, "boss:2").__setitem__("predicate", dict(op="count", item=35010002, count=1))),
        ("garage over supply", lambda c: b_req(c, "boss:2").__setitem__("predicate", dict(op="count", item=35010002, count=99))),
        ("record level", lambda c: c["content_plan"]["bosses"].__setitem__("requirement_level", "chaotic")),
        ("record level unknown", lambda c: c["content_plan"]["bosses"].__setitem__("requirement_level", "extreme")),
        ("Oxide 1 companions dropped", lambda c: b_req(c, "oxide:first").__setitem__("predicate", b_req(c, "oxide:first")["predicate"]["terms"][0])),
        ("Final relic count", lambda c: b_req(c, "oxide:final")["predicate"]["terms"][-1].__setitem__("count", 5)),
        ("Final relic tier", lambda c: b_req(c, "oxide:final")["predicate"]["terms"][-1].__setitem__("item", 35010002)),
        ("stray boss record", lambda c: c["content_plan"]["requirements"].append(dict(id="boss:9", predicate=dict(op="always")))),
        ("garage gate", lambda c: b_pad(c, "garage:3").__setitem__("entrance_req", "keys:2")),
        ("Oxide garage gate", lambda c: b_pad(c, "garage:4").__setitem__("entrance_req", "free")),
        ("encounter requirement id", lambda c: c["content_plan"]["encounters"][4].__setitem__("requirement_id", "free")),
        ("encounter index", lambda c: c["content_plan"]["encounters"][5].__setitem__("index", 0)),
        ("encounter code", lambda c: c["content_plan"]["encounters"][0].__setitem__("completion_location", 35011101)),
        ("encounter dropped", lambda c: c["content_plan"]["encounters"].pop()),
        ("encounters swapped", lambda c: c["content_plan"]["encounters"].reverse()),
        ("boss check award", lambda c: b_check(c, 35011100)["routes"][0].__setitem__("award", "hit")),
        ("boss check owner", lambda c: b_check(c, 35011102).__setitem__("owner", "encounter/garage:0/0")),
        ("boss check name", lambda c: b_check(c, 35011103).__setitem__("display_name", "Pinstripe")),
        ("boss check dropped", lambda c: c["content_plan"]["checks"].remove(b_check(c, 35011101))),
        ("boss Hit route dropped", lambda c: b_check(c, 35025010)["routes"].pop()),
        ("boss Hit route elsewhere", lambda c: b_check(c, 35025001)["routes"].append(copy.deepcopy(b_check(c, 35025010)["routes"][-1]))),
        ("goal boss count", lambda c: c["content_plan"]["goal"]["terms"][1].__setitem__("count", 3)),
        ("goal Oxide arm", lambda c: c["content_plan"]["goal"]["terms"][0]["locations"].__setitem__(0, 35011105)),
        ("goal arm order", lambda c: c["content_plan"]["goal"]["terms"].reverse()),
        ("goal without arms", lambda c: c["content_plan"].__setitem__("goal", c["content_plan"]["goal"]["terms"][2])),
        ("record oxide goal", lambda c: c["content_plan"]["bosses"].__setitem__("oxide_goal", 2)),
        ("record boss count", lambda c: c["content_plan"]["bosses"].__setitem__("bosses_required", 3)),
        ("record shuffle", lambda c: c["content_plan"]["bosses"].__setitem__("track_shuffle", False)),
        ("record repeats", lambda c: c["content_plan"]["bosses"].__setitem__("repeats", True)),
        ("record optional", lambda c: c["content_plan"]["bosses"].__setitem__("oxide_1_optional", True)),
        ("record relic mode", lambda c: c["content_plan"]["bosses"].__setitem__("oxide_final_unlock", 1)),
        ("record extra", lambda c: c["content_plan"]["bosses"].__setitem__("pairing", "retail_pairs")),
        ("record dropped", lambda c: c["content_plan"].pop("bosses")),
        ("feature dropped", lambda c: c["content_plan"]["required_features"].remove("boss_encounters_v1")),
        ("envelope goal_oxide", lambda c: c["ctr_options"].__setitem__("goal_oxide", 3)),
        ("envelope goal_bosses", lambda c: c["ctr_options"].__setitem__("goal_bosses", 0)),
        ("envelope goal_gems", lambda c: c["ctr_options"].__setitem__("goal_gems", 0)),
        ("boss-only venue mode", lambda c: next(t for t in c["content_plan"]["tracks"] if t["id"] == "cortex_vortex")["modes"].append("ctr")),
        ("no filler for the Oxide goal check", lambda c: (b_row(c, 35010000).update(extra=75, remaining=91, receipt_cap=91),
                                                          c["content_plan"]["items"].update(filler_count=0, trap_count=0))),
        ("boss rank 0", lambda c: b_check(c, 35011105).__setitem__("boost", 0)),
        ("boss rank above the chain", lambda c: b_check(c, 35011105).__setitem__("boost", 3)),
        ]:
    boss_env(name, change)
# A boss check's `boost` is the apworld's logic as data, read as given like
# every other check's rank (#133 PR 4c above): a forged rank inside the chain
# is refused by the generator alone.
for name, change in [
        ("boss rank lowered", lambda c: b_check(c, 35011105).__setitem__("boost", 1)),
        ("boss rank dropped", lambda c: b_check(c, 35011100).pop("boost")),
        # #133 PR 6b: Cortex Vortex is a certified garage venue now; moved there
        # without its USF rank, only the rank is forged.
        ("venue Cortex Vortex on a garage", lambda c: b_entry(c, 2).__setitem__("track_id", "cortex_vortex")),
        ]:
    boss_env(name, change)
    native_trusts.add("bosses-" + name)
    assert cases[-1][2] is False, name + ": the apworld must refuse a forged rank"
final_base = next(w for name, w, ok in cases if name == "bosses-final-fixture")
for name, change in [
        ("final unchanged copy", lambda c: None),
        ("final retail pair moved", lambda c: b_entry(c, 0).__setitem__("track_id", "retail:3")),
        ("final total relics over supply", lambda c: (c["content_plan"]["bosses"].__setitem__("oxide_final_count", 13),
                                                      b_req(c, "oxide:final")["predicate"].__setitem__("count", 13))),
        ("final optional without 101", lambda c: c["content_plan"]["bosses"].__setitem__("oxide_goal", 1)),
        ("final gem arm added", lambda c: c["ctr_options"].__setitem__("goal_gems", 1)),
        ("final venue Cortex Vortex", lambda c: b_entry(c, 4, 1).__setitem__("track_id", "cortex_vortex")),
        ]:
    boss_env(name, change, final_base)
# Custom and Cortex Vortex garage venues (#133 PR 6b): forgeries on
# bosses-custom.json, where Papu Papu races Cortex Vortex and Pinstripe Baby T Park.
custom_base = next(w for name, w, ok in cases if name == "bosses-custom-fixture")
def c_pkg(c):
    return next(p for p in c["content_plan"]["packages"] if p["content_id"] == "baby-t-park")
for name, change in [
        ("custom unchanged copy", lambda c: None),
        ("custom count too small", lambda c: c_pkg(c)["capabilities"].__setitem__("checkpoints", 3)),
        ("custom count other", lambda c: c_pkg(c)["capabilities"].__setitem__("checkpoints", 60)),
        ("custom no nav", lambda c: c_pkg(c)["capabilities"].__setitem__("ai_nav", False)),
        ("custom no count", lambda c: c_pkg(c)["capabilities"].pop("checkpoints")),
        ("custom shuffle off", lambda c: c["content_plan"]["bosses"].__setitem__("track_shuffle", False)),
        ("custom repeats off", lambda c: c["content_plan"]["bosses"].__setitem__("repeats", False)),
        ("Cortex Vortex on Roo", lambda c: b_entry(c, 0).__setitem__("track_id", "cortex_vortex")),
        ("custom on Roo", lambda c: b_entry(c, 0).__setitem__("track_id", "custom/baby-t-park/1.0.2")),
        ]:
    boss_env(name, change, custom_base)
run = subprocess.run([str(args.parser.resolve()), "--probe"], input="".join(json.dumps(v) + "\n" for _, v, _ in cases),
                     text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, cwd=root)
answers = run.stdout.splitlines()
assert len(answers) == len(cases), (len(answers), len(cases), run.stderr[-1000:])
disagreements = [name for (name, _, expected), actual in zip(cases, answers)
                 if name not in native_trusts and (actual == "ACCEPT") != expected]
assert not disagreements, disagreements
# Forged ranks inside the chain: the generator refuses, native trusts them.
trusted = [name for (name, _, _), actual in zip(cases, answers) if name in native_trusts and actual != "ACCEPT"]
assert not trusted, trusted
print(f"PASS registry parity and {len(cases)} native/generator admission cases ({sum(x[2] for x in cases)} accepted, "
      f"{len(native_trusts)} forged ranks and box terms the generator alone refuses)")
