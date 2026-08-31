#ifndef AP_CUSTOM_TROPHY_CEREMONY_LOGIC_H
#define AP_CUSTOM_TROPHY_CEREMONY_LOGIC_H

// Process-local latch for the one custom Trophy podium that follows a newly
// sent check. The check is emitted before the hub load; the podium is born
// after it. Keep that cross-load ownership explicit and freestanding so a
// reconnect, replay, or later retail podium cannot inherit the presentation.

typedef struct
{
	int active;
} ap_custom_trophy_ceremony_state;

static void AP_CustomTrophyCeremonyReset(
	ap_custom_trophy_ceremony_state *state)
{
	state->active = 0;
}

static int AP_CustomTrophyCeremonyArm(
	ap_custom_trophy_ceremony_state *state, int sentNewCheck)
{
	if (sentNewCheck)
		state->active = 1;
	return state->active;
}

static int AP_CustomTrophyCeremonyActive(
	const ap_custom_trophy_ceremony_state *state)
{
	return state->active != 0;
}

static void AP_CustomTrophyCeremonyEnd(
	ap_custom_trophy_ceremony_state *state)
{
	state->active = 0;
}

#endif // AP_CUSTOM_TROPHY_CEREMONY_LOGIC_H
