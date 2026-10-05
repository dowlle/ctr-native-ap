#ifndef AP_BOSS_WON_FLAGS_H
#define AP_BOSS_WON_FLAGS_H

// Issue #458: "this slot personally won this boss race", per seed, team and
// slot, kept in the room's server data storage.
//
// Why not the checked location. The server marks a location checked for a
// won race, but also for another player's Collect, this player's own
// !collect or !release, and admin commands. Native cannot tell those apart,
// so counting checked Boss Race locations let a Collect complete a Bosses
// goal (or open the Oxide garage for one) with no boss raced. And under
// `oxide_goal: any_percent` a Collect-marked "N. Oxide's Challenge" made the
// garage offer the Final Challenge, whose win does not satisfy any_percent:
// the goal could no longer complete.
//
// So every reader that asks "has the player BEATEN a boss / Oxide's first
// challenge" reads these bits instead:
//   * AP_ComposedBossesWon (ap_hooks.c): the goal, the Oxide garage companion
//     arm, the pause menu BOSS n/4 line and the garage BOSSES n/m advert, and
//     the DeathLink win logic `bosses` term, all through that one function;
//   * AP_OxideFirstChallengeCleared (ap_hooks.c): which Oxide race the
//     garage offers;
//   * the boss-door scene's "boss not beaten yet" term.
// The checked location stays the answer for "is this location done", e.g.
// the in-game tracker and the boss garage door's first-win podium.
//
// Storage is the shared scene-flag store (ap_scene_seen_flags.h): same
// scoping, same Get barrier, written with the data storage "or" operation so
// bits are only ever added, survives client restarts and reconnects through
// the room save. A fresh room of the same seed starts clean, but then its
// checks are gone too.
//
// Value: an integer bitmask, 0..63.
//   bit h (0..3)  boss h personally won, the same order as
//                 ADV_REWARD_FIRST_BOSS_KEY + h: Ripper Roo, Papu Papu,
//                 Komodo Joe, Pinstripe
//   bit 4         N. Oxide's Challenge (the first Oxide race) personally won
//   bit 5         the key was created by a client that knows this key. Its
//                 only job is to make the key present after the one-time
//                 migration below, even when no boss bit is set.
//
// MIGRATION (seeds already in progress). A seed played before this client
// has no key. The first time a client reads the key and finds it ABSENT, it
// copies the boss locations already checked into the bits once (bit 5 makes
// the key present, so it never happens again). That keeps an in-progress
// player's earlier boss wins without re-racing them. The same copy also
// keeps any Collect marks made before the update: native cannot tell them
// apart. Oxide's Challenge is copied too, except under any_percent. There the
// garage only opens Oxide's Challenge once every companion arm is met, so a
// real win completes the goal at once, and copying a Collect mark would keep
// the softlock this change removes. At worst an any_percent player whose
// goal is already done can race Oxide's Challenge again.
//
// Wins while disconnected are recorded locally (the identity of the last
// connection is kept) and sent after the next Get reply for the same seed
// and slot. A different seed or slot clears them, like held checks
// (ap_held_checks.h). Before the first connect there is no slot_data, so no
// seed's boss race can be won, and nothing is recorded.

#define AP_BOSS_WON_BOSS_COUNT 4
#define AP_BOSS_WON_BOSS_ALL   0x0Fu
#define AP_BOSS_WON_OXIDE_FIRST 0x10u
#define AP_BOSS_WON_PRESENT    0x20u
#define AP_BOSS_WON_ALL        0x3Fu
#define AP_BOSS_WON_BIT(b) \
	(((b) >= 0 && (b) < AP_BOSS_WON_BOSS_COUNT) ? (1u << (b)) : 0u)

// Frozen apworld location ids (data/locations.json): Boss Race b is
// 35011100 + b, N. Oxide's Challenge is 35011104. tools/test-boss-won-flags.cpp
// checks them against AP_LOCATION_TABLE.
#define AP_BOSS_WON_LOCATION_CODE(b) (35011100LL + (long long)(b))
#define AP_BOSS_WON_OXIDE_FIRST_CODE 35011104LL

// goal_oxide wire value for any_percent (ap_oxide_encounter.h
// AP_OXIDE_GOAL_ANY; repeated so this header stays freestanding).
#define AP_BOSS_WON_GOAL_OXIDE_ANY 1

// How many of the four boss races the bits say were personally won.
static inline int AP_BossWonCount(unsigned bits)
{
	int n = 0, b;
	for (b = 0; b < AP_BOSS_WON_BOSS_COUNT; b++)
		if (bits & (1u << b))
			n++;
	return n;
}

// The bits a one-time migration writes. `checked` uses the flag layout: bit h
// for a checked Boss Race h, AP_BOSS_WON_OXIDE_FIRST for a checked N. Oxide's
// Challenge. Always includes AP_BOSS_WON_PRESENT.
static inline unsigned AP_BossWonMigrationBits(unsigned checked, int goalOxide)
{
	unsigned out = AP_BOSS_WON_PRESENT | (checked & AP_BOSS_WON_BOSS_ALL);
	if (goalOxide != AP_BOSS_WON_GOAL_OXIDE_ANY)
		out |= checked & AP_BOSS_WON_OXIDE_FIRST;
	return out;
}

#ifdef __cplusplus
#include "ap_scene_seen_flags.h"

struct APBossWonFlags : APSceneSeenFlags {
    bool seeded = false; // the one-time migration ran for this identity

    APBossWonFlags() : APSceneSeenFlags("ctr_boss_won_v1", AP_BOSS_WON_ALL) {}

    void connect(const std::string &endpoint, const std::string &seed, int team, int slot) {
        std::string before = identity;
        APSceneSeenFlags::connect(endpoint, seed, team, slot);
        if (identity != before) seeded = false;
    }
    // The Get reply. Returns true when the key was absent and the caller must
    // run the one-time migration (seed()) with the currently checked set.
    bool retrievedNeedsSeed(const nlohmann::json &v) {
        retrieved(v);
        return v.is_null() && known() && !seeded;
    }
    void seed(unsigned migrationBits) {
        unsigned m = migrationBits & all;
        seeded = true;
        if (identity.empty() || m == 0) return;
        bits |= m; pending |= m;
    }
    int bossesWon() const { return AP_BossWonCount(bits); }
    bool bossWon(int b) const {
        unsigned bit = AP_BOSS_WON_BIT(b);
        return bit != 0 && has(bit);
    }
    bool oxideFirstWon() const { return has(AP_BOSS_WON_OXIDE_FIRST); }
    // Returns true when this call is the one that recorded the boss.
    bool recordBoss(int b) {
        unsigned bit = AP_BOSS_WON_BIT(b);
        return bit != 0 && record(bit);
    }
    bool recordOxideFirst() { return record(AP_BOSS_WON_OXIDE_FIRST); }
};
#endif
#endif
