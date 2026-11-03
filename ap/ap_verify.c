// Client-side seed completability verification. See ap_verify.h for the
// contract. Compiled into the C unity build (game/game_unity.h) after
// ap_hooks.c, so the public AP_* / ctr_cfg_* / ap_net_* interfaces and the
// game's static data (data.metaDataLEV) are all in scope.

#ifdef CTR_AP

#include "ap_verify.h"
#include "ap_box_map.h"
#include "ap_verify_logic.h"
#include "ap_lettersanity.h"
#include "ap_cup_box_policy.h" // AP_HubKeysForPad: the shared hub-spine Key table
#include "ap_relic_goal.h"
#include "ap_oxide_encounter.h" // the ONE Oxide encounter + gate decision (#320/#321)
#include "ap_content_plan.h"    // the content-plan sweep (#133 PR 8)

// ---------------------------------------------------------------------------
// Verdict state (recomputed by the sweep, read by the log/banner).
// ---------------------------------------------------------------------------
static int      ap_vf_have = 0;         // 1 once a verdict was computed
static int      ap_vf_goal_ok = 0;      // goal reachable at the fixed point
static int      ap_vf_total = 0;        // locations present in this seed
static int      ap_vf_reachable = 0;    // reachable at the fixed point
static int      ap_vf_keys_fp = 0;      // Keys held at the fixed point
static int      ap_vf_solo = 0;         // 1 = single-slot room (definitive)
static unsigned ap_vf_gen = 0;          // AP_StateGen at last compute
static int      ap_vf_truncated = 0;    // 1 worklist overflowed, 2 content-plan model or tally incomplete: verdict is not trustworthy
static int      ap_vf_settled = 1;      // #85: last verdict computed with no checks in flight
static int      ap_vf_coverage_exact = 0; // modeled locations == server-declared locations
static unsigned ap_vf_won_bits = 0;     // #458 boss-won flags the last content-plan verdict read

// #144: reachable/Keys last printed by the "waiting on other worlds" line, so a
// resettle re-run (line 535) that lands on an unchanged multiworld snapshot
// doesn't repeat the same line verbatim -- one submitted log printed it 15
// times in a row with identical numbers.
static int      ap_vf_wait_last_reachable = -1;
static int      ap_vf_wait_last_keys = -1;

// Requirement met under SIMULATED counts. Single source of truth is
// AP_ReqMetCounts (ap_hooks.c) -- the same comparator the live gates use,
// parameterised by the counts array.

