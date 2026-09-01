// Which AP location code a 10-wumpa crossing sends: the whole truth table,
// compiled out of engine. ap/ap_wumpa_dispatch.h is included directly, so there
// is no reimplementation of the decision to drift from the shipped one.
//
//   cc -Wall -Wextra -Werror -DCTR_AP -I ap -I . -o /tmp/test-wumpa-dispatch
//      tools/test-wumpa-dispatch.c && /tmp/test-wumpa-dispatch
//
// Exit 0 = every assertion held. No disc, no display, no seed, no network.
//
// The behaviour under test, in one sentence: the wire says which codes exist,
// the runtime says which destination is being raced, and a crossing sends the
// code those two agree on or it sends nothing at all.
//
// WHAT THIS DOES NOT COVER, deliberately: server location membership and the
// already-checked dedup. Both live in ap_hooks.c on top of this decision and
// both need a live connection; the "reaching 10 twice sends one location" gate
// is AP_EmitClassCheck's, not this function's.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_wumpa_dispatch.h"

static int checks;
static int failures;

static void expect_long(long got, long want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s (got %ld, want %ld)\n", name, got, want);
	}
}

static void expect_int(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		printf("FAIL %s (got %d, want %d)\n", name, got, want);
	}
}

// The permanent codes the approved 2026-08-29 unfreeze minted. Spelled out
// rather than derived, because a harness that computes the block arithmetic the
// same way the parser does cannot catch the parser getting it wrong.
#define GLOBAL_CODE     35016100L
#define CRASH_COVE_CODE 35016101L // retail LevelID 3
#define ROOS_TUBES_CODE 35016102L // retail LevelID 6
#define CUSTOM_CODE     35016120L // the purple_gem_cup destination slot

#define PACKAGE_UUID "60d5a8a8-b69a-4f6a-a0d8-9a43d91e3f2e"
#define OTHER_UUID   "11111111-2222-4333-8444-555555555555"

#define PURPLE_CUP_LEVEL_ID 104

// The three engine LevelIDs used below. Crash Cove is 3 and Roo's Tubes is 6 in
// the canonical pad table, and the apworld keys retail_tracks by exactly that.
#define LEVEL_CRASH_COVE 3
#define LEVEL_ROOS_TUBES 6

static ctr_wumpa_checks off_block(void)
{
	ctr_wumpa_checks w;
	int i;
	memset(&w, 0, sizeof w);
	w.mode = CTR_CFG_WUMPA_OFF;
	w.global_code = -1;
	for (i = 0; i < CTR_CFG_WUMPA_TRACK_COUNT; i++)
		w.tracks[i] = -1;
	for (i = 0; i < CTR_CFG_WUMPA_CUSTOM_MAX; i++)
	{
		w.custom[i].cup_level_id = -1;
		w.custom[i].code = -1;
	}
	return w;
}

static ctr_wumpa_checks global_block(void)
{
	ctr_wumpa_checks w = off_block();
	w.mode = CTR_CFG_WUMPA_GLOBAL;
	w.global_code = GLOBAL_CODE;
	return w;
}

// Per-track with the two retail destinations this file exercises. Every OTHER
// destination stays -1, which is the "this seed minted no check here" state the
// wire can genuinely carry -- an apworld could ship a subset and this build must
// not invent codes for the gaps.
static ctr_wumpa_checks per_track_block(void)
{
	ctr_wumpa_checks w = off_block();
	w.mode = CTR_CFG_WUMPA_PER_TRACK;
	w.tracks[LEVEL_CRASH_COVE] = CRASH_COVE_CODE;
	w.tracks[LEVEL_ROOS_TUBES] = ROOS_TUBES_CODE;
	return w;
}

static void add_custom(ctr_wumpa_checks *w, int cupLevelID, long code,
                       int collectible, const char *uuid)
{
	w->custom[0].cup_level_id = cupLevelID;
	w->custom[0].code = code;
	w->custom[0].wumpa_collectible = collectible;
	snprintf(w->custom[0].package_uuid, sizeof w->custom[0].package_uuid, "%s",
	         uuid);
	w->custom_count = 1;
}

