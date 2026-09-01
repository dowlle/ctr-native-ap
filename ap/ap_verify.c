// Client-side seed completability verification. See ap_verify.h for the
// contract. Compiled into the C unity build (game/game_unity.h) after
// ap_hooks.c, so the public AP_* / ctr_cfg_* / ap_net_* interfaces and the
// game's static data (data.metaDataLEV) are all in scope.

#ifdef CTR_AP

#include "ap_verify.h"
#include "ap_box_map.h"
#include "ap_verify_logic.h"
#include "ap_verify_wumpa.h"
#include "ap_cup_box_policy.h" // AP_HubKeysForPad: the shared hub-spine Key table
#include "ap_relic_goal.h"

// ---------------------------------------------------------------------------
// Static vanilla topology (native is the source of truth).
//
// The hub-spine Key table this sweep needs used to be a private copy here. It
// now lives in ap_cup_box_policy.h, because the Gem Cup AP-box policy (WO-A3)
// needs the SAME structural reachability predicate and two copies of a topology
// table drift. AP_HubKeysForPad is that table's accessor: received Keys needed
// to stand at a physical pad, -1 for a non-adventure pad. Values unchanged.
// ---------------------------------------------------------------------------

// Keys required to stand at each boss garage (garage hub). Oxide's own Key-4
// requirement (boss_req[4]) subsumes his Gem Stone Valley hub cost.
static const int ap_vf_boss_keys[4] = {
	/* 0 roo (N. Sanity) */ 0, /* 1 papu (Lost Ruins) */ 1,
	/* 2 komodo (Glacier) */ 2, /* 3 pinstripe (Citadel) */ 3,
};

// Crystal arena LevelID for each crystal AdvProgress bit 111..114 (the
// ap_locations.h order: Skull Rock, Rampage Ruins, Rocky Road, Nitro Court).
static const int ap_vf_crystal_lid[4] = { 21, 19, 23, 18 };

// ---------------------------------------------------------------------------
// Location worklist: the static table entries plus every podium rung the seed
// can carry. The multiplier is DERIVED from CTR_CFG_PODIUM_RUNG_COUNT on
// purpose: this used to hardcode 3 (the pre-Phase-A ladder) while the fill loop
// below already wrote 5 rungs per track, so the worklist silently truncated at
// 149 entries and the sweep then declared perfectly good seeds unbeatable.
// If the rung model changes again, this follows it automatically.
// ---------------------------------------------------------------------------
#define AP_VF_MAX_LOCS (AP_LOCATION_TABLE_LEN + \
	CTR_CFG_PODIUM_TRACK_COUNT * CTR_CFG_PODIUM_RUNG_COUNT + \
	AP_BOX_LOCATION_COUNT + CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT + \
	AP_ITEMSANITY_WEAPON_COUNT * 2 + CTR_CFG_WUMPA_TRACK_COUNT + \
	CTR_CFG_WUMPA_CUSTOM_MAX)

typedef enum
{
	AP_VF_TROPHY = 0, // trophy race (dest 0..15) -> pad stage 1
	AP_VF_TIER2,      // relic TT / token challenge  -> pad stage 1 + stage 2
	AP_VF_TRIAL_TT,   // Slide/Turbo relic TT (dest 16/17) -> pad stage 1
	AP_VF_PODIUM,     // podium rung (dest 0..15) -> pad stage 1
	AP_VF_CRYSTAL,    // crystal bonus (arena dest)  -> pad stage 1
	AP_VF_GEMCUP,     // gem cup reward (dest 100..104) -> pad stage 1
	AP_VF_BOSS,       // boss race 0..3 -> garage hub keys + garage gate
	AP_VF_OXIDE,      // N. Oxide's Challenge -> boss_req[4]
	AP_VF_OXIDE_FIN,  // N. Oxide's Final Challenge -> boss_req[4] + relic mode
	AP_VF_BOX,        // authored AP item box on a race track
	AP_VF_LETTER,     // C/T/R pickup inside a token challenge
	AP_VF_ITEMSANITY, // global weapon-use check (plain/juiced share access)
	AP_VF_WUMPA,      // global reach-10-Wumpa check
	AP_VF_CUSTOM,     // generic custom Trophy or podium rung -> assigned surface
} ap_vf_kind;

