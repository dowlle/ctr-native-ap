#ifndef AP_HIT_POLICY_H
#define AP_HIT_POLICY_H

// Freestanding decision logic for the Hit Character encounter feature (global
// schema 16, block schema 2: the pool draw). Pulled out of the engine glue (ap/ap_hit_encounter.c) so a
// host harness can pin every ordering / exclusion / attribution rule without
// linking the engine, exactly the way ap_pad_state.h splits the pad decision
// from its gather.
//
// NOTHING here reads the parsed config, checked state or engine globals: the
// caller supplies every fact. ap_hit_encounter.c is the gather; this header is
// the decision. Both sides compile the same header, so the harness and the game
// cannot drift.

#include "ap_seedcfg.h" // ctr_hit_order / CTR_CFG_HIT_* (guarded by CTR_AP)

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

// A drawn field: the AI engine ids in seat order (seat 0 first). `unhit` is how
// many leading seats are unlocked guests with an unchecked Hit, `other` how many
// seats follow them from the other non-stock ids; the rest are stock ids.
typedef struct
{
	int ids[AP_HIT_FIELD_MAX];
	int count;
	int unhit;
	int other;
} ap_hit_field;

// The three draw cursors of one destination: positions (0..15) in its order of
// the last id taken from each pool. AP_HIT_CURSOR_INIT makes the first walk
// start at position 0.
#define AP_HIT_CURSOR_INIT 15
typedef struct
{
	int unhit; // unlocked guests with an unchecked Hit
	int other; // other non-stock ids (checked guests; Pura for a guest player)
	int stock; // the seven ids the player's arcade pack carries
} ap_hit_cursors;

static inline void AP_HitCursorsResetPure(ap_hit_cursors *c)
{
	c->unhit = AP_HIT_CURSOR_INIT;
	c->other = AP_HIT_CURSOR_INIT;
	c->stock = AP_HIT_CURSOR_INIT;
}

// The seven ids the player's arcade pack guarantees: exactly what LOAD_Robots1P
// writes, counting up from 0 and skipping the player. A default player gets the
// seven other defaults; a guest player gets 0..6 (Pura, 7, then needs an extra).
static inline void AP_HitStockPure(int player, int *out7)
{
	int i;
	int next = 0;
	for (i = 0; i < 7; i++)
	{
		if (next == player)
			next++;
		out7[i] = next++;
	}
}

static inline int AP_HitInStockPure(int id, int player)
{
	// 0..7 minus the player, truncated to seven ids: for a default player that
	// is every other default, for a guest player it is 0..6.
	if (id < 0 || id > 7 || id == player)
		return 0;
	if (player < 0 || player > 7)
		return id <= 6;
	return 1;
}

// Pool membership for one walk. Kept as a small enum instead of a callback so
// the header stays plain C and the harness can pin each pool.
enum
{
	AP_HIT_POOL_UNHIT = 0, // eligible guest, not the player, Hit unchecked
	AP_HIT_POOL_OTHER,     // eligible non-stock id, not the player, not taken
	AP_HIT_POOL_STOCK      // stock id, not taken
};

static inline int AP_HitPoolMemberPure(int pool, int id, int player,
                                       const unsigned char *eligible,
                                       const unsigned char *unchecked,
                                       const unsigned char *taken)
{
	if (id < 0 || id >= CTR_CFG_HIT_CHARACTER_COUNT || id == player || taken[id])
		return 0;
	if (pool == AP_HIT_POOL_UNHIT)
		return id >= 8 && eligible[id] && unchecked[id];
	if (pool == AP_HIT_POOL_OTHER)
		return !AP_HitInStockPure(id, player) && eligible[id];
	return AP_HitInStockPure(id, player);
}

// Walk `order` cyclically from position *cursor + 1 (sixteen steps at most) and
// append up to `want` pool members to out->ids, marking them taken. *cursor
// becomes the position of the last id taken (unchanged when none was). Returns
// the number taken.
static inline int AP_HitTakePure(const int *order, int pool, int player,
                                 const unsigned char *eligible,
                                 const unsigned char *unchecked,
                                 unsigned char *taken, int *cursor, int want,
                                 ap_hit_field *out)
{
	int step;
	int got = 0;
	int start = *cursor;

	if (start < 0 || start > 15)
		start = AP_HIT_CURSOR_INIT;
	for (step = 1; step <= 16 && got < want; step++)
	{
		int pos = (start + step) % 16;
		int id = order[pos];
		if (!AP_HitPoolMemberPure(pool, id, player, eligible, unchecked, taken))
			continue;
		taken[id] = 1;
		out->ids[out->count++] = id;
		*cursor = pos;
		got++;
	}
	return got;
}

