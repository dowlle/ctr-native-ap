#ifndef AP_CONTENT_CAPABILITY_H
#define AP_CONTENT_CAPABILITY_H
// Open content-plan admission (#133 PR 2): a seed's package descriptor names
// measured capability flags. The apworld cannot see the bytes, so native
// measures the exact pinned files when it locates them and refuses the whole
// package when a flag the descriptor sets to true is not in the bytes. A flag
// set to false is never a refusal: a descriptor may claim less than the track
// offers.
//
// Pure: the caller supplies the structural report of the hash-verified pair
// (CustomContentVerify_Files). tools/test-content-capability.c pins every
// branch. Freestanding C99 so the harness checks exactly what the game uses.
#include <stdio.h>
#include "ap_content_plan.h" /* AP_CONTENT_CAP_* */
#include <platform/native_custom_content_verify.h>

// Same floor as CustomTrackManager_MeasureWumpa: five guaranteed fruit per
// fruit crate plus one per loose fruit, ten needed in one lap.
#define AP_CONTENT_WUMPA_PER_CRATE 5
#define AP_CONTENT_WUMPA_TARGET 10

// The capabilities the report proves. A report that is not loadable proves none.
static inline unsigned AP_ContentMeasuredCapabilities(const struct CustomContentVerification *report)
{
	unsigned caps = 0;
	const struct CustomContentMeasurements *m;
	if (report == NULL || !report->loadable)
		return 0;
	m = &report->measured;
	if (CustomContentVerify_TimeTrialReason(report, NULL, 0))
		caps |= AP_CONTENT_CAP_TIME_TRIAL;
	if (CustomContentVerify_ArcadeRaceReason(report, NULL, 0))
		caps |= AP_CONTENT_CAP_ARCADE;
	if (report->fileAnalysis[CTR_CCV_RELIC_RACE].result == CTR_CCV_DETECTED)
		caps |= AP_CONTENT_CAP_RELIC;
	if (report->fileAnalysis[CTR_CCV_CTR_CHALLENGE].result == CTR_CCV_DETECTED)
		caps |= AP_CONTENT_CAP_CTR_TOKENS;
	if (m->fruitCrates <= 1000000ul && m->looseWumpa <= 1000000ul &&
	    m->fruitCrates * AP_CONTENT_WUMPA_PER_CRATE + m->looseWumpa >= AP_CONTENT_WUMPA_TARGET)
		caps |= AP_CONTENT_CAP_WUMPA;
	return caps;
}

static inline const char *AP_ContentCapabilityName(unsigned bit)
{
	switch (bit)
	{
	case AP_CONTENT_CAP_TIME_TRIAL: return "time_trial";
	case AP_CONTENT_CAP_ARCADE: return "arcade";
	case AP_CONTENT_CAP_RELIC: return "relic";
	case AP_CONTENT_CAP_CTR_TOKENS: return "ctr_tokens";
	case AP_CONTENT_CAP_WUMPA: return "wumpa";
	default: return "unknown";
	}
}

// 1 when every claimed flag is measured. 0 otherwise, with a one-line reason
// naming the first missing flag and what was measured.
static inline int AP_ContentCapabilitiesHold(unsigned claimed, const struct CustomContentVerification *report,
                                             char *why, size_t whySize)
{
	unsigned measured = AP_ContentMeasuredCapabilities(report);
	unsigned missing = (claimed & AP_CONTENT_CAP_ALL & ~measured) | (claimed & ~AP_CONTENT_CAP_ALL);
	unsigned bit;
	if (why && whySize)
		why[0] = '\0';
	if (report == NULL || !report->loadable)
	{
		if (why && whySize)
			snprintf(why, whySize, "the track files failed the structural check");
		return 0;
	}
	if (!missing)
		return 1;
	for (bit = 1; bit <= AP_CONTENT_CAP_WUMPA && !(missing & bit); bit <<= 1)
		;
	if (why && whySize)
	{
		const struct CustomContentMeasurements *m = &report->measured;
		snprintf(why, whySize,
		         "the descriptor claims %s, but the files do not have it "
		         "(checkpoints %lu, spawns %lu, AI paths %lu, relic crates %lu, letters %lu/%lu/%lu, "
		         "fruit crates %lu, loose fruit %lu)",
		         bit <= AP_CONTENT_CAP_WUMPA ? AP_ContentCapabilityName(bit) : "an unknown capability",
		         m->checkpoints, m->spawns, m->navPaths, m->relicCrates, m->letterC, m->letterT, m->letterR,
		         m->fruitCrates, m->looseWumpa);
	}
	return 0;
}