typedef struct
{
	long code;    // AP location code
	int  kind;    // ap_vf_kind
	int  track;   // destination LevelID (races/trials/arenas/cups) or bossIdx
	int  detail;  // podium rung / box slot (1-based) / letter / weapon index
} ap_vf_loc;

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
static int      ap_vf_truncated = 0;    // worklist overflowed: verdict is not trustworthy
static int      ap_vf_settled = 1;      // #85: last verdict computed with no checks in flight
static int      ap_vf_coverage_exact = 0; // modeled locations == server-declared locations

// #144: reachable/Keys last printed by the "waiting on other worlds" line, so a
// resettle re-run (line 535) that lands on an unchanged multiworld snapshot
// doesn't repeat the same line verbatim -- one submitted log printed it 15
// times in a row with identical numbers.
static int      ap_vf_wait_last_reachable = -1;
static int      ap_vf_wait_last_keys = -1;

// Requirement met under SIMULATED counts. Single source of truth is
// AP_ReqMetCounts (ap_hooks.c) -- the same comparator the live gates use,
// parameterised by the counts array.

// ctr_cfg_warp_unlocked, evaluated against simulated counts. Mirrors the live
// predicate branch-for-branch (per-seed stage1, cup fallback, Phase-1 trophy
// floor) so sweep and gate cannot disagree.
static int ap_vf_stage1_met(int lid, const int *counts)
{
	int i, owned;

	if (ctr_cfg_active() && lid >= 0 && lid < CTR_CFG_PAD_COUNT &&
	    ctr_cfg.warp_pad_unlock[lid].stage1.type != 0)
		return AP_ReqMetCounts(&ctr_cfg.warp_pad_unlock[lid].stage1, counts);
	if (lid >= 100 && lid <= 104)
	{
		int cupIdx = lid - 100;
		if (ctr_cfg_active() && ctr_cfg.gem_cup_unlock[cupIdx].stage1.type != 0)
			return AP_ReqMetCounts(&ctr_cfg.gem_cup_unlock[cupIdx].stage1, counts);
		return counts[AP_IDX_TOKEN_RED + cupIdx] >= 4;
	}
	// VANILLA FALLBACKS. These must mirror AP_PadStage1Met (ap_hooks.c) class for
	// class -- the trophy floor below is the rule for RACE pads (0..15) ONLY.
	// Trial pads: Slide Coliseum = 10 Sapphire relics, Turbo Track = all 5 gem
	// colours. data.metaDataLEV[16/17].numTrophiesToOpen (10 / 15) is NOT a
	// trophy gate for these pads and reading it here both over- and
	// under-constrained the sweep.
	if (lid == 16)
		return counts[AP_IDX_SAPPHIRE] >= 10;
	if (lid == 17)
	{
		for (owned = 0, i = 0; i < 5; i++)
			if (counts[AP_IDX_GEM_RED + i] >= 1)
				owned++;
		return owned >= 5;
	}
	// Battle arenas: vanilla rule is the hub-key gate ONLY (the sweep already
	// applies that via AP_HubKeysForPad), never a trophy floor.
	if (lid == 18 || lid == 19 || lid == 21 || lid == 23)
		return 1;
	return counts[AP_IDX_TROPHY] >= data.metaDataLEV[lid].numTrophiesToOpen;
}

static int ap_vf_stage2_met(int lid, const int *counts)
{
	const ctr_req *r = ctr_cfg_warp_stage2_req(lid);
	return r ? AP_ReqMetCounts(r, counts) : 1;
}

