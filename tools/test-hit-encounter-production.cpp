// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include
//     tools/test-hit-encounter-production.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-hit-encounter-production && /tmp/test-hit-encounter-production
//
// Production coverage for the Hit Character encounter GATHER (ticket 06). The
// real parser (ap/ap_seedcfg.cpp) and the real gather (ap/ap_hit_encounter.c)
// are compiled in; only the network state and the check-send sink are stubbed.
// This is the layer that cannot be reached by the freestanding policy harness:
// trigger-to-next-load eligibility reconstruction from checked state, the pool
// draw for all sixteen player choices (block schema 2), the ordinary race
// snapshot (retry reuse, fresh after any other load), cursor persistence across
// a same-seed reconnect, stock/extra planning (three guests, three extras),
// generic victim dispatch, negative attribution and repeated/reconnect dedup.
//
// Build + run (from the repo root):
//   g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include \
//       -I ap/vendor/json/include tools/test-hit-encounter-production.cpp \
//       ap/ap_seedcfg.cpp -o /tmp/test-hit-encounter-production && \
//       /tmp/test-hit-encounter-production

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

// ── stubbed network / sink ─────────────────────────────────────────────────
static std::set<long long> g_checked;
static int g_emitCount;
static long g_lastEmit;

extern "C" int ap_net_location_checked(long long code)
{
	return g_checked.count(code) ? 1 : 0;
}

extern "C" int ap_net_location_exists(long long code)
{
	// Every canonical Hit location is present this seed; anything checked is
	// present by definition.
	if (code >= 35025000LL && code <= 35025015LL)
		return 1;
	return g_checked.count(code) ? 1 : 0;
}

// Held Keys as the gather reads them: the RECEIVED Key item count
// (AP_GateCount(AP_IDX_KEY)), which drives the block schema 3 Key fallback.
static int g_heldKeys;

extern "C" int AP_GateCount(int itemType)
{
	return itemType == 14 /* AP_IDX_KEY */ ? g_heldKeys : 0;
}

extern "C" int AP_EmitHitCharacterCheck(long code)
{
	g_emitCount++;
	g_lastEmit = code;
	return 1;
}

extern "C" void AP_LogLine(const char *msg)
{
	(void)msg;
}

// The real gather, compiled as C++ in this translation unit.
// Cup snapshot seed/slot identity (the gather references these through
// AP_HitEncounterConnectReset; these harnesses do not exercise reconnects).
extern "C" int ap_net_seed_name(char *buf, int n)
{
	std::snprintf(buf, n, "%s", "seed");
	return 1;
}

extern "C" int ap_net_slot_name(char *buf, int n)
{
	std::snprintf(buf, n, "%s", "slot");
	return 1;
}

#include "../ap/ap_hit_encounter.c"

// ── harness plumbing ───────────────────────────────────────────────────────
static nlohmann::json g_fixture;
static int g_checks;
static int g_failures;

