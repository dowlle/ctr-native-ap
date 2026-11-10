#!/usr/bin/env python3
"""Export AP-rule verdicts for the native sanity completability regressions.

Run with --apworld pointing at the paired source checkout. The deliberately
blocked placements put the final Gem on a pickup behind a missing capability.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apworld', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('tools/fixtures/content-plan/sanity-verify.jsonl'))
    args = parser.parse_args()
    sys.path.insert(0, str(args.apworld.resolve()))
    from BaseClasses import CollectionState
    from worlds.ctr import sanity
    from worlds.ctr.item_boxes import TIGER_TEMPLE_DOOR_OPENERS
    from worlds.ctr.test.test_content_plan import make_world, profile_options, profile_pool

    common = dict(itemboxsanity=5, wumpasanity='both', item_box_count=0,
                  progressive_boost='shared_global', progressive_stats='off', character_unlocks=False,
                  wumpa_shop_slots=0, shuffle_keys=True, shuffle_gems=True,
                  warppad_unlock_requirements='vanilla')
    pool = profile_pool()
    pool['base']['relics']['sapphire'] = 2
    cup = None
    for seed in range(30):
        world = make_world(dict(common, retail_tracks=['Hot Air Skyway', 'Dingo Canyon'], custom_tracks=[],
                                track_modes={'default': ['relic']}, cup_count=1, cup_tracks='random',
                                crystal_challenge_count=0, boss_garages=False, oxide_goal='disabled',
                                bosses_required_goal=0, gems_required_goal=5, item_pool=pool),
                           seed=seed, stop_before='generate_basic')
        plan = world.ctr_content_plan
        entry = next(e for e in plan['entries'] if e['kind'] == 'cup')
        if entry['legs'][0]['track_id'] == 'retail:7' and any(leg['track_id'] == 'retail:0' for leg in entry['legs'][1:]):
            cup = world
            break
    assert cup is not None
    door = make_world(dict(profile_options(), **common, itemsanity=True), seed=3, stop_before='generate_basic')
    awards = ['Trophy', 'Key', 'Sapphire Relic', 'Gold Relic', 'Platinum Relic',
              'Red CTR Token', 'Green CTR Token', 'Blue CTR Token', 'Yellow CTR Token', 'Purple CTR Token',
              'Red Gem', 'Green Gem', 'Blue Gem', 'Yellow Gem']
    rich = [name for name in awards for _ in range(4 if name == 'Key' else 1 if name.endswith('Gem') else 18)]
    weapon = sorted(TIGER_TEMPLE_DOOR_OPENERS)[0]
    rows = []
    revision = subprocess.check_output(['git', '-C', str(args.apworld), 'rev-parse', 'HEAD'], text=True).strip()
    for tag, world in [('sanity-cup-prefix', cup), ('sanity-tiger-door', door)]:
        mw, player, plan = world.multiworld, world.player, world.ctr_content_plan
        checks = [c for c in plan['checks'] if c['kind'] in sanity.KINDS]
        target = (next(c['location'] for c in checks if c['owner'] == 'retail:0' and c['kind'] == 'weapon_crate')
                  if tag == 'sanity-cup-prefix' else min(sanity.door_codes(plan)))
        locations = [loc for loc in mw.get_locations(player) if loc.address is not None]
        by_code = {loc.address: loc for loc in locations}
        by_code[target].place_locked_item(world.create_item('Purple Gem'))

        def verdict(names, full=False):
            state = CollectionState(mw)
            for item in mw.precollected_items[player]:
                state.collect(item, True)
            for name in names:
                state.collect(world.create_item(name), True)
            state.sweep_for_advancements(locations=None if full else [loc for loc in mw.get_locations(player) if loc.address is None])
            return dict(reachable=sorted(loc.address for loc in locations if loc.can_reach(state)),
                        goal=bool(mw.completion_condition[player](state)))

        blocked = rich + (['Progressive Boost'] * 2 if tag == 'sanity-tiger-door' else [])
        allowed = rich + ['Progressive Boost'] * 2 + ([weapon] if tag == 'sanity-tiger-door' else [])
        full = verdict(blocked, full=True)
        positive = verdict(allowed, full=True)
        assert target not in full['reachable'] and not full['goal'], tag
        assert target in positive['reachable'] and positive['goal'], tag
        inventories = [[], rich, rich + ['Progressive Boost'], rich + ['Progressive Boost'] * 2, allowed]
        samples = [dict(items=[world.item_name_to_id[name] for name in names], **verdict(names)) for names in inventories]
        row = dict(tag=tag, provenance=dict(generator='tools/build-sanity-verify-fixture.py', apworld_commit=revision),
                   slot_data=world.fill_slot_data(), locations=[loc.address for loc in locations],
                   own={str(loc.address): loc.item.code for loc in locations if loc.item is not None},
                   foreign=[world.item_name_to_id[name] for name in blocked],
                   precollected=[item.code for item in mw.precollected_items[player] if item.code is not None],
                   full=full, samples=samples)
        rows.append(row)
    text = ''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows)
    assert '/home/' not in text
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    print('Wrote', len(rows), 'AP verdict rows with blocked goals and positive capability controls')


if __name__ == '__main__':
    main()