// A retail race on `level`. Not a cup leg, no custom bytes: the ordinary case.
static struct AP_WumpaDispatchFacts retail_facts(const ctr_wumpa_checks *w,
                                                 int level)
{
	struct AP_WumpaDispatchFacts f;
	memset(&f, 0, sizeof f);
	f.wumpa = w;
	f.destLevelID = level;
	f.servingCupLevelID = -1;
	f.seedPackageUuid = "";
	return f;
}

static struct AP_WumpaDispatchFacts custom_facts(const ctr_wumpa_checks *w,
                                                 int cupLevelID,
                                                 const char *seedUuid,
                                                 int seedCollectible)
{
	struct AP_WumpaDispatchFacts f;
	memset(&f, 0, sizeof f);
	f.wumpa = w;
	// The host arcade slot the custom bytes borrow. Present precisely so the
	// tests can prove it is never used as an identity: this is Roo's Tubes, and
	// Roo's Tubes has a real per-track code in the block below.
	f.destLevelID = LEVEL_ROOS_TUBES;
	f.servingCustom = 1;
	f.servingCupLevelID = cupLevelID;
	f.seedCustomOk = 1;
	f.seedPackageUuid = seedUuid;
	f.seedWumpaCollectible = seedCollectible;
	return f;
}

static long resolve(const struct AP_WumpaDispatchFacts *f, int *reason)
{
	*reason = -1;
	return AP_WumpaResolveCode(f, reason);
}

// ── step 1: off ─────────────────────────────────────────────────────────────