static void expect(int ok, const char *what)
{
	g_checks++;
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static void expect_eq(long long got, long long want, const char *what)
{
	g_checks++;
	if (got != want)
	{
		std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
		g_failures++;
	}
}

// Flags for an accepted local-P1 hit on a live AI in a supported race.
static const unsigned kAccepted = 1u | 2u | 16u | 32u | 128u; // AI|live|attacker|localP1|race
// Type 4 while already damage-active still applies burn.
static const unsigned kAcceptedBurning = kAccepted | 256u;

static void reset_state(void)
{
	g_checked.clear();
	g_emitCount = 0;
	g_lastEmit = -1;
	g_heldKeys = 0;
	AP_HitEncounterConnectReset();
	AP_HitEncounterResetDrawState();
}

// One ordinary race load the way LOAD_DriverMPK drives it: a hub load first
// (so the race draws fresh), then the race load.
static int race_after_hub(int level, int player, int *ids)
{
	int fresh = -1;
	AP_HitLoadBegin(); // hub
	AP_HitLoadBegin(); // race
	int n = AP_HitRaceField(level, player, 7, ids, &fresh);
	expect_eq(fresh, 1, "a race after a hub load draws fresh");
	return n;
}

static bool field_has(const int *ids, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (ids[i] == id)
			return true;
	return false;
}

static void test_activation_and_candidates(void)
{
	ap_seedcfg_parse_json(g_fixture);
	expect(ap_seedcfg_hit_encounters() != NULL, "fixture activates encounters");
	expect_eq(AP_HitEncounterEnabled(), 1, "feature enabled");
	expect(AP_HitEncounterOrder(3) != NULL, "track 3 order");
	expect(AP_HitEncounterOrder(17) != NULL, "track 17 order");
	expect(AP_HitEncounterOrder(18) == NULL, "track 18 unsupported");
	expect(AP_HitEncounterOrder(100) != NULL, "cup 100 order");
	expect(AP_HitEncounterOrder(105) == NULL, "cup 105 unsupported");

	// Feature-off and unsupported-destination loads must not apply the roster.
	// Ticket 10: all ordinary tracks 0..15 AND the two trial Trophy tracks 16/17,
	// single-player ordinary Adventure Trophy only.
	expect_eq(AP_HitEncounterShouldApply(1, 3, 0, 0, 0, 0, 0, 0, 1), 1, "track 3 applies");
	expect_eq(AP_HitEncounterShouldApply(1, 0, 0, 0, 0, 0, 0, 0, 1), 1, "track 0 applies");
	expect_eq(AP_HitEncounterShouldApply(1, 16, 0, 0, 0, 0, 0, 0, 1), 1, "trial 16 applies");
	expect_eq(AP_HitEncounterShouldApply(1, 17, 0, 0, 0, 0, 0, 0, 1), 1, "trial 17 applies");
	expect_eq(AP_HitEncounterShouldApply(0, 3, 0, 0, 0, 0, 0, 0, 1), 0, "no adventure does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 3, 0, 0, 0, 0, 0, 0, 2), 0, "multiplayer does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 3, 1, 0, 0, 0, 0, 0, 1), 0, "cup does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 3, 0, 1, 0, 0, 0, 0, 1), 0, "boss does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 3, 0, 0, 1, 0, 0, 0, 1), 0, "arcade mode does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 3, 0, 0, 0, 1, 0, 0, 1), 0, "relic does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 3, 0, 0, 0, 0, 1, 0, 1), 0, "token does not apply");
	expect_eq(AP_HitEncounterShouldApply(1, 18, 0, 0, 0, 0, 0, 0, 1), 0, "arena 18 does not apply");

	// The corrected AI field bound.
	expect_eq(AP_HIT_FIELD_MAX, 7, "field max is seven AI seats");
}

static void test_trigger_to_next_load(void)
{
	int ids[AP_HIT_FIELD_MAX];

	reset_state();
	ap_seedcfg_parse_json(g_fixture);

	// Before either authoritative win: Fake Crash is not eligible, so Crash
	// Cove's field is defaults only. Default racers are Hit targets, so a
	// default opportunity still exists (correction A).
	expect_eq(AP_HitEncounterGuestEligible(14), 0, "guest ineligible before win");
	{
		int n = race_after_hub(3, 0, ids);
		expect_eq(n, 7, "pre-win field size");
		expect(!field_has(ids, n, 14), "pre-win field has no Fake Crash");
		for (int i = 0; i < n; i++)
			expect(ids[i] >= 1 && ids[i] <= 7, "pre-win field is the seven other defaults");
		expect_eq(AP_HitEncounterOpportunity(3, 0), 1, "pre-win opportunity is a default");
	}

	// Win Crash Cove (35011000) -> the NEXT load reconstructs eligibility and
	// seats Fake Crash first (G1), on Crash Cove AND on any other track.
	g_checked.insert(35011000);
	expect_eq(AP_HitEncounterGuestEligible(14), 1, "guest eligible after win");
	for (int level : {3, 0, 5, 13, 16})
	{
		int n = race_after_hub(level, 0, ids);
		expect_eq(n, 7, "post-win field size");
		expect_eq(ids[0], 14, "post-win field seats the unhit guest first, any track");
	}

	// The other authoritative win code also unlocks the guest.
	g_checked.clear();
	g_checked.insert(35011003);
	expect_eq(AP_HitEncounterGuestEligible(14), 1, "second trigger unlocks");

	// Once his Hit is checked, Fake Crash leaves the priority seat but stays in
	// the pool: he takes one of the free extra slots.
	g_checked.insert(35025014);
	{
		int n = race_after_hub(3, 0, ids);
		expect(field_has(ids, n, 14), "checked guest still races from the pool");
		expect_eq(AP_HitEncounterOpportunity(3, 0), 1, "opportunity moves to the lowest unchecked default");
	}
}