// Draw one fresh ordinary or cup field ("unhit_first_rotation", block schema 2).
//
//   order     the destination's seeded permutation of 0..15 (wire, verbatim)
//   eligible  eligible[16]: defaults always 1, guests once an unlock win is
//             checked (AP_HitGuestEligiblePure)
//   unchecked unchecked[16]: the Hit location is in the seed and not checked
//   player    the EFFECTIVE player engine id, never seated
//   aiSeats   7 ordinary, 4 retail Purple cup
//   cur       in/out: the destination's cursors
//
// Seats, in order: up to three unlocked guests with an unchecked Hit; then, in
// the extra-model slots those leave free, other unlocked non-stock ids (checked
// guests, and Pura when the player is a guest); then stock ids until the field
// is full. Every pool rotates through the order, so each member is seated
// within a bounded number of fresh draws (guarantees G1 to G5, see the
// apworld's hit_character module). Never the player, never a duplicate, at most
// three non-stock ids, always exactly aiSeats opponents. Returns the count.
static inline int AP_HitDrawFieldPure(const int *order,
                                      const unsigned char *eligible,
                                      const unsigned char *unchecked,
                                      int player, int aiSeats,
                                      ap_hit_cursors *cur, ap_hit_field *out)
{
	unsigned char taken[CTR_CFG_HIT_CHARACTER_COUNT] = {0};
	int want;

	out->count = 0;
	out->unhit = 0;
	out->other = 0;
	if (order == NULL || aiSeats <= 0)
		return 0;
	if (aiSeats > AP_HIT_FIELD_MAX)
		aiSeats = AP_HIT_FIELD_MAX;

	want = aiSeats < CTR_CFG_HIT_MAX_GUESTS ? aiSeats : CTR_CFG_HIT_MAX_GUESTS;
	out->unhit = AP_HitTakePure(order, AP_HIT_POOL_UNHIT, player, eligible, unchecked,
	                            taken, &cur->unhit, want, out);

	want = CTR_CFG_HIT_MAX_GUESTS - out->unhit;
	if (want > aiSeats - out->count)
		want = aiSeats - out->count;
	out->other = AP_HitTakePure(order, AP_HIT_POOL_OTHER, player, eligible, unchecked,
	                            taken, &cur->other, want, out);

	AP_HitTakePure(order, AP_HIT_POOL_STOCK, player, eligible, unchecked, taken,
	               &cur->stock, aiSeats - out->count, out);
	return out->count;
}

// Is a guest eligible from its two facts alone? `triggerMet` is the parsed
// any_of list reduced to a 0/1 fact by the gather; `fallbackKeys` is the parsed
// block schema 3 fallback (0 = none, else 1..4) and `heldKeys` the received Key
// count. A trigger win still unlocks a guest at zero Keys, and a fallback guest
// unlocks at its count without any win. Both being live at once is a malformed
// seed the parser/admission refuses, so this only has to be monotone in Keys.
static inline int AP_HitGuestFallbackEligiblePure(int triggerMet, int fallbackKeys,
                                                  int heldKeys)
{
	if (triggerMet)
		return 1;
	if (fallbackKeys <= 0)
		return 0;
	return heldKeys >= fallbackKeys ? 1 : 0;
}

// Is `guest` (engine id 0..15) allowed to appear as an encounter? Defaults
// (0..7) always are. A guest (8..15) needs one authoritative trigger win checked
// (`triggerMet` is the parsed any_of list reduced to a 0/1 fact by the gather),
// or its Key fallback met (`fallbackKeys` is indexed the same way, 0 where the
// guest has no fallback).
static inline int AP_HitGuestEligiblePure(int guest, const unsigned char *triggerMet,
                                          const int *fallbackKeys, int heldKeys)
{
	if (guest < 0 || guest >= CTR_CFG_HIT_CHARACTER_COUNT)
		return 0;
	if (guest < 8)
		return 1;
	return AP_HitGuestFallbackEligiblePure(triggerMet[guest] ? 1 : 0,
	                                       fallbackKeys != NULL ? fallbackKeys[guest] : 0,
	                                       heldKeys);
}

