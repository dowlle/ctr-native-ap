#ifndef MAINFRAME_RACE_CLOCK_H
#define MAINFRAME_RACE_CLOCK_H

static inline void MainFrame_AdvanceRaceClock(struct GameTracker *gGT)
{
	if (gGT->trafficLightsTimer >= 1)
	{
		gGT->elapsedEventTime = 0;
		return;
	}
	if (gGT->gameMode1 & DEBUG_MENU)
		return;
	if (gGT->frozenTimeRemaining < 1)
	{
		if (!(gGT->gameMode1 & END_OF_RACE))
			gGT->elapsedEventTime += gGT->elapsedTimeMS;
		return;
	}

	int remaining = gGT->frozenTimeRemaining - gGT->elapsedTimeMS;
	gGT->frozenTimeRemaining = remaining < 0 ? 0 : remaining;
	const u32 frame = gGT->timer;
	// Keep the retail freeze sound cadence, including the frame that reaches zero.
	if (remaining >= 0 && frame % 6 == 0)
		OtherFX_Play_LowLevel(0x40, '\0', frame % 12 == 0 ? 0x8c9080 : 0x8c8880);
}

#endif