static void test_three_guests_three_extras(void)
{
	int ids[AP_HIT_FIELD_MAX];

	// Fake Crash, Penta and N. Tropy unlocked and unhit: all three seated, three
	// extra models, at every supported destination.
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	g_checked.insert(35011000);
	g_checked.insert(35011008);
	g_checked.insert(35016200);
	for (int level = 0; level <= 17; level++)
	{
		int extras[3] = {-1, -1, -1};
		int n = race_after_hub(level, 0, ids);
		expect_eq(n, 7, "three-guest field size");
		expect(field_has(ids, n, 12) && field_has(ids, n, 13) && field_has(ids, n, 14),
		       "all three unhit guests seated");
		int need = AP_HitEncounterExtras(ids, n, 0, extras, 3);
		expect_eq(need, 3, "three extra models");
	}

	// Five unhit guests: three per race, rotating; two races cover all five.
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	for (long code : {35011000L, 35011008L, 35016200L, 35011100L, 35011101L})
		g_checked.insert(code);
	{
		std::set<int> seen;
		for (int r = 0; r < 2; r++)
		{
			int n = race_after_hub(7, 0, ids);
			int guests = 0;
			for (int i = 0; i < n; i++)
				if (ids[i] >= 8)
				{
					guests++;
					seen.insert(ids[i]);
				}
			expect_eq(guests, 3, "at most three guests per race");
		}
		expect_eq((long long)seen.size(), 5, "two fresh races seat all five unhit guests");
	}
}

static void test_race_snapshot(void)
{
	int a[AP_HIT_FIELD_MAX], b[AP_HIT_FIELD_MAX];
	int fresh = -1;

	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	for (long code : {35011000L, 35011008L, 35016200L, 35011100L, 35011101L})
		g_checked.insert(code);

	int n = race_after_hub(3, 0, a);

	// Restart/retry: the next driver load is the same race -> same field, even
	// after a Hit was checked mid-race, and the cursors do not advance.
	g_checked.insert(35025000LL + a[0]);
	int cur0[3], cur1[3];
	unsigned d0 = 0, d1 = 0;
	AP_HitEncounterDrawState(3, cur0, &d0);
	AP_HitLoadBegin();
	int m = AP_HitRaceField(3, 0, 7, b, &fresh);
	expect_eq(fresh, 0, "retry reuses");
	expect_eq(m, n, "retry same size");
	for (int i = 0; i < n; i++)
		expect_eq(b[i], a[i], "retry same field");
	AP_HitEncounterDrawState(3, cur1, &d1);
	expect_eq(d1, d0, "retry does not advance");

	// A different player or level never reuses.
	AP_HitLoadBegin();
	AP_HitRaceField(3, 1, 7, b, &fresh);
	expect_eq(fresh, 1, "different player draws fresh");
	AP_HitLoadBegin();
	AP_HitRaceField(8, 1, 7, b, &fresh);
	expect_eq(fresh, 1, "different level draws fresh");

	// A reconnect to the same seed keeps the snapshot and cursors: the retry
	// after it still reuses.
	AP_HitLoadBegin();
	AP_HitRaceField(3, 0, 7, a, &fresh);
	AP_HitEncounterConnectReset();
	AP_HitLoadBegin();
	AP_HitRaceField(3, 0, 7, b, &fresh);
	expect_eq(fresh, 0, "same-seed reconnect keeps the race snapshot");
	for (int i = 0; i < 7; i++)
		expect_eq(b[i], a[i], "same field after a same-seed reconnect");
}

