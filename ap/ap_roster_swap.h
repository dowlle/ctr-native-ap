#ifndef AP_ROSTER_SWAP_H
#define AP_ROSTER_SWAP_H

// The roster swap (ruled 2026-10-30 and 2026-11-01): one rule for three
// features, freestanding so tools/test-roster-swap.c runs the same code.
//
// Guest racers with random bosses: "the slots should be changed out with the
// bosses". A racer drawn as a boss leaves the opponent pool until the player
// beats them or the garage check is collected (Collect counts), and an original
// boss raced in no garage races from the start.
//
// Purple Gem Cup gauntlet: a leg's drawn racer sits in the field for that leg
// and fires the kit. Chaos Final Challenge: the seed's five bosses are the field.
//
// Three extra models is the ordinary limit. A chaos field seats five rivals of
// any kind, so it has its own larger plan.

#include "ap_hit_boss_shuffle.h"

#define AP_ROSTER_CHARACTERS 16
#define AP_ROSTER_FIRST_GUEST 8
#define AP_ROSTER_CHAOS_RIVALS 5
#define AP_ROSTER_STOCK 7

// Which boss win codes seat `racer`, as a mask over AP_HIT_BOSS_WIN_CODES.
static inline unsigned AP_RosterBossMask(const int ident[AP_HIT_BOSS_CODES], int racer)
{
	unsigned mask = 0;
	int k;
	for (k = 0; k < AP_HIT_BOSS_CODES; k++)
		if (ident[k] == racer)
			mask |= 1u << k;
	return mask;
}

// One of the five retail bosses?
static inline int AP_RosterOriginalBoss(int racer)
{
	return (racer >= 8 && racer <= 11) || racer == AP_BOSS_OXIDE_CHARACTER;
}

// A default racer's place in the pool: free unless drawn as a boss, then held
// until any of its boss wins is checked. `checked` is a mask over the codes.
static inline int AP_RosterDefaultEligible(const int ident[AP_HIT_BOSS_CODES], int racer, unsigned checked)
{
	unsigned mask = AP_RosterBossMask(ident, racer);
	return mask == 0 || (mask & checked) != 0;
}

// Eligibility with no Hit block, where the only guests in the pool are the
// ones the swap moves: an original boss raced nowhere, and a racer drawn as a
// boss once beaten. N. Tropy, Penta Penguin and Fake Crash keep to the stock
// field on such a seed unless the draw put them in a garage.
static inline int AP_RosterEligibleNoHit(const int ident[AP_HIT_BOSS_CODES], int racer, unsigned checked)
{
	unsigned mask;
	if (racer < 0 || racer >= AP_ROSTER_CHARACTERS)
		return 0;
	if (racer < AP_ROSTER_FIRST_GUEST)
		return AP_RosterDefaultEligible(ident, racer, checked);
	mask = AP_RosterBossMask(ident, racer);
	if (mask != 0)
		return (mask & checked) != 0;
	return AP_RosterOriginalBoss(racer);
}

// The gauntlet's seat. `ids` is the leg's field, the cup's frozen roster.
// Leaves it alone when the attacker already races, is the player (the first AI
// fires then) or the leg is not armed (attacker < 0). Otherwise the attacker
// takes a seat that keeps the field within `maxExtras` models the player's pack
// does not carry: the last stock seat, or with the extras full the last extra's
// seat. A stock attacker costs no model and takes the last seat. Returns the
// seat written, or -1.
static inline int AP_RosterSeatAttacker(int *ids, int count, int attacker, int player, int maxExtras,
                                        int (*inStock)(int id, int player))
{
	int i, extras = 0, seat = -1;

	if (attacker < 0 || attacker == player || count <= 0)
		return -1;
	for (i = 0; i < count; i++)
	{
		if (ids[i] == attacker)
			return -1;
		if (!inStock(ids[i], player))
			extras++;
	}
	if (inStock(attacker, player) || extras < maxExtras)
	{
		// Seats run unhit guests, other extras, stock: the last seat is the
		// one that costs a waiting Hit target least.
		seat = count - 1;
		if (!inStock(attacker, player))
			for (i = count - 1; i >= 0; i--)
				if (inStock(ids[i], player))
				{
					seat = i;
					break;
				}
	}
	else
		for (i = count - 1; i >= 0; i--)
			if (!inStock(ids[i], player))
			{
				seat = i;
				break;
			}
	if (seat >= 0)
		ids[seat] = attacker;
	return seat;
}

