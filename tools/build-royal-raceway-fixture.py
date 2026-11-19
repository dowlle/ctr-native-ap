"""Export real Royal rooms and Archipelago answers for the native evaluator.

Use the AP virtualenv; pass --apworld CHECKOUT --output JSON.
Run test-win-logic-roundtrip --current JSON on the resulting fixture.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--apworld", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
os.environ["SKIP_REQUIREMENTS_UPDATE"] = "1"
sys.path.insert(0, str(args.apworld.resolve()))
from BaseClasses import CollectionState
from worlds.ctr import characters
from worlds.ctr.test.test_royal_raceway import royal_options
from worlds.ctr.test.test_content_plan import make_world

seeds = []
for count in (0, 1, 5, 15, 50):
    for mode in ("off", "shared_global", "per_character"):
        for wumpa in ("off", "crates"):
            world = make_world(royal_options(item_box_count=count, progressive_boost=mode,
                                              wumpasanity=wumpa, character_unlocks=True,
                                              racer_locked_pads=20), seed=3, stop_before="pre_fill")
            wire = json.loads(json.dumps(world.fill_slot_data()))
            states = []
            for rank, omit in ((0, None), (1, None), (2, None), (2, "Key"), (2, "racers")):
                state = CollectionState(world.multiworld)
                received = collections.Counter()
                all_items = world.multiworld.get_all_state().prog_items[1]
                for name, num in all_items.items():
                    if name == omit or (omit == "racers" and name in characters.ROSTER_CHARACTER_ID):
                        continue
                    if name.startswith("Progressive Boost"):
                        num = min(num, rank)
                    for _ in range(num):
                        item = world.create_item(name)
                        state.collect(item, True)
                        if item.code is not None:
                            received[item.code] += 1
                states.append(dict(received=dict(received), bosses=0,
                                   can_reach={str(loc.address): loc.can_reach(state)
                                              for loc in world.multiworld.get_locations(1)
                                              if loc.address is not None}))
            seeds.append(dict(label=f"royal-{count}-{mode}-{wumpa}", slot_data=wire,
                              start_entries=[], states=states))
args.output.write_text(json.dumps(dict(seeds=seeds)))
print(f"exported {len(seeds)} Royal rooms with {sum(len(s['states']) for s in seeds)} states")
