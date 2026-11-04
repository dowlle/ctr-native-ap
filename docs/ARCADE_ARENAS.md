# Arcade Crystal Challenges (#259)

The development content-plan client can run Crystal Challenges in Parking Lot,
The North Bowl and Lab Basement using twenty fixed authored positions each.
The four retail challenges can use the same content-plan routing. A selected
arena occupies one ordinary physical pad seat; it owns a completion check and,
with Crystalsanity, twenty position-owned pickup checks. Clearing a challenge
does not grant a local Purple Token.

Use the paired development apworld and its `experimental-content-plan.yaml`
fixture. Set `include_arcade_arenas: true` and choose
`content_pool.crystals: {pool: all, count: 7}` to include all seven arenas.
Set `crystalsanity: true` for pickup checks. Explicit pools accept distinct
arena LevelIDs 18 through 24; `count` also accepts `{min: N, max: M}`.
`include_battle_arenas: false` suppresses the arena subset. Legacy seeds keep
their existing routing; enabling arcade arenas requires an explicit content plan.

The three authored layouts currently use **provisional ten-minute test limits**.
Positions are accepted test candidates, not a completed driving/timing review.
Steam acceptance must check every pickup, retry/exit, shuffled physical entry,
completion, remaining-check display and seed/reconnect handling before release.

`tools/fixtures/arcade-arenas-v1.json` mirrors the paired world manifest;
`ap/ap_arena_content.h` carries the exact stable anchor order and check identities.
Runtime objects use the retail crystal model and pickup routine, with additive
swept collision as used by the trial letters. A construction failure returns to
the hub without a reward. No extracted game assets are included.

Wire feature negotiation requires `crystal_entries_v1`, optional
`crystal_pickups_v1`, and `arcade_crystals_v1` when arcade content is selected.
The parser admits the exact room location union before pad entry. Pickup and
completion validate the current standalone context and seed epoch. A stale
arena never falls through to retail reward-bit arithmetic.
