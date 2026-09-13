#!/usr/bin/env python3
"""Generate tools/fixtures/ctr_hit_roster_vectors.json from the FROZEN contract.

The fixture is the shared native/apworld roster vector set: for each case it
records the seed, level, checked set, player id and the expected seven-AI
roster, plus the resolved candidate lists. Expected values are derived from the
contract algorithm in
"Encounter implementation contract - 2026-09-12.md" (Deterministic policy +
Pins), NOT from the native implementation, so the manager can run the same file
against the apworld.

Algorithm (contract):
  base    = [0..7]  left-rotated by (seed + L) % 8
  reserve = [8..15] left-rotated by (seed + L) % 8
  guest   = first eligible pinned (not the player), else first eligible reserve
            (not the player); a guest is eligible when one of its any-of win
            codes is checked (defaults 0..7 are always eligible)
  field   = [guest if any] + non-player unique base ids in wire order, to 7

Usage: python3 tools/gen-hit-roster-vectors.py [output.json]
"""

import json
import sys

FIELD_SIZE = 7

# Approved ordinary pins: destination level -> guest engine id.
PINS = {
    3: 14, 8: 14,      # Fake Crash
    2: 13, 12: 13,     # Penta Penguin
    16: 12, 17: 12,    # N. Tropy
    6: 10,             # Ripper Roo
    5: 9,              # Papu Papu
    1: 11,             # Komodo Joe
    7: 8,              # Pinstripe
    13: 15,            # Nitros Oxide
}

# Guest engine id -> authoritative any-of win codes (contract pin table).
TRIGGERS = {
    14: [35011000, 35011003],
    13: [35011008, 35011010],
    12: [35016200, 35016201],
    10: [35011100],
    9: [35011101],
    11: [35011102],
    8: [35011103],
    15: [35011104, 35011105],
}

# kind is informational for the native parser; both kinds use any-of checked.
TRIGGER_KIND = {8: "boss", 9: "boss", 10: "boss", 11: "boss",
                12: "track", 13: "track", 14: "track", 15: "boss"}

LOCATIONS = {i: 35025000 + i for i in range(16)}

# Flattened any-of win codes, for "all guests eligible" cases.
ALL_TRIGGER_CODES = sorted({c for cs in TRIGGERS.values() for c in cs})

SEED_0 = 0
SEED_MAX = 4294967295


def rotate(lst, k):
    k %= len(lst)
    return lst[k:] + lst[:k]


def base_for(seed, level):
    return rotate(list(range(0, 8)), (seed + level) % 8)


def reserve_for(seed, level):
    return rotate(list(range(8, 16)), (seed + level) % 8)


def pinned_for(level):
    return [PINS[level]] if level in PINS else []


def eligible(guest, checked):
    if guest < 8:
        return True
    return any(code in checked for code in TRIGGERS[guest])


def select(seed, level, checked, player):
    base = base_for(seed, level)
    reserve = reserve_for(seed, level)
    pinned = pinned_for(level)

    guest = -1
    for g in pinned:
        if g != player and eligible(g, checked):
            guest = g
            break
    if guest < 0:
        for g in reserve:
            if g != player and eligible(g, checked):
                guest = g
                break

    out = []
    if guest >= 0:
        out.append(guest)
    for b in base:
        if len(out) >= FIELD_SIZE:
            break
        if b == player or b == guest or b in out:
            continue
        out.append(b)
    return out, guest


def case(name, seed, level, checked, player):
    roster, guest = select(seed, level, checked, player)
    return {
        "name": name,
        "seed": seed,
        "level": level,
        "player": player,
        "checked": sorted(checked),
        "base": base_for(seed, level),
        "pinned": pinned_for(level),
        "reserve": reserve_for(seed, level),
        "expected": roster,
        "expected_guest": guest,
        "field_size": FIELD_SIZE,
    }


# ── Gem Cups (ticket 11) ────────────────────────────────────────────────────
# The cups block keys are 100..104. Cups have no pins: the first eligible
# reserve is the guest, then non-player base ids fill to the field size (four AI
# in the retail Purple cup 104, seven otherwise). "Cups use the corresponding
# cup ID in place of L" for the deterministic rotation.

def cup_field_size(cup_id):
    return 4 if cup_id == 104 else FIELD_SIZE


def cup_select(seed, cup_id, checked, player):
    base = base_for(seed, cup_id)
    reserve = reserve_for(seed, cup_id)
    size = cup_field_size(cup_id)

    guest = -1
    for g in reserve:
        if g != player and eligible(g, checked):
            guest = g
            break

    out = []
    if guest >= 0:
        out.append(guest)
    for b in base:
        if len(out) >= size:
            break
        if b == player or b == guest or b in out:
            continue
        out.append(b)
    return out, guest