static void test_off(void)
{
	ctr_wumpa_checks w = off_block();
	struct AP_WumpaDispatchFacts f = retail_facts(&w, LEVEL_CRASH_COVE);
	int reason;

	expect_long(resolve(&f, &reason), -1, "an off seed sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_MODE_OFF, "off is reported as off");

	// A mode from a newer apworld is refused the same way. Sending nothing is
	// the only answer that cannot send a wrong code.
	w.mode = 99;
	expect_long(resolve(&f, &reason), -1, "an unknown mode sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_MODE_OFF, "an unknown mode reads as off");

	// A NULL block is the pre-parse state and must be inert rather than a crash.
	f.wumpa = 0;
	expect_long(resolve(&f, &reason), -1, "an absent block sends nothing");
	expect_long(AP_WumpaResolveCode(0, &reason), -1, "no facts at all sends nothing");
}

// ── step 2: global ──────────────────────────────────────────────────────────

static void test_global(void)
{
	ctr_wumpa_checks w = global_block();
	struct AP_WumpaDispatchFacts f = retail_facts(&w, LEVEL_CRASH_COVE);
	int reason;

	expect_long(resolve(&f, &reason), GLOBAL_CODE, "global mode sends 35016100");
	expect_int(reason, AP_WUMPA_SENT, "global mode reports a send");

	// The global identity is deliberately not a destination's, so it is valid on
	// every race where fruit can be collected, including Cup legs and the custom
	// event race.
	f.isCupLeg = 1;
	expect_long(resolve(&f, &reason), GLOBAL_CODE,
	            "global mode is valid on a Cup leg");

	f = custom_facts(&w, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 1);
	expect_long(resolve(&f, &reason), GLOBAL_CODE,
	            "global mode is valid on a custom track too");

	// A global-mode block with no code is malformed rather than off, and saying
	// so is what makes the log line useful.
	w.global_code = -1;
	f = retail_facts(&w, LEVEL_CRASH_COVE);
	expect_long(resolve(&f, &reason), -1, "global mode with no code sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_NO_GLOBAL, "the missing code is named");
}

// ── steps 3 and 4: per-track retail ─────────────────────────────────────────

static void test_per_track_retail(void)
{
	ctr_wumpa_checks w = per_track_block();
	struct AP_WumpaDispatchFacts f = retail_facts(&w, LEVEL_CRASH_COVE);
	int reason;

	expect_long(resolve(&f, &reason), CRASH_COVE_CODE,
	            "per-track sends the destination's own code");
	expect_int(reason, AP_WUMPA_SENT, "per-track reports a send");

	// Two different destinations resolve to two different codes. That, plus the
	// caller's dedup, is what makes "reach 10 on two tracks, get two locations".
	f.destLevelID = LEVEL_ROOS_TUBES;
	expect_long(resolve(&f, &reason), ROOS_TUBES_CODE,
	            "a second destination sends its own code");

	// DESTINATION IDENTITY. The caller passes gGT->levelID, which under
	// destination shuffle is the track actually loaded -- so loading Crash Cove
	// from the Roo's Tubes pad arrives here as Crash Cove and sends Crash Cove's
	// code. The physical pad never enters this decision.
	f.destLevelID = LEVEL_CRASH_COVE;
	expect_long(resolve(&f, &reason), CRASH_COVE_CODE,
	            "destination shuffle preserves destination identity");

	// A destination this seed minted no check for.
	f.destLevelID = 0;
	expect_long(resolve(&f, &reason), -1, "an unminted destination sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_NO_TRACK_CODE, "the missing code is named");

	// Not a retail race level at all: a battle arena, a hub, a cup pad LevelID.
	f.destLevelID = -1;
	expect_long(resolve(&f, &reason), -1, "a non-race level sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_UNKNOWN_LEVEL, "the bad level is named");
	f.destLevelID = CTR_CFG_WUMPA_TRACK_COUNT;
	expect_long(resolve(&f, &reason), -1, "a level past the table sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_UNKNOWN_LEVEL, "the bound is checked");
	f.destLevelID = 104;
	expect_long(resolve(&f, &reason), -1, "a cup LevelID is not a race destination");
}

static void test_gem_cup_route(void)
{
	ctr_wumpa_checks w = per_track_block();
	struct AP_WumpaDispatchFacts f = retail_facts(&w, LEVEL_CRASH_COVE);
	int reason;

	// The apworld gives every legging Cup an alternative route to this one
	// track-owned location. Native therefore dispatches from the resolved leg
	// identity without consulting the standalone pad.
	f.isCupLeg = 1;
	expect_long(resolve(&f, &reason), CRASH_COVE_CODE,
	            "a Cup leg sends its resolved track-owned Wumpa code");
	expect_int(reason, AP_WUMPA_SENT, "the Cup route is reported as a send");

	// Outside a Cup the same destination identity sends the same one location.
	f.isCupLeg = 0;
	expect_long(resolve(&f, &reason), CRASH_COVE_CODE,
	            "the standalone race is the alternative route to the same code");
}

// ── step 5: per-track custom destination ────────────────────────────────────

static void test_per_track_custom(void)
{
	ctr_wumpa_checks w = per_track_block();
	struct AP_WumpaDispatchFacts f;
	int reason;

	add_custom(&w, PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, 1, PACKAGE_UUID);

	f = custom_facts(&w, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 1);
	expect_long(resolve(&f, &reason), CUSTOM_CODE,
	            "an eligible custom destination sends its own slot code");
	expect_int(reason, AP_WUMPA_SENT, "the custom send is reported as a send");

	// THE identity rule. `destLevelID` is the borrowed host arcade slot, and
	// Roo's Tubes has a perfectly good per-track code in this same block -- so a
	// dispatch that fell back to the host level would send ROOS_TUBES_CODE here
	// and look entirely plausible in a log. It must not.
	expect_int(resolve(&f, &reason) != ROOS_TUBES_CODE, 1,
	           "a custom race never sends the host retail level's code");

	// UUID comparison is case-blind: the apworld lower-cases, a hand-written
	// config may not, and identity is not spelling.
	{
		ctr_wumpa_checks upper = w;
		struct AP_WumpaDispatchFacts uf;
		snprintf(upper.custom[0].package_uuid, sizeof upper.custom[0].package_uuid,
		         "60D5A8A8-B69A-4F6A-A0D8-9A43D91E3F2E");
		uf = custom_facts(&upper, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 1);
		expect_long(resolve(&uf, &reason), CUSTOM_CODE,
		            "package identity compares case-blind");
	}
}

static void test_custom_refusals(void)
{
	ctr_wumpa_checks w = per_track_block();
	struct AP_WumpaDispatchFacts f;
	int reason;

	add_custom(&w, PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, 1, PACKAGE_UUID);

	// The serving cup has no destination slot on the wire. That is a seed that
	// bound a custom track to a role this build's block does not carry a code
	// for, and there is nothing safe to send.
	f = custom_facts(&w, 100, PACKAGE_UUID, 1);
	expect_long(resolve(&f, &reason), -1, "an unbound cup sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_NO_CUSTOM_SLOT, "the missing slot is named");

	// A slot naming a different package. This is the wrong-content case the
	// whole identity check exists for.
	f = custom_facts(&w, PURPLE_CUP_LEVEL_ID, OTHER_UUID, 1);
	expect_long(resolve(&f, &reason), -1, "a package mismatch sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_CUSTOM_PACKAGE, "the mismatch is named");
	expect_int(resolve(&f, &reason) != ROOS_TUBES_CODE, 1,
	           "a refused custom race still never falls back to the host level");

	// No usable descriptor at all: custom_tracks_ok is 0, so there is nothing to
	// compare the slot's package against.
	f = custom_facts(&w, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 1);
	f.seedCustomOk = 0;
	expect_long(resolve(&f, &reason), -1, "no usable descriptor sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_CUSTOM_PACKAGE, "the missing descriptor is named");

	// The measured capability says there is no route to ten fruit. The apworld
	// would not have minted a slot for such a package, so reaching this state
	// means the two halves disagree -- but the client refuses on its own terms
	// rather than trusting that the apworld got it right.
	{
		ctr_wumpa_checks no = per_track_block();
		add_custom(&no, PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, 0, PACKAGE_UUID);
		f = custom_facts(&no, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 0);
		expect_long(resolve(&f, &reason), -1,
		            "a package with no route to ten fruit sends nothing");
		expect_int(reason, AP_WUMPA_REFUSE_NOT_COLLECTIBLE,
		           "the measured capability is named");
	}

	// The wire and the seed's own descriptor disagree about a MEASURED
	// capability. One of them is wrong, nothing here can tell which, so neither
	// is believed -- in both directions.
	f = custom_facts(&w, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 0);
	expect_long(resolve(&f, &reason), -1, "wire true, descriptor false sends nothing");
	expect_int(reason, AP_WUMPA_REFUSE_CAPABILITY_DISAGREE, "the disagreement is named");
	{
		ctr_wumpa_checks no = per_track_block();
		add_custom(&no, PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, 0, PACKAGE_UUID);
		f = custom_facts(&no, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 1);
		expect_long(resolve(&f, &reason), -1,
		            "wire false, descriptor true sends nothing");
		expect_int(reason, AP_WUMPA_REFUSE_CAPABILITY_DISAGREE,
		           "the disagreement is named in both directions");
	}

	// A slot with no code is not a slot. -1 would otherwise flow straight
	// through as "send nothing" with a misleading reason.
	{
		ctr_wumpa_checks empty = per_track_block();
		add_custom(&empty, PURPLE_CUP_LEVEL_ID, -1, 1, PACKAGE_UUID);
		f = custom_facts(&empty, PURPLE_CUP_LEVEL_ID, PACKAGE_UUID, 1);
		expect_long(resolve(&f, &reason), -1, "a codeless slot sends nothing");
		expect_int(reason, AP_WUMPA_REFUSE_NO_CUSTOM_SLOT,
		           "a codeless slot reads as no slot");
	}
}

static void test_custom_faulted(void)
{
	ctr_wumpa_checks blocks[2] = { global_block(), per_track_block() };
	const char *modes[2] = { "global", "per-track" };
	int mode;
	int cupLeg;

	add_custom(&blocks[1], PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, 1, PACKAGE_UUID);
	for (mode = 0; mode < 2; mode++)
	for (cupLeg = 0; cupLeg <= 1; cupLeg++)
	{
		struct AP_WumpaDispatchFacts f = retail_facts(
		    &blocks[mode], LEVEL_ROOS_TUBES);
		char name[96];
		int reason;

		f.isCupLeg = cupLeg;
		f.customFaulted = 1;
		snprintf(name, sizeof name, "faulted %s %s sends nothing",
		         modes[mode], cupLeg ? "cup leg" : "standalone load");
		expect_long(resolve(&f, &reason), -1, name);
		snprintf(name, sizeof name, "faulted %s %s names the serve fault",
		         modes[mode], cupLeg ? "cup leg" : "standalone load");
		expect_int(reason, AP_WUMPA_REFUSE_CUSTOM_FAULTED, name);
	}
}

// ── the lookup itself ───────────────────────────────────────────────────────

static void test_slot_lookup(void)
{
	ctr_wumpa_checks w = per_track_block();

	expect_int(AP_WumpaCustomSlot(&w, PURPLE_CUP_LEVEL_ID) == 0, 1,
	           "an empty block has no slots");
	add_custom(&w, PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, 1, PACKAGE_UUID);
	expect_int(AP_WumpaCustomSlot(&w, PURPLE_CUP_LEVEL_ID) != 0, 1,
	           "the bound cup finds its slot");
	expect_int(AP_WumpaCustomSlot(&w, 100) == 0, 1, "another cup finds nothing");

	// The cup range is checked before the array is walked, so a caller may hand
	// over whatever the runtime reported without a range test of its own.
	expect_int(AP_WumpaCustomSlot(&w, 99) == 0, 1, "below the cup range is refused");
	expect_int(AP_WumpaCustomSlot(&w, 105) == 0, 1, "above the cup range is refused");
	expect_int(AP_WumpaCustomSlot(&w, -1) == 0, 1, "a negative cup is refused");
	expect_int(AP_WumpaCustomSlot(0, PURPLE_CUP_LEVEL_ID) == 0, 1,
	           "a NULL block is refused");

	// custom_count bounds the walk, not the array size: a slot past the count is
	// stale data, not a binding.
	w.custom_count = 0;
	expect_int(AP_WumpaCustomSlot(&w, PURPLE_CUP_LEVEL_ID) == 0, 1,
	           "custom_count bounds the search");
}

static void test_refusal_text(void)
{
	int reason;
	// Every enumerator has a phrase, and none of them is the fallback. A refusal
	// that logs "unknown reason" is a refusal nobody can act on.
	for (reason = AP_WUMPA_SENT;
	     reason <= AP_WUMPA_REFUSE_CUSTOM_FAULTED; reason++)
	{
		checks++;
		if (strcmp(AP_WumpaRefusalText(reason), "unknown reason") == 0)
		{
			failures++;
			printf("FAIL refusal %d has no phrase\n", reason);
		}
	}
	expect_int(strcmp(AP_WumpaRefusalText(-1), "unknown reason") == 0, 1,
	           "an out-of-range reason falls back rather than reading past the table");
}

// ── every term shown load-bearing ───────────────────────────────────────────
//
// A sweep over the whole fact space this decision reads. Its job is not to
// enumerate outcomes -- the named cases above do that -- but to assert the
// invariants that must hold for EVERY combination, including the ones nobody
// thought to name.
static void test_sweep(void)
{
	ctr_wumpa_checks w;
	int mode, level, cupLeg, custom, cupID, slotCollectible, seedCollectible;
	int seedOk, sameUuid;
	int cases = 0;
	int sent = 0;

	for (mode = 0; mode <= 2; mode++)
	for (level = -1; level < CTR_CFG_WUMPA_TRACK_COUNT; level++)
	for (cupLeg = 0; cupLeg <= 1; cupLeg++)
	for (custom = 0; custom <= 1; custom++)
	for (cupID = 100; cupID <= 104; cupID++)
	for (slotCollectible = 0; slotCollectible <= 1; slotCollectible++)
	for (seedCollectible = 0; seedCollectible <= 1; seedCollectible++)
	for (seedOk = 0; seedOk <= 1; seedOk++)
	for (sameUuid = 0; sameUuid <= 1; sameUuid++)
	{
		struct AP_WumpaDispatchFacts f;
		int reason = -1;
		long code;

		w = off_block();
		w.mode = mode;
		if (mode == CTR_CFG_WUMPA_GLOBAL)
			w.global_code = GLOBAL_CODE;
		if (mode == CTR_CFG_WUMPA_PER_TRACK)
		{
			w.tracks[LEVEL_CRASH_COVE] = CRASH_COVE_CODE;
			w.tracks[LEVEL_ROOS_TUBES] = ROOS_TUBES_CODE;
			add_custom(&w, PURPLE_CUP_LEVEL_ID, CUSTOM_CODE, slotCollectible,
			           PACKAGE_UUID);
		}

		memset(&f, 0, sizeof f);
		f.wumpa = &w;
		f.destLevelID = level;
		f.isCupLeg = cupLeg;
		f.servingCustom = custom;
		f.servingCupLevelID = custom ? cupID : -1;
		f.seedCustomOk = seedOk;
		f.seedPackageUuid = sameUuid ? PACKAGE_UUID : OTHER_UUID;
		f.seedWumpaCollectible = seedCollectible;

		code = AP_WumpaResolveCode(&f, &reason);
		cases++;
		if (code >= 0)
			sent++;

		// 1. A code is sent only with a reason of SENT, and no code always has a
		//    refusal reason. The log line can never contradict the outcome.
		checks++;
		if ((code >= 0) != (reason == AP_WUMPA_SENT))
		{
			failures++;
			printf("FAIL sweep: code %ld with reason %d\n", code, reason);
		}

		// 2. Only ever one of the four minted codes. Nothing here can invent a
		//    code that was not on the wire.
		checks++;
		if (code >= 0 && code != GLOBAL_CODE && code != CRASH_COVE_CODE &&
		    code != ROOS_TUBES_CODE && code != CUSTOM_CODE)
		{
			failures++;
			printf("FAIL sweep: invented code %ld\n", code);
		}

		// 3. An off seed never sends, whatever the runtime looks like.
		checks++;
		if (mode == CTR_CFG_WUMPA_OFF && code >= 0)
		{
			failures++;
			printf("FAIL sweep: off seed sent %ld\n", code);
		}

		// 4. THE identity invariant. While custom bytes are being served in
		//    per-track mode, the only code that may be sent is the custom slot's
		//    -- never the borrowed host level's, and never another destination's.
		checks++;
		if (mode == CTR_CFG_WUMPA_PER_TRACK && custom && code >= 0 &&
		    code != CUSTOM_CODE)
		{
			failures++;
			printf("FAIL sweep: custom race sent %ld\n", code);
		}

		// 5. A custom send requires the package to match AND both measured
		//    capabilities to agree on true. Written as the contrapositive so the
		//    sweep proves each term is load-bearing rather than merely present.
		checks++;
		if (code == CUSTOM_CODE &&
		    !(seedOk && sameUuid && slotCollectible && seedCollectible))
		{
			failures++;
			printf("FAIL sweep: custom sent without every term "
			       "(ok=%d uuid=%d slot=%d seed=%d)\n",
			       seedOk, sameUuid, slotCollectible, seedCollectible);
		}

		// 6. A retail per-track send is always the destination's OWN code,
		//    including when that destination is running as a Cup leg.
		checks++;
		if (mode == CTR_CFG_WUMPA_PER_TRACK && !custom && code >= 0 &&
		    code != w.tracks[level])
		{
			failures++;
			printf("FAIL sweep: retail sent %ld for level %d\n", code, level);
		}
	}

	// A sweep that never sends anything would satisfy every invariant above and
	// prove nothing, so assert it exercised both outcomes.
	expect_int(cases > 1000, 1, "the sweep covers a real combination space");
	expect_int(sent > 0, 1, "the sweep actually sends in some configurations");
	printf("sweep: %d configurations, %d sends\n", cases, sent);
}

int main(void)
{
	test_off();
	test_global();
	test_per_track_retail();
	test_gem_cup_route();
	test_per_track_custom();
	test_custom_refusals();
	test_custom_faulted();
	test_slot_lookup();
	test_refusal_text();
	test_sweep();

	printf("test-wumpa-dispatch: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