// The start grid of a short field. Retail fills kartSpawnOrderArray for eight
// karts: the Adventure pad gives P1 place 7 and drivers 1..7 the places 0..6.
// When the field could not fill, drivers past `karts` are never born, so their
// places stay empty: a gap on the grid, and before the lights go green the
// standings take each kart's rank from this array, so the HUD shows a blank
// portrait and P1 reads 8TH (A4-77, 2026-11-14). Renumber the places of the
// karts that race to 0..karts-1, keeping their order, so P1 stays last.
// Entries past `karts` are left alone. A tie keeps the lower driver first.
static inline void AP_RosterCompactSpawnOrder(unsigned char *order, int karts)
{
	unsigned char dense[8];
	int i, j;

	if (order == 0 || karts <= 0 || karts > 8)
		return;
	for (i = 0; i < karts; i++)
	{
		int below = 0;
		for (j = 0; j < karts; j++)
			if (order[j] < order[i] || (order[j] == order[i] && j < i))
				below++;
		dense[i] = (unsigned char)below;
	}
	for (i = 0; i < karts; i++)
		order[i] = dense[i];
}

// Is `racer` held by the swap: drawn as a boss and none of its boss wins
// checked yet? `checked` is a mask over the codes.
static inline int AP_RosterHeld(const int ident[AP_HIT_BOSS_CODES], int racer, unsigned checked)
{
	unsigned mask = AP_RosterBossMask(ident, racer);
	return mask != 0 && (mask & checked) == 0;
}

// Ordinary races always have eight karts (ruled 2026-11-14, A4-81). This
// replaces the short field of 2026-11-02: when the drawn field cannot fill
// `aiSeats`, the empty seats are filled here. Racers the pool allows come first
// (an eligible id not seated yet), then racers the swap holds behind their
// garages (`held`). Within each pass a stock id, which needs no model, comes
// before a non-stock id, and a non-stock id only while the field stays within
// `maxExtras` models the player's pack does not carry.
//
// The filler is chosen by walking the destination's seeded order from its
// first position. No cursor moves and no RNG is drawn, so the same pool on the
// same track picks the same filler, and every later draw is exactly what it
// was before. Never the player, never a duplicate. Returns the new count.
static inline int AP_RosterFillField(int *ids, int count, int aiSeats, const int *order,
                                     const unsigned char *eligible, const unsigned char *held,
                                     int player, int maxExtras, int (*inStock)(int id, int player))
{
	int pass, pos, i, extras = 0;

	if (ids == 0 || order == 0 || eligible == 0 || held == 0 || count < 0 || count >= aiSeats)
		return count < 0 ? 0 : count;
	for (i = 0; i < count; i++)
		if (!inStock(ids[i], player))
			extras++;
	// Pass 0: pool, stock. 1: pool, extra model. 2: held, stock. 3: held, extra.
	for (pass = 0; pass < 4 && count < aiSeats; pass++)
		for (pos = 0; pos < AP_ROSTER_CHARACTERS && count < aiSeats; pos++)
		{
			int id = order[pos], stock, seated = 0;
			if (id < 0 || id >= AP_ROSTER_CHARACTERS || id == player)
				continue;
			if (pass < 2 ? !eligible[id] : !held[id])
				continue;
			for (i = 0; i < count; i++)
				if (ids[i] == id)
					seated = 1;
			if (seated)
				continue;
			stock = inStock(id, player);
			if ((pass & 1) == 0 ? !stock : (stock || extras >= maxExtras))
				continue;
			if (!stock)
				extras++;
			ids[count++] = id;
		}
	return count;
}

