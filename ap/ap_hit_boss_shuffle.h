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
//                 final record, 15 without one);
//   any_of[g]     the guest's retail track wins (N. Tropy, Penta, Fake Crash)
//                 in retail order, then every boss code drawn as g, ascending;
//   kind[g]       boss if any_of holds a boss code, else track if it holds a
//                 track win, else the retail kind (boss), with any_of empty;
//   fallback_keys the content-plan table for every guest.
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
	tw = AP_HIT_GUEST_TRACK_WINS[guest - AP_HIT_GUEST_FIRST];
	for (k = 0; k < 2; k++)
		if (tw[k] > 0)
		{
			anyOf[n++] = tw[k];
			hasTrack = 1;
		}
	for (k = 0; k < AP_HIT_BOSS_CODES; k++)
		if (ident[k] == guest)
		{
			anyOf[n++] = AP_HIT_BOSS_WIN_CODES[k];
			hasBoss = 1;
		}
	*kindBoss = hasBoss ? 1 : (hasTrack ? 0 : 1);
	return n;
}

#endif
