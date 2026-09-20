#ifndef AP_PODIUM_PRESENTATION_LOGIC_H
#define AP_PODIUM_PRESENTATION_LOGIC_H

#ifdef CTR_AP

// Freestanding podium presentation policy for issue #235. The AP Trophy
// presentation owns its own received-count line and #330 item text, so it must
// not birth the retail Trophy prize Instance/thread and must not set the
// INC_TROPHY count-up. Pure and engine-independent, so the host harness
// (tools/test-podium-trophy-presentation.c) pins the decision directly;
// game/233/CS_Podium.c consumes this function rather than keeping a second copy
// of the policy.
//
// The AP Trophy presentation is exactly AP-active AND an ordinary retail-track
// Trophy reward. Everything else is untouched: a non-AP Trophy keeps the prize
// and the count-up, a trial-track or Cortex Vortex podium (which reuses the
// Trophy model without a retail trophy bit) keeps its vanilla path, and an AP
// Key / Gem / Relic / BIG1 podium keeps its own vanilla prize path. Relic in
// particular must stay untouched because CS_Camera_BoolGotoBoss() consumes
// STATIC_RELIC for Oxide's transition.

// Mirror of the engine's STATIC_TROPHY model id (namespace_Instance.h), spelled
// out here so the policy stays freestanding. ap_hooks.c static-asserts it
// against the engine constant so the mirror can never drift.
#define AP_PODIUM_TROPHY_MODEL 0x62

// 1 when the current podium is an AP-owned ordinary retail-track Trophy
// presentation. `specialTrack` is 1 for a podium that reuses the retail Trophy
// model without an ordinary retail trophy bit (a trial track or Cortex Vortex);
// those keep the vanilla prize path and are outside #235's ordinary-Trophy
// scope, which also keeps prevLEV + ADV_REWARD_FIRST_TROPHY a valid location.
static inline int AP_PodiumIsApTrophyPresentation(int apActive, int rewardId,
                                                  int specialTrack)
{
	return apActive && rewardId == AP_PODIUM_TROPHY_MODEL && !specialTrack;
}

// 1 when the retail prize Instance/thread must be created for this podium.
// Every reward keeps its vanilla prize except the AP Trophy presentation, which
// draws the received count and the #330 item text itself.
static inline int AP_PodiumShouldBirthPrize(int apActive, int rewardId,
                                            int specialTrack)
{
	return !AP_PodiumIsApTrophyPresentation(apActive, rewardId, specialTrack);
}

// Terminal work the retail prize thread performs when the ceremony ends
// (CS_Podium_Prize_ThTick3, game/233/CS_Podium.c): raise overlayTransition to 2,
// clear VEH_FREEZE_PODIUM, and play the completion FX (0x67). The AP ordinary
// Trophy presentation births no prize thread, so the podium exit must reproduce
// exactly that work itself or the player is left frozen. Zero means "do
// nothing"; the engine applies only the effects that are set.
typedef struct
{
	int overlayTransition; // 2 -> request the normal podium-exit overlay
	int releaseFreeze;     // 1 -> clear VEH_FREEZE_PODIUM
	int completionSound;   // 1 -> play the retail completion FX (0x67)
} ap_podium_exit_work;

// One-shot latch for the terminal work, scoped to a single podium lifetime.
// CS_Podium_FullScene_Init resets it when the podium is born; the AP
// ordinary-Trophy exit consumes it, so the work can never run twice.
typedef struct
{
	int consumed;
} ap_podium_exit_state;

static inline void AP_PodiumExitStateReset(ap_podium_exit_state *state)
{
	state->consumed = 0;
}

// The production decision used at podium exit. Returns the terminal work the
// exit must perform: all three effects exactly once for an AP ordinary-Trophy
// presentation, and nothing for every other podium (non-AP Trophy, AP relic,
// trial/Cortex, custom, and every other reward), whose retail prize thread
// still performs its own terminal work.
static inline ap_podium_exit_work AP_PodiumExitWork(
	int apActive, int rewardId, int specialTrack, ap_podium_exit_state *state)
{
	ap_podium_exit_work work;

	work.overlayTransition = 0;
	work.releaseFreeze = 0;
	work.completionSound = 0;

	if (!AP_PodiumIsApTrophyPresentation(apActive, rewardId, specialTrack))
		return work;
	if (state->consumed)
		return work;

	state->consumed = 1;
	work.overlayTransition = 2;
	work.releaseFreeze = 1;
	work.completionSound = 1;
	return work;
}

#endif // CTR_AP
#endif // AP_PODIUM_PRESENTATION_LOGIC_H
