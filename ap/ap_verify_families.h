#ifndef AP_VERIFY_FAMILIES_H
#define AP_VERIFY_FAMILIES_H

// Seed-verifier worklist rows for three location families that are carried in
// their own slot_data blocks rather than in the static location table:
//
//   Relic Race Perfect   relic_perfect_checks (#49), one row per LevelID 0..17
//   trial-track races    trial_track_checks (#203): Slide Coliseum and Turbo
//                        Track Trophy Race and CTR Token Challenge
//   Hit <Character>      hit_character_encounters, one row per engine id 0..15
//
// Pure: reads only the parsed seed config, so the host harness can drive it
// with the real parser. Scout presence in ap_verify.c stays the final
// membership authority, exactly as for every other family.

#include "ap_seedcfg.h"

enum
{
	AP_VF_FAMILY_RELIC_PERFECT = 0, // track = LevelID 0..17
	AP_VF_FAMILY_TRIAL_TROPHY,      // track = LevelID 16/17
	AP_VF_FAMILY_TRIAL_CTR,         // track = LevelID 16/17
	AP_VF_FAMILY_HIT,               // detail = target engine id 0..15
};

#define AP_VF_FAMILY_MAX (CTR_CFG_RELIC_PERFECT_COUNT + \
	CTR_CFG_TRIAL_TRACK_COUNT * CTR_CFG_TRIAL_CHECK_COUNT + \
	CTR_CFG_HIT_CHARACTER_COUNT)

// Boss-win codes in ctr_hit_encounters.boss_identity[] order (the parser's
// canonical key order): four garage bosses, then both Oxide encounters.
static const long AP_VF_HIT_BOSS_CODES[CTR_CFG_HIT_BOSS_COUNT] = {
	35011100L, 35011101L, 35011102L, 35011103L, 35011104L, 35011105L,
};

typedef struct
{
	long code;
	int family;
	int track;
	int detail;
} AP_VerifyFamilyLocation;

// Append one row, or raise *truncated when the caller's buffer is full.
static inline int AP_VerifyFamilyPush(AP_VerifyFamilyLocation *out, int n,
	int capacity, int *truncated, long code, int family, int track, int detail)
{
	if (n >= capacity)
	{
		*truncated = 1;
		return n;
	}
	out[n].code = code;
	out[n].family = family;
	out[n].track = track;
	out[n].detail = detail;
	return n + 1;
}

// Every row of the three families this seed config names. Returns the row
// count; *truncated is set (never cleared) when `capacity` was too small.
static inline int AP_VerifyFamilyWorklist(const ctr_seed_config *cfg,
	AP_VerifyFamilyLocation *out, int capacity, int *truncated)
{
	int i, n = 0;

	if (cfg == 0 || out == 0 || truncated == 0)
		return 0;

	if (cfg->relic_perfect_enabled)
		for (i = 0; i < CTR_CFG_RELIC_PERFECT_COUNT; i++)
			if (cfg->relic_perfect[i] >= 0)
				n = AP_VerifyFamilyPush(out, n, capacity, truncated,
					cfg->relic_perfect[i], AP_VF_FAMILY_RELIC_PERFECT, i, -1);

	// A refused trial row is not payable at runtime (AP_TrialTrackLocation), so
	// it is left out and the coverage check makes no claim for that seed.
	for (i = 0; i < CTR_CFG_TRIAL_TRACK_COUNT; i++)
	{
		if (!cfg->trial_track_valid[i])
			continue;
		if (cfg->trial_track_locations[i][CTR_CFG_TRIAL_TROPHY] >= 0)
			n = AP_VerifyFamilyPush(out, n, capacity, truncated,
				cfg->trial_track_locations[i][CTR_CFG_TRIAL_TROPHY],
				AP_VF_FAMILY_TRIAL_TROPHY, 16 + i, -1);
		if (cfg->trial_track_locations[i][CTR_CFG_TRIAL_CTR] >= 0)
			n = AP_VerifyFamilyPush(out, n, capacity, truncated,
				cfg->trial_track_locations[i][CTR_CFG_TRIAL_CTR],
				AP_VF_FAMILY_TRIAL_CTR, 16 + i, -1);
	}

	if (cfg->hit.enabled && cfg->hit.valid)
		for (i = 0; i < CTR_CFG_HIT_CHARACTER_COUNT; i++)
			if (cfg->hit.locations[i] > 0)
				n = AP_VerifyFamilyPush(out, n, capacity, truncated,
					cfg->hit.locations[i], AP_VF_FAMILY_HIT, -1, i);

	return n;
}

#endif
