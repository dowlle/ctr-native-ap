# Verifier sweep profiles

Player YAMLs for the pre-release seed-verifier sweep. This sweep is separate
from the apworld fuzz gate: the fuzzer checks that generation succeeds, while
this sweep checks that the client's own connect-time verifier (`ap/ap_verify.c`,
shipped in v0.1.2) reaches the RIGHT verdict on seeds generated from
potentially problematic option combinations.

## How to run the sweep

For each YAML in this folder:

1. Generate a solo seed with the release `ctr.apworld`
   (`accessibility: full` is set in the profiles themselves).
2. Host the seed locally and connect the release client.
3. Record the verifier verdict from the AP log and the hub banner.

A verdict mismatch is a release blocker for the verifier, in either direction:

- A "seed not completable" warning on a seed that generation (or play)
  proves beatable is a FALSE POSITIVE. It teaches players to ignore the
  banner, which defeats the reason the verifier exists.
- No warning on a seed known to be unbeatable is a FALSE NEGATIVE
  (the case the verifier was built for, see #80).

## Profiles

The v0.1.2 pre-release sweep used six profiles: battle arenas off, both
extremes of two-stage density, and each warp pad unlock mode. Those six are
not yet committed here; add them when they are next regenerated so the sweep
is reproducible from the repo alone.

### profile-community-01.yaml

From a community report by Bethany on Discord, 2026-07-21, the morning after
the v0.1.2 release: the client declared her seed not completable after her
first race, and she reports she went on to complete the goal (she beat
N. Oxide's Final Challenge). That makes this option combination a suspected
verifier false positive; the cause is not yet established. Options are hers
verbatim, only the slot name and description are replaced.

Notable combination points not covered by the original six profiles:
`requirement_variety: custom` with a Key weight of 0, an 18-entry
`exclude_locations` list, `oxide_final_challenge_unlock: any_relic_type`,
and merged warp pad shuffle across crystals, tracks and cups together with
`two_stage_density: full` and every podium rung option enabled.

The originally reported seed was Archipelago 0.6.7 seed 7198354776817670438
(181 locations). Regenerating with that seed number and this YAML reproduces
the reported placement exactly.

Keep this profile in the sweep permanently, including after the false
positive is fixed, so it cannot regress silently.
