#ifdef CTR_AP

// Hit Character encounters (schema 14, ticket 06): the gather half. See
// ap/ap_hit_encounter.h for the contract and ap/ap_hit_policy.h for the
// freestanding decisions this module feeds.

#include "ap_hit_encounter.h"
#include "ap_hit_policy.h"
#include "ap_net.h"
#include "ap_hooks.h" // AP_EmitHitCharacterCheck (wraps AP_EmitClassCheck)

// One bit per engine id (0..15): a Hit check has already been requested this
// session. Cleared on a fresh slot-connect so the held-check/reconnect path can
// resend anything the server never confirmed. The server's own checked set is
// consulted too, so a reconnect cannot duplicate a settled check.
static unsigned s_hit_sent_mask;

int AP_HitEncounterEnabled(void)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	return (h != NULL && h->enabled) ? 1 : 0;
}

const ctr_hit_candidates *AP_HitEncounterCandidates(int destLevelID)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	if (h == NULL)
		return NULL;
	if (destLevelID >= 0 && destLevelID < CTR_CFG_HIT_TRACK_COUNT)
		return &h->tracks[destLevelID];
	if (destLevelID >= 100 && destLevelID < 100 + CTR_CFG_HIT_CUP_COUNT)
		return &h->cups[destLevelID - 100];
	return NULL;
}

int AP_HitEncounterShouldApply(int isAdventure, int destLevelID, int isCup,
                               int isBoss, int isArcade, int isRelic,
                               int isToken, int isCrystal, int numPlayers)
{
	if (!AP_HitEncounterEnabled())
		return 0;
	return AP_HitOrdinaryAppliesPure(isAdventure, destLevelID, isCup, isBoss, isArcade,
	                                 isRelic, isToken, isCrystal, numPlayers);
}

int AP_HitEncounterGuestEligible(int guest)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	int i;
	if (h == NULL)
		return 0;
	if (guest < 0 || guest >= CTR_CFG_HIT_CHARACTER_COUNT)
		return 0;
	if (guest < 8)
		return 1; // defaults always appear
	{
		const ctr_hit_trigger *t = &h->triggers[guest - 8];
		for (i = 0; i < t->count; i++)
			if (ap_net_location_checked(t->any_of[i]))
				return 1;
	}
	return 0;
}

int AP_HitEncounterOpportunity(int destLevelID, int player)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	const ctr_hit_candidates *cand = AP_HitEncounterCandidates(destLevelID);
	unsigned char eligible[CTR_CFG_HIT_CHARACTER_COUNT];
	unsigned char unchecked[CTR_CFG_HIT_CHARACTER_COUNT];
	int i;

	if (h == NULL || cand == NULL)
		return -1;

	for (i = 0; i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
	{
		long code = h->locations[i];
		eligible[i] = (unsigned char)AP_HitEncounterGuestEligible(i);
		unchecked[i] = (code > 0 && ap_net_location_exists(code) &&
		                !ap_net_location_checked(code))
		                   ? 1
		                   : 0;
	}
	return AP_HitOpportunityPure(cand, eligible, unchecked, player);
}

int AP_HitEncounterBuildField(int destLevelID, int player, int aiSeats, int *outIDs)
{
	const ctr_hit_candidates *cand = AP_HitEncounterCandidates(destLevelID);
	unsigned char eligible[CTR_CFG_HIT_CHARACTER_COUNT];
	ap_hit_field field;
	int i;

	if (cand == NULL)
		return 0;

	for (i = 0; i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
		eligible[i] = (unsigned char)AP_HitEncounterGuestEligible(i);

	AP_HitSelectFieldPure(cand, eligible, player, aiSeats, &field);
	for (i = 0; i < field.count; i++)
		outIDs[i] = field.ids[i];
	return field.count;
}

int AP_HitEncounterExtras(const int *selected, int selectedCount, int player,
                          int *outExtras, int cap)
{
	// The stock set is exactly what LOAD_Robots1P writes: seven ids starting at
	// 0, skipping the player when the player is itself a default. A nondefault
	// player therefore guarantees only 0..6 (Pura, 7, needs an extra).
	int stock[8];
	int stockCount = 0;
	int next = 0;
	int i;

	for (i = 0; i < 7; i++)
	{
		if (next == player)
			next++;
		stock[stockCount++] = next;
		next++;
	}
	return AP_HitExtrasPlanPure(selected, selectedCount, stock, stockCount, player,
	                            outExtras, cap);
}

int AP_HitEncounterOnDamage(int victimEngineID, int damageType, unsigned flags)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	long code;
	int accepted;
	int raceSupported = (flags & 128u) != 0;
	int wasDamageActive = (flags & 256u) != 0;

	if (h == NULL || !h->enabled)
		return 0;

	// Only a supported live race may award; the gather computes the mode bits.
	if (!raceSupported)
		return 0;

	// Only a call that actually applied an effect may award. Type 1 while
	// already damage-active applies nothing; type 4 while already spinning still
	// applies burn.
	if (!AP_HitEffectAppliedPure(damageType, wasDamageActive))
		return 0;

	accepted = AP_HitDamageAcceptedPure(damageType, victimEngineID,
	                                    (flags & 1u) != 0,  /* victimIsAI */
	                                    (flags & 2u) != 0,  /* victimLive */
	                                    (flags & 4u) != 0,  /* victimIsGhost */
	                                    (flags & 8u) != 0,  /* victimIsPlayer */
	                                    (flags & 16u) != 0, /* attackerPresent */
	                                    (flags & 32u) != 0, /* attackerIsLocalP1 */
	                                    (flags & 64u) != 0); /* attackerIsAI */
	if (!accepted)
		return 0;

	code = h->locations[victimEngineID];
	if (code <= 0)
		return 0;

	if (s_hit_sent_mask & (1u << victimEngineID))
		return 1; // already requested this session
	if (ap_net_location_checked(code))
	{
		s_hit_sent_mask |= (1u << victimEngineID);
		return 1; // already settled on the server
	}

	s_hit_sent_mask |= (1u << victimEngineID);
	AP_EmitHitCharacterCheck(code);
	return 1;
}

void AP_HitEncounterConnectReset(void)
{
	s_hit_sent_mask = 0;
}

#endif // CTR_AP