// Admission consistency (block schema 3): `fallback_keys` means "this guest has
// no unlock win in this seed", so a fallback set while ANY of that guest's
// any_of locations exists in the connected room is a malformed block -- the two
// halves disagree about the seed and the apworld is the single authority.
// `fallbackKeys` and `anyExists` are indexed by guest - 8 over the eight guest
// entries. Returns the offending guest engine id (8..15), or -1.
static inline int AP_HitFallbackConflictPure(const int *fallbackKeys,
                                             const unsigned char *anyExists)
{
	int i;
	if (fallbackKeys == NULL || anyExists == NULL)
		return -1;
	for (i = 0; i < CTR_CFG_HIT_TRIGGER_COUNT; i++)
		if (fallbackKeys[i] > 0 && anyExists[i])
			return 8 + i;
	return -1;
}

// Is there a Hit opportunity for `player`? Under the pool draw every eligible
// target can be seated at every supported destination (it rotates in within a
// bounded number of races), so the opportunity is the lowest engine id other
// than the player that is eligible and whose Hit is present and unchecked.
// Defaults (always eligible) AND unlocked guests count: the apworld logic relies
// on both appearing on ordinary Trophy races, and a won pad must keep offering
// the plain rerace while any of them is still unchecked (ruling 4, 2026-09-14).
// Returns that target id, or -1.
static inline int AP_HitOpportunityPure(const unsigned char *eligible,
                                        const unsigned char *unchecked,
                                        int player)
{
	int i;
	for (i = 0; i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
		if (i != player && eligible[i] && unchecked[i])
			return i;
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
// ordinary races, Adventure boss races AND Adventure Gem Cups (cups are extra
// opportunities per the contract). A boss Hit check is reachable at the boss
// race whether or not the boss is cleared; a cup Hit uses the frozen cup roster.
// Time trial, arcade, battle, relic, token and crystal are rejected.
static inline int AP_HitRaceSupportedPure(int isAdventure, int isBoss, int isCup,
                                          int isTimeTrial, int isArcade, int isBattle,
                                          int isRelic, int isToken, int isCrystal)
{
	(void)isBoss; // boss races are supported
	(void)isCup;  // Gem Cup races are supported
	return isAdventure && !isTimeTrial && !isArcade &&
	       !isBattle && !isRelic && !isToken && !isCrystal;
}

// ── Gem Cup roster snapshot (ticket 11) ─────────────────────────────────────
//
// A cup resolves its opponent roster ONCE at the pad entry and reuses it for
// every leg and same-session retry. Exiting/abandoning and starting a new cup
// resolves fresh, so an unlock mid-cup enters the next cup, not the active one.
// The snapshot is keyed by cupID; `pending` is set by the pad entry (a NEW cup)
// and cleared when the roster is resolved.

typedef struct
{
	int valid;
	int pending;     // a new cup start was signalled (pad entry)
	int cupID;       // 0..4 (wire cups block keys are 100 + cupID)
	int trackIndex;  // last leg seen, for continue/retry classification
	int count;
	int ids[AP_HIT_FIELD_MAX];
} ap_hit_cup_snapshot;

// How a cup load relates to the stored snapshot.
enum
{
	AP_HIT_CUP_LEG_NEW = 0,  // new cup start -> resolve fresh
	AP_HIT_CUP_LEG_CONTINUE, // a later leg -> reuse the snapshot
	AP_HIT_CUP_LEG_RETRY     // the same leg again -> reuse the snapshot
};

static inline void AP_HitCupSnapshotResetPure(ap_hit_cup_snapshot *s)
{
	s->valid = 0;
	s->pending = 0;
	s->cupID = -1;
	s->trackIndex = -1;
	s->count = 0;
}

// Called at the cup pad entry: the next cup load is a NEW cup.
static inline void AP_HitCupSnapshotBeginPure(ap_hit_cup_snapshot *s, int cupID)
{
	s->pending = 1;
	s->cupID = cupID;
	s->trackIndex = 0;
}

static inline int AP_HitCupLegKindPure(int cupID, int trackIndex, int pending,
                                       int snapshotValid, int snapshotCupID,
                                       int snapshotTrackIndex)
{
	if (pending || !snapshotValid || snapshotCupID != cupID)
		return AP_HIT_CUP_LEG_NEW;
	if (trackIndex == snapshotTrackIndex)
		return AP_HIT_CUP_LEG_RETRY;
	return AP_HIT_CUP_LEG_CONTINUE;
}

// AI seat count for a cup: four in the retail Purple cup (cupID 4), seven
// otherwise, matching MainInit's field count.
static inline int AP_HitCupFieldSizePure(int cupID)
{
	return (cupID == 4) ? 4 : 7;
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