// The verdict's shared tail: settle and coverage state, and the log lines.
// Both sweeps (the static-table one and the content-plan one) set
// ap_vf_goal_ok, ap_vf_reachable, ap_vf_total, ap_vf_keys_fp and the first
// `listed` unreachable locations, then call this.
#define AP_VF_LISTED 16
static long ap_vf_listed_code[AP_VF_LISTED];
static int  ap_vf_listed_kind[AP_VF_LISTED];  // -1 for a content-plan check
static int  ap_vf_listed_track[AP_VF_LISTED];
static void ap_vf_publish(int listed)
{
	ap_vf_solo = (ap_net_player_count() == 1);
	ap_vf_coverage_exact = (ap_vf_total == ap_net_location_count());
	ap_vf_have = 1;
	// #85: a verdict computed while an own check is still in flight is a transient
	// snapshot; the banner waits for a settled one (the log still records this).
	ap_vf_settled = (ap_net_checks_in_flight() == 0);
	// A truncated worklist means the sweep reasoned over a partial seed. Never
	// report that as a definitive failure: an "I could not check this" verdict
	// is useful, a false "your seed is broken" is worse than no verifier.
	if (ap_vf_truncated || !ap_vf_coverage_exact)
		ap_vf_goal_ok = 1;

	{
		char msg[256];
		int wait_repeat = 0;
		// Two vocabularies on purpose: in SOLO the sweep is definitive, so a
		// blocked goal is alarming and should read that way. In a MULTIWORLD the
		// sweep can only see this world's own items, so "not reachable from here
		// alone" is the NORMAL state early on -- alarming words here just send
		// players to Discord with healthy seeds.
		if (ap_vf_truncated == 2)
			snprintf(msg, sizeof msg,
			         "[AP VERIFY] INDETERMINATE: the content-plan model could not "
			         "be built or the received-item tally overflowed. No "
			         "completability claim is made for this seed.\n");
		else if (!ap_vf_coverage_exact)
			snprintf(msg, sizeof msg,
			         "[AP VERIFY] INDETERMINATE: model covers %d/%d server-declared "
			         "locations. No completability claim is made.\n",
			         ap_vf_total, ap_net_location_count());
		else if (ap_vf_solo)
			snprintf(msg, sizeof msg,
			         "[AP VERIFY] %s: goal %s, %d/%d locations reachable, Keys %d "
			         "(solo: definitive)\n",
			         ap_vf_goal_ok ? "seed OK" : "GOAL BLOCKED",
			         ap_vf_goal_ok ? "reachable" : "NOT reachable",
			         ap_vf_reachable, ap_vf_total, ap_vf_keys_fp);
		else if (ap_vf_goal_ok)
			snprintf(msg, sizeof msg,
			         "[AP VERIFY] seed OK from this world alone: goal reachable, "
			         "%d/%d locations reachable, Keys %d (multiworld projection)\n",
			         ap_vf_reachable, ap_vf_total, ap_vf_keys_fp);
		else
		{
			// #144: same reachable/Keys as the last time this line printed -- the
			// snapshot hasn't moved, so repeating it verbatim is noise, not signal.
			wait_repeat = (ap_vf_reachable == ap_vf_wait_last_reachable &&
			               ap_vf_keys_fp == ap_vf_wait_last_keys);
			ap_vf_wait_last_reachable = ap_vf_reachable;
			ap_vf_wait_last_keys = ap_vf_keys_fp;
			if (!wait_repeat)
				snprintf(msg, sizeof msg,
				         "[AP VERIFY] waiting on other worlds (NORMAL in a multiworld): "
				         "%d/%d locations reachable with this world's items alone, Keys "
				         "%d. Informational only; improves as items arrive from other "
				         "players.\n",
				         ap_vf_reachable, ap_vf_total, ap_vf_keys_fp);
		}
		if (!wait_repeat)
			AP_LogLine(msg);
		if (ap_vf_truncated == 1)
			AP_LogLine("[AP VERIFY] INDETERMINATE: location worklist overflowed "
			           "(seed carries more locations than this build can track). "
			           "No completability claim is made for this seed.\n");

		// The per-location dump is diagnostic detail for SOLO (where "unreachable"
		// means broken). In a multiworld it would enumerate locations that simply
		// need other players' items -- 16 lines of noise under an informational
		// header, so it stays solo-only.
		if (ap_vf_coverage_exact && ap_vf_solo && ap_vf_truncated != 2 &&
		    (!ap_vf_goal_ok || ap_vf_reachable < ap_vf_total))
		{
			int k;
			for (k = 0; k < listed; k++)
			{
				if (ap_vf_listed_kind[k] < 0)
					snprintf(msg, sizeof msg,
					         "[AP VERIFY]   unreachable: code %ld (content plan)\n",
					         ap_vf_listed_code[k]);
				else
					snprintf(msg, sizeof msg,
					         "[AP VERIFY]   unreachable: code %ld (kind %d track %d)\n",
					         ap_vf_listed_code[k], ap_vf_listed_kind[k], ap_vf_listed_track[k]);
				AP_LogLine(msg);
			}
		}
	}
}

