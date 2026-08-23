#!/usr/bin/env python3
"""Validate an ap-state.json dump's token identities (#281).

ap-state.json is the offline game-state versus AP-state alignment tool, and pad
requirements can depend on a specific token colour, so the aggregate counters
alone cannot identify a wrong regular-colour mirror. This checks the dump is
valid JSON and that the seven token identities hold:

  1-5. each game_counters per-colour token equals the corresponding
       received_items count (Red, Green, Blue, Yellow, Purple CTR Token)
  6.   tokens_regular equals red + green + blue + yellow
  7.   tokens equals tokens_regular + tokens_purple

The per-colour game counters mirror received items only while the adventure
profile is live, so identities 1-5 are skipped (with a warning) when the dump
says in_adventure is 0.

When the dump carries a `transition.diag` block (diagnostics builds from the
2026-08-23 Alpha 3 bundle inspection onward) its shape is validated too: every
field present with the right type, the kart-state name matching the numeric
state, `received_keys` equal to received_items.Key, and the block's hold
summary printed so a bundle reader sees at a glance what owns the kart.
Dumps without the block are still valid; the section is then reported as absent.

Usage: check-ap-state.py <path-to-ap-state.json>
Exit 0 when every applicable identity holds, 1 otherwise.
"""

import json
import sys

COLOURS = ("red", "green", "blue", "yellow", "purple")

# Mirrors ap/ap_transition_diag.h. Keep both in step; tools/test-transition-diag.c
# pins the C side and this table pins the reader.
KART_STATE_NAMES = {
    -1: "no_driver", 0: "normal", 1: "crashing", 2: "drifting", 3: "spinning",
    4: "engine_revving", 5: "mask_grabbed", 6: "blasted", 9: "antivshift",
    10: "warp_pad", 11: "freeze",
}
INIT_FUNC_NAMES = ("no_driver", "null", "driving_init", "freeze_end_event_init", "other")
DIAG_INT_FIELDS = ("kart_state", "pause_state", "active_menu", "aku_hint_state",
                   "loading_stage", "received_keys", "profile_keys")
DIAG_PICKER_FIELDS = ("open", "pending_swap", "restore_pos")
DIAG_TRAP_FIELDS = ("armed", "warning", "active", "suspended")


def check_transition_diag(state, received, check):
    """Validate transition.diag when present; return a one-line hold summary."""
    transition = state.get("transition")
    if not isinstance(transition, dict) or "diag" not in transition:
        print("transition.diag: absent (pre-diagnostics client), nothing to check")
        return None
    diag = transition["diag"]
    print("transition.diag shape:")
    for field in DIAG_INT_FIELDS:
        check(f"diag.{field} is an int", isinstance(diag.get(field), int), True)
    check("diag.kart_state_name matches kart_state",
          diag.get("kart_state_name"),
          KART_STATE_NAMES.get(diag.get("kart_state"), "unknown"))
    check("diag.init_func is a known name", diag.get("init_func") in INIT_FUNC_NAMES, True)
    picker = diag.get("picker", {})
    for field in DIAG_PICKER_FIELDS:
        check(f"diag.picker.{field} is 0 or 1", picker.get(field) in (0, 1), True)
    traps = diag.get("traps", {})
    for field in DIAG_TRAP_FIELDS:
        check(f"diag.traps.{field} is a non-negative int",
              isinstance(traps.get(field), int) and traps.get(field) >= 0, True)
    check("diag.traps.suspended <= diag.traps.active",
          traps.get("suspended", 0) <= traps.get("active", 0), True)
    if "Key" in received:
        check("diag.received_keys == received_items.Key",
              diag.get("received_keys"), received["Key"])

    holds = []
    if transition.get("freeze_door"):
        holds.append("VEH_FREEZE_DOOR")
    if transition.get("freeze_podium"):
        holds.append("VEH_FREEZE_PODIUM")
    if diag.get("kart_state") == 11:
        holds.append("kartState=KS_FREEZE")
    if diag.get("init_func") == "freeze_end_event_init":
        holds.append("INIT=FreezeEndEvent")
    if diag.get("pause_state"):
        holds.append(f"pause_state={diag['pause_state']}")
    if diag.get("active_menu"):
        holds.append("RectMenu active")
    if diag.get("aku_hint_state"):
        holds.append(f"AkuAkuHintState={diag['aku_hint_state']}")
    if picker.get("open") or picker.get("pending_swap") or picker.get("restore_pos"):
        holds.append(f"picker busy {picker}")
    if traps.get("suspended"):
        holds.append(f"{traps['suspended']} trap(s) suspended")
    if diag.get("profile_keys") != diag.get("received_keys"):
        holds.append(f"profile_keys {diag.get('profile_keys')} != received_keys "
                     f"{diag.get('received_keys')} (unreconciled local bit)")
    summary = "; ".join(holds) if holds else "no hold recorded (kart should drive)"
    print(f"hold summary: {summary}")
    return summary


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1

    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        state = json.load(handle)  # identity 0: the dump parses as JSON at all

    counters = state["game_counters"]
    received = state["received_items"]
    failures = []

    def check(label, got, want):
        verdict = "ok" if got == want else "FAIL"
        print(f"  {verdict}: {label}: {got} vs {want}")
        if got != want:
            failures.append(label)

    print("per-colour mirrors (game_counters vs received_items):")
    if state.get("in_adventure", 1):
        for colour in COLOURS:
            check(
                f"tokens_{colour}",
                counters[f"tokens_{colour}"],
                received[f"{colour.capitalize()} CTR Token"],
            )
    else:
        print("  skipped: in_adventure is 0, the adventure profile is not live")

    print("aggregates (within game_counters):")
    regular_sum = sum(counters[f"tokens_{c}"] for c in COLOURS[:4])
    check("tokens_regular == red+green+blue+yellow",
          counters["tokens_regular"], regular_sum)
    check("tokens == tokens_regular + tokens_purple", counters["tokens"],
          counters["tokens_regular"] + counters["tokens_purple"])

    check_transition_diag(state, received, check)

    if failures:
        print(f"FAIL: {len(failures)} identity(ies) violated: {failures}")
        return 1
    print("all applicable token identities hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
