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

Per-track check families (#133 PR 3b): `track-checks.json` and
`track-checks-global.json` are slot data the paired apworld emitted
(`worlds/ctr/test/test_track_checks.py` helpers, seeds 20261017 and 20261018,
script `emit-track-checks-fixtures.py` in the PR 3b artifacts) with all three
modes, two stages and a per-pad order, on three retail tracks (Crash Cove,
Roo's Tubes, Slide Coliseum, small enough for the field mutations), Baby T Park
1.0.2 on named block 8 (generic slot 1) and the B+ `fixture-loop` test package
with letters and Wumpa claimed (generic slot 2, pins name no real files). The
first has four podium rungs, per-track Reach 10 Wumpa and Lettersanity mode 2
with two letters per CTR Challenge; the second has all five rungs, global
Wumpa and Lettersanity mode 3. Both carry a `track_checks` record and the
matching `podium_rungs_v1`, `wumpa_checks_v1` and `lettersanity_v1` features.
`test-content-plan.cpp` checks the resolved rung, Wumpa and letter codes and
the pooled letter items per pad, runs the field mutations on the first, and
refuses forged settings, routes, letter draws and letter item rows. The
earlier fixtures have no `track_checks` record and are unchanged.

Cortex Vortex as a track and Relic Race Perfect checks (#133 PR 3c):
`cortex-perfect.json` is slot data the paired apworld emitted
(`worlds/ctr/test/test_cortex_relic_perfect.py` helpers, seed 20261017,
script `emit-cortex-perfect-fixture.py` in the PR 3c artifacts) with all three
modes, two stages and a per-pad order on Crash Cove, Oxide Station (Cortex
Vortex's engine host) and Slide Coliseum, Cortex Vortex (origin
`cortex_vortex`, no package row, no custom slot), Baby T Park 1.0.2 on named
block 8 (no Relic Race), North Pole test data on named block 53 and the B+
`fixture-loop` (both with a Relic Race; pins name no real files). It has
`relic_perfect: true`, four rungs, per-track Wumpa and Lettersanity mode 3, so
Cortex Vortex pools its own letter items 35010200 to 35010202.
`test-content-plan.cpp` checks every pad's perfect code (retail frozen,
35026005, block offset 10, generic slot), Cortex Vortex's frozen codes and
host, runs the field mutations and refuses forged perfects, settings and
Cortex Vortex records. The earlier fixtures have no perfect and are unchanged.

Pool-only packs and the content-plan envelope (#133 PR 4a):
`item-packs.json` is slot data the paired apworld emitted
(`worlds/ctr/test/test_item_packs.py` helpers, seed 20261017, script
`emit-item-packs-fixture.py` in the PR 4a artifacts) on Crash Cove, Papu's
Pyramid and Slide Coliseum plus Baby T Park 1.0.2 and the B+ `fixture-loop`,
with all three modes, four rungs and per-track Wumpa. Its plan carries
`item_packs` (Progressive Starting Wumpa 3, Turbo Grant, Tizi Helper) with one
ledger row each, and its envelope an explicit `death_link_send` 6,
`warp_pad_item_display` 1, `ap_item_type_colors` false, `death_link` 3
(race_loss) and the `trap_identities` block. `test-content-plan.cpp` checks
the applied settings, the trap block (read like a normal room's, a malformed
block silences traps without refusing the room), the absent-key defaults, runs
the field mutations outside the trap block and refuses forged envelope values
and pack records and rows. The earlier fixtures have none of these and are
unchanged.
