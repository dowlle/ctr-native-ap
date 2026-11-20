#!/usr/bin/env python3
"""Build the A6-48 final-only venue admission and reachability fixtures.

Run once against the paired world and once against pre-feature main with
--legacy, then join the two outputs. The output contains generated plans,
never a played room. Row shape as make-oxide-pickup-fixtures.py.
"""
import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apworld', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--legacy', action='store_true')
    args = parser.parse_args()
    sys.path.insert(0, str(args.apworld.resolve()))
    from BaseClasses import CollectionState
    from worlds.ctr.test.test_content_plan import make_world

    common = dict(retail_tracks=['Dingo Canyon', 'Tiny Arena', 'Hot Air Skyway'], custom_tracks=[],
                  cortex_vortex_track='off', cup_count=0, crystal_challenge_count=0, boss_garages=True,
                  oxide_goal='101_percent', oxide_final_track='cortex_vortex', bosses_required_goal=0,
                  gems_required_goal=0, progressive_boost='off', progressive_stats='off', character_unlocks=False,
                  lap_completion_checks=True, itemboxsanity=2, wumpasanity='both', wumpa_check='per_track',
                  item_box_count=0, wumpa_shop_slots=0, podium_rungs=[], accessibility='full')
    prefix = 'legacy-' if args.legacy else ''
    cases = [(prefix + 'vortex-final', {}), (prefix + 'station-final', dict(oxide_final_track='oxide_station'))]
    if not args.legacy:
        cases += [('vortex-chaos', dict(oxide_final_track='vortex_chaos')),
                  ('any-percent', dict(oxide_goal='any_percent')),
                  ('vortex-pad', dict(cortex_vortex_track='own_pad'))]
    rows = []
    for tag, extra in cases:
        world = make_world(dict(common, **extra), seed=3)
        mw = world.multiworld
        locations = [loc for loc in mw.get_locations(1) if loc.address is not None]
        items = [item for item in mw.itempool if item.code is not None]

        def verdict(inventory):
            state = CollectionState(mw)
            for item in inventory:
                state.collect(item, True)
            state.sweep_for_advancements([loc for loc in mw.get_locations(1) if loc.address is None])
            return dict(reachable=sorted(loc.address for loc in locations if loc.can_reach(state)),
                        goal=bool(mw.completion_condition[1](state)))

        samples = []
        for banned in (set(), {'Key'}, {'Trophy'}, {'Sapphire Relic'}, {'Key', 'Trophy'}):
            inventory = [item for item in items if item.name not in banned]
            samples.append(dict(items=[item.code for item in inventory], **verdict(inventory)))
        rows.append(dict(tag=tag, slot_data=world.fill_slot_data(), locations=sorted(loc.address for loc in locations),
                         precollected=[], foreign=[item.code for item in items], own={},
                         full=verdict(items), samples=samples))
    args.output.write_text(''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows))


if __name__ == '__main__':
    main()
