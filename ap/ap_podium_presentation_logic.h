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
// The AP Trophy presentation is exactly AP-active AND reward == retail Trophy.
// Everything else is untouched: a non-AP Trophy keeps the prize and the
// count-up, and an AP Key / Gem / Relic / BIG1 podium keeps its own vanilla
// prize path. Relic in particular must stay untouched because
// CS_Camera_BoolGotoBoss() consumes STATIC_RELIC for Oxide's transition.

// Mirror of the engine's STATIC_TROPHY model id (namespace_Instance.h), spelled
// out here so the policy stays freestanding. ap_hooks.c static-asserts it
// against the engine constant so the mirror can never drift.
#define AP_PODIUM_TROPHY_MODEL 0x62

// 1 when the current podium is an AP-owned Trophy presentation.
static inline int AP_PodiumIsApTrophyPresentation(int apActive, int rewardId)
{
	return apActive && rewardId == AP_PODIUM_TROPHY_MODEL;
}

// 1 when the retail prize Instance/thread must be created for this podium.
// Every reward keeps its vanilla prize except the AP Trophy presentation, which
// draws the received count and the #330 item text itself.
static inline int AP_PodiumShouldBirthPrize(int apActive, int rewardId)
{
	return !AP_PodiumIsApTrophyPresentation(apActive, rewardId);
}

#endif // CTR_AP
#endif // AP_PODIUM_PRESENTATION_LOGIC_H