// #133 PR 8: a content-plan room. Its plan is the room's logic as data, and
// native admitted it only after re-deriving every check, route, requirement
// and the goal, so the sweep interprets the plan (ap_content_verify.hpp)
// instead of the static tables below, from the same foreign tally and scout
// cache.
typedef struct { int overflow; } ap_vf_content_ctx;
static int ap_vf_content_foreign(void *ctx, int64_t item)
{
	int n = AP_VerifyForeignItemCodeCount(item);
	if (n < 0)
	{
		((ap_vf_content_ctx *)ctx)->overflow = 1;
		return 0;
	}
	return n;
}
static int ap_vf_content_scout(void *ctx, int64_t location, int64_t *own_item)
{
	long long item; int player; unsigned flags;
	(void)ctx;
	*own_item = 0;
	if (!ap_net_scout_known(location, &item, &player, &flags))
		return 0;
	if (player == ap_net_self_slot())
		*own_item = item;
	return 1;
}
static int ap_vf_content_checked(void *ctx, int64_t location)
{
	(void)ctx;
	return ap_net_location_checked(location);
}
// The #458 boss-won flags, the same source the goal reads (ap_hooks_goal.c):
// bosses 0..3 and N. Oxide's Challenge. The Final Challenge has no flag and
// its checked location proves no win, so it takes its encounter rule only.
static int ap_vf_content_won(void *ctx, int64_t completion_location)
{
	unsigned bits = ap_net_boss_won_bits();
	(void)ctx;
	if (completion_location >= AP_BOSS_WON_LOCATION_CODE(0) &&
	    completion_location <= AP_BOSS_WON_LOCATION_CODE(3))
		return (bits & AP_BOSS_WON_BIT((int)(completion_location - AP_BOSS_WON_LOCATION_CODE(0)))) != 0;
	if (completion_location == AP_BOSS_WON_OXIDE_FIRST_CODE)
		return (bits & AP_BOSS_WON_OXIDE_FIRST) != 0;
	return 0;
}
static void ap_vf_recompute_content(void)
{
	ap_vf_content_ctx ctx = { 0 };
	ap_content_verify_io io = { &ctx, ap_vf_content_foreign, ap_vf_content_scout,
	                            ap_vf_content_checked, ap_vf_content_won, 0 };
	ap_content_verify_result r;
	int k;
	ap_vf_truncated = 0;
	if (!ap_content_plan_verify(&io, &r) || ctx.overflow)
		ap_vf_truncated = 2;
	ap_vf_goal_ok = r.goal;
	ap_vf_total = r.total;
	ap_vf_reachable = r.reachable;
	ap_vf_keys_fp = r.keys;
	for (k = 0; k < r.unreachable_count && k < AP_VF_LISTED; k++)
	{
		ap_vf_listed_code[k] = (long)r.unreachable[k];
		ap_vf_listed_kind[k] = -1;
		ap_vf_listed_track[k] = -1;
	}
	ap_vf_publish(k);
}

// ---------------------------------------------------------------------------
// The sweep.
// ---------------------------------------------------------------------------
static void ap_vf_recompute(void)
{
	// Every admitted room is a content plan (one generator). Row 12 deleted
	// the normal-seed sweep that used to follow here.
	ap_vf_recompute_content();
}

void AP_VerifyOnFrame(void)
{
	if (!ap_net_is_connected())
	{
		ap_vf_have = 0;
		return;
	}
	// slot_data absent (vanilla-rules seed) or from a NEWER apworld than this
	// build parses: the sweep would reason over wrong/partial gates -- skip.
	// The schema-newer case already has its own loud banner. A content-plan
	// room is swept from its plan (#133 PR 8); a plan that is present but not
	// yet bound to the room is skipped until it is.
	if (!ctr_cfg_active() || ctr_cfg.schema_newer ||
	    (ap_content_plan_present() && !ap_content_plan_active()))
	{
		ap_vf_have = 0; // never let a previous seed's verdict survive here
		return;
	}
	if (!ap_net_scouts_ready())
	{
		ap_vf_have = 0; // scouts were cleared by a (re)connect: verdict is stale
		return;         // LocationInfo not in yet; try again next frame
	}
	// #133 PR 8: a content-plan room with boss garages reads the #458
	// boss-won flags, which arrive from data storage after connect and do not
	// bump the state generation. Wait for them like the scouts, and treat a
	// change in them like a generation change.
	if (ap_content_plan_active() && ap_content_plan_bosses()->present &&
	    !ap_net_boss_won_known())
	{
		ap_vf_have = 0;
		return;
	}
	unsigned gen = AP_StateGen();
	unsigned won = ap_content_plan_active() ? ap_net_boss_won_bits() : 0;
	if (won != ap_vf_won_bits)
	{
		ap_vf_won_bits = won;
		ap_vf_have = 0;
	}
	if (ap_vf_have && gen == ap_vf_gen)
	{
		// Gen unchanged, but a drain of only NON-gate items (Wumpa idx 15, traps)
		// clears the in-flight check set WITHOUT bumping the gen (ap_hooks only bumps
		// for gate items). Re-run once so a verdict computed while a check was still
		// outstanding settles and the banner is no longer withheld; the gen compare
		// alone would strand an unsettled verdict forever (issue #85).
		if (!ap_vf_settled && ap_net_checks_in_flight() == 0)
			ap_vf_recompute();
		return;
	}
	ap_vf_gen = gen;
	ap_vf_recompute();
}

void AP_DrawVerifyWarning(void)
{
	// Deliberately log-only. The verifier duplicates a changing apworld logic
	// surface and has produced player-facing false positives when a location
	// class drifted. Keep the draw hook as a no-op so existing call sites remain
	// stable; schema/version incompatibility retains its separate loud banner.
}

#endif // CTR_AP
