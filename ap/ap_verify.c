// Client-side seed completability verification. See ap_verify.h for the
// contract. Compiled into the C unity build (game/game_unity.h) after
// ap_hooks.c, so the public AP_* / ctr_cfg_* / ap_net_* interfaces and the
// game's static data (data.metaDataLEV) are all in scope.

#ifdef CTR_AP

#include "ap_verify.h"

// ---------------------------------------------------------------------------
// Static vanilla topology (native is the source of truth).
//
// Keys required to physically STAND at a pad / garage: the minimum received-Key
// count that opens every hub door between the N. Sanity Beach spawn and that
// pad's hub. Mirrors AH_Door.c (NSB->GSV 1 key, NSB->Glacier doorID 4 = 2
// keys, other doors D232.arrKeysNeeded = {2,1,2,3,4}) and matches the
// apworld's world.json hub spine: N. Sanity 0, Gem Stone Valley 1, Lost Ruins
// 1, Glacier Park 2, Citadel City 3, Cups Room 2. Keyed by physical pad
// LevelID; -1 = not an adventure warp pad (battle maps 20/22/24..27).
// ---------------------------------------------------------------------------
static const int ap_vf_pad_keys[CTR_CFG_PAD_COUNT] = {
	/*  0 Dingo Canyon   (Lost Ruins)   */ 1,
	/*  1 Dragon Mines   (Glacier Park) */ 2,
	/*  2 Blizzard Bluff (Glacier Park) */ 2,
	/*  3 Crash Cove     (N. Sanity)    */ 0,
	/*  4 Tiger Temple   (Lost Ruins)   */ 1,
	/*  5 Papu's Pyramid (Lost Ruins)   */ 1,
	/*  6 Roo's Tubes    (N. Sanity)    */ 0,
	/*  7 Hot Air Skyway (Citadel City) */ 3,
	/*  8 Sewer Speedway (N. Sanity)    */ 0,
	/*  9 Mystery Caves  (N. Sanity)    */ 0,
	/* 10 Cortex Castle  (Citadel City) */ 3,
	/* 11 N. Gin Labs    (Citadel City) */ 3,
	/* 12 Polar Pass     (Glacier Park) */ 2,
	/* 13 Oxide Station  (Citadel City) */ 3,
	/* 14 Coco Park      (Lost Ruins)   */ 1,
	/* 15 Tiny Arena     (Glacier Park) */ 2,
	/* 16 Slide Coliseum (Gem Stone V.) */ 1,
	/* 17 Turbo Track    (Gem Stone V.) */ 1,
	/* 18 Nitro Court    (Citadel City) */ 3,
	/* 19 Rampage Ruins  (Lost Ruins)   */ 1,
	/* 20 (battle map)                  */ -1,
	/* 21 Skull Rock     (N. Sanity)    */ 0,
	/* 22 (battle map)                  */ -1,
	/* 23 Rocky Road     (Glacier Park) */ 2,
	/* 24..27 (battle maps)             */ -1, -1, -1, -1,
};
#define AP_VF_CUP_KEYS 2 // Cups Room (physical cup pads 100..104)

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
#define AP_VF_MAX_LOCS 	(AP_LOCATION_TABLE_LEN + CTR_CFG_PODIUM_TRACK_COUNT * CTR_CFG_PODIUM_RUNG_COUNT)

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
} ap_vf_kind;

typedef struct
{
	long code;    // AP location code
	int  kind;    // ap_vf_kind
	int  track;   // destination LevelID (races/trials/arenas/cups) or bossIdx
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
static char     ap_vf_line2[96];        // banner detail line
static int      ap_vf_truncated = 0;    // worklist overflowed: verdict is not trustworthy

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
	// applies that via ap_vf_pad_keys), never a trophy floor.
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
	switch (ctr_cfg.oxide_final_unlock)
	{
	case OXIDE_FINAL_MODE_GOLD:     return g >= n;
	case OXIDE_FINAL_MODE_PLATINUM: return p >= n;
	case OXIDE_FINAL_MODE_ANY:      return s >= n || g >= n || p >= n;
	case OXIDE_FINAL_MODE_TOTAL:    return s + g + p >= n;
	case OXIDE_FINAL_MODE_SAPPHIRE:
	default:                        return s >= n;
	}
}

