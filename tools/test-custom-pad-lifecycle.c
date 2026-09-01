#include <stdio.h>
#include <string.h>

#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#include "../ap/ap_custom_pad_logic.h"
#include "../ap/ap_pad_state.h"

static int failures;

static void expect_int(int got, int want, const char *name)
{
	printf("%s %s (got %d, want %d)\n", got == want ? "PASS" : "FAIL",
	       name, got, want);
	if (got != want)
		failures++;
}

static void expect_long(long got, long want, const char *name)
{
	printf("%s %s (got %ld, want %ld)\n", got == want ? "PASS" : "FAIL",
	       name, got, want);
	if (got != want)
		failures++;
}

static ctr_seed_config fixture(void)
{
	ctr_seed_config cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.custom_tracks_ok = 1;
	cfg.podium_enabled = 1;
	cfg.custom_track.slot = 1;
	cfg.custom_track.replaces_cup_level_id = 104;
	cfg.custom_track.trophy_location = 35016300;
	cfg.custom_track.podium.held_1st = 35016400;
	cfg.custom_track.podium.held_3rd = 35016401;
	cfg.custom_track.podium.held_5th = -1;
	cfg.custom_track.podium.finish_podium = 35016403;
	cfg.custom_track.podium.finish_any = 35016404;
	cfg.custom_track.flags.wumpa_collectible = 1;
	strcpy(cfg.custom_track.package_uuid,
	       "60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e");
	cfg.wumpa.mode = CTR_CFG_WUMPA_PER_TRACK;
	cfg.wumpa.custom_count = 1;
	cfg.wumpa.custom[0].cup_level_id = 104;
	cfg.wumpa.custom[0].code = 35016120;
	cfg.wumpa.custom[0].wumpa_collectible = 1;
	strcpy(cfg.wumpa.custom[0].package_uuid,
	       "60D5A8A8-B69A-4F6A-A0D8-9A43D91E3F2E");
	return cfg;
}

struct query_state
{
	long checked[7];
	int checked_count;
};

static int location_exists(long code, void *ctx)
{
	(void)ctx;
	return code > 0;
}

static int location_checked(long code, void *ctx)
{
	struct query_state *state = (struct query_state *)ctx;
	int i;
	for (i = 0; i < state->checked_count; i++)
		if (state->checked[i] == code)
			return 1;
	return 0;
}

static void test_identity_bridge(void)
{
	ctr_seed_config cfg = fixture();
	int track = AP_CustomPadPodiumTrack(&cfg, 104);
	cfg.podium[15].held_1st = 35015900;
	expect_int(track, CTR_CFG_PODIUM_TRACK_COUNT,
	           "slot 1 follows the retail podium tracks");
	expect_long(AP_PodiumPseudoLocationCode(
	                &cfg, AP_PODIUM_PSEUDO_BASE + 15 * 5),
	            35015900, "retail podium pseudo-bit still resolves");
	expect_long(AP_CustomPadSpecialLocationCode(
	                &cfg, AP_CUSTOM_TROPHY_PSEUDO_BIT),
	            35016300, "custom Trophy pseudo-bit resolves exact wire code");
	expect_long(AP_CustomPadSpecialLocationCode(
	                &cfg, AP_CUSTOM_WUMPA_PSEUDO_BIT),
	            35016120, "custom Wumpa pseudo-bit resolves exact wire code");
	expect_long(AP_PodiumPseudoLocationCode(
	                &cfg, AP_PODIUM_PSEUDO_BASE + track * 5 + 0),
	            35016400, "custom Held 1st pseudo-bit resolves");
	expect_long(AP_PodiumPseudoLocationCode(
	                &cfg, AP_PODIUM_PSEUDO_BASE + track * 5 + 2),
	            -1, "absent custom Held 5th stays absent");
	expect_long(AP_PodiumPseudoLocationCode(
	                &cfg, AP_PODIUM_PSEUDO_BASE + track * 5 + 4),
	            35016404, "custom Finish Any pseudo-bit resolves");
	expect_long(AP_PodiumPseudoLocationCode(
	                &cfg, AP_CUSTOM_TROPHY_PSEUDO_BIT),
	            -1, "direct custom pseudo-bits do not alias podium space");
}

