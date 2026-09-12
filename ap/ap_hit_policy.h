#ifndef AP_HIT_POLICY_H
#define AP_HIT_POLICY_H

// Freestanding decision logic for the Hit Character encounter feature (schema
// 14, ticket 06). Pulled out of the engine glue (ap/ap_hit_encounter.c) so a
// host harness can pin every ordering / exclusion / attribution rule without
// linking the engine, exactly the way ap_pad_state.h splits the pad decision
// from its gather.
//
// NOTHING here reads the parsed config, checked state or engine globals: the
// caller supplies every fact. ap_hit_encounter.c is the gather; this header is
// the decision. Both sides compile the same header, so the harness and the game
// cannot drift.

#include "ap_seedcfg.h" // ctr_hit_candidates / CTR_CFG_HIT_* (guarded by CTR_AP)

#ifdef CTR_AP

#ifdef __cplusplus
extern "C" {
#endif

// The most AI seats any ordinary field can carry. The field is P1 plus at most
// seven AI (8 drivers total); the retail Purple cup is the smaller 4-seat case,
// passed as `aiSeats`.
#define AP_HIT_FIELD_MAX 7

// The ordinary destinations the encounter roster applies to: the sixteen retail
// race tracks (0..15) plus the two trial Trophy tracks Slide Coliseum (16) and
// Turbo Track (17). Cups are ticket 11.
#define AP_HIT_ORDINARY_TRACK_MAX 17

// A selected ordinary field: the AI engine ids in seat order (seat 0 first),
// plus the guest chosen for the single guest slot (-1 = no guest).
typedef struct
{
	int ids[AP_HIT_FIELD_MAX];
	int count;
	int guest;
} ap_hit_field;

// Fill one ordinary race's AI field.
//
//   cand      the DESTINATION's parsed {base, pinned, reserve} lists. NULL
//             (feature off / unsupported destination) leaves an empty field.
//   eligible  eligible[16]: may this engine id be seated? Defaults (0..7) are
//             always eligible; a guest (8..15) is eligible only when one of its
//             authoritative unlock wins is checked (see AP_HitGuestEligiblePure).
//   player    the EFFECTIVE player engine id (after any racer lock), excluded
//             from every seat.
//   aiSeats   opponents to seat: 7 ordinary, 4 retail Purple cup.
//
// Contract order (frozen): at most ONE eligible non-player guest, taking the
// first eligible entry of `pinned` then of `reserve`; then append non-player
// unique base ids in wire order until the field is full. If no guest is
// eligible, fill entirely from base. Never exceeds `aiSeats`, never duplicates,
// never seats the player. Returns the number of AI seats written.
static inline int AP_HitSelectFieldPure(const ctr_hit_candidates *cand,
                                        const unsigned char *eligible,
                                        int player, int aiSeats,
                                        ap_hit_field *out)
{
	int i;
	int n = 0;

	out->count = 0;
	out->guest = -1;

	if (cand == NULL || aiSeats <= 0)
		return 0;
	if (aiSeats > AP_HIT_FIELD_MAX)
		aiSeats = AP_HIT_FIELD_MAX;

	// One guest slot: first eligible pinned id, else first eligible reserve id.
	// The pinned list is exactly the approved pin (parser-enforced), so a
	// non-empty pinned list has one entry; the loop keeps the rule general.
	for (i = 0; i < cand->pinned.count && out->guest < 0; i++)
	{
		int id = cand->pinned.ids[i];
		if (id != player && eligible[id])
			out->guest = id;
	}
	for (i = 0; i < cand->reserve.count && out->guest < 0; i++)
	{
		int id = cand->reserve.ids[i];
		if (id != player && eligible[id])
			out->guest = id;
	}

	if (out->guest >= 0 && n < aiSeats)
		out->ids[n++] = out->guest;

	// Fill the remaining seats from base, in wire order, excluding the player,
	// the guest and any duplicate.
	for (i = 0; i < cand->base.count && n < aiSeats; i++)
	{
		int id = cand->base.ids[i];
		int k;
		int seen = 0;
		if (id == player || id == out->guest)
			continue;
		for (k = 0; k < n; k++)
			if (out->ids[k] == id)
				seen = 1;
		if (seen)
			continue;
		out->ids[n++] = id;
	}

	out->count = n;
	return n;
}

// Is `guest` (engine id 0..15) allowed to appear as an encounter? Defaults
// (0..7) always are. A guest (8..15) needs one authoritative trigger win checked
// (`triggerMet` is the parsed any_of list reduced to a 0/1 fact by the gather).
static inline int AP_HitGuestEligiblePure(int guest, const unsigned char *triggerMet)
{
	if (guest < 0 || guest >= CTR_CFG_HIT_CHARACTER_COUNT)
		return 0;
	if (guest < 8)
		return 1;
	return triggerMet[guest] ? 1 : 0;
}

// Is there a Hit opportunity behind this destination for `player`? Computed
// from the ACTUAL field AP_HitSelectFieldPure would seat, so pad routing and the
// loaded field can never diverge: the opportunity is the first seated
// non-player target whose Hit location is present and unchecked. Defaults (0..7)
// AND guests (8..15) both count -- the apworld logic relies on default targets
// appearing on ordinary Trophy races, and the pad must keep offering the plain
// rerace while any of them is still unchecked. Returns that target id, or -1.
//
// `unchecked` is 0/1 per engine id: the seed carries the location AND the server
// has not checked it. A checked/absent location is no opportunity.
static inline int AP_HitOpportunityPure(const ctr_hit_candidates *cand,
                                        const unsigned char *eligible,
                                        const unsigned char *unchecked,
                                        int player)
{
	ap_hit_field field;
	int i;

	if (cand == NULL)
		return -1;

	AP_HitSelectFieldPure(cand, eligible, player, AP_HIT_FIELD_MAX, &field);
	for (i = 0; i < field.count; i++)
		if (field.ids[i] != player && unchecked[field.ids[i]])
			return field.ids[i];
	return -1;
}

// The EFFECTIVE player engine id at a physical pad: a met racer lock OVERRIDES
// the chosen racer, because AH_WarpPad.c calls AP_RacerLock_ForceForWarp
// immediately before the load reads characterIDs[0]. Predicting the roster from
// the chosen racer alone would seat the locked racer as their own opponent and
// could advertise a guest the lock is about to become.
static inline int AP_HitEffectivePlayerPure(int chosen, int lock, int lockMet)
{
	if (lock >= 0 && lockMet)
		return lock;
	return chosen;
}

// Does the seeded encounter roster apply to this load? Single-player ordinary
// Adventure Trophy races only: the sixteen retail tracks (0..15) and the two
// trial Trophy tracks (16/17). `isAdventure` is ADVENTURE_MODE (the persistent
// adventure flag, NOT the hub-only ADVENTURE_ARENA); `numPlayers` is the pending
// player count (1 = single-player). Cups (ticket 11), boss, arcade,
// relic/token/crystal and multiplayer loads return 0.
static inline int AP_HitOrdinaryAppliesPure(int isAdventure, int destLevelID,
                                            int isCup, int isBoss, int isArcade,
                                            int isRelic, int isToken, int isCrystal,
                                            int numPlayers)
{
	if (!isAdventure)
		return 0;
	if (destLevelID < 0 || destLevelID > AP_HIT_ORDINARY_TRACK_MAX)
		return 0;
	if (isCup || isBoss || isArcade || isRelic || isToken || isCrystal)
		return 0;
	if (numPlayers != 1)
		return 0;
	return 1;
}

// Can this ordinary destination host a Hit opportunity? Trials (16/17) require
// a valid trial row: an invalid row has no Trophy route, so a Hit opportunity
// there would keep the pad open with no way to earn the check (item D). Cup ids
// are excluded (ticket 11). `trialValid` is the trial_track_valid row for the
// destination's trial (ignored for 0..15).
static inline int AP_HitPadDestEligiblePure(int destLevelID, int trialValid)
{
	if (destLevelID < 0 || destLevelID > AP_HIT_ORDINARY_TRACK_MAX)
		return 0;
	if (destLevelID >= 16 && !trialValid)
		return 0;
	return 1;
}

// Is this the kind of live race a Hit check may be awarded in? Adventure
// ordinary races AND Adventure boss races. A boss Hit check is reachable at the
// boss race whether or not the boss is cleared (the boss clear only unlocks its
// ordinary appearances), and the apworld logic relies on that route. Cup (ticket
// 11), time trial, arcade, battle, relic, token and crystal are rejected.
static inline int AP_HitRaceSupportedPure(int isAdventure, int isBoss, int isCup,
                                          int isTimeTrial, int isArcade, int isBattle,
                                          int isRelic, int isToken, int isCrystal)
{
	(void)isBoss; // boss races are supported
	return isAdventure && !isCup && !isTimeTrial && !isArcade &&
	       !isBattle && !isRelic && !isToken && !isCrystal;
}

// Did BOTS_ChangeState actually APPLY a new damage effect on this call? Type 1
// while the victim was already damage-active applies nothing (the inner block is
// skipped) and must not award; type 4 while already spinning still applies burn
// and must award. Types 2 and 3 always apply their state. Follow the actual
// branch effects rather than the return value, which is 1 in all of these.
static inline int AP_HitEffectAppliedPure(int damageType, int wasDamageActive)
{
	if (damageType == 1)
		return wasDamageActive ? 0 : 1;
	if (damageType >= 2 && damageType <= 4)
		return 1;
	return 0;
}

// May a BOTS_ChangeState event earn a Hit check? Pure attribution/range gate:
//   damageType      1..4 only (0/5/unknown, and the mask-grab state, are rejected)
//   victimEngineID  0..15
//   victimIsAI      victim has ACTION_BOT
//   victimLive      victim is a live driver in the current field (not a ghost)
//   victimIsGhost   victim's model is DYNAMIC_GHOST
//   victimIsPlayer  victim is the local P1
//   attackerPresent attacker != NULL (rejects environmental / unattributed)
//   attackerIsLocalP1 attacker == the current local P1
//   attackerIsAI    attacker has ACTION_BOT (rejects AI-on-AI)
static inline int AP_HitDamageAcceptedPure(int damageType, int victimEngineID,
                                           int victimIsAI, int victimLive,
                                           int victimIsGhost, int victimIsPlayer,
                                           int attackerPresent, int attackerIsLocalP1,
                                           int attackerIsAI)
{
	if (damageType < 1 || damageType > 4)
		return 0;
	if (victimEngineID < 0 || victimEngineID >= CTR_CFG_HIT_CHARACTER_COUNT)
		return 0;
	if (!victimIsAI || !victimLive || victimIsGhost || victimIsPlayer)
		return 0;
	if (!attackerPresent || !attackerIsLocalP1 || attackerIsAI)
		return 0;
	return 1;
}

// Plan the extra high-LOD models an ordinary load must queue for the selected
// field. An opponent whose model the player's arcade pack already guarantees
// (the LOAD_Robots1P stock set, plus the player's own model) needs nothing; any
// other selected id needs a BI_RACERMODELHI extra. Writes at most `cap` ids and
// returns the number NEEDED (which may exceed `cap`, so the caller can refuse a
// required load it cannot fit rather than silently drop it).
static inline int AP_HitExtrasPlanPure(const int *selected, int selectedCount,
                                       const int *stock, int stockCount,
                                       int player, int *outExtras, int cap)
{
	int i, j;
	int n = 0;

	for (i = 0; i < selectedCount; i++)
	{
		int id = selected[i];
		int have = 0;
		if (id == player)
			continue;
		for (j = 0; j < stockCount; j++)
			if (stock[j] == id)
				have = 1;
		if (have)
			continue;
		if (n < cap)
			outExtras[n] = id;
		n++;
	}
	return n;
}

// Per-load required-extra state. The loader resets it at EVERY load entry (not
// just the slice branch), records the extras it queued, and validates their
// models after conversion and before birth. A stale count would validate a
// previous load's ids against this load's models and can trigger a false fatal.
typedef struct
{
	int count;
	int ids[3];
} ap_hit_extras_state;

static inline void AP_HitExtrasResetPure(ap_hit_extras_state *s)
{
	s->count = 0;
}

// Record `n` required ids (bounded by the three driverModelExtras slots).
static inline void AP_HitExtrasRecordPure(ap_hit_extras_state *s, const int *ids, int n)
{
	int i;
	if (n < 0)
		n = 0;
	if (n > 3)
		n = 3;
	s->count = n;
	for (i = 0; i < n; i++)
		s->ids[i] = ids[i];
}

// Index of the first recorded extra whose model pointer is NULL, or -1 when all
// are present. `models` is indexed parallel to the driverModelExtras slots.
static inline int AP_HitExtrasFirstMissingPure(const ap_hit_extras_state *s,
                                               const void *const *models)
{
	int i;
	for (i = 0; i < s->count; i++)
		if (models[i] == NULL)
			return i;
	return -1;
}

// Does the load queue have room for `needed` more entries? LOAD_AppendQueue
// silently drops anything past its eight slots, so a required extra must be
// capacity-checked before it is queued.
static inline int AP_HitQueueFitsPure(int queueLength, int needed, int limit)
{
	if (needed < 0)
		return 0;
	return queueLength + needed <= limit;
}

// How the ordinary Hit chooser routes this pad:
//   NORMAL  no Hit opportunity -> the existing tier-2 routing is unchanged
//   MENU    a Hit opportunity AND a CTR/Relic route -> offer the chooser
//   PLAIN   the Hit opportunity is the ONLY remaining check -> plain rerace
enum
{
	AP_HIT_ORDINARY_NORMAL = 0,
	AP_HIT_ORDINARY_MENU,
	AP_HIT_ORDINARY_PLAIN
};

static inline int AP_HitOrdinaryRoutePure(int hasHitOpportunity, int tokenLeft,
                                          int relicLeft)
{
	if (!hasHitOpportunity)
		return AP_HIT_ORDINARY_NORMAL;
	if (tokenLeft || relicLeft)
		return AP_HIT_ORDINARY_MENU;
	return AP_HIT_ORDINARY_PLAIN;
}

// Encode a chooser selection into the mode and pending bits. `route` is the row
// choice (0 Trophy, 1 CTR, 2 Relic); `tokenBit`/`relicBit` are the engine's
// gameMode bit values (passed in so this header stays freestanding).
//
// MainMain resolves a load as `(gameMode | AddBits) & ~RemBits`, so the chosen
// route must ADD its bit and clear its Rem; a route that only set gameMode was
// stripped straight back to Trophy (item 3). Trophy clears both. The engine
// assigns the outputs verbatim.
static inline void AP_HitChooserApplyPure(int route, unsigned tokenBit, unsigned relicBit,
                                          unsigned curGm1, unsigned curGm2,
                                          unsigned curAdd0, unsigned curRem0,
                                          unsigned curAdd8, unsigned curRem8,
                                          unsigned *outGm1, unsigned *outGm2,
                                          unsigned *outAdd0, unsigned *outRem0,
                                          unsigned *outAdd8, unsigned *outRem8)
{
	unsigned gm1 = curGm1 & ~relicBit;
	unsigned gm2 = curGm2 & ~tokenBit;
	unsigned add0 = curAdd0 & ~relicBit;
	unsigned add8 = curAdd8 & ~tokenBit;
	unsigned rem0 = curRem0 | relicBit;
	unsigned rem8 = curRem8 | tokenBit;

	if (route == 1)
	{
		gm2 |= tokenBit;
		rem8 &= ~tokenBit;
		add8 |= tokenBit;
	}
	else if (route == 2)
	{
		gm1 |= relicBit;
		rem0 &= ~relicBit;
		add0 |= relicBit;
	}
	// route 0 (Trophy): both routes stay cleared.

	*outGm1 = gm1;
	*outGm2 = gm2;
	*outAdd0 = add0;
	*outRem0 = rem0;
	*outAdd8 = add8;
	*outRem8 = rem8;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CTR_AP
#endif // AP_HIT_POLICY_H
