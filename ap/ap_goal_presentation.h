#ifndef AP_GOAL_PRESENTATION_H
#define AP_GOAL_PRESENTATION_H

// Pure state machine for issue #244. Goal evaluation and the game presentation
// are deliberately separate: reconnect may need to resend StatusUpdate(GOAL),
// but only a live completion event may start the ending or credits again.
typedef struct AP_GoalPresentationState
{
	int liveEventSeen;
	int creditsPending;
} AP_GoalPresentationState;

static void AP_GoalPresentationReset(AP_GoalPresentationState *state)
{
	state->liveEventSeen = 0;
	state->creditsPending = 0;
}

static void AP_GoalPresentationArm(AP_GoalPresentationState *state)
{
	state->liveEventSeen = 1;
}

static void AP_GoalPresentationEvaluate(AP_GoalPresentationState *state,
	                                    int requirementsMet)
{
	if (requirementsMet && state->liveEventSeen)
		state->creditsPending = 1;
}

static int AP_GoalPresentationClaim(AP_GoalPresentationState *state)
{
	if (!state->creditsPending)
		return 0;
	state->creditsPending = 0;
	state->liveEventSeen = 0;
	return 1;
}

#endif
