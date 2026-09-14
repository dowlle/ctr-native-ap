#ifdef CTR_AP

// Hit Character encounters (global schema 16, block schema 2): the gather half. See
// ap/ap_hit_encounter.h for the contract and ap/ap_hit_policy.h for the
// freestanding decisions this module feeds.

#include "ap_hit_encounter.h"
#include "ap_hit_policy.h"
#include "ap_net.h"
#include "ap_hooks.h" // AP_EmitHitCharacterCheck (wraps AP_EmitClassCheck)

#include <string.h> // strcmp (draw-state seed/slot identity)

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

// The destination key for cursors: tracks 0..17 map to 0..17, cups 100..104 to
// 18..22; anything else is -1.
#define AP_HIT_DEST_KEYS (CTR_CFG_HIT_TRACK_COUNT + CTR_CFG_HIT_CUP_COUNT)
static int ap_hit_dest_key(int destLevelID)
{
	if (destLevelID >= 0 && destLevelID < CTR_CFG_HIT_TRACK_COUNT)
		return destLevelID;
	if (destLevelID >= 100 && destLevelID < 100 + CTR_CFG_HIT_CUP_COUNT)
		return CTR_CFG_HIT_TRACK_COUNT + (destLevelID - 100);
	return -1;
}

const int *AP_HitEncounterOrder(int destLevelID)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	if (h == NULL)
		return NULL;
	if (destLevelID >= 0 && destLevelID < CTR_CFG_HIT_TRACK_COUNT)
		return h->tracks[destLevelID].ids;
	if (destLevelID >= 100 && destLevelID < 100 + CTR_CFG_HIT_CUP_COUNT)
		return h->cups[destLevelID - 100].ids;
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