// AP_OxideFinalOpen against simulated counts (same mode table).
static int ap_vf_oxide_final_met(const int *counts)
{
	int n = ctr_cfg.oxide_final_count > 0 ? ctr_cfg.oxide_final_count : 18;
	int s = counts[AP_IDX_SAPPHIRE], g = counts[AP_IDX_GOLD],
	    p = counts[AP_IDX_PLATINUM];
	return AP_RelicGoalMet(ctr_cfg.oxide_final_unlock, n, s, g, p);
}

// Bank a location's OWN scouted item into the simulated counts (issue #85). A
// no-op for foreign or unscouted locations -- their receipts live in the foreign
// tally (AP_GateCountForeign) instead, so nothing is double-counted. Shared by the
// checked-at-start pass and the fixed-point sweep so the two banking paths cannot
// drift.
static void ap_vf_bank_own(long code, int *counts)
{
	long long item; int player; unsigned flags;
	if (ap_net_scout_known(code, &item, &player, &flags) &&
	    player == ap_net_self_slot())
	{
		long long idx = item - AP_ITEM_BASE;
		if (idx >= 0 && idx < AP_VF_ITEM_COUNT)
			counts[(int)idx]++;
	}
}

static int ap_vf_required_character(int pad)
{
	if (!ctr_cfg.racer_locked_pads)
		return -1;
	if (pad >= 100 && pad <= 104)
		return ctr_cfg.gem_cup_racer_lock[pad - 100];
	if (pad >= 0 && pad < CTR_CFG_PAD_COUNT)
		return ctr_cfg.racer_lock[pad];
	return -1;
}

static AP_VerifyOptions ap_vf_options(void)
{
	AP_VerifyOptions o;
	o.boost_mode = ctr_cfg.boost_mode;
	o.stats_mode = ctr_cfg.stats_mode;
	o.character_unlocks = ctr_cfg.character_unlocks;
	o.starting_character = ctr_cfg.starting_character;
	o.itemsanity = ctr_cfg.itemsanity;
	o.logic_difficulty = ctr_cfg.logic_difficulty;
	o.shortcut_knowledge = ctr_cfg.shortcut_knowledge;
	return o;
}

static int ap_vf_pad_open(int pad, const int *counts)
{
	AP_VerifyOptions o = ap_vf_options();
	int keys, required;
	if (pad < 0)
		return 0;
	keys = AP_HubKeysForPad(pad);
	if (keys < 0 || counts[AP_IDX_KEY] < keys || !ap_vf_stage1_met(pad, counts))
		return 0;
	required = ap_vf_required_character(pad);
	return required < 0 || AP_VerifyCharacterUnlocked(&o, counts, required);
}

static int ap_vf_trophy_capable(int track, int pad, const int *counts)
{
	AP_VerifyOptions o = ap_vf_options();
	return AP_VerifyTrophyCapabilityGate(&o, counts, track,
		ap_vf_required_character(pad));
}

