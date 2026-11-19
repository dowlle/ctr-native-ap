#!/usr/bin/env python3
"""Build synthetic Oxide pickup admission and reachability parity fixtures.

Run once against the paired world and once against pre-feature main with
--legacy. The output contains generated plans, never a played room.
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
                  cup_count=0, crystal_challenge_count=0, boss_garages=True,
                  oxide_goal='101_percent', bosses_required_goal=0, gems_required_goal=0,
                  progressive_boost='off', progressive_stats='off', character_unlocks=False,
                  itemboxsanity=2, wumpasanity='both', wumpa_check='per_track',
                  item_box_count=0, wumpa_shop_slots=0, podium_rungs=[], accessibility='full')
    cases = [('legacy-first-only' if args.legacy else 'first-only', {})]
    if not args.legacy:
        cases += [('final-only', dict(oxide_final_track='oxide_station', oxide_1_optional='optional')),
                  ('own-pad', dict(retail_tracks=common['retail_tracks'] + ['Oxide Station'])),
                  ('cup-only', dict(retail_tracks=common['retail_tracks'] + ['Oxide Station'],
                                    track_modes={'default': ['relic']}, cup_count=1, cup_tracks='random_no_repeats')),
                  ('yellow-leg-four', None)]
    rows = []
    for tag, extra in cases:
        if tag == 'yellow-leg-four':
            from worlds.ctr.test.test_oxide_pickups import yellow_fixture, oxide_checks
            from worlds.ctr import content_plan
            world = yellow_fixture()
            assert content_plan.validate(world.ctr_content_plan)
            pad = next(p for p in world.ctr_content_plan['pads'] if p['entry_id'] == 'entry/gem_cup:3')
            assert pad['id'] == 'pad:11' and pad['entrance_req'] == 'keys:3'
        else:
            world = make_world(dict(common, **extra), seed=1)
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
        own = {}
        full_inventory = items
        if tag == 'yellow-leg-four':
            key, trophy = world.create_item('Key'), world.create_item('Trophy')
            rest = [item for item in items if item.name not in ('Key', 'Trophy')]
            targets = {c['location'] for c in oxide_checks(world.ctr_content_plan)}
            samples = []
            for keys, trophies in ((2, 4), (3, 3), (4, 3), (3, 4), (4, 4)):
                inventory = rest + [key] * keys + [trophy] * trophies
                result = verdict(inventory)
                assert targets <= set(result['reachable']) if keys >= 3 and trophies >= 4 else not targets & set(result['reachable'])
                samples.append(dict(items=[item.code for item in inventory], **result))
            target = min(targets)
            own[str(target)] = trophy.code
            full_inventory = rest + [key] * 4 + [trophy] * 3
            assert target not in verdict(full_inventory)['reachable']
        rows.append(dict(tag=tag, slot_data=world.fill_slot_data(), locations=sorted(loc.address for loc in locations),
                         precollected=[], foreign=[item.code for item in full_inventory], own=own,
                         full=verdict(full_inventory), samples=samples))
    args.output.write_text(''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows))


if __name__ == '__main__':
    main()
