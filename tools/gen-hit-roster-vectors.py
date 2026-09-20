#!/usr/bin/env python3
"""Generate tools/fixtures/ctr_hit_roster_vectors.json (Hit Character block schema 2).

The fixture is the shared native/apworld vector set for the pool draw
("unhit_first_rotation"). Expected values come from the algorithm below, which
transcribes the frozen spec, NOT from the native implementation:
tools/test-hit-roster-fixture.cpp replays every case through the production C
draw, and the apworld replays a byte-identical copy through its own
independent reference (worlds/ctr/test/test_hit_pool_draw.py).

Algorithm (per fresh draw at one destination):
  order     the destination's seeded permutation of engine ids 0..15 (wire)
  stock(p)  7 ids counted up from 0 skipping the player (LOAD_Robots1P set)
  take(member, cursor, want)
            walk order cyclically from position cursor+1 (16 steps), collect
            up to `want` ids with member(id); new cursor = position of the last
            id collected (unchanged if none)
  S  unhit  eligible guests (8..15), not the player, Hit unchecked; at most 3
  X  other  non-stock eligible ids not the player and not in S; up to 3-|S|
  T  stock  stock(p) ids; fills the rest
  field = S + X + T, cursors [cS, cX, cT] start at 15 per destination.
Opportunity (pad): lowest engine id != player that is eligible and unchecked.

Usage: python3 tools/gen-hit-roster-vectors.py [output.json]
"""

import json
import random
import sys

MAX_GUESTS = 3
CURSOR_INIT = 15


def stock(player):
    out, nxt = [], 0
    for _ in range(7):
        if nxt == player:
            nxt += 1
        out.append(nxt)
        nxt += 1
    return out


def take(order, member, cursor, want):
    picked, pos = [], cursor
    for step in range(1, 17):
        if len(picked) >= want:
            break
        p = (cursor + step) % 16
        if member(order[p]):
            picked.append(order[p])
            pos = p
    return picked, pos


def draw(order, player, eligible, unchecked, seats, cursors):
    st = set(stock(player))
    c_s, c_x, c_t = cursors
    s, c_s = take(order, lambda i: i >= 8 and i != player and eligible[i] and unchecked[i],
                  c_s, min(MAX_GUESTS, seats))
    x, c_x = take(order, lambda i: i != player and i not in st and eligible[i] and i not in s,
                  c_x, min(MAX_GUESTS - len(s), seats - len(s)))
    t, c_t = take(order, lambda i: i in st, c_t, seats - len(s) - len(x))
    return s + x + t, [c_s, c_x, c_t]


def opportunity(player, eligible, unchecked):
    for cid in range(16):
        if cid != player and eligible[cid] and unchecked[cid]:
            return cid
    return -1


def eligible_of(guests):
    g = set(guests)
    return [1] * 8 + [1 if cid in g else 0 for cid in range(8, 16)]


def flags(ids):
    s = set(ids)
    return [1 if cid in s else 0 for cid in range(16)]