static int ap_vf_cup_capable(int cup, const int *counts, const int *pad_for_dest)
{
	AP_VerifyOptions o = ap_vf_options();
	int leg;
	// A cup displaced by this seed's custom_tracks block LEGS NOTHING: it is one
	// race on a custom track, not four retail legs. The wire still carries its
	// complete four-track gem_cup_legs row -- it has to, or the block stops being
	// the complete mapping every other consumer relies on -- so the row is a
	// don't-care this verifier has to actively not care about. Reading it would
	// verify a cup this seed does not contain, and would gate the Gem behind
	// capability terms belonging to tracks the player never races.
	if (ctr_cfg_cup_displaced(cup))
		return 1;

	for (leg = 0; leg < 4; leg++)
	{
		int track = ctr_cfg_cup_leg(cup, leg);
		int pad = track >= 0 && track < 105 ? pad_for_dest[track] : -1;
		if (!AP_VerifyCupLegCapability(&o, counts, track,
				ap_vf_required_character(pad)))
			return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------
// The sweep.
// ---------------------------------------------------------------------------
static void ap_vf_recompute(void)
{
	ap_vf_loc  locs[AP_VF_MAX_LOCS];
	char       state[AP_VF_MAX_LOCS]; // 0 open, 1 collected
	int        counts[AP_VF_ITEM_COUNT];
	int        n = 0, i, t;
	AP_VerifyWumpaLocation wumpa_locs[CTR_CFG_WUMPA_TRACK_COUNT +
		CTR_CFG_WUMPA_CUSTOM_MAX];

	ap_vf_truncated = 0; // per-sweep state: a stale flag must not poison later verdicts

	// Build the worklist from the static table...
	for (i = 0; i < AP_LOCATION_TABLE_LEN; i++)
	{
		int bit = AP_LOCATION_TABLE[i].bit_index;
		ap_vf_loc L;
		L.code = AP_LOCATION_TABLE[i].location_code;
		L.detail = -1;
		if (bit >= 6 && bit <= 21)        { L.kind = AP_VF_TROPHY;  L.track = bit - 6; }
		else if (bit >= 22 && bit <= 39)  { L.kind = (bit - 22 >= 16) ? AP_VF_TRIAL_TT : AP_VF_TIER2; L.track = bit - 22; }
		else if (bit >= 40 && bit <= 57)  { L.kind = (bit - 40 >= 16) ? AP_VF_TRIAL_TT : AP_VF_TIER2; L.track = bit - 40; }
		else if (bit >= 58 && bit <= 75)  { L.kind = (bit - 58 >= 16) ? AP_VF_TRIAL_TT : AP_VF_TIER2; L.track = bit - 58; }
		else if (bit >= 76 && bit <= 91)  { L.kind = AP_VF_TIER2;   L.track = bit - 76; }
		else if (bit >= 94 && bit <= 97)  { L.kind = AP_VF_BOSS;    L.track = bit - 94; }
		else if (bit >= 106 && bit <= 110){ L.kind = AP_VF_GEMCUP;  L.track = 100 + (bit - 106); }
		else if (bit >= 111 && bit <= 114){ L.kind = AP_VF_CRYSTAL; L.track = ap_vf_crystal_lid[bit - 111]; }
		else if (bit == AP_GOAL_BIT_OXIDE_FIRST)  { L.kind = AP_VF_OXIDE;     L.track = -1; }
		else if (bit == AP_GOAL_BIT_OXIDE_SECOND) { L.kind = AP_VF_OXIDE_FIN; L.track = -1; }
		else continue;
		locs[n++] = L;
	}
	// ...plus the per-seed podium rungs (scouted on connect like everything else).
	if (ctr_cfg.podium_enabled)
		for (t = 0; t < CTR_CFG_PODIUM_TRACK_COUNT; t++)
		{
			long rung[5] = { ctr_cfg.podium[t].held_1st, ctr_cfg.podium[t].held_3rd,
			                 ctr_cfg.podium[t].held_5th, ctr_cfg.podium[t].finish_podium,
			                 ctr_cfg.podium[t].finish_any };
			for (i = 0; i < CTR_CFG_PODIUM_RUNG_COUNT; i++)
				if (rung[i] >= 0)
				{
					if (n >= AP_VF_MAX_LOCS)
					{
						// Must never happen now the capacity is derived, but if
						// it ever does, say so loudly and refuse to give a
						// verdict rather than reporting a false failure.
						ap_vf_truncated = 1;
						break;
					}
					locs[n].code = rung[i];
					locs[n].kind = AP_VF_PODIUM;
					locs[n].track = t;
					locs[n].detail = i;
					n++;
				}
		}

#ifdef CTR_CUSTOM_TRACKS
	// The displaced retail cup Gem is absent from this seed's scout set. Replace
	// it in the model with the generic custom Trophy and this seed's enabled
	// custom podium rungs, all reached through the assigned destination surface.
	if (ctr_cfg.custom_tracks_ok)
	{
		long custom[1 + CTR_CFG_PODIUM_RUNG_COUNT] = {
			ctr_cfg.custom_track.trophy_location,
			ctr_cfg.custom_track.podium.held_1st,
			ctr_cfg.custom_track.podium.held_3rd,
			ctr_cfg.custom_track.podium.held_5th,
			ctr_cfg.custom_track.podium.finish_podium,
			ctr_cfg.custom_track.podium.finish_any,
		};
		for (i = 0; i < 1 + CTR_CFG_PODIUM_RUNG_COUNT; i++)
			if (custom[i] > 0)
			{
				locs[n].code = custom[i];
				locs[n].kind = AP_VF_CUSTOM;
				locs[n].track = ctr_cfg.custom_track.replaces_cup_level_id;
				locs[n].detail = i - 1;
				n++;
			}
	}
#endif

	// 0.2.0 optional classes. Scout presence remains the final membership
	// authority below, so frozen-but-disabled names never enter the verdict.
	for (t = 0; t < AP_BOX_TRACK_COUNT; t++)
		for (i = 0; i < AP_BOX_SLOTS_PER_TRACK; i++)
		{
			locs[n].code = AP_BoxMap_Code(t, i);
			locs[n].kind = AP_VF_BOX;
			locs[n].track = t;
			locs[n].detail = i + 1;
			n++;
		}
	for (t = 0; t < CTR_CFG_LETTER_TRACK_COUNT; t++)
		for (i = 0; i < CTR_CFG_LETTER_COUNT; i++)
			if (ctr_cfg.lettersanity_locations[t][i] >= 0)
			{
				locs[n].code = ctr_cfg.lettersanity_locations[t][i];
				locs[n].kind = AP_VF_LETTER;
				locs[n].track = t;
				locs[n].detail = i;
				n++;
			}
	for (i = 0; i < AP_ITEMSANITY_WEAPON_COUNT; i++)
	{
		int juiced;
		for (juiced = 0; juiced < 2; juiced++)
		{
			locs[n].code = 35016000L + i * 2 + juiced;
			locs[n].kind = AP_VF_ITEMSANITY;
			locs[n].track = -1;
			locs[n].detail = i;
			n++;
		}
	}
	for (i = 0, t = AP_VerifyWumpaWorklist(&ctr_cfg.wumpa, wumpa_locs,
	     CTR_CFG_WUMPA_TRACK_COUNT + CTR_CFG_WUMPA_CUSTOM_MAX); i < t; i++)
	{
		locs[n].code = wumpa_locs[i].code;
		locs[n].kind = AP_VF_WUMPA;
		locs[n].track = wumpa_locs[i].destination;
		locs[n].detail = -1;
		n++;
	}

	// Seed the simulated tally from the FOREIGN receipts only (multiworld items +
	// starting inventory). OWN items are banked from the scout cache below -- when a
	// location starts checked here, and as the sweep opens each reachable location --
	// so the own component follows checked-state synchronously and never races the
	// ReceivedItems drain (issue #85). The verdict is still "completable from here":
	// the seed verdict on a fresh connect and a stuck-detector mid-run.
	for (i = 0; i < AP_VF_ITEM_COUNT; i++)
		counts[i] = AP_VerifyForeignItemCount(i);

	// A location is IN this seed iff the connect-time scout knows it (options
	// prune arenas/cups/rungs; the server only answers for locations that
	// exist). Already-checked locations start collected AND have their own scouted
	// item banked now: the sweep only banks locations IT opens (below), so a
	// checked-at-start location would otherwise be counted nowhere.
	ap_vf_total = 0;
	for (i = 0; i < n; i++)
	{
		long long item; int player; unsigned flags;
		if (!ap_net_scout_known(locs[i].code, &item, &player, &flags))
		{
			state[i] = 1; // absent from seed: never counted, never collected
			locs[i].code = -1;
			continue;
		}
		ap_vf_total++;
		if (ap_net_location_checked(locs[i].code))
		{
			state[i] = 1;
			ap_vf_bank_own(locs[i].code, counts);
		}
		else
			state[i] = 0;
	}

	// Destination -> loading physical pad, from the same slot_data permutation
	// the load gate uses (identity when shuffle is off).
	int pad_for_dest[105];
	for (i = 0; i < 105; i++)
		pad_for_dest[i] = -1;
	for (i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		if (AP_HubKeysForPad(i) < 0)
			continue;
		int d = ctr_cfg_warp_dest(i);
		if (d >= 0 && d <= 104)
			pad_for_dest[d] = i;
	}
	for (i = 100; i <= 104; i++)
	{
		int d = ctr_cfg_warp_dest(i);
		if (d >= 0 && d <= 104 && pad_for_dest[d] < 0)
			pad_for_dest[d] = i;
	}
	AP_VerifyWumpaRoutes wumpa_routes;
	memset(&wumpa_routes, 0, sizeof wumpa_routes);
	for (i = 0; i < 105; i++)
		if (pad_for_dest[i] >= 0)
			wumpa_routes.destination_open[i] =
				(unsigned char)ap_vf_pad_open(pad_for_dest[i], counts);
	for (i = 0; i < 5; i++)
	{
		int leg;
		wumpa_routes.cup_displaced[i] = (unsigned char)ctr_cfg_cup_displaced(i);
		for (leg = 0; leg < 4; leg++)
			wumpa_routes.cup_legs[i][leg] = ctr_cfg_cup_leg(i, leg);
	}

	// Fixed-point sweep: open everything reachable, bank own items, repeat.
	int progress = 1;
	while (progress)
	{
		progress = 0;
		for (i = 0; i < 105; i++)
			if (pad_for_dest[i] >= 0)
				wumpa_routes.destination_open[i] =
					(unsigned char)ap_vf_pad_open(pad_for_dest[i], counts);
		for (i = 0; i < n; i++)
		{
			if (state[i])
				continue;
			int ok = 0, lid, pad;
			switch (locs[i].kind)
			{
			case AP_VF_TROPHY:
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts) &&
					ap_vf_trophy_capable(lid, pad, counts);
				break;
			case AP_VF_TIER2:
			{
				// The per-location boost term (2026-08-21 relic tier ruling):
				// Gold and Platinum Time Trials carry a first-boost floor,
				// the USF-class tracks carry the two-boost rank, and every
				// other tier-2 location passes through vacuously.
				AP_VerifyOptions opts = ap_vf_options();
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts) &&
					ap_vf_trophy_capable(lid, pad, counts) &&
					ap_vf_stage2_met(pad, counts) &&
					AP_VerifyLocationCapabilityGate(&opts, counts,
						locs[i].code, ap_vf_required_character(pad));
				break;
			}
			case AP_VF_TRIAL_TT:
			case AP_VF_CRYSTAL:
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts);
				break;
			case AP_VF_GEMCUP:
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts) &&
					ap_vf_cup_capable(lid - 100, counts, pad_for_dest);
				break;
			case AP_VF_CUSTOM:
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts) &&
				     ap_vf_cup_capable(lid - 100, counts, pad_for_dest);
				break;
			case AP_VF_PODIUM:
			{
				AP_VerifyOptions opts = ap_vf_options();
				int own, cup, finishRung, heldFirst;
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				finishRung = locs[i].detail >= 3;
				heldFirst = locs[i].detail == 0;
				own = ap_vf_pad_open(pad, counts);
				if (own && (finishRung || (heldFirst && lid == 13)))
					own = ap_vf_trophy_capable(lid, pad, counts);
				if (own && opts.logic_difficulty == 0 &&
					(heldFirst || locs[i].detail == 3) &&
					AP_VerifyDifficultyTrack(lid))
					own = AP_VerifyTrophyCapabilityGate(&opts, counts, lid,
						ap_vf_required_character(pad));
				ok = own;
				for (cup = 0; cup < 5 && !ok; cup++)
				{
					int leg, hasLeg = 0, cupPad = pad_for_dest[100 + cup];
					// Same displacement rule as ap_vf_cup_capable, and it matters
					// more here: this loop credits a cup as an ADDITIVE route to a
					// track's podium rungs. A displaced cup races none of its
					// retail legs, so it grants no such route and its wire row
					// must not be scanned at all.
					if (ctr_cfg_cup_displaced(cup))
						continue;
					for (leg = 0; leg < 4; leg++)
						if (ctr_cfg_cup_leg(cup, leg) == lid) hasLeg = 1;
					// The held-1st half of the Oxide term is vacuous when the
					// boost chain is not randomized, exactly like the cup-leg
					// term above: no pad gate precedes this call to absorb the
					// capability gate's racer-unlock check, and the apworld's
					// usf_term never evaluates a racer in that case.
					if (hasLeg && ap_vf_pad_open(cupPad, counts) &&
						(!finishRung || ap_vf_cup_capable(cup, counts, pad_for_dest)) &&
						(!heldFirst || lid != 13 || opts.boost_mode == 0 ||
						 ap_vf_trophy_capable(lid, pad, counts)))
						ok = 1;
				}
				break;
			}
			case AP_VF_BOX:
			{
				AP_VerifyOptions opts = ap_vf_options();
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts) &&
					AP_VerifyBoxGate(&opts, counts, lid, locs[i].detail,
						ap_vf_required_character(pad));
				break;
			}
			case AP_VF_LETTER:
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				ok = ap_vf_pad_open(pad, counts) &&
					ap_vf_trophy_capable(lid, pad, counts) &&
					ap_vf_stage2_met(pad, counts);
				if (ok && ctr_cfg.lettersanity_mode == 2)
					ok = counts[139 + (int)(locs[i].code - 35012500L)] > 0;
				break;
			case AP_VF_ITEMSANITY:
			{
				AP_VerifyOptions opts = ap_vf_options();
				ok = AP_VerifyWeaponOwned(counts, locs[i].detail);
				if (ok && locs[i].detail == 0 && opts.boost_mode != 0)
					ok = AP_VerifyCapabilityGate(&opts, counts, -1, 1, 0);
				break;
			}
			case AP_VF_WUMPA:
				ok = AP_VerifyWumpaReachable(locs[i].track, &wumpa_routes);
				break;
			case AP_VF_BOSS:
			{
				int b = locs[i].track;
				ok = counts[AP_IDX_KEY] >= ap_vf_boss_keys[b];
				if (ok)
				{
					// AP_BossGarageOpen under sim: track-based modes count a
					// required track as WON once its trophy-race location is in
					// the collected set; mode 2 falls back to the flat req.
					int mode = ctr_cfg.bossgarage_mode;
					int nt = ctr_cfg.boss_n_tracks[b];
					if ((mode == 0 || mode == 1) && nt > 0)
					{
						int k, j;
						for (k = 0; k < nt && ok; k++)
						{
							int need = ctr_cfg.boss_tracks[b][k];
							if (need < 0)
								continue;
							int won = 0;
							for (j = 0; j < n; j++)
								if (locs[j].kind == AP_VF_TROPHY &&
								    locs[j].track == need && state[j] &&
								    locs[j].code >= 0)
									won = 1;
							ok = won;
						}
					}
					else
						ok = AP_ReqMetCounts(&ctr_cfg.boss_req[b], counts);
				}
				break;
			}
			case AP_VF_OXIDE:
				ok = AP_ReqMetCounts(&ctr_cfg.boss_req[4], counts);
				break;
			case AP_VF_OXIDE_FIN:
				ok = AP_ReqMetCounts(&ctr_cfg.boss_req[4], counts) &&
				     ap_vf_oxide_final_met(counts);
				break;
			}
			if (!ok)
				continue;
			state[i] = 1;
			progress = 1;
			ap_vf_bank_own(locs[i].code, counts);
		}
	}

	// Tally + goal verdict (issue #152: mirrors AP_EvaluateGoal's composed
	// AND-of-active-conditions exactly, in "can it still be reached" form --
	// dossier C11: "any N", a tally compared to the configured count, on
	// both sides of the contract).
	ap_vf_reachable = 0;
	int oxide_ok = 0, oxide_fin_ok = 0, bosses_won = 0;
	for (i = 0; i < n; i++)
	{
		if (locs[i].code < 0)
			continue;
		if (state[i])
			ap_vf_reachable++;
		if (locs[i].kind == AP_VF_OXIDE && state[i])
			oxide_ok = 1;
		if (locs[i].kind == AP_VF_OXIDE_FIN && state[i])
			oxide_fin_ok = 1;
		if (locs[i].kind == AP_VF_BOSS && state[i])
			bosses_won++;
	}
	{
		int gems_held = (counts[AP_IDX_GEM_RED] > 0) + (counts[AP_IDX_GEM_RED + 1] > 0) +
		                (counts[AP_IDX_GEM_RED + 2] > 0) + (counts[AP_IDX_GEM_RED + 3] > 0) +
		                (counts[AP_IDX_GEM_RED + 4] > 0);
		// The Oxide condition also carries Oxide Station's finish capability:
		// the challenge is raced on that track, so the goal must not resolve
		// from boss_req[4] alone while the track's ordinary finish logic still
		// demands the term. This gates the GOAL, not the two Oxide LOCATIONS,
		// which keep their own boss_req reachability above.
		AP_VerifyOptions goal_opts = ap_vf_options();
		int oxide_finish = AP_VerifyOxideGoalFinish(&goal_opts, counts,
			ap_vf_required_character(pad_for_dest[13]));
		ap_vf_goal_ok = 1;
		if (ctr_cfg.goal_oxide == 1)
			ap_vf_goal_ok = ap_vf_goal_ok && oxide_ok && oxide_finish;
		else if (ctr_cfg.goal_oxide == 2)
			ap_vf_goal_ok = ap_vf_goal_ok && oxide_fin_ok && oxide_finish;
		// goal_oxide == 0: no Oxide requirement, contributes nothing.
		if (ctr_cfg.goal_bosses > 0)
			ap_vf_goal_ok = ap_vf_goal_ok && (bosses_won >= ctr_cfg.goal_bosses);
		if (ctr_cfg.goal_gems > 0)
			ap_vf_goal_ok = ap_vf_goal_ok && (gems_held >= ctr_cfg.goal_gems);
	}
	ap_vf_keys_fp = counts[AP_IDX_KEY];
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
		if (!ap_vf_coverage_exact)
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
		if (ap_vf_truncated)
			AP_LogLine("[AP VERIFY] INDETERMINATE: location worklist overflowed "
			           "(seed carries more locations than this build can track). "
			           "No completability claim is made for this seed.\n");
		// The per-location dump is diagnostic detail for SOLO (where "unreachable"
		// means broken). In a multiworld it would enumerate locations that simply
		// need other players' items -- 16 lines of noise under an informational
		// header, so it stays solo-only.
		if (ap_vf_coverage_exact && ap_vf_solo &&
		    (!ap_vf_goal_ok || ap_vf_reachable < ap_vf_total))
		{
			int listed = 0;
			for (i = 0; i < n && listed < 16; i++)
				if (!state[i] && locs[i].code >= 0)
				{
					snprintf(msg, sizeof msg,
					         "[AP VERIFY]   unreachable: code %ld (kind %d track %d)\n",
					         locs[i].code, locs[i].kind, locs[i].track);
					AP_LogLine(msg);
					listed++;
				}
		}
	}
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
	// The schema-newer case already has its own loud banner.
	if (!ctr_cfg_active() || ctr_cfg.schema_newer)
	{
		ap_vf_have = 0; // never let a previous seed's verdict survive here
		return;
	}
	if (!ap_net_scouts_ready())
	{
		ap_vf_have = 0; // scouts were cleared by a (re)connect: verdict is stale
		return;         // LocationInfo not in yet; try again next frame
	}
	unsigned gen = AP_StateGen();
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