def cup_case(name, seed, cup_id, checked, player):
    roster, guest = cup_select(seed, cup_id, checked, player)
    return {
        "name": name,
        "seed": seed,
        "level": cup_id,
        "player": player,
        "checked": sorted(checked),
        "base": base_for(seed, cup_id),
        "pinned": [],
        "reserve": reserve_for(seed, cup_id),
        "expected": roster,
        "expected_guest": guest,
        "field_size": cup_field_size(cup_id),
    }


def build_cases():
    cases = []

    # No guest: nothing checked -> base fill on every ordinary destination.
    for level in range(0, 18):
        cases.append(case(f"no_guest_seed0_L{level}", SEED_0, level, [], 0))

    # Seed boundary: seed 0 and the max uint32, no guest.
    for seed in (SEED_0, SEED_MAX):
        for level in (0, 3, 7, 8, 16, 17):
            cases.append(case(f"seed{seed}_no_guest_L{level}", seed, level, [], 0))

    # All sixteen player choices at a pinned destination, guest eligible.
    for seed in (SEED_0, SEED_MAX):
        for player in range(0, 16):
            cases.append(case(f"all_players_seed{seed}_L3_player{player}",
                              seed, 3, [35011000], player))

    # Pin priority: the pinned guest wins over an eligible reserve guest.
    cases.append(case("pin_priority_L3", SEED_0, 3, [35011000, 35011100], 0))
    # Reserve guest on an unpinned level.
    cases.append(case("reserve_guest_L0", SEED_0, 0, [35011100], 0))
    # Reserve rotation: all guests eligible, each unpinned level picks its first
    # eligible reserve.
    all_guests = ALL_TRIGGER_CODES
    for level in (0, 1, 4, 7, 9, 10, 11, 14, 15):
        cases.append(case(f"reserve_rotation_L{level}", SEED_0, level, all_guests, 0))

    # Player is the pinned guest -> no guest seat, base fill.
    cases.append(case("player_is_pinned_guest_L3_p14", SEED_0, 3, [35011000], 14))
    # Player is a reserve guest -> the next eligible reserve is seated.
    cases.append(case("player_is_reserve_guest_L0_p10", SEED_0, 0,
                      [35011100, 35011103], 10))

    # All six required pinned opportunities.
    six = [(3, 35011000), (8, 35011003), (2, 35011008), (12, 35011010),
           (16, 35016200), (17, 35016201)]
    for level, trigger in six:
        cases.append(case(f"required_pin_L{level}", SEED_0, level, [trigger], 0))
        cases.append(case(f"required_pin_L{level}_p{PINS[level]}", SEED_0, level,
                          [trigger], PINS[level]))

    # Growing checked sets on Crash Cove.
    for n, checked in enumerate(([], [35011000], [35011000, 35011008],
                                 [35011000, 35011008, 35011100],
                                 ALL_TRIGGER_CODES)):
        cases.append(case(f"growing_L3_step{n}", SEED_0, 3, checked, 0))

    # Every guest's own trigger, on its pinned level.
    for guest in sorted(TRIGGERS):
        level = next(l for l, g in sorted(PINS.items()) if g == guest)
        cases.append(case(f"guest{guest}_on_L{level}", SEED_0, level,
                          [TRIGGERS[guest][0]], 0))

    # Gem Cups: no pins, reserve guest then base fill, field size four in the
    # retail Purple cup (104) and seven otherwise.
    for seed in (SEED_0, SEED_MAX):
        for cup_id in (100, 101, 102, 103, 104):
            cases.append(cup_case(f"cup{cup_id}_seed{seed}_no_guest", seed, cup_id,
                                  [], 0))
        for player in range(0, 16):
            cases.append(cup_case(f"cup104_seed{seed}_p{player}", seed, 104,
                                  [35011000], player))
        cases.append(cup_case(f"cup100_seed{seed}_reserve", seed, 100,
                              [35011100], 0))
    cases.append(cup_case("cup104_reserve_guest", SEED_0, 104, [35011103], 0))
    cases.append(cup_case("cup100_player_is_reserve_guest", SEED_0, 100,
                          [35011100, 35011103], 10))

    return cases