static void test_player_exclusion(void)
{
	int ids[AP_HIT_FIELD_MAX];

	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	g_checked.insert(35011000); // Fake Crash eligible

	// A player who is Fake Crash must not be seated as their own opponent.
	{
		int n = race_after_hub(3, 14, ids);
		expect_eq(n, 7, "player=14 field size");
		expect(!field_has(ids, n, 14), "player=14 never seats self");
	}

	// Every other player still gets the unhit guest first (G1).
	for (int p = 0; p < 16; p++)
	{
		int n = race_after_hub(3, p, ids);
		expect(!field_has(ids, n, p), "field never seats the player");
		if (p != 14)
			expect_eq(ids[0], 14, "non-14 player gets Fake Crash first");
	}
}

static void test_extras_for_all_players(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	g_checked.insert(35011000); // Fake Crash eligible, unhit

	for (int p = 0; p < 16; p++)
	{
		int ids[AP_HIT_FIELD_MAX];
		int extras[3] = {-1, -1, -1};
		int n = race_after_hub(3, p, ids);
		int need = AP_HitEncounterExtras(ids, n, p, extras, 3);

		if (p <= 7)
		{
			// Default player: the arcade pack covers every other default, so
			// only the guest is an extra.
			expect_eq(need, 1, "default player needs one extra");
			expect_eq(extras[0], 14, "default player extra is the guest");
		}
		else if (p == 14)
		{
			// Player IS the guest: no guest seat; Pura (7) is outside the
			// nondefault pack's 0..6 stock and takes a free extra slot.
			expect_eq(need, 1, "player=14 needs one extra");
			expect_eq(extras[0], 7, "player=14 extra is Pura");
		}
		else
		{
			// Nondefault player: the guest plus Pura in a free extra slot.
			expect_eq(need, 2, "nondefault player needs two extras");
			expect(field_has(extras, need, 14), "nondefault extras include the guest");
			expect(field_has(extras, need, 7), "nondefault extras include Pura");
		}
		expect(need <= 3, "extras fit the three slots");
	}
}

// Every guest, once unlocked, is seated at every supported destination in the
// next fresh race and needs exactly one extra model under a default player.
static void test_all_guest_extras(void)
{
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "extras: encounters parsed");
	if (!h)
		return;

	for (int guest = 8; guest < 16; guest++)
	{
		const ctr_hit_trigger *t = &h->triggers[guest - 8];
		for (int level = 0; level <= 17; level++)
		{
			reset_state();
			ap_seedcfg_parse_json(g_fixture);
			g_checked.insert(t->any_of[0]);
			int ids[AP_HIT_FIELD_MAX];
			int extras[3] = {-1, -1, -1};
			int n = race_after_hub(level, 0, ids);
			expect_eq(n, 7, "guest level field size");
			expect_eq(ids[0], guest, "unlocked guest seated first");
			int need = AP_HitEncounterExtras(ids, n, 0, extras, 3);
			expect_eq(need, 1, "one extra (default player)");
			expect_eq(extras[0], guest, "extra is the guest");
		}
	}
}

static void test_generic_victim_dispatch(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);

	// Generic dispatch maps ANY valid actual victim through the parsed sixteen
	// locations, defaults included.
	for (int victim = 0; victim < 16; victim++)
	{
		reset_state();
		g_emitCount = 0;
		expect_eq(AP_HitEncounterOnDamage(victim, 1, kAccepted), 1, "victim accepted");
		expect_eq(g_emitCount, 1, "one emit per victim");
		expect_eq(g_lastEmit, 35025000LL + victim, "victim maps to its location");
	}
}

