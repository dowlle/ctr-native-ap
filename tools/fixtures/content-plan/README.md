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