// Super turbo pads (#133 PR 4c, ruling 2026-10-17): a custom track whose LEV
// has any super turbo pad trigger quad takes the Hot Air Skyway finish rule
// in logic, so the descriptor's optional `super_turbo_pads` must say what the
// bytes hold. Unlike the mode flags above, both wrong answers refuse: a
// false claim on a track with super pads would put its finish in logic
// without USF, and a true claim on one without would gate it for nothing.
// Unstated (AP_CONTENT_SUPER_PADS_UNSTATED) is never a refusal: logic then
// already treats the track as having super pads.
static inline int AP_ContentSuperTurboPadsHold(int stated, const struct CustomContentVerification *report,
                                               char *why, size_t whySize)
{
	int measured;
	if (why && whySize)
		why[0] = '\0';
	if (report == NULL || !report->loadable)
	{
		if (why && whySize)
			snprintf(why, whySize, "the track files failed the structural check");
		return 0;
	}
	if (stated == AP_CONTENT_SUPER_PADS_UNSTATED)
		return 1;
	measured = report->measured.superTurboPads > 0;
	if ((stated != 0) == measured)
		return 1;
	if (why && whySize)
		snprintf(why, whySize, "the descriptor says super_turbo_pads is %s, but the files have %lu super turbo pad quads",
		         stated ? "true" : "false", report->measured.superTurboPads);
	return 0;
}

// Weapon crates (#133 PR 4e): the descriptor's optional `weapon_crates`
// says how many weapon crates the LEV holds. A named custom track's Item Box
// checks are seated on them, and a count of 0 takes the track out of the
// Itemsanity and Hit routes, so any disagreement with the bytes refuses: a
// higher count would create box checks with no crate to seat them, a lower
// one would seat them on the wrong crates. Unstated
// (AP_CONTENT_WEAPON_CRATES_UNSTATED) is never a refusal: such a track has no
// box checks.
static inline int AP_ContentWeaponCratesHold(int stated, const struct CustomContentVerification *report,
                                             char *why, size_t whySize)
{
	if (why && whySize)
		why[0] = '\0';
	if (report == NULL || !report->loadable)
	{
		if (why && whySize)
			snprintf(why, whySize, "the track files failed the structural check");
		return 0;
	}
	if (stated == AP_CONTENT_WEAPON_CRATES_UNSTATED)
		return 1;
	if (stated >= 0 && (unsigned long)stated == report->measured.weaponCrates)
		return 1;
	if (why && whySize)
		snprintf(why, whySize, "the descriptor says weapon_crates is %d, but the files have %lu weapon crates",
		         stated, report->measured.weaponCrates);
	return 0;
}

// Optional measured `checkpoints` and `ai_nav` (#133 PR 6b): the restart
// point count and whether the LEV has AI navigation paths, which certify the
// track as a boss venue. A stated value the bytes disagree with refuses the
// package; unstated (AP_CONTENT_UNSTATED) is never a refusal, such a track is
// only never a boss venue.
static inline int AP_ContentBossVenueFactsHold(int checkpoints, int aiNav,
                                               const struct CustomContentVerification *report,
                                               char *why, size_t whySize)
{
	if (why && whySize)
		why[0] = '\0';
	if (report == NULL || !report->loadable)
	{
		if (why && whySize)
			snprintf(why, whySize, "the track files failed the structural check");
		return 0;
	}
	if (checkpoints != AP_CONTENT_UNSTATED && (unsigned long)checkpoints != (unsigned long)report->measured.checkpoints)
	{
		if (why && whySize)
			snprintf(why, whySize, "the descriptor says checkpoints is %d, but the files have %lu restart points",
			         checkpoints, (unsigned long)report->measured.checkpoints);
		return 0;
	}
	if (aiNav != AP_CONTENT_UNSTATED && (aiNav != 0) != (report->measured.navPaths > 0))
	{
		if (why && whySize)
			snprintf(why, whySize, "the descriptor says ai_nav is %s, but the files have %s AI paths",
			         aiNav ? "true" : "false", report->measured.navPaths > 0 ? "" : "no");
		return 0;
	}
	return 1;
}

#endif
