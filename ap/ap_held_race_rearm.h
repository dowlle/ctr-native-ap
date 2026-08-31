#ifndef CTR_AP_HELD_RACE_REARM_H
#define CTR_AP_HELD_RACE_REARM_H

// Returns 1 exactly once when a genuine countdown starts a new attempt.
static inline int AP_HeldRaceRearmStep(int onTrack, int loadingIdle,
                                      int endOfRace, int trafficLightsTimer,
                                      int *countdownSeen)
{
	if (!onTrack || !loadingIdle || endOfRace)
	{
		*countdownSeen = 0;
		return 0;
	}
	if (trafficLightsTimer >= 1)
	{
		int newAttempt = !*countdownSeen;
		*countdownSeen = 1;
		return newAttempt;
	}
	return 0;
}

#endif