static void test_negative_attribution(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);

	struct Case
	{
		int victim;
		int damage;
		unsigned flags;
		const char *name;
	};
	static const Case cases[] = {
	    {14, 0, kAccepted, "damage 0 rejected"},
	    {14, 5, kAccepted, "damage 5 mask-grab rejected"},
	    {14, 9, kAccepted, "unknown damage rejected"},
	    {14, 1, kAccepted | 256u, "type 1 while already damage-active rejected"},
	    {14, 1, 1u | 2u | 16u | 32u, "unsupported race rejected"},
	    {14, 1, 2u | 16u | 32u | 128u, "non-AI victim rejected"},
	    {14, 1, 1u | 16u | 32u | 128u, "nonlive victim rejected"},
	    {14, 1, 1u | 2u | 4u | 16u | 32u | 128u, "ghost victim rejected"},
	    {14, 1, 1u | 2u | 8u | 16u | 32u | 128u, "player victim rejected"},
	    {14, 1, 1u | 2u | 32u | 128u, "no attacker rejected"},
	    {14, 1, 1u | 2u | 16u | 128u, "non-P1 attacker rejected"},
	    {14, 1, 1u | 2u | 16u | 32u | 64u | 128u, "AI attacker rejected"},
	    {16, 1, kAccepted, "engine id 16 rejected"},
	    {-1, 1, kAccepted, "engine id -1 rejected"},
	};
	for (const Case &c : cases)
	{
		g_emitCount = 0;
		g_lastEmit = -1;
		AP_HitEncounterOnDamage(c.victim, c.damage, c.flags);
		expect_eq(g_emitCount, 0, c.name);
	}

	// A fresh type-1 spin is accepted, and type 4 keeps its burn even while the
	// victim is already damage-active.
	g_emitCount = 0;
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	expect_eq(g_emitCount, 1, "fresh type 1 accepted");
	reset_state();
	AP_HitEncounterOnDamage(14, 4, kAcceptedBurning);
	expect_eq(g_emitCount, 1, "type 4 burn while already spinning accepted");
}

static void test_repeat_and_reconnect_dedup(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);

	g_emitCount = 0;
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	AP_HitEncounterOnDamage(14, 3, kAccepted);
	expect_eq(g_emitCount, 1, "repeated hits dedup to one emit");

	// Reconnect with the check still unconfirmed: the session mask re-arms (the
	// production held-check queue is the cross-connection dedup authority).
	AP_HitEncounterConnectReset();
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	expect_eq(g_emitCount, 2, "reconnect re-arms an unconfirmed check");

	// Reconnect once the server HAS confirmed it: never re-emit.
	g_checked.insert(35025014);
	AP_HitEncounterConnectReset();
	g_emitCount = 0;
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	expect_eq(g_emitCount, 0, "server-checked location never re-emits");
}

// Ticket 09: eligibility for all eight guests is reconstructed from checked
// state via each guest's parsed any-of list, with growing checked sets.
static void test_all_guest_eligibility(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	const ctr_hit_encounters *h = ap_seedcfg_hit_encounters();
	expect(h != NULL, "eligibility: encounters parsed");
	if (!h)
		return;

	for (int g = 0; g < 8; g++)
		expect_eq(AP_HitEncounterGuestEligible(g), 1, "default always eligible");

	for (int g = 8; g < 16; g++)
	{
		const ctr_hit_trigger *t = &h->triggers[g - 8];
		expect_eq(AP_HitEncounterGuestEligible(g), 0, "guest ineligible with no checks");
		for (int i = 0; i < t->count; i++)
		{
			reset_state();
			ap_seedcfg_parse_json(g_fixture);
			g_checked.insert(t->any_of[i]);
			expect_eq(AP_HitEncounterGuestEligible(g), 1,
			          "any-of code unlocks the guest");
		}
	}

	// Growing checked sets on one guest's any-of list: each additional code keeps
	// it eligible, and a code from another guest does not affect it.
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	expect_eq(AP_HitEncounterGuestEligible(13), 0, "Penta ineligible initially");
	g_checked.insert(35011000); // Fake Crash trigger only
	expect_eq(AP_HitEncounterGuestEligible(13), 0, "unrelated code leaves Penta ineligible");
	g_checked.insert(35011008); // first Penta trigger
	expect_eq(AP_HitEncounterGuestEligible(13), 1, "first Penta trigger unlocks");
	g_checked.insert(35011010); // second Penta trigger
	expect_eq(AP_HitEncounterGuestEligible(13), 1, "second Penta trigger keeps unlocked");
}

