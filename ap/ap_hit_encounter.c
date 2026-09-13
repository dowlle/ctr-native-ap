#ifdef CTR_AP

// Hit Character encounters (schema 14, ticket 06): the gather half. See
// ap/ap_hit_encounter.h for the contract and ap/ap_hit_policy.h for the
// freestanding decisions this module feeds.

#include "ap_hit_encounter.h"
#include "ap_hit_policy.h"
#include "ap_net.h"
#include "ap_hooks.h" // AP_EmitHitCharacterCheck (wraps AP_EmitClassCheck)

#include <string.h> // strcmp (cup snapshot seed/slot identity)

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

// ── Gem Cup roster snapshot (ticket 11) ─────────────────────────────────────
// The snapshot lives here so the loader and the harness share one lifecycle.
static ap_hit_cup_snapshot s_cupSnapshot;

// The seed/slot identity the snapshot was resolved under. A reconnect to the
// SAME seed must NOT redraw the active cup (a held offline win can flush on
// reconnect mid-cup); only a different room/slot or a different roster seed
// clears it.
static char s_cupSeed[128];
static char s_cupSlot[64];
static unsigned int s_cupPolicySeed;

static void ap_hit_cup_capture_identity(void)
{
	if (!ap_net_seed_name(s_cupSeed, (int)sizeof s_cupSeed))
		s_cupSeed[0] = '\0';
	if (!ap_net_slot_name(s_cupSlot, (int)sizeof s_cupSlot))
		s_cupSlot[0] = '\0';
	s_cupPolicySeed = ctr_cfg.hit.seed;
}

int AP_HitEncounterCupFieldSize(int cupID)
{
	return AP_HitCupFieldSizePure(cupID);
}

void AP_HitCupSnapshotBegin(int cupID)
{
	AP_HitCupSnapshotBeginPure(&s_cupSnapshot, cupID);
}

void AP_HitCupSnapshotReset(void)
{
	AP_HitCupSnapshotResetPure(&s_cupSnapshot);
}

// Resolve/reuse the cup roster for this load. A NEW cup (pad entry, or an
// invalid/mismatched snapshot) resolves fresh from current eligibility; every
// continuing leg and same-session retry copies the stored roster unchanged, so a
// mid-cup unlock cannot alter the active cup. Returns the AI seat count.
int AP_HitCupSnapshotField(int cupID, int trackIndex, int player, int aiSeats,
                           int *outIDs)
{
	int kind = AP_HitCupLegKindPure(cupID, trackIndex, s_cupSnapshot.pending,
	                                s_cupSnapshot.valid, s_cupSnapshot.cupID,
	                                s_cupSnapshot.trackIndex);
	int i;

	if (kind == AP_HIT_CUP_LEG_NEW)
	{
		// Wire cup keys are 100 + cupID.
		int n = AP_HitEncounterBuildField(100 + cupID, player, aiSeats,
		                                  s_cupSnapshot.ids);
		s_cupSnapshot.count = n;
		s_cupSnapshot.valid = 1;
		s_cupSnapshot.cupID = cupID;
		s_cupSnapshot.pending = 0;
		ap_hit_cup_capture_identity();
	}
	s_cupSnapshot.trackIndex = trackIndex;

	for (i = 0; i < s_cupSnapshot.count; i++)
		outIDs[i] = s_cupSnapshot.ids[i];
	return s_cupSnapshot.count;
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
	char seed[128], slot[64];
	int haveSeed, haveSlot;

	// Always re-arm the Hit-check session mask: a reconnect resends anything the
	// server never confirmed.
	s_hit_sent_mask = 0;

	// Keep the cup snapshot across a reconnect to the SAME seed/slot. A held
	// offline win can flush on an automatic reconnect mid-cup, which must not
	// redraw the active cup's roster. Clear it only when the room/slot or the
	// roster seed changes.
	if (!s_cupSnapshot.valid)
		return;
	haveSeed = ap_net_seed_name(seed, (int)sizeof seed);
	haveSlot = ap_net_slot_name(slot, (int)sizeof slot);
	if (!haveSeed || strcmp(seed, s_cupSeed) != 0 ||
	    !haveSlot || strcmp(slot, s_cupSlot) != 0 ||
	    ctr_cfg.hit.seed != s_cupPolicySeed)
		AP_HitCupSnapshotReset();
}

#endif // CTR_AP
