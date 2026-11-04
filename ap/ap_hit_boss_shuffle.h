#ifndef AP_HIT_BOSS_SHUFFLE_H
#define AP_HIT_BOSS_SHUFFLE_H

// Hit Character checks under boss opponent shuffle (J03, decision 9 of
// 2026-10-28). A Hit check's boss-race route follows the racer the seed drew
// for that garage, and beating a garage unlocks the drawn racer as an
// opponent in other races (never as a playable racer). So with the opponent
// switch on, or a final record whose racer is not N. Oxide, and an identity
// that differs from the retail one, the apworld emits
// `hit_character_encounters` with:
//
//   bosses[code]  the drawn engine id per boss-win code (garages 35011100 to
//                 35011103, both Oxide races 35011104 and 35011105 from the
//                 final record, 15 without one); the triggers below read
//                 AP_HitBossSeats, where a code raced nowhere seats nobody;
//   any_of[g]     every boss code drawn as g, ascending: a drawn boss waits
//                 for that race, whatever its retail track wins say. A guest
//                 drawn nowhere keeps its retail track wins (N. Tropy, Penta,
//                 Fake Crash) in retail order, and an original boss drawn
//                 nowhere has none: it swapped slots and races from the start
//                 (ruled 2026-10-30);
//   kind[g]       boss if any_of holds a boss code, else track if it holds a
//                 track win, else the retail kind (boss), with any_of empty;
//   fallback_keys the content-plan table for a guest whose any_of is not empty
//                 and not in the seed.
//
// A default racer drawn as a boss has no trigger on the wire: ap_roster_swap.h
// reads it from `bosses`.
//
// Native derives the same tables from the parsed boss record and the Hit
// parser compares them exactly. Freestanding: tools/test-hit-boss-shuffle.cpp
// pins it. Dispatch needs nothing: the boss race already credits
// data.characterIDs[1], the drawn racer.

#include "ap_boss_venue.h"

#define AP_HIT_BOSS_CODES 6
#define AP_HIT_GUEST_FIRST 8
#define AP_HIT_GUESTS 8
#define AP_HIT_TRIGGER_WINS_MAX 8

static const long AP_HIT_BOSS_WIN_CODES[AP_HIT_BOSS_CODES] = {
	35011100L, 35011101L, 35011102L, 35011103L, 35011104L, 35011105L,
};

// Retail track wins per guest 8..15 (only N. Tropy, Penta and Fake Crash).
static const long AP_HIT_GUEST_TRACK_WINS[AP_HIT_GUESTS][2] = {
	{0, 0}, {0, 0}, {0, 0}, {0, 0},
	{35016200L, 35016201L}, // 12 N. Tropy
	{35011008L, 35011010L}, // 13 Penta Penguin
	{35011000L, 35011003L}, // 14 Fake Crash
	{0, 0},                 // 15 Nitros Oxide
};

// The engine id each boss-win code races in the retail game.
static const int AP_HIT_BOSS_RETAIL_IDENTITY[AP_HIT_BOSS_CODES] = {10, 9, 11, 8, 15, 15};

static inline void AP_HitBossIdentity(const AP_BossEncounters *e, int out[AP_HIT_BOSS_CODES]);

// Is the Hit block in its boss-shuffled shape for this boss record? Only when
// the drawn identity differs from the retail one: an opponent draw that put
// the four retail bosses back in their own garages stays canonical (the
// apworld's hit_character.identity_shuffled is the twin).
static inline int AP_HitBossShuffleActive(const AP_BossEncounters *e)
{
	int ident[AP_HIT_BOSS_CODES], i;
	if (e == 0 || e->state != AP_BOSS_ENC_VALID || e->version != AP_BOSS_ENCOUNTERS_VERSION_2)
		return 0;
	AP_HitBossIdentity(e, ident);
	for (i = 0; i < AP_HIT_BOSS_CODES; i++)
		if (ident[i] != AP_HIT_BOSS_RETAIL_IDENTITY[i])
			return 1;
	return 0;
}

// The engine id each boss-win code races, in AP_HIT_BOSS_WIN_CODES order.
static inline void AP_HitBossIdentity(const AP_BossEncounters *e, int out[AP_HIT_BOSS_CODES])
{
	int g;
	for (g = 0; g < AP_BOSS_VENUE_GARAGES; g++)
		out[g] = e->opponent[g];
	out[4] = out[5] = e->finalPresent ? e->finalOpponent : AP_BOSS_OXIDE_CHARACTER;
}

// Who each boss-win code actually seats, for the unlock triggers and the
// roster swap: the identity, except that with the shuffle active a code the
// seed races nowhere (N. Oxide's two, with his races disabled) seats nobody
// (-1). So N. Oxide with his races disabled swaps slots like any original boss
// raced nowhere and races from the start (ruled 2026-10-30: the fixed Key
// fallback goes on seeds with random bosses). The wire's `bosses` table stays
// the identity. The apworld's content_bosses.seats is the twin.
static inline void AP_HitBossSeats(const AP_BossEncounters *e, int out[AP_HIT_BOSS_CODES])
{
	AP_HitBossIdentity(e, out);
	if (AP_HitBossShuffleActive(e) && e->oxideDisabled)
		out[4] = out[5] = -1;
}

// Guest `guest` (8..15): writes its expected any_of (at most
// AP_HIT_TRIGGER_WINS_MAX codes) and returns the count; *kindBoss is 1 for
// "boss", 0 for "track".
static inline int AP_HitBossGuestTrigger(const int ident[AP_HIT_BOSS_CODES], int guest, long *anyOf,
                                         int *kindBoss)
{
	int n = 0, k, hasBoss = 0, hasTrack = 0;
	const long *tw;
	*kindBoss = 1;
	if (guest < AP_HIT_GUEST_FIRST || guest >= AP_HIT_GUEST_FIRST + AP_HIT_GUESTS)
		return 0;
	for (k = 0; k < AP_HIT_BOSS_CODES; k++)
		if (ident[k] == guest)
		{
			anyOf[n++] = AP_HIT_BOSS_WIN_CODES[k];
			hasBoss = 1;
		}
	if (!hasBoss)
	{
		tw = AP_HIT_GUEST_TRACK_WINS[guest - AP_HIT_GUEST_FIRST];
		for (k = 0; k < 2; k++)
			if (tw[k] > 0)
			{
				anyOf[n++] = tw[k];
				hasTrack = 1;
			}
	}
	*kindBoss = hasBoss ? 1 : (hasTrack ? 0 : 1);
	return n;
}

#endif