// Block schema 3 (2026-09-18): a guest whose unlock wins do not exist in the
// seed joins the pool on held Keys alone, and once eligible it is drawn, seated
// and given an extra model exactly like a trigger-unlocked guest.
static nlohmann::json fallback_fixture(int guest, int keys)
{
	nlohmann::json d = g_fixture;
	d["hit_character_encounters"]["schema"] = 3;
	d["hit_character_encounters"]["unlock_triggers"][std::to_string(guest)]
	 ["fallback_keys"] = keys;
	return d;
}

static void test_key_fallback_eligibility(void)
{
	const int guests[4] = {14, 13, 12, 15};
	const int counts[4] = {1, 2, 3, 4};

	for (int i = 0; i < 4; i++)
	{
		const nlohmann::json d = fallback_fixture(guests[i], counts[i]);
		reset_state();
		ap_seedcfg_parse_json(d);
		expect(ap_seedcfg_hit_encounters() != NULL, "fallback: seed parsed");

		for (int k = 0; k < counts[i]; k++)
		{
			g_heldKeys = k;
			expect_eq(AP_HitEncounterGuestEligible(guests[i]), 0,
			          "fallback guest locked below its count");
		}
		for (int k = counts[i]; k <= 4; k++)
		{
			g_heldKeys = k;
			expect_eq(AP_HitEncounterGuestEligible(guests[i]), 1,
			          "fallback guest unlocked at its count");
		}
		// No other guest is affected by the Keys.
		g_heldKeys = 4;
		for (int g = 8; g <= 15; g++)
			if (g != guests[i])
				expect_eq(AP_HitEncounterGuestEligible(g), 0,
				          "Keys unlock only the fallback guest");
		// A trigger win still unlocks the fallback guest at zero Keys.
		reset_state();
		ap_seedcfg_parse_json(d);
		g_heldKeys = 0;
		g_checked.insert(ap_seedcfg_hit_encounters()->triggers[guests[i] - 8].any_of[0]);
		expect_eq(AP_HitEncounterGuestEligible(guests[i]), 1,
		          "a trigger win still unlocks a fallback guest");
	}

	// Without a fallback, Keys change nothing (block schema 2 behaviour).
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	g_heldKeys = 4;
	for (int g = 8; g <= 15; g++)
		expect_eq(AP_HitEncounterGuestEligible(g), 0,
		          "no fallback: Keys leave every guest locked");

	// Seated and extra-loaded like any other unlocked guest: the gather reports
	// it eligible, the draw seats it first (its Hit is unchecked) and the extras
	// plan carries its model.
	{
		reset_state();
		ap_seedcfg_parse_json(fallback_fixture(12, 3));
		unsigned char eligible[CTR_CFG_HIT_CHARACTER_COUNT];
		unsigned char unchecked[CTR_CFG_HIT_CHARACTER_COUNT];
		g_heldKeys = 3;
		AP_HitEncounterGather(eligible, unchecked);
		expect_eq(eligible[12], 1, "fallback guest is gathered eligible");
		expect_eq(unchecked[12], 1, "fallback guest's Hit is unchecked");

		int ids[AP_HIT_FIELD_MAX];
		const int n = race_after_hub(0, 0, ids);
		int seated = 0;
		for (int i = 0; i < n; i++)
			if (ids[i] == 12)
				seated = 1;
		expect(seated, "fallback guest is seated in the first fresh race");

		int extras[AP_HIT_FIELD_MAX];
		const int nx = AP_HitEncounterExtras(ids, n, 0, extras, AP_HIT_FIELD_MAX);
		int extraLoaded = 0;
		for (int i = 0; i < nx; i++)
			if (extras[i] == 12)
				extraLoaded = 1;
		expect(extraLoaded, "fallback guest gets an extra model slot");

		// The pad keeps its Trophy re-race offer while that Hit is unchecked.
		for (int g = 0; g < 8; g++)
			g_checked.insert(35025000 + g);
		expect_eq(AP_HitEncounterOpportunity(0, 0), 12,
		          "a fallback guest is a live pad opportunity");
		g_heldKeys = 2;
		expect_eq(AP_HitEncounterOpportunity(0, 0), -1,
		          "below its count it is no opportunity");
	}
}