def main():
    rng = random.Random(20260914)
    orders = {
        "identity": list(range(16)),
        "reversed": list(range(15, -1, -1)),
    }
    for seed in (0, 4294967295, 2101):
        order = list(range(16))
        # The apworld's emission recipe for destination 3 (Crash Cove).
        random.Random(f"ctr-hit-order:{seed}:3").shuffle(order)
        orders[f"seed{seed}-cove"] = order

    all_ids = list(range(16))
    draw_cases = []

    def add(name, order_name, player, guests, unchecked, seats, cursors):
        order = orders[order_name]
        field, out = draw(order, player, eligible_of(guests), flags(unchecked), seats, cursors)
        st = set(stock(player))
        draw_cases.append({
            "name": name, "order": order, "player": player,
            "eligible_guests": sorted(guests), "unchecked": sorted(unchecked),
            "ai_seats": seats, "cursors_in": list(cursors),
            "field": field, "extras": [c for c in field if c not in st],
            "cursors_out": out,
        })

    init = [CURSOR_INIT] * 3
    for oname in orders:
        add(f"{oname}: no guest", oname, 0, [], all_ids, 7, init)
        add(f"{oname}: one unhit guest (G1)", oname, 0, [14], all_ids, 7, init)
        add(f"{oname}: two unhit guests (G1)", oname, 0, [13, 14], all_ids, 7, init)
        add(f"{oname}: three unhit guests (G1)", oname, 0, [12, 13, 14], all_ids, 7, init)
        for n in range(4, 9):
            guests = list(range(8, 8 + n))
            add(f"{oname}: {n} unhit guests rotate (G2)", oname, 0, guests, all_ids, 7, init)
        add(f"{oname}: player is an unhit guest", oname, 14, [12, 13, 14], all_ids, 7, init)
        add(f"{oname}: checked guests fill the other slots", oname, 0,
            [8, 9, 10, 11, 14], [c for c in all_ids if c not in (8, 9, 10, 11)], 7, init)
        add(f"{oname}: non-default player, Pura in the other slots", oname, 15,
            [14], all_ids, 7, init)
        add(f"{oname}: non-default player, Pura crowded out", oname, 15,
            [12, 13, 14], all_ids, 7, init)
        add(f"{oname}: purple cup four seats", oname, 0, [12, 13, 14], all_ids, 4, init)
        add(f"{oname}: purple cup, non-default player", oname, 9, [8, 9, 10, 11], all_ids, 4, init)
        add(f"{oname}: all checked", oname, 0, list(range(8, 16)), [], 7, init)
        for player in range(16):
            add(f"{oname}: player {player}, all guests unlocked", oname, player,
                list(range(8, 16)), all_ids, 7, init)
    # Random states and cursors.
    for i in range(120):
        oname = rng.choice(sorted(orders))
        player = rng.randrange(16)
        guests = [g for g in range(8, 16) if rng.random() < 0.5]
        unchecked = [c for c in all_ids if rng.random() < 0.6]
        seats = rng.choice((7, 7, 7, 4))
        cursors = [rng.randrange(16) for _ in range(3)]
        add(f"random {i}", oname, player, guests, unchecked, seats, cursors)

    sequence_cases = []

    def seq(name, order_name, player, seats, steps):
        order = orders[order_name]
        cursors = [CURSOR_INIT] * 3
        out_steps = []
        for guests, unchecked in steps:
            field, cursors = draw(order, player, eligible_of(guests), flags(unchecked),
                                  seats, cursors)
            out_steps.append({"eligible_guests": sorted(guests),
                              "unchecked": sorted(unchecked),
                              "field": field, "cursors_out": list(cursors)})
        sequence_cases.append({"name": name, "order": order, "player": player,
                               "ai_seats": seats, "steps": out_steps})

    for oname in orders:
        seq(f"{oname}: eight unhit guests, six fresh races", oname, 0, 7,
            [(list(range(8, 16)), all_ids)] * 6)
        seq(f"{oname}: unlock then hit", oname, 0, 7,
            [([], all_ids), ([14], all_ids), ([14], [c for c in all_ids if c != 14]),
             ([12, 14], [c for c in all_ids if c != 14])])
        # Strategy G5: hit every seated target each race.
        steps = []
        unc = set(all_ids)
        guests = list(range(8, 16))
        field_cursors = [CURSOR_INIT] * 3
        for _ in range(5):
            steps.append((guests, sorted(unc)))
            field, field_cursors = draw(orders[oname], 5, eligible_of(guests), flags(unc),
                                        7, field_cursors)
            unc -= set(field)
        seq(f"{oname}: hit everything seated, player Coco", oname, 5, 7, steps)
        seq(f"{oname}: guest player, crowded then freed", oname, 15, 7,
            [([8, 9, 10, 14], all_ids), ([8, 9, 10, 14], all_ids),
             ([8, 9, 10, 14], [c for c in all_ids if c not in (8, 9, 10)]),
             ([8, 9, 10, 14], [c for c in all_ids if c not in (8, 9, 10)])])
        seq(f"{oname}: purple cup cycle", oname, 0, 4,
            [(list(range(8, 16)), all_ids)] * 4)

    opportunity_cases = []
    for i, (player, guests, unchecked) in enumerate([
            (0, [], all_ids), (0, [], []), (0, [], [0]), (0, [], [0, 14]),
            (0, [14], [0, 14]), (14, [14], [14]), (14, [14], [0, 14]),
            (3, [], [3]), (3, [], [3, 7]), (9, [9], [9]), (9, [9, 10], [9, 10]),
            (0, [8, 9], [15]), (0, list(range(8, 16)), [15])]):
        opportunity_cases.append({
            "name": f"opportunity {i}", "player": player,
            "eligible_guests": sorted(guests), "unchecked": sorted(unchecked),
            "target": opportunity(player, eligible_of(guests), flags(unchecked))})
    for i in range(40):
        player = rng.randrange(16)
        guests = [g for g in range(8, 16) if rng.random() < 0.4]
        unchecked = [c for c in all_ids if rng.random() < 0.15]
        opportunity_cases.append({
            "name": f"random opportunity {i}", "player": player,
            "eligible_guests": guests, "unchecked": unchecked,
            "target": opportunity(player, eligible_of(guests), flags(unchecked))})

    data = {
        "schema": 2,
        "algorithm": "unhit_first_rotation",
        "max_guests": MAX_GUESTS,
        "cursor_init": CURSOR_INIT,
        "generator": "tools/gen-hit-roster-vectors.py",
        "draw_cases": draw_cases,
        "sequence_cases": sequence_cases,
        "opportunity_cases": opportunity_cases,
    }
    out = sys.argv[1] if len(sys.argv) > 1 else "tools/fixtures/ctr_hit_roster_vectors.json"
    with open(out, "w", newline="\n") as handle:
        json.dump(data, handle, indent=1, sort_keys=False)
        handle.write("\n")
    print(f"wrote {out}: {len(draw_cases)} draw, {len(sequence_cases)} sequence, "
          f"{len(opportunity_cases)} opportunity cases")


if __name__ == "__main__":
    main()