// The chaos field: the final racer first, in the seat a boss race gives its
// opponent, then the four garages. Racers are pairwise distinct on the wire.
static inline void AP_RosterChaosLineup(const int ident[AP_HIT_BOSS_CODES], int out[AP_ROSTER_CHAOS_RIVALS])
{
	out[0] = ident[5];
	out[1] = ident[0];
	out[2] = ident[1];
	out[3] = ident[2];
	out[4] = ident[3];
}

// The chaos field as seated. The player may drive one of the five drawn
// racers, and the retail field never seats the player's own racer: there
// LOAD_Robots1P skips the player's id and takes the next. So a rival who is
// the player's racer gives the seat to the first id of the player's pack not
// in the lineup (a stock id, so no extra model). The stand-in races but is no
// boss: it takes no attack turn (AP_RosterChaosStandIn). Returns the stand-in's
// seat, or -1. Fixes 2026-11-02.
static inline int AP_RosterChaosSeat(const int lineup[AP_ROSTER_CHAOS_RIVALS], int player,
                                     int (*inStock)(int id, int player), int out[AP_ROSTER_CHAOS_RIVALS])
{
	int i, j, id, seat = -1;

	for (i = 0; i < AP_ROSTER_CHAOS_RIVALS; i++)
	{
		out[i] = lineup[i];
		if (lineup[i] == player)
			seat = i;
	}
	if (seat < 0)
		return -1;
	for (id = 0; id < AP_ROSTER_CHARACTERS; id++)
	{
		int used = 0;
		if (!inStock(id, player))
			continue;
		for (j = 0; j < AP_ROSTER_CHAOS_RIVALS; j++)
			if (lineup[j] == id)
				used = 1;
		if (!used)
		{
			out[seat] = id;
			return seat;
		}
	}
	return -1;
}

// Is the kart in chaos seat `seat` a stand-in for the player's racer? It then
// takes no turn on the shared attack schedule.
static inline int AP_RosterChaosStandIn(const int lineup[AP_ROSTER_CHAOS_RIVALS], int seat, int seatedId)
{
	return seat >= 0 && seat < AP_ROSTER_CHAOS_RIVALS && seatedId != lineup[seat];
}

// The seat whose kart paces the shared attack schedule: the final racer's,
// or the first drawn rival's when a stand-in holds it. `seated` is the field
// as AP_RosterChaosSeat wrote it.
static inline int AP_RosterChaosPaceSeat(const int lineup[AP_ROSTER_CHAOS_RIVALS],
                                         const int seated[AP_ROSTER_CHAOS_RIVALS])
{
	int i;
	for (i = 0; i < AP_ROSTER_CHAOS_RIVALS; i++)
		if (!AP_RosterChaosStandIn(lineup, i, seated[i]))
			return i;
	return 0;
}

// The chaos field's own models: every rival neither the player nor in the
// player's pack. Five at most, never above `cap`; the caller has room for all.
static inline int AP_RosterChaosExtras(const int lineup[AP_ROSTER_CHAOS_RIVALS], int player,
                                       int (*inStock)(int id, int player), int *out, int cap)
{
	int i, n = 0;
	for (i = 0; i < AP_ROSTER_CHAOS_RIVALS; i++)
		if (lineup[i] != player && !inStock(lineup[i], player) && n < cap)
			out[n++] = lineup[i];
	return n;
}

// Whose turn on the shared attack schedule: the first rival from `cursor` that
// can fire, or -1. A rival held by damage, the clock or the finish loses its
// turn to the next one, so the schedule never stalls on one kart.
static inline int AP_RosterChaosOwner(int cursor, const unsigned char *canFire, int count)
{
	int step;
	if (count <= 0)
		return -1;
	if (cursor < 0 || cursor >= count)
		cursor = 0;
	for (step = 0; step < count; step++)
	{
		int i = (cursor + step) % count;
		if (canFire[i])
			return i;
	}
	return -1;
}

#endif
