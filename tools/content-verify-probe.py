"""Fuzz hook for the content-plan verifier parity (#133 PR 8).

Install as `hooks/content_verify_probe.py` in the apworld fuzzer's
Archipelago tree and run a content-plan arm with
`--hook hooks.content_verify_probe:Hook`. After every successful generation it
appends one JSON row per CTR content-plan slot to CTR_VERIFY_PROBE_LOG: the
room (slot data and location codes), its own placement and starting
inventory, and the apworld's own verdicts from AP's CollectionState:

  * `full`: the reachable locations and the goal with the whole placement;
  * `samples`: for random subsets of the slot's progression items, the
    reachable locations and the goal with only those items (plus starts),
    after collecting the reachable companion events.

tools/test-content-verify.cpp --probe <log> replays each row through the
native sweep, which must agree exactly. Items this slot receives from other
slots are listed as `foreign`."""
import json
import os
import random

from BaseClasses import CollectionState
from fuzz import BaseHook

LOG = os.environ.get("CTR_VERIFY_PROBE_LOG", "/tmp/ctr138-vf/verify-probe.jsonl")
SAMPLES = int(os.environ.get("CTR_VERIFY_PROBE_SAMPLES", "6"))


def _verdict(mw, player, state, locations):
    events = [loc for loc in mw.get_locations(player) if loc.address is None]
    state.sweep_for_advancements(locations=events)
    reachable = sorted(loc.address for loc in locations if loc.can_reach(state))
    return reachable, bool(mw.completion_condition[player](state))


class Hook(BaseHook):
    def after_generate(self, mw, output_path):
        if mw is None:
            return
        for player in mw.player_ids:
            world = mw.worlds[player]
            if getattr(world, "game", None) != "Crash Team Racing" or not getattr(world, "ctr_content_plan", None):
                continue
            locations = [loc for loc in mw.get_locations(player) if loc.address is not None]
            own = {str(loc.address): loc.item.code for loc in locations
                   if loc.item is not None and loc.item.player == player and loc.item.code is not None}
            foreign = [loc.item.code for loc in mw.get_filled_locations()
                       if loc.player != player and loc.item.player == player and loc.item.code is not None]
            starts = [item.code for item in mw.precollected_items[player] if item.code is not None]
            full_state = CollectionState(mw)
            full_state.sweep_for_advancements()
            reachable, goal = _verdict(mw, player, full_state, locations)
            row = dict(tag=f"{mw.seed_name}/{player}", slot_data=world.fill_slot_data(),
                       locations=[loc.address for loc in locations], own=own, foreign=foreign,
                       precollected=starts, full=dict(reachable=reachable, goal=goal), samples=[])
            pool = [loc.item for loc in mw.get_filled_locations()
                    if loc.item.player == player and loc.item.advancement and loc.item.code is not None]
            rng = random.Random(f"{mw.seed}/{player}")
            for _ in range(SAMPLES):
                share = rng.random()
                subset = [item for item in pool if rng.random() < share]
                state = CollectionState(mw)
                for item in subset:
                    state.collect(item, True)
                reachable, goal = _verdict(mw, player, state, locations)
                row["samples"].append(dict(items=[item.code for item in subset], reachable=reachable, goal=goal))
            with open(LOG, "a") as fd:
                fd.write(json.dumps(row, default=str) + "\n")
