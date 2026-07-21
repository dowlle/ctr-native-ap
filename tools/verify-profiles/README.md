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

Thirteen profiles, promoted after the v0.1.3 pre-release sweep so the whole
sweep is reproducible from the repo alone. Each YAML carries its own
`description` explaining what the combination stresses. The roster:

- `profile-community-01` -- Bethany's verbatim v0.1.2 report config (see below).
- `profile-candidate-02..06, -08` -- the de-confounding variations staged for
  the v0.1.3 sweep (structural merged shuffle vs custom weights, goal modes,
  density extremes, shuffle scopes). Candidate 07 was retired: its
  `Trophy:0 + Key:0` weights are now correctly rejected at generation by the
  #87 guard, which is itself proof the guard works. Its intent lives on as
  `profile-gap-07b` (`Trophy:1`, the legal minimum).
- `profile-gap-*` -- coverage gaps found while sweeping: unshuffled vanilla
  mode (`vanilla`), the #50 pinning path (`pinning`), `accessibility: minimal`
  (`minimal`), the gold/platinum oxide-final modes (`goldmode`, `platmode`),
  and the reworked candidate 07 (`07b`).

This roster supersedes the old note about the six uncommitted v0.1.2-era
profiles; their axes (arena toggles, density extremes, unlock modes) are
covered by the files above.

Sweep results, 2026-07-21 (v0.1.3 release binary): all thirteen profiles
verify `seed OK ... (solo: definitive)` in game. Four of them (candidate-03,
candidate-05, gap-platmode, gap-goldmode) were FALSE POSITIVES before the
#107 fix (the verifier did not model the gem-cup-leg path to podium rungs)
and are the permanent regression profiles for it. The false-negative
direction is guarded separately: a genuinely unbeatable pre-#80 vanilla-mode
seed still gets GOAL BLOCKED on the same binary (fixture archived with the
project's release evidence, PR #108 has the story).

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
