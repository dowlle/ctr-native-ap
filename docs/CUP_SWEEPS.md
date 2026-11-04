# Cup sweep checks

Set `cup_sweep_checks: true` to add `<Colour> Gem Cup: Win All Legs` for each
present four-leg Gem Cup. The option defaults off. Existing Gem checks still
reward an overall points win independently. A Cup displaced by a single custom
race or removed by Cortex Vortex replacement has no sweep check. Future
composed Cups are outside this implementation.

Win first place in all four ordered legs of one uninterrupted attempt. The
check sends when the final standings close the Cup. A lost or forced-loss leg,
restart, exit, changed room or changed Cup configuration invalidates that
attempt. Repeated track identities in separate legs are supported. One-lap
Cups change race length without changing eligibility. Pending sweeps remain
visible on a Cup pad after its Gem is collected.

## Schema 18 wire

Off omits both keys. On adds Boolean `ctr_options.cup_sweep_checks: true` and
`cup_sweep_checks: {"version": 1, "cups": {...}}`. Keys are destination Cup
LevelIDs `"100"` through `"104"`, in Red, Green, Blue, Yellow, Purple order.
Each row has exactly `location` (the already registered code `35028300 +
colour index`) and `legs` (four ordered logical track LevelIDs). Native requires
the exact resolved `gem_cup_legs` row, including vanilla defaults and Cortex
Vortex's logical 110 alias, and excludes a displaced Cup. Malformed enabled
data refuses seed admission; every parse clears the previous table.

Rules use the Cup's actual entrance and racer binding plus all four legs'
finish and first-place capability requirements. Universal Tracker restores
the switch and validates the same identities and ordered legs against saved
room data, including persisted tuple arrays. An open sweep is an independent
Cup win in the race-loss stakes table.

Refs #246. Steam acceptance remains required for standings timing, pad display,
restart/exit/reconnect, shuffled pads and same-track repeated legs.
