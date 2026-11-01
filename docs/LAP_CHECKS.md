# Lap completion checks (#344)

`lap_completion_checks` (default off) adds `<Track>: Complete One Lap`, one check per track the seed races. It is sent at the first accepted lap of any Adventure race with laps on that track: a Trophy Race, CTR Challenge, Relic Race, cup leg or boss race. Position does not matter. A track raced several ways still has one check. A Crystal Challenge has no laps.

## Race side

`PlayLevel_UpdateLapStats` calls `AP_LapOnCrossing` right after a forward finish crossing advances `lapIndex`, which also covers the final crossing of a one-lap race and skips the behind-the-start-line crossing. Only the local player's kart counts (driver slot 0, not a bot). The hub, Arcade, Battle, the main menu and the demo send nothing. The usual membership and checked-state guards apply, so repeats send nothing.

## Identity

`ap/ap_hooks_laps.c` reads the plan, never the engine host's LevelID:

- the running cup's `legs[n].lap`, boss cups included;
- a boss race's `ctr_content_bosses.lap[bossID]`, 0 to 3 the garages, 4 and 5 N. Oxide's two races;
- a standalone pad's `lap_location`.

## Wire

Lap checks are plan checks of kind `lap`, owner the track id, with one route per standalone mode (award `lap`), one per cup leg (mode `cup`, with the leg id), one per boss race (mode `boss_race`, with the encounter id) and one per boss cup leg (mode `boss_cup`, with both ids), in entry order. Feature `lap_checks_v1` is appended last in `required_features`. Native recomputes every lap check from the registry (`lap`, `custom_lap_locations`, named offset 16) and refuses any difference. A venue raced nowhere else has its check too: with the default goal that is Oxide Station and the Final Challenge's Cortex Vortex.

## Display

A pad's first lap shows in its glow and rides the race slot (family slot 10). A cup lists each of its tracks' first lap, Wumpa check and boxes once. The cup's state counts them too, so it wears no grey X while one is left. A leg can break a box only while the track's own pad is open, so a shut pad's boxes stay out of the cup's glow.

## Logic

A standalone route needs its pad, its mode's stage 2 and the track's finish rank. A cup route needs the cup pad and the finish of every leg up to its own. A boss route needs its encounter and the same finish terms. None of them needs a win or a place.
