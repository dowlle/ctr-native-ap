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

Usage: check-ap-state.py <path-to-ap-state.json>
Exit 0 when every applicable identity holds, 1 otherwise.
"""

import json
import sys

COLOURS = ("red", "green", "blue", "yellow", "purple")


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

    if failures:
        print(f"FAIL: {len(failures)} identity(ies) violated: {failures}")
        return 1
    print("all applicable token identities hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
