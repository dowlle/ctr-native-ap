// Host harness for the Tizi Madness Mask (A4-96): the pure rules in
// ap/ap_tizi_madness_logic.h and the track identity in
// include/platform/native_tizi_madness.h.
// cc -Wall -Wextra -Werror -DCTR_AP -I ap -I . -I include -o /tmp/test-tizi-madness tools/test-tizi-madness.c
//
// Cases:
//   1. The refill gate, exhaustively: on or off the track, Itemsanity on or
//      off, Mask item received or not (ruling of 2026-11-14, 23:03 to 23:06 CET).
//   2. The free-slot test: only an empty, idle slot takes a Mask, so a Mask
//      already held, a roll in progress, a stacked weapon, an expiring item, a
//      committed fire, a roll cloud and an incoming clock all wait.
//   3. The identity: the exact LEV and VRM pair, and nothing else; the pair is
//      the one the track source table lists for the Coco patch at BIGFILE 47
//      under the title "Tizi Madness", so the two tables cannot drift apart.
//
// Not covered here: the engine glue (race window, drivers[0]) and the serving
// paths that pick the pair for a load. Those need a running game and are Steam
// test rows.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_tizi_madness_logic.h"
#include <platform/native_tizi_madness.h>
#include <platform/native_track_sources.h>

static int failures, checks;

static void expect_(int ok, const char *what)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("FAIL: %s\n", what);
	}
}

#define expect(cond, what) expect_((cond) != 0, what)

#define COCO_PATCH "17e43e108b7f732cb8302636f6984a3b0df59df6af517fffab0e860d00209b54"
#define TRACK6_LEV_PREFIX "80fb4239" /* track 6's package pin starts like this; not a LEV */

static void test_gate(void)
{
	/* Off the track nothing happens, whatever the items. */
	expect(!AP_TiziMadnessRefillOn(0, 0, 0), "off the track, itemsanity off");
	expect(!AP_TiziMadnessRefillOn(0, 0, 1), "off the track, itemsanity off, mask");
	expect(!AP_TiziMadnessRefillOn(0, 1, 0), "off the track, itemsanity on");
	expect(!AP_TiziMadnessRefillOn(0, 1, 1), "off the track, itemsanity on, mask");
	/* Itemsanity off, or no AP seed at all: always. */
	expect(AP_TiziMadnessRefillOn(1, 0, 0), "on the track, itemsanity off: a Mask without any item");
	expect(AP_TiziMadnessRefillOn(1, 0, 1), "on the track, itemsanity off, mask received");
	/* Itemsanity on: only once the Mask weapon item is in. */
	expect(!AP_TiziMadnessRefillOn(1, 1, 0), "on the track, itemsanity on, no Mask item yet: waits");
	expect(AP_TiziMadnessRefillOn(1, 1, 1), "on the track, itemsanity on, Mask item received");
	expect(AP_TiziMadnessRefillOn(1, 1, 5), "any non-zero receipt counts");
}

static void test_slot(void)
{
	const int none = AP_TIZI_MADNESS_HELD_NONE;
	expect(AP_TiziMadnessSlotFree(none, 0, 0, 0, 0, 0), "an empty idle slot takes a Mask");
	expect(!AP_TiziMadnessSlotFree(AP_TIZI_MADNESS_HELD_MASK, 0, 0, 0, 0, 0), "a held Mask is kept, not rewritten");
	expect(!AP_TiziMadnessSlotFree(0x0, 0, 0, 0, 0, 0), "a held Turbo (Turbo Grant, a box) is kept until used");
	expect(!AP_TiziMadnessSlotFree(0x10, 0, 0, 0, 0, 0), "a box roll in progress finishes as normal");
	expect(!AP_TiziMadnessSlotFree(none, 2, 0, 0, 0, 0), "stacked weapons left: not free");
	expect(!AP_TiziMadnessSlotFree(none, 0, 12, 0, 0, 0), "an item still expiring out of the slot: wait");
	expect(!AP_TiziMadnessSlotFree(none, 0, 0, 1, 0, 0), "a fire committed this frame: wait");
	expect(!AP_TiziMadnessSlotFree(none, 0, 0, 0, 1, 0), "an item-roll cloud: wait");
	expect(!AP_TiziMadnessSlotFree(none, 0, 0, 0, 0, 30), "an N. Tropy clock being received: wait");
	expect(AP_TIZI_MADNESS_HELD_MASK == 0x7, "the Mask is engine held-item 7, the id the Tizi Helper uses");
}

static void test_identity(void)
{
	struct TrackSource s;
	char upper[65];
	size_t i;

	expect(CustomTrack_IsTiziMadness(TIZI_MADNESS_LEV_SHA256, TIZI_MADNESS_VRM_SHA256), "the exact pair");
	expect(!CustomTrack_IsTiziMadness(TIZI_MADNESS_LEV_SHA256, NULL), "no VRM");
	expect(!CustomTrack_IsTiziMadness(NULL, TIZI_MADNESS_VRM_SHA256), "no LEV");
	expect(!CustomTrack_IsTiziMadness(TIZI_MADNESS_LEV_SHA256,
	                                  "0000000000000000000000000000000000000000000000000000000000000000"),
	       "the LEV with another VRM is another track");
	expect(!CustomTrack_IsTiziMadness("0000000000000000000000000000000000000000000000000000000000000000",
	                                  TIZI_MADNESS_VRM_SHA256),
	       "the VRM with another LEV is another track");
	expect(!CustomTrack_IsTiziMadness(TIZI_MADNESS_VRM_SHA256, TIZI_MADNESS_LEV_SHA256), "swapped roles");
	expect(!CustomTrack_IsTiziMadness("Tizi Madness", "Tizi Madness"), "a title is never an identity");
	for (i = 0; i < 64; i++)
	{
		char c = TIZI_MADNESS_LEV_SHA256[i];
		upper[i] = (c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
	}
	upper[64] = 0;
	expect(!CustomTrack_IsTiziMadness(upper, TIZI_MADNESS_VRM_SHA256),
	       "pins are lowercase on every path; an uppercase spelling is not the pin");
	expect(strlen(TIZI_MADNESS_LEV_SHA256) == 64 && strlen(TIZI_MADNESS_VRM_SHA256) == 64, "both pins are sha256 hex");
	expect(strncmp(TIZI_MADNESS_LEV_SHA256, TRACK6_LEV_PREFIX, 8) != 0, "not track 6");

	/* The source table names this pair as Coco Team Racing BIGFILE 47, "Tizi Madness" (A4-95). */
	expect(TrackSources_Find(TIZI_MADNESS_LEV_SHA256, TIZI_MADNESS_VRM_SHA256, &s), "the source table knows the pair");
	expect(s.slot == 47 && !strcmp(s.patchSha256, COCO_PATCH) && !strcmp(s.patchFile, "CocoTeamRacing.xdelta"),
	       "the pair is the Coco patch's BIGFILE 47");
	expect(!strcmp(s.title, "Tizi Madness"), "and that row is titled Tizi Madness");
}

int main(void)
{
	test_gate();
	test_slot();
	test_identity();
	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
