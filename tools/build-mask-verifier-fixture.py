#!/usr/bin/env python3
"""Export native verifier regression rooms from the paired apworld."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys
from unittest.mock import patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apworld', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('tools/fixtures/content-plan/mask-verifier.jsonl'))
    args = parser.parse_args()
    sys.path.insert(0, str(args.apworld.resolve()))
    from BaseClasses import CollectionState
    from worlds.ctr import content_bosses, content_pickups, tizi_madness
    from worlds.ctr.test.test_content_bosses import boss_options
    from worlds.ctr.test.test_content_plan import make_world
    from worlds.ctr.test.test_royal_raceway import PACKAGE as ROYAL, TRACK as ROYAL_ID
    from worlds.ctr.test.test_tizi_madness import TIZI_ID, tizi_descriptor

    def row(tag, world, boosts=0, *, accessible=(), blocked=(), own=None):
        mw = world.multiworld
        inv = [item for item in mw.itempool if item.code is not None and item.name not in ('Mask', 'Progressive Boost')]
        inv.extend(world.create_item('Progressive Boost') for _ in range(boosts))
        state = CollectionState(mw)
        for item in inv:
            state.collect(item, True)
        state.sweep_for_advancements([loc for loc in mw.get_locations(1) if loc.address is None])
        locations = [loc for loc in mw.get_locations(1) if loc.address is not None]
        reached = {loc.address for loc in locations if loc.can_reach(state)}
        assert set(accessible) <= reached and not (set(blocked) & reached)
        return dict(tag=tag, slot_data=world.fill_slot_data(), locations=sorted(loc.address for loc in locations),
                    precollected=[], foreign=[item.code for item in inv], own=own or {}, samples=[],
                    full=dict(reachable=sorted(reached), goal=bool(mw.completion_condition[1](state))))

    tizi = tizi_descriptor()
    tizi['capabilities'].update(ai_nav=True, checkpoints=35, super_turbo_pads=False)
    opts = boss_options(oxide_goal='disabled', bosses_required_goal=4, itemboxsanity=5,
                        progressive_boost='off', itemsanity=True, shortcut_knowledge=2)
    opts['content_pool']['tracks']['custom'] = [tizi]
    opts['content_pool']['required_entries'] = [TIZI_ID]
    world = make_world(opts, seed=3, stop_before='pre_fill')
    assert not content_pickups.enabled(world.ctr_content_plan)
    trophy = next(check['location'] for check in world.ctr_content_plan['checks']
                  if check['kind'] == 'trophy' and check['owner'] == TIZI_ID)
    rows = [row('tizi-oxide-disabled-mask-self-lock', world, blocked=(trophy,),
                own={str(trophy): world.create_item('Mask').code})]
    # Model the logic exported before the Mask ruling on the same hard room.
    with patch.object(tizi_madness, 'tizi_track_ids', return_value=set()):
        legacy = make_world(opts, seed=3, stop_before='pre_fill')
        rows.append(row('tizi-legacy-without-carried-mask-gate', legacy, accessible=(trophy,)))

    royal = deepcopy(ROYAL)
    royal['capabilities'].update(ai_nav=True, checkpoints=35, super_turbo_pads=False)
    opts = boss_options(oxide_goal='101_percent', boss_track_shuffle=True, itemboxsanity=5,
                        wumpa_check='per_track', progressive_boost='shared_global', itemsanity=True,
                        hit_character=True, shortcut_knowledge=2, logic_difficulty='hard')
    opts['content_pool']['tracks']['custom'] = [royal, tizi]
    opts['content_pool']['bosses']['cups'] = 1
    opts['content_pool']['required_entries'] = [ROYAL_ID, TIZI_ID]
    opts['item_pool']['base']['trophies'] = 12
    draw = content_bosses.draw_cups

    def draw_cup(world, record, custom=()):
        cups = draw(world, record, custom)
        for garage, legs in cups.items():
            first, last = [track for track in legs if track not in (ROYAL_ID, TIZI_ID)][:2]
            cups[garage] = [first, ROYAL_ID, TIZI_ID, last]
        return cups

    with patch.object(content_bosses, 'draw_cups', draw_cup):
        world = make_world(opts, seed=3, stop_before='pre_fill')
    plan = world.ctr_content_plan
    boss = next(entry for entry in plan['entries'] if entry['kind'] == 'boss_cup')
    finish = next(enc['completion_location'] for enc in plan['encounters'] if enc['entry_id'] == boss['id'])
    hit = next(check['location'] for check in plan['checks'] if check['kind'] == 'hit'
               and any(route['entry_id'] == boss['id'] for route in check['routes']))
    rows.append(row('royal-tizi-boss-hit-before-win', world, boosts=1, accessible=(hit,), blocked=(finish,)))

    args.output.write_text(''.join(json.dumps(r, separators=(',', ':')) + '\n' for r in rows))


if __name__ == '__main__':
    main()