def build_cup_sessions():
    """New/same-cup lifecycle boundaries (ticket 11). Each step is an event the
    native snapshot consumes: `begin` marks a NEW cup (pad entry), `load` is a
    cup race load with a track index. The expected roster is what the snapshot
    must hold after that step, derived from the contract selection for the
    eligibility current at the cup's begin."""
    sessions = []

    # One cup, three legs, a mid-cup unlock, a retry, then a NEW cup of the same
    # id resolves fresh.
    seed = SEED_0
    cup_id = 100
    sessions.append({
        "name": "cup100_continue_retry_new",
        "seed": seed,
        "cup": cup_id,
        "player": 0,
        "steps": [
            {"event": "begin", "checked": []},
            {"event": "load", "trackIndex": 0,
             "expected": cup_select(seed, cup_id, [], 0)[0]},
            {"event": "load", "trackIndex": 1,
             "expected": cup_select(seed, cup_id, [], 0)[0]},
            # Unlock mid-cup: the active cup keeps its roster.
            {"event": "unlock", "checked": [35011000]},
            {"event": "load", "trackIndex": 2,
             "expected": cup_select(seed, cup_id, [], 0)[0]},
            {"event": "load", "trackIndex": 3,
             "expected": cup_select(seed, cup_id, [], 0)[0]},
            # Retry the same leg: unchanged.
            {"event": "load", "trackIndex": 3,
             "expected": cup_select(seed, cup_id, [], 0)[0]},
            # Exit and re-enter the same cup: a NEW cup resolves fresh, now with
            # the unlock applied.
            {"event": "begin", "checked": [35011000]},
            {"event": "load", "trackIndex": 0,
             "expected": cup_select(seed, cup_id, [35011000], 0)[0]},
        ],
    })

    # Purple cup: four seats, fresh resolve after a new begin.
    sessions.append({
        "name": "cup104_four_seats",
        "seed": SEED_0,
        "cup": 104,
        "player": 0,
        "steps": [
            {"event": "begin", "checked": []},
            {"event": "load", "trackIndex": 0,
             "expected": cup_select(SEED_0, 104, [], 0)[0]},
            {"event": "begin", "checked": ALL_TRIGGER_CODES},
            {"event": "load", "trackIndex": 0,
             "expected": cup_select(SEED_0, 104, ALL_TRIGGER_CODES, 0)[0]},
        ],
    })

    return sessions


def build_invariants():
    inv = []
    specs = [
        ("inv_seed0_p0_none", SEED_0, 0, []),
        ("inv_seed0_p0_fc", SEED_0, 0, [35011000]),
        ("inv_seed0_p0_all", SEED_0, 0, ALL_TRIGGER_CODES),
        ("inv_seedmax_p0_all", SEED_MAX, 0, ALL_TRIGGER_CODES),
        ("inv_seed0_p14_fc", SEED_0, 14, [35011000]),
        ("inv_seedmax_p15_all", SEED_MAX, 15, ALL_TRIGGER_CODES),
    ]
    for name, seed, player, checked in specs:
        levels = {}
        must = []
        for level in range(0, 18):
            roster, _ = select(seed, level, checked, player)
            levels[str(level)] = {
                "base": base_for(seed, level),
                "pinned": pinned_for(level),
                "reserve": reserve_for(seed, level),
            }
            for id_ in roster:
                if id_ != player and id_ not in must:
                    must.append(id_)
        # The invariant is that every eligible non-player id appears somewhere;
        # record the eligible set explicitly so the test asserts a superset.
        eligible_ids = [i for i in range(16)
                        if i != player and eligible(i, checked)]
        inv.append({
            "name": name,
            "seed": seed,
            "player": player,
            "checked": sorted(checked),
            "eligible": eligible_ids,
            "levels": levels,
        })
    return inv


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else \
        "tools/fixtures/ctr_hit_roster_vectors.json"
    fixture = {
        "description": "Shared native/apworld Hit Character roster vectors. "
                       "Expected rosters are derived from the frozen contract "
                       "algorithm, not from either implementation.",
        "field_size": FIELD_SIZE,
        "algorithm": {
            "base": "rotate([0..7], (seed + level) % 8)",
            "reserve": "rotate([8..15], (seed + level) % 8)",
            "guest": "first eligible pinned (not player), else first eligible reserve",
            "fill": "non-player unique base ids in wire order until 7",
        },
        "pins": {str(k): v for k, v in sorted(PINS.items())},
        "triggers": {str(k): {"kind": TRIGGER_KIND[k], "any_of": v}
                     for k, v in sorted(TRIGGERS.items())},
        "locations": {str(k): v for k, v in sorted(LOCATIONS.items())},
        "cases": build_cases(),
        "invariants": build_invariants(),
        "cup_sessions": build_cup_sessions(),
    }
    with open(out_path, "w") as f:
        json.dump(fixture, f, indent=1)
        f.write("\n")
    print(f"wrote {out_path}: {len(fixture['cases'])} cases, "
          f"{len(fixture['invariants'])} invariant cases, "
          f"{len(fixture['cup_sessions'])} cup sessions")


if __name__ == "__main__":
    main()