// Ticket 09: a new seed / reconnect clears the session state, and eligibility
// re-derives from the new checked set.
static void test_seed_change_and_reconnect(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);
	g_emitCount = 0;
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	expect_eq(g_emitCount, 1, "seed A emits the check");

	// A new seed arrives; the connect reset clears the session mask so the same
	// victim can be checked again under the new seed.
	nlohmann::json other = g_fixture;
	other["hit_character_encounters"]["policy"]["seed"] = 12345;
	ap_seedcfg_parse_json(other);
	AP_HitEncounterConnectReset();
	g_emitCount = 0;
	AP_HitEncounterOnDamage(14, 1, kAccepted);
	expect_eq(g_emitCount, 1, "new seed re-emits after the mask clears");

	// Eligibility itself follows the checked set across the seed change.
	expect_eq(AP_HitEncounterGuestEligible(14), 0, "new seed: FC ineligible with no checks");
	g_checked.insert(35011000);
	expect_eq(AP_HitEncounterGuestEligible(14), 1, "new seed: FC eligible after its win");
}

// Correction A under the pool draw: default racers (0..7) are Hit targets, and
// every eligible target rotates into every supported destination, so the pad
// opportunity is any eligible non-player target with an unchecked Hit.
static void test_default_target_opportunity(void)
{
	reset_state();
	ap_seedcfg_parse_json(g_fixture);

	expect_eq(AP_HitEncounterOpportunity(0, 0), 1, "lowest unchecked default");
	g_checked.insert(35025001);
	expect_eq(AP_HitEncounterOpportunity(0, 0), 2, "advances past a checked default");
	for (int i = 0; i < 8; i++)
		g_checked.insert(35025000LL + i);
	expect_eq(AP_HitEncounterOpportunity(0, 0), -1, "all defaults checked, no guest -> none");
	g_checked.insert(35011000); // Fake Crash unlocked, Hit unchecked
	expect_eq(AP_HitEncounterOpportunity(0, 0), 14, "unlocked unhit guest");
	expect_eq(AP_HitEncounterOpportunity(0, 14), -1, "player's own Hit is no opportunity");
	expect_eq(AP_HitEncounterOpportunity(18, 0), -1, "arena has no opportunity");
	expect_eq(AP_HitEncounterOpportunity(110, 0), -1, "Cortex Vortex destination has none");
}

// Correction C: boss races award Hit checks; feature off stays inert.
static void test_boss_race_feature_off(void)
{
	nlohmann::json off = g_fixture;
	off["ctr_options"]["hit_character"] = false;
	off.erase("hit_character_encounters");
	ap_seedcfg_parse_json(off);
	g_emitCount = 0;
	// Boss-race accepted flags (raceSupported bit included); feature off -> none.
	AP_HitEncounterOnDamage(10, 1, kAccepted);
	expect_eq(g_emitCount, 0, "boss race with feature off emits nothing");
	ap_seedcfg_parse_json(g_fixture);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "tools/fixtures/ctr_hit_character_seed2101.json";
	std::ifstream in(path);
	if (!in)
	{
		std::fprintf(stderr, "cannot open fixture: %s\n", path);
		return 2;
	}
	try
	{
		in >> g_fixture;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "fixture is not valid JSON: %s\n", e.what());
		return 2;
	}

	test_activation_and_candidates();
	test_trigger_to_next_load();
	test_three_guests_three_extras();
	test_race_snapshot();
	test_player_exclusion();
	test_extras_for_all_players();
	test_all_guest_extras();
	test_generic_victim_dispatch();
	test_negative_attribution();
	test_repeat_and_reconnect_dedup();
	test_all_guest_eligibility();
	test_key_fallback_eligibility();
	test_seed_change_and_reconnect();
	test_default_target_opportunity();
	test_boss_race_feature_off();

	std::printf("%s: %d checks, %d failures\n",
	            g_failures ? "FAIL" : "PASS", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
