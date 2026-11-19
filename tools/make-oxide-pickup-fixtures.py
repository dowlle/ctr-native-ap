#!/usr/bin/env python3
"""Build synthetic Oxide pickup admission and reachability parity fixtures.

Run once against the paired world and once against pre-feature main with
--legacy. The output contains generated plans, never a played room.
"""
import argparse
import json
from pathlib import Path
import sys
from unittest.mock import patch


MIXED_PREFIXES = ('tizi-before-oxide', 'royal-before-oxide', 'green-before-oxide')


def mixed_prefix(tag, common):
    from copy import deepcopy
    from worlds.ctr import content_plan
    from worlds.ctr.test.test_content_plan import make_world
    from worlds.ctr.test.test_green_hill_raceway import PACKAGE as GREEN
    from worlds.ctr.test.test_royal_raceway import PACKAGE as ROYAL
    from worlds.ctr.test.test_tizi_madness import tizi_descriptor

    opts = dict(common, retail_tracks=common['retail_tracks'] + ['Oxide Station'],
                track_modes={'default': ['trophy'], 'Oxide Station': ['relic']},
                cup_count=1, cup_tracks='random_repeats', shortcut_knowledge=2)
    if tag == 'tizi-before-oxide':
        package = tizi_descriptor()
        package['capabilities'].update(ai_nav=True, checkpoints=35)
        opts['itemsanity'] = True
    else:
        is_royal = tag == 'royal-before-oxide'
        package = deepcopy(ROYAL if is_royal else GREEN)
        # Exercise the overrides against the opposite descriptor value.
        package['capabilities']['super_turbo_pads'] = not is_royal
        opts.update(progressive_boost='shared_global', logic_difficulty='hard')
    opts['custom_tracks'] = [package]

    draw = content_plan.content_cups.draw_cups

    def draw_cup(*args, **kwargs):
        cups = draw(*args, **kwargs)
        tracks = {track['display_name']: track for pool in args[2:4] for track in pool}
        cups[0]['legs'] = [tracks[name] for name in (package['display_name'], 'Oxide Station', 'Dingo Canyon', 'Tiny Arena')]
        return cups

    with patch.object(content_plan.content_cups, 'draw_cups', draw_cup):
        world = make_world(opts, seed=3, stop_before='pre_fill')
    assert content_plan.validate(world.ctr_content_plan)
    return world


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
        cases += [(tag, None) for tag in MIXED_PREFIXES]
    rows = []
    for tag, extra in cases:
        if tag in MIXED_PREFIXES:
            world = mixed_prefix(tag, common)
        elif tag == 'yellow-leg-four':
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
        focus = {}
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
        if tag in MIXED_PREFIXES:
            from worlds.ctr.test.test_oxide_pickups import oxide_checks
            # These probes isolate the new route from the existing Tizi Cup
            # and Itemsanity checks, which have separate rules.
            targets = {c['location'] for c in oxide_checks(world.ctr_content_plan)}
            assert targets and targets <= set(verdict(items)['reachable'])
            missing = 'Mask' if tag == 'tizi-before-oxide' else 'Progressive Boost'
            removed = [item for item in items if item.name != missing]
            assert len(removed) < len(items)
            result = verdict(removed)
            reached = targets & set(result['reachable'])
            assert reached if tag == 'green-before-oxide' else not reached
            focus = dict(focus=sorted(targets))
            samples = [dict(items=[item.code for item in removed], **result)]
        rows.append(dict(tag=tag, slot_data=world.fill_slot_data(), locations=sorted(loc.address for loc in locations),
                         precollected=[], foreign=[item.code for item in full_inventory], own=own,
                         full=verdict(full_inventory), samples=samples, **focus))
    args.output.write_text(''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows))


if __name__ == '__main__':
    main()