int AP_HitEncounterGather(unsigned char *eligible, unsigned char *unchecked)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	int i;

	for (i = 0; i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
	{
		eligible[i] = 0;
		unchecked[i] = 0;
	}
	if (h == NULL)
		return 0;
	for (i = 0; i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
	{
		long code = h->locations[i];
		eligible[i] = (unsigned char)AP_HitEncounterGuestEligible(i);
		unchecked[i] = (code > 0 && ap_net_location_exists(code) &&
		                !ap_net_location_checked(code))
		                   ? 1
		                   : 0;
	}
	return 1;
}

int AP_HitEncounterOpportunity(int destLevelID, int player)
{
	unsigned char eligible[CTR_CFG_HIT_CHARACTER_COUNT];
	unsigned char unchecked[CTR_CFG_HIT_CHARACTER_COUNT];

	if (AP_HitEncounterOrder(destLevelID) == NULL)
		return -1;
	if (!AP_HitEncounterGather(eligible, unchecked))
		return -1;
	return AP_HitOpportunityPure(eligible, unchecked, player);
}

// ── Draw state: per-destination cursors and the seed identity they belong to ──
static ap_hit_cursors s_cursors[AP_HIT_DEST_KEYS];
static unsigned s_draws[AP_HIT_DEST_KEYS];
static int s_stateInit;

// The seed/slot identity the cursors and snapshots were produced under. A
// reconnect to the SAME seed must NOT reset them (a held offline win can flush
// on reconnect mid-cup or mid-retry); only a different room/slot or a different
// roster seed clears them.
static int s_identValid;
static char s_identSeed[128];
static char s_identSlot[64];
static unsigned int s_identPolicySeed;

static void ap_hit_state_init(void)
{
	int i;
	if (s_stateInit)
		return;
	for (i = 0; i < AP_HIT_DEST_KEYS; i++)
	{
		AP_HitCursorsResetPure(&s_cursors[i]);
		s_draws[i] = 0;
	}
	s_stateInit = 1;
}

static void ap_hit_capture_identity(void)
{
	if (s_identValid)
		return;
	if (!ap_net_seed_name(s_identSeed, (int)sizeof s_identSeed))
		s_identSeed[0] = '\0';
	if (!ap_net_slot_name(s_identSlot, (int)sizeof s_identSlot))
		s_identSlot[0] = '\0';
	s_identPolicySeed = ctr_cfg.hit.seed;
	s_identValid = 1;
}

int AP_HitEncounterDrawFresh(int destLevelID, int player, int aiSeats, int *outIDs)
{
	const int *order = AP_HitEncounterOrder(destLevelID);
	int key = ap_hit_dest_key(destLevelID);
	unsigned char eligible[CTR_CFG_HIT_CHARACTER_COUNT];
	unsigned char unchecked[CTR_CFG_HIT_CHARACTER_COUNT];
	ap_hit_field field;
	int i;

	if (order == NULL || key < 0)
		return 0;
	ap_hit_state_init();
	ap_hit_capture_identity();
	AP_HitEncounterGather(eligible, unchecked);
	AP_HitDrawFieldPure(order, eligible, unchecked, player, aiSeats, &s_cursors[key],
	                    &field);
	s_draws[key]++;
	for (i = 0; i < field.count; i++)
		outIDs[i] = field.ids[i];
	return field.count;
}

int AP_HitEncounterDrawState(int destLevelID, int *outCursors3, unsigned *outDraws)
{
	int key = ap_hit_dest_key(destLevelID);
	if (key < 0)
		return 0;
	ap_hit_state_init();
	outCursors3[0] = s_cursors[key].unhit;
	outCursors3[1] = s_cursors[key].other;
	outCursors3[2] = s_cursors[key].stock;
	*outDraws = s_draws[key];
	return 1;
}

int AP_HitEncounterExtras(const int *selected, int selectedCount, int player,
                          int *outExtras, int cap)
{
	// The stock set is exactly what LOAD_Robots1P writes: seven ids starting at
	// 0, skipping the player when the player is itself a default. A nondefault
	// player therefore guarantees only 0..6 (Pura, 7, needs an extra).
	int stock[7];
	AP_HitStockPure(player, stock);
	return AP_HitExtrasPlanPure(selected, selectedCount, stock, 7, player,
	                            outExtras, cap);
}

// ── Ordinary race snapshot ─────────────────────────────────────────────────
// A restart or retry reloads the same field: an ordinary apply load reuses the
// stored field only when the IMMEDIATELY previous driver load was an ordinary
// apply for the same level, player and seat count. Any other load in between
// (hub, menu, boss, relic, token, cup...) makes the next ordinary load draw
// fresh, which is what advances the rotation from one race to the next.
typedef struct
{
	int valid;
	int level;
	int player;
	int aiSeats;
	int count;
	int ids[AP_HIT_FIELD_MAX];
} ap_hit_race_snapshot;

static ap_hit_race_snapshot s_raceSnap;
static int s_lastLoadWasRace; // set by an ordinary apply load
static int s_raceArmed;       // latched at the next load's begin

void AP_HitLoadBegin(void)
{
	s_raceArmed = s_lastLoadWasRace;
	s_lastLoadWasRace = 0;
}

int AP_HitRaceField(int destLevelID, int player, int aiSeats, int *outIDs, int *outFresh)
{
	int i;
	int reuse = s_raceArmed && s_raceSnap.valid && s_raceSnap.level == destLevelID &&
	            s_raceSnap.player == player && s_raceSnap.aiSeats == aiSeats;

	if (!reuse)
	{
		int n = AP_HitEncounterDrawFresh(destLevelID, player, aiSeats, s_raceSnap.ids);
		s_raceSnap.valid = n > 0;
		s_raceSnap.level = destLevelID;
		s_raceSnap.player = player;
		s_raceSnap.aiSeats = aiSeats;
		s_raceSnap.count = n;
	}
	s_raceArmed = 0;
	s_lastLoadWasRace = s_raceSnap.valid;
	if (outFresh != NULL)
		*outFresh = !reuse;
	for (i = 0; i < s_raceSnap.count; i++)
		outIDs[i] = s_raceSnap.ids[i];
	return s_raceSnap.count;
}

// ── Gem Cup roster snapshot (ticket 11) ─────────────────────────────────────
// The snapshot lives here so the loader and the harness share one lifecycle.
static ap_hit_cup_snapshot s_cupSnapshot;

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

void AP_HitEncounterResetDrawState(void)
{
	int i;
	for (i = 0; i < AP_HIT_DEST_KEYS; i++)
	{
		AP_HitCursorsResetPure(&s_cursors[i]);
		s_draws[i] = 0;
	}
	s_stateInit = 1;
	s_raceSnap.valid = 0;
	s_raceArmed = 0;
	s_lastLoadWasRace = 0;
	AP_HitCupSnapshotReset();
	s_identValid = 0;
}

// Resolve/reuse the cup roster for this load. A NEW cup (pad entry, or an
// invalid/mismatched snapshot) draws fresh with the cup's cursors; every
// continuing leg and same-session retry copies the stored roster unchanged, so a
// mid-cup unlock or hit cannot alter the active cup. Returns the AI seat count.
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
		int n = AP_HitEncounterDrawFresh(100 + cupID, player, aiSeats,
		                                 s_cupSnapshot.ids);
		s_cupSnapshot.count = n;
		s_cupSnapshot.valid = 1;
		s_cupSnapshot.cupID = cupID;
		s_cupSnapshot.pending = 0;
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

	// Keep the draw cursors, the ordinary race snapshot and the cup snapshot
	// across a reconnect to the SAME seed/slot. A held offline win can flush on
	// an automatic reconnect mid-cup or before a retry, which must not redraw
	// the active field. Clear them only when the room/slot or the roster seed
	// changes.
	if (!s_identValid)
		return;
	haveSeed = ap_net_seed_name(seed, (int)sizeof seed);
	haveSlot = ap_net_slot_name(slot, (int)sizeof slot);
	if (!haveSeed || strcmp(seed, s_identSeed) != 0 ||
	    !haveSlot || strcmp(slot, s_identSlot) != 0 ||
	    ctr_cfg.hit.seed != s_identPolicySeed)
		AP_HitEncounterResetDrawState();
}

#endif // CTR_AP