// ---------------------------------------------------------------------------
// The sweep.
// ---------------------------------------------------------------------------
static void ap_vf_recompute(void)
{
	ap_vf_loc  locs[AP_VF_MAX_LOCS];
	char       state[AP_VF_MAX_LOCS]; // 0 open, 1 collected
	int        counts[AP_ITEM_INDEX_COUNT];
	int        n = 0, i, t;

	ap_vf_truncated = 0; // per-sweep state: a stale flag must not poison later verdicts

	// Build the worklist from the static table...
	for (i = 0; i < AP_LOCATION_TABLE_LEN; i++)
	{
		int bit = AP_LOCATION_TABLE[i].bit_index;
		ap_vf_loc L;
		L.code = AP_LOCATION_TABLE[i].location_code;
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
					n++;
				}
		}

	// A location is IN this seed iff the connect-time scout knows it (options
	// prune arenas/cups/rungs; the server only answers for locations that
	// exist). Already-checked locations start collected -- their own items are
	// in the live tallies below, so the sweep must not add them again.
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
		state[i] = ap_net_location_checked(locs[i].code) ? 1 : 0;
	}

	// Start from the LIVE received tallies: the verdict is "completable from
	// here", which is the seed verdict on a fresh connect and a stuck-detector
	// mid-run.
	for (i = 0; i < AP_ITEM_INDEX_COUNT; i++)
		counts[i] = AP_GateCount(i);

	// Destination -> loading physical pad, from the same slot_data permutation
	// the load gate uses (identity when shuffle is off).
	int pad_for_dest[105];
	for (i = 0; i < 105; i++)
		pad_for_dest[i] = -1;
	for (i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		if (ap_vf_pad_keys[i] < 0)
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

	// Fixed-point sweep: open everything reachable, bank own items, repeat.
	int progress = 1;
	while (progress)
	{
		progress = 0;
		for (i = 0; i < n; i++)
		{
			if (state[i])
				continue;
			int ok = 0, lid, pad;
			switch (locs[i].kind)
			{
			case AP_VF_TROPHY:
			case AP_VF_PODIUM:
			case AP_VF_TIER2:
			case AP_VF_TRIAL_TT:
			case AP_VF_CRYSTAL:
			case AP_VF_GEMCUP:
				lid = locs[i].track;
				pad = pad_for_dest[lid];
				if (pad < 0)
					break;
				{
					int keys = (pad >= 100) ? AP_VF_CUP_KEYS : ap_vf_pad_keys[pad];
					ok = counts[AP_IDX_KEY] >= keys && ap_vf_stage1_met(pad, counts);
					if (ok && locs[i].kind == AP_VF_TIER2)
						ok = ap_vf_stage2_met(pad, counts);
				}
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
			{
				long long item; int player; unsigned flags;
				if (ap_net_scout_known(locs[i].code, &item, &player, &flags) &&
				    player == ap_net_self_slot())
				{
					long long idx = item - AP_ITEM_BASE;
					if (idx >= 0 && idx < AP_ITEM_INDEX_COUNT)
						counts[(int)idx]++;
				}
			}
		}
	}

	// Tally + goal verdict (mirrors AP_EvaluateGoal's per-goal conditions, in
	// "can it still be reached" form).
	ap_vf_reachable = 0;
	int oxide_ok = 0, oxide_fin_ok = 0, bosses_ok = 1;
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
		if (locs[i].kind == AP_VF_BOSS && !state[i])
			bosses_ok = 0;
	}
	switch (ctr_cfg.goal)
	{
	case 1:  ap_vf_goal_ok = oxide_fin_ok; break;
	case 3:  ap_vf_goal_ok = bosses_ok; break;
	case 4:  ap_vf_goal_ok = counts[AP_IDX_GEM_RED] > 0 &&
	                         counts[AP_IDX_GEM_RED + 1] > 0 &&
	                         counts[AP_IDX_GEM_RED + 2] > 0 &&
	                         counts[AP_IDX_GEM_RED + 3] > 0 &&
	                         counts[AP_IDX_GEM_RED + 4] > 0; break;
	case 0:
	default: ap_vf_goal_ok = oxide_ok; break;
	}
	ap_vf_keys_fp = counts[AP_IDX_KEY];
	ap_vf_solo = (ap_net_player_count() == 1);
	ap_vf_have = 1;
	// A truncated worklist means the sweep reasoned over a partial seed. Never
	// report that as a definitive failure: an "I could not check this" verdict
	// is useful, a false "your seed is broken" is worse than no verifier.
	if (ap_vf_truncated)
		ap_vf_goal_ok = 1;

	{
		char msg[192];
		snprintf(msg, sizeof msg,
		         "[AP VERIFY] %s: goal %s, %d/%d locations reachable, Keys %d "
		         "(%s)\n",
		         ap_vf_goal_ok ? "seed OK" : "GOAL BLOCKED",
		         ap_vf_goal_ok ? "reachable" : "NOT reachable",
		         ap_vf_reachable, ap_vf_total, ap_vf_keys_fp,
		         ap_vf_solo ? "solo: definitive" : "multiworld: advisory");
		AP_LogLine(msg);
		if (ap_vf_truncated)
			AP_LogLine("[AP VERIFY] INDETERMINATE: location worklist overflowed "
			           "(seed carries more locations than this build can track). "
			           "No completability claim is made for this seed.\n");
		if (!ap_vf_goal_ok || ap_vf_reachable < ap_vf_total)
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
	snprintf(ap_vf_line2, sizeof ap_vf_line2,
	         "goal blocked: %d/%d locations reachable, %d Keys obtainable",
	         ap_vf_reachable, ap_vf_total, ap_vf_keys_fp);
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
		return;
	ap_vf_gen = gen;
	ap_vf_recompute();
}

void AP_DrawVerifyWarning(void)
{
	if (!ap_vf_have || ap_vf_goal_ok || !ap_vf_solo)
		return;
	static char warn1[] = "!! SEED NOT COMPLETABLE !!";
	DecalFont_DrawLine(warn1, AP_FEED_X, 0x2c, FONT_SMALL, RED);
	DecalFont_DrawLine(ap_vf_line2, AP_FEED_X, 0x2c + AP_FEED_LINE_H,
	                   FONT_SMALL, RED);
}

#endif // CTR_AP