static void test_fail_closed_identity(void)
{
	ctr_seed_config cfg = fixture();
	strcpy(cfg.wumpa.custom[0].package_uuid,
	       "00000000-0000-0000-0000-000000000000");
	expect_long(AP_CustomPadWumpaLocationCode(&cfg, 104), -1,
	            "mismatched package never owns the Wumpa check");
	cfg = fixture();
	cfg.wumpa.custom[0].wumpa_collectible = 0;
	expect_long(AP_CustomPadWumpaLocationCode(&cfg, 104), -1,
	            "capability disagreement never owns the Wumpa check");
	cfg = fixture();
	cfg.custom_track.slot = 32;
	expect_int(AP_CustomPadPodiumTrack(&cfg, 104),
	           CTR_CFG_PODIUM_TRACK_COUNT + 31,
	           "last frozen custom slot has a stable logical track");
	cfg.custom_track.slot = 33;
	expect_int(AP_CustomPadPodiumTrack(&cfg, 104), -1,
	           "out-of-range custom slot fails closed");
	expect_long(AP_CustomPadSpecialLocationCode(
	                &cfg, AP_CUSTOM_TROPHY_PSEUDO_BIT),
	            -1, "out-of-range slot cannot resolve a custom Trophy");
}

static void test_pad_lifecycle(void)
{
	ctr_seed_config cfg = fixture();
	struct query_state state;
	int bits[8];
	int count;
	memset(&state, 0, sizeof state);

	count = AP_CustomPadAppendUnchecked(&cfg, 104, 1, bits, 8, 0,
	                                    location_exists, location_checked,
	                                    &state);
	expect_int(count, 6,
	           "exact initial gather has Trophy, four rungs and Wumpa");
	// Initial: Trophy + four enabled podium rungs + Wumpa remain.  A Gem Cup is
	// a reduced-lifecycle destination, so stage 1 opens it directly as Raceable.
	expect_int(AP_PadStateDecide(0, 1, 1, 0, 1, count, 0), 2,
	           "custom cup is raceable before its first race");

	state.checked[0] = 35016300;
	state.checked[1] = 35016400;
	state.checked[2] = 35016401;
	state.checked[3] = 35016403;
	state.checked[4] = 35016404;
	state.checked_count = 5;
	count = AP_CustomPadAppendUnchecked(&cfg, 104, 1, bits, 8, 0,
	                                    location_exists, location_checked,
	                                    &state);
	expect_int(count, 1, "post-win gather retains only Wumpa when missed");
	// First place normally settles Trophy and all four enabled podium rungs.
	// If ten Wumpa was not reached, that attached check alone keeps entry alive.
	expect_int(AP_PadStateDecide(0, 1, 1, 1, 1, count, 0), 2,
	           "remaining custom Wumpa check prevents a premature Done lock");

	state.checked[5] = 35016120;
	state.checked_count = 6;
	count = AP_CustomPadAppendUnchecked(&cfg, 104, 1, bits, 8, 0,
	                                    location_exists, location_checked,
	                                    &state);
	expect_int(count, 0, "settled gather has no custom checks left");
	// Only after every attached custom identity is settled may Done hard-lock it.
	expect_int(AP_PadStateDecide(0, 1, 1, 1, 1, count, 0), 5,
	           "custom cup becomes Done after every custom check");
	// The physical pad's item and racer gates still outrank the open lifecycle.
	expect_int(AP_PadStateDecide(0, 0, 1, 0, 1, 6, 0), 1,
	           "custom cup still respects its physical item gate");
	expect_int(AP_PadStateDecide(0, 1, 0, 0, 1, 6, 0), 1,
	           "custom cup still respects its physical racer lock");
}

static void test_serve_fault_reentry(void)
{
	const char *fault = "Package file changed after verification.";
	expect_int(AP_CustomPadContentReady(1, 0, fault), 0,
	           "serve fault refuses custom-pad re-entry");
	expect_int(AP_CustomPadContentReady(1, 1, fault), 0,
	           "content remains required while the fault is latched");
	expect_int(AP_CustomPadContentReady(1, 0, NULL), 1,
	           "successful verify clears the fault and unlocks re-entry");
	expect_int(AP_CustomPadContentReady(0, 0, NULL), 0,
	           "a seed without custom content leaves the loader withdrawn");
}

int main(void)
{
	test_identity_bridge();
	test_fail_closed_identity();
	test_pad_lifecycle();
	test_serve_fault_reentry();
	printf("%s: custom pad identity and lifecycle regression\n",
	       failures ? "FAIL" : "PASS");
	return failures != 0;
}
