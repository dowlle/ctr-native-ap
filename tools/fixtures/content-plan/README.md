# Content-plan integration fixtures

`baseline.json` and `starts-extras.json` are actual emitted slot data from the
Generate.main to Main.main room probes on 2026-09-22. Paired apworld base:
`56ad9999b121eddf855377cb580e1e9ded833a28` plus the content-plan implementation.
Both use all 18 retail Trophy tracks and Baby T Park 1.0.2, with identical content
and physical placement. The second changes only exact item extras and starts.

The standalone C++ harness checks structural rejection, registry ownership,
item accounting, room membership and reconnect identity. The parity script
also generates fresh layouts through the paired apworld and compares decisions.
These fixtures do not claim Steam gameplay or live-server acceptance.

`manual-rules.json` (#463) is slot data the paired apworld emitted for five
`warp_pad_rules` entries on the same profile (a concrete Trophy rule, a Gold
Relic rule, an AnyGem and an AnyRelic sum rule, and one free pad), with a small
item pool of 5 Trophies, 2 Sapphire and 2 Gold Relics, the five Gems and 4
Keys. It carries the `manual_pad_requirements_v1` required feature. All content
plan fixtures are on the schema 18 envelope. `test-content-plan.cpp` checks the
resolved per-pad rules, the evaluator answers and the refusals; the parity
script compares the production parser with the paired generator's validator on
generated rule maps and on forged records.

Open package admission (#133 PR 2): package records now carry `laps` and
`capabilities` (and `relic_targets` when `relic` is true). The two fields were
added by hand to the package record of `baseline.json`, `starts-extras.json`,
`manual-rules.json` and the two `named-blocks/content-plan-*.json` fixtures,
with Baby T Park's measured values (laps 7; time_trial, arcade, ctr_tokens and
wumpa true; relic false). Nothing else in them changed.

`open-packages.json` is slot data the paired apworld emitted
(`worlds/ctr/test/test_custom_packages.py` helpers, seed 20261016) for two
packages outside any compiled registry: test data on the `tv-world` key (named
block 78, laps 5) and the made-up `fixture-loop` key (generic slot 1, path B+,
laps 3, relic targets). Its pins name no real files. `test-content-plan.cpp`
checks the resolved pads and refuses forged descriptors; the parity script
compares both sides on the same forgeries.

Track identity (#133 PR 2 review): native now holds the apworld's named-block
table (`named_blocks` in `ap/ap_content_registry.hpp`) and requires a custom
track to use exactly the block its track key owns. Baby T Park owns block 8,
so `baseline.json`, `starts-extras.json` and `manual-rules.json` were edited by
hand to put it there: the track record gained `named_block: 8` and
`room_title: "Baby T Park"`, and its Trophy check moved from the generic slot
code `35016300` to `35201600`. Nothing else in them changed. The paired
validator accepts all three again.

Track modes (#133 PR 3): `track-modes.json` is slot data the paired apworld
emitted (`worlds/ctr/test/test_track_modes.py` helpers, seed 20261016) with
`modes: [trophy, ctr, relic]`, two stages and a per-pad mode order. It holds
the 18 retail tracks (all three modes, frozen Trophy, CTR Token Challenge and
Time Trial codes) and three test packages whose pins name no real files:
`tv-world` on named block 78 with CTR and Relic claimed (block offsets 0, 9
and 6 to 8), the B+ `fixture-loop` with Trophy and Relic (generic slot 1), and
the Relic-only `relic-loop` (no arcade, generic slot 2). Every pad with two or
more modes has a `stage2:<physical>` record, and the plan declares
`track_modes_v1`. `test-content-plan.cpp` checks the resolved modes, stages,
codes and gate answers, refuses forged gates, records, modes and awards, and
admits the same plan rewritten to one stage. The earlier fixtures are Trophy
only and unchanged.
