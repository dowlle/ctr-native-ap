# Lap completion checks

Set `lap_completion_checks: true` to add `<Track>: Complete One Lap` for every track the seed races. The option defaults off. The check is sent by the first lap you complete on that track in any race with laps: a Trophy Race, CTR Challenge, Relic Race or a Cup leg. Finishing position does not matter, and a track has one check however often it appears. Crystal Challenges have no laps. Boss races do not send a lap check (see below).

Only the local player's kart counts. The send happens at the forward finish-line crossing that advances the lap counter (`PlayLevel_UpdateLapStats`), which also covers the final crossing of a one-lap race. The usual server membership and checked-state guards apply, so a repeat lap or another appearance of the track sends nothing.

## Which tracks have a check

A normal room gives a check to each retail race track, Slide Coliseum and Turbo Track, and the Cortex Vortex pad track, when the seed races it from its own pad or as a Gem Cup leg. A destination the Cortex Vortex pad track drops counts only through the Gem Cups that still leg it. A legacy `custom_tracks` entry gets its generic slot check, or offset 16 of its named block.

A content plan gives a check to every track it seats on a standalone pad or races as a cup leg, retail, Cortex Vortex or custom. A boss race or boss cup is no route.

Boss races are left out on both room shapes for now: the apworld models no boss route to a lap check, so a track raced only as a boss venue has none, and native sends none from a boss race.

## Codes

Retail tracks use `35028000` onward in the canonical retail order (the Reach 10 Wumpa order), Cortex Vortex `35026009`, generic custom slot N `35028100 + N - 1`, and a named track offset 16 of its block. All names were minted in the 0.3.0 id step.

## Schema 18 wire

Off omits both keys on a normal room. On adds Boolean `ctr_options.lap_completion_checks: true` and `lap_checks: {"version": 1, "tracks": {...}, "custom_slots": {...}}`. `tracks` keys are engine LevelIDs `"0"` to `"17"` and `"110"` for Cortex Vortex; `custom_slots` has at most the one legacy custom track's slot. Only tracks with a check travel. Native requires version 1 and the frozen code for every key, and refuses the seed otherwise; every parse clears the previous table.

A content plan has no envelope key. Its lap checks are plan checks of kind `lap`, owner the track id, with one route per standalone mode (award `lap`) and one per cup leg (mode `cup`, award `lap`, with the leg id), in entry order. Feature `lap_checks_v1` is appended last in `required_features`. Native recomputes every lap check from the registry (`lap`, `custom_lap_locations`, named offset 16) and refuses any difference. A pad keeps its code in `lap_location`, a cup leg in `legs[n].lap`.

## Logic

A lap needs what finishing that race needs and no more: the route's pad or Cup entry, a later mode's second-stage gate, and the track's finish rank where an Ultimate Sacred Fire finish is ruled, bound to the route pad's racer. A Cup route also needs the finish of every earlier leg. No win, rank or difficulty term.

Refs #344. Steam acceptance remains required for the send timing, a one-lap Cup, retail, Cortex Vortex, named and generic custom tracks, a cup leg, reconnect and the absent boss route.
