// Per-seed slot_data parser for CTR-Native (Phase 2-MVP).
//
// Compiled in the same isolated C++17 static lib as ap_net.cpp (see the CTR_AP
// block in CMakeLists.txt), so nlohmann/json never leaks into the C unity build.
// ap_net.cpp's slot-connected handler calls ap_seedcfg_parse_json(); the C gate
// sites read the resulting ctr_cfg through the C API in ap_seedcfg.h.
//
// CONTRACT (§0): apclientpp surfaces slot_data as nlohmann::json. Integer object
// keys round-trip as JSON *strings*, so warp_pad_map / warp_pad_unlock keys are
// parsed with std::stoi. All type/mode/count values are RESOLVED integers
// (the apworld did all mode logic) -- native never re-derives. schema_version is
// written LAST so a partial parse never flips ctr_cfg_active() true.

#include "ap_seedcfg.h"

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>

extern "C"
{
	ctr_seed_config ctr_cfg = {0};
}

// §0 available-items bound table, indexed by type code (0..5). Defensive clamp
// so a malformed/oversized count can never make a pad unsolvable native-side,
// even though the apworld already bounds these. Index 0 (none) is unused.
static const int CTR_REQ_AVAIL[6] = {
    0,  // 0 none
    16, // 1 trophies
    4,  // 2 keys
    4,  // 3 tokens (per colour)
    18, // 4 sapphire
    1   // 5 gems (per colour)
};

static int ctr_clamp_count(int type, int count)
{
	if (type < 0 || type > 5)
		return count;
	int hi = CTR_REQ_AVAIL[type];
	if (hi <= 0)
		return count; // type 0 -> count unused
	if (count < 0)
		return 0;
	if (count > hi)
		return hi;
	return count;
}

// A warp_pad_map / gem_cup_map VALUE is a destination LevelID. slot_data v3 opens
// the destination shuffle to the full ID space in BOTH directions, so the legal
// set is the dense pads 0..27 PLUS the five cup LevelIDs 100..104. A value outside
// this set is a malformed/forward-incompatible entry and is dropped (the pad keeps
// its identity destination), never clamped into a wrong track.
static int ctr_valid_dest(int v)
{
	return (v >= 0 && v < CTR_CFG_PAD_COUNT) || (v >= 100 && v <= 104);
}

static int json_int(const nlohmann::json &j, const char *key, int dflt)
{
	auto it = j.find(key);
	if (it == j.end() || it->is_null())
		return dflt;
	try
	{
		if (it->is_boolean())
			return it->get<bool>() ? 1 : 0;
		return it->get<int>();
	}
	catch (...)
	{
		return dflt;
	}
}

static ctr_req parse_req(const nlohmann::json &o)
{
	ctr_req r;
	r.type = json_int(o, "type", 0);
	r.count = json_int(o, "count", 0);
	r.colour = json_int(o, "colour", -1);
	r.count = ctr_clamp_count(r.type, r.count);
	return r;
}

// Parse a per-pad warp-pad unlock entry into its two stages.
//
//   schema 2 (two-stage, open-rando):  {"stage1": {type,count,colour},
//                                        "stage2": {type,count,colour}}
//   schema 1 (flat, pre two-stage):    {type,count,colour}
//
// Back-compat: a flat object has no "stage1" key, so it is read directly into
// stage1 and stage2 is left type:0 (always-open) -- the tier-2 menu opens the
// instant stage1 is met, exactly the pre-two-stage behaviour.
static ctr_warp_unlock parse_warp_unlock(const nlohmann::json &o)
{
	ctr_warp_unlock u;
	auto s1 = o.find("stage1");
	if (s1 != o.end() && s1->is_object())
	{
		u.stage1 = parse_req(*s1);
		auto s2 = o.find("stage2");
		if (s2 != o.end() && s2->is_object())
			u.stage2 = parse_req(*s2);
		else
		{
			u.stage2.type = 0;
			u.stage2.count = 0;
			u.stage2.colour = -1;
		}
	}
	else
	{
		// flat v1 shape -> stage1, stage2 stays always-open
		u.stage1 = parse_req(o);
		u.stage2.type = 0;
		u.stage2.count = 0;
		u.stage2.colour = -1;
	}
	return u;
}

void ap_seedcfg_parse_json(const nlohmann::json &j)
{
	// Reset to a clean state; identity warp map; type:0 reqs (= native vanilla).
	ctr_cfg.schema_version = 0;
	ctr_cfg.schema_newer = 0;
	// Oxide-final gate defaults: mode 0 (Sapphire) + count 18 = the pre-v0.1.1
	// behaviour, applied when a seed omits the fields (schema < 5 or Phase-1).
	ctr_cfg.oxide_final_unlock = OXIDE_FINAL_MODE_SAPPHIRE;
	ctr_cfg.oxide_final_count = 18;
	for (int i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		ctr_cfg.warp_pad_map[i] = i;
		ctr_cfg.warp_pad_unlock[i].stage1.type = 0;
		ctr_cfg.warp_pad_unlock[i].stage1.count = 0;
		ctr_cfg.warp_pad_unlock[i].stage1.colour = -1;
		ctr_cfg.warp_pad_unlock[i].stage2.type = 0;
		ctr_cfg.warp_pad_unlock[i].stage2.count = 0;
		ctr_cfg.warp_pad_unlock[i].stage2.colour = -1;
	}
	// Gem cups (LevelID 100..104) -> gem_cup_unlock[0..4] by colour; type 0 = vanilla.
	// gem_cup_map is the destination-shuffle counterpart to warp_pad_map: identity
	// (cup pad 100+i loads cup 100+i) until warp_pad_map overlays a "100".."104" key.
	for (int i = 0; i < 5; i++)
	{
		ctr_cfg.gem_cup_unlock[i].stage1.type = 0;
		ctr_cfg.gem_cup_unlock[i].stage1.count = 0;
		ctr_cfg.gem_cup_unlock[i].stage1.colour = -1;
		ctr_cfg.gem_cup_unlock[i].stage2.type = 0;
		ctr_cfg.gem_cup_unlock[i].stage2.count = 0;
		ctr_cfg.gem_cup_unlock[i].stage2.colour = -1;
		ctr_cfg.gem_cup_map[i] = 100 + i; // identity
	}
	for (int i = 0; i < CTR_CFG_BOSS_COUNT; i++)
	{
		ctr_cfg.boss_req[i].type = 0;
		ctr_cfg.boss_req[i].count = 0;
		ctr_cfg.boss_req[i].colour = -1;
		ctr_cfg.boss_n_tracks[i] = 0;
		for (int k = 0; k < 4; k++)
			ctr_cfg.boss_tracks[i][k] = -1;
	}
	// AI-difficulty slot_data default: -1 = unset (absent from ctr_options, or no
	// slot_data at all) so the connect-time pull leaves the local value untouched.
	ctr_cfg.ai_difficulty_default = -1;
	// DeathLink defaults: off, amnesty 1 (every death). Additive keys, so an old
	// seed without them degrades to the disabled state.
	ctr_cfg.death_link = 0;
	ctr_cfg.deathlink_amnesty = 1;
	// Podium checks -> disabled + all rungs absent (-1) until parsed below.
	ctr_cfg.podium_enabled = 0;
	ctr_cfg.podium_any_position = 0;
	for (int i = 0; i < CTR_CFG_PODIUM_TRACK_COUNT; i++)
	{
		ctr_cfg.podium[i].held_1st = -1;
		ctr_cfg.podium[i].held_3rd = -1;
		ctr_cfg.podium[i].held_5th = -1;
		ctr_cfg.podium[i].finish_podium = -1;
		ctr_cfg.podium[i].finish_any = -1;
	}

	if (!j.is_object())
	{
		std::fprintf(stderr, "[AP CFG] slot_data is not an object -> Phase-1 statics\n");
		return;
	}

	// ── ctr_options (gates the parser) ──
	auto optIt = j.find("ctr_options");
	if (optIt == j.end() || !optIt->is_object())
	{
		std::fprintf(stderr, "[AP CFG] no ctr_options -> Phase-1 statics\n");
		return;
	}
	const nlohmann::json &opt = *optIt;

	int schema = json_int(opt, "schema_version", 0);
	if (schema < 1)
	{
		std::fprintf(stderr, "[AP CFG] schema_version=%d (<1) -> Phase-1 statics\n", schema);
		return;
	}
	// Issue #8: a seed schema NEWER than this build understands. We still parse
	// what we can (best effort, so the session runs), but flag it so the game
	// draws a loud, persistent "update the client" banner -- never silently
	// enforce a gate whose shape we may have guessed wrong. Parsing continues:
	// every field we DO know still round-trips through the shared >=1 shape.
	if (schema > CTR_CFG_SCHEMA_KNOWN)
	{
		ctr_cfg.schema_newer = 1;
		std::fprintf(stderr,
		             "[AP CFG] *** schema_version=%d is NEWER than this build "
		             "understands (max %d). This seed was generated by a newer "
		             "apworld -- UPDATE THE CTR CLIENT. Gates may be enforced "
		             "incorrectly until you do. ***\n",
		             schema, CTR_CFG_SCHEMA_KNOWN);
	}

	ctr_cfg.goal = json_int(opt, "goal", 0);
	ctr_cfg.relic_min_time = json_int(opt, "relic_min_time", 0);
	ctr_cfg.relics_require_perfect = json_int(opt, "relics_require_perfect", 0);
	// schema >= 5: oxide_final_unlock is a relic-goal MODE and oxide_final_count
	// carries the shared 1..18 count. Older seeds omit the count -> the default 18
	// set above (with mode 0/1 from a <5 seed, though a <5 seed cannot reach here
	// on a v5-generated wire; the schema bump gates that).
	ctr_cfg.oxide_final_unlock = json_int(opt, "oxide_final_unlock", OXIDE_FINAL_MODE_SAPPHIRE);
	ctr_cfg.oxide_final_count = json_int(opt, "oxide_final_count", 18);
	ctr_cfg.shuffle_warp_pads = json_int(opt, "shuffle_warp_pads", 0);
	ctr_cfg.shuffle_gems = json_int(opt, "shuffle_gems", 0);
	ctr_cfg.shuffle_keys = json_int(opt, "shuffle_keys", 0);
	ctr_cfg.warppad_unlock_mode = json_int(opt, "warppad_unlock_mode", 0);
	ctr_cfg.bossgarage_mode = json_int(opt, "bossgarage_mode", 0);
	// QoL additive key (no schema bump): default 0 = vanilla lap counts.
	ctr_cfg.one_lap_cups = json_int(opt, "one_lap_cups", 0);
	// Optional comfort field: absent -> stays -1 (unset). Not gated by and does not
	// change schema_version -- generation never depends on it.
	ctr_cfg.ai_difficulty_default = json_int(opt, "ai_difficulty", -1);
	// DeathLink (additive keys, no schema bump). Absent -> off / amnesty 1.
	ctr_cfg.death_link = json_int(opt, "death_link", 0);
	ctr_cfg.deathlink_amnesty = json_int(opt, "deathlink_amnesty", 1);
	if (ctr_cfg.deathlink_amnesty < 1)
		ctr_cfg.deathlink_amnesty = 1; // guard: never divide the send cadence by < 1

	// ── warp_pad_map: identity already set; overlay string-keyed entries ──
	// slot_data v3 keys span "0".."27" (-> warp_pad_map) AND "100".."104" (-> the
	// cup destination map gem_cup_map). Values are validated against the full
	// {0..27, 100..104} destination set (ctr_valid_dest): a cup pad may host a race
	// destination and a race pad may host a cup destination under `merged` grouping.
	auto mapIt = j.find("warp_pad_map");
	if (mapIt != j.end() && mapIt->is_object())
	{
		for (auto it = mapIt->begin(); it != mapIt->end(); ++it)
		{
			int pad;
			try { pad = std::stoi(it.key()); } catch (...) { continue; }
			int dest;
			try { dest = it.value().get<int>(); } catch (...) { continue; }
			if (!ctr_valid_dest(dest))
				continue; // out-of-range destination -> keep identity
			if (pad >= 100 && pad <= 104)
			{
				ctr_cfg.gem_cup_map[pad - 100] = dest; // cup PHYSICAL pad destination
				continue;
			}
			if (pad < 0 || pad >= CTR_CFG_PAD_COUNT)
				continue;
			ctr_cfg.warp_pad_map[pad] = dest;
		}
	}

	// ── warp_pad_unlock: per-pad two-stage {stage1,stage2} (v1 flat accepted) ──
	auto unlIt = j.find("warp_pad_unlock");
	if (unlIt != j.end() && unlIt->is_object())
	{
		for (auto it = unlIt->begin(); it != unlIt->end(); ++it)
		{
			int pad;
			try { pad = std::stoi(it.key()); } catch (...) { continue; }
			if (!it.value().is_object())
				continue;
			// Gem cups (LevelID 100..104) live outside the 28-wide warp_pad_unlock
			// array; route them into gem_cup_unlock[colour] (= LevelID - 100).
			if (pad >= 100 && pad <= 104)
			{
				ctr_cfg.gem_cup_unlock[pad - 100] = parse_warp_unlock(it.value());
				continue;
			}
			if (pad < 0 || pad >= CTR_CFG_PAD_COUNT)
				continue;
			ctr_cfg.warp_pad_unlock[pad] = parse_warp_unlock(it.value());
		}
	}

	// ── boss_garage_req: fixed 5 bosses by name ──
	static const char *kBossKeys[CTR_CFG_BOSS_COUNT] = {
	    "ripper_roo", "papu_papu", "komodo_joe", "pinstripe", "oxide"};
	auto bossIt = j.find("boss_garage_req");
	if (bossIt != j.end() && bossIt->is_object())
	{
		for (int b = 0; b < CTR_CFG_BOSS_COUNT; b++)
		{
			auto it = bossIt->find(kBossKeys[b]);
			if (it != bossIt->end() && it->is_object())
			{
				ctr_cfg.boss_req[b] = parse_req(*it);
				// Optional per-boss track list (garage modes 0/1). Mode 2 omits
				// it; extra key is harmless to parse_req above. Cap at 4 (each
				// boss hub has exactly four race tracks).
				auto trIt = it->find("tracks");
				if (trIt != it->end() && trIt->is_array())
				{
					int n = 0;
					for (auto &tv : *trIt)
					{
						if (n >= 4)
							break;
						try { ctr_cfg.boss_tracks[b][n++] = tv.get<int>(); }
						catch (...) {}
					}
					ctr_cfg.boss_n_tracks[b] = n;
				}
			}
		}
	}

	// ── podium_checks: the placement-rung ladder for the 16 trophy races ──
	// Additive block (feat/podium-checks). Keyed by physical race-pad LevelID as
	// a JSON string "0".."15" (== the [AP RACE] track field the listener logs).
	// schema_version is NOT keyed off this block -- it is purely additive, so a
	// pre-podium seed just leaves podium_enabled 0.
	//
	// TWO wire shapes, keyed off schema_version (the #9 rung rework):
	//   schema >= 6: a 5-slot array [held_1st, held_3rd, held_5th, finish_podium,
	//                finish_any] of location codes (-1 / null = rung absent). A
	//                named object with those same keys is also accepted (forward-
	//                robust to a future rung addition) -- whichever the apworld
	//                emits round-trips here.
	//   schema <= 5: the legacy object {first, podium, any}. "first" (Finish 1st)
	//                is retired -- mapped nowhere; podium -> finish_podium; any ->
	//                finish_any; the held_* live rungs did not exist, stay absent.
	// A rung absent from the seed (held_5th default-off, a rung the seed did not
	// place, the whole feature off) is stored -1 so the native fan-out skips it.
	auto podIt = j.find("podium_checks");
	if (podIt != j.end() && podIt->is_object())
	{
		// `enabled` defaults ON at schema >= 6 when the block is present: the apworld
		// omits the whole podium_checks block for a podium-off seed (the pre-podium
		// precedent), so a present block means the feature is on even if the now-
		// redundant explicit `enabled` bool were ever dropped. Pre-6 seeds keep the
		// original default-0 behaviour (an explicit `enabled` bool is always emitted).
		ctr_cfg.podium_enabled = json_int(*podIt, "enabled", schema >= 6 ? 1 : 0);
		// any_position is a schema <= 5 concept (the optional third finish rung). It
		// is GONE in schema 6 -- per-rung presence is the -1 sentinel now -- so read
		// it only on legacy seeds; a schema-6 seed leaves it 0.
		ctr_cfg.podium_any_position =
		    (schema <= 5) ? json_int(*podIt, "any_position", 0) : 0;
		auto locIt = podIt->find("locations");
		if (ctr_cfg.podium_enabled && locIt != podIt->end() && locIt->is_object())
		{
			for (auto it = locIt->begin(); it != locIt->end(); ++it)
			{
				int lid;
				try { lid = std::stoi(it.key()); } catch (...) { continue; }
				if (lid < 0 || lid >= CTR_CFG_PODIUM_TRACK_COUNT)
					continue; // only the 16 trophy races carry rungs
				const nlohmann::json &r = it.value();
				ctr_podium_rungs &pr = ctr_cfg.podium[lid];
				if (schema >= 6)
				{
					if (r.is_array())
					{
						// [held_1st, held_3rd, held_5th, finish_podium, finish_any].
						// A short array leaves the missing tail rungs at -1; a null /
						// non-int element is treated as absent.
						long *slot[CTR_CFG_PODIUM_RUNG_COUNT] = {
						    &pr.held_1st, &pr.held_3rd, &pr.held_5th,
						    &pr.finish_podium, &pr.finish_any};
						for (size_t k = 0;
						     k < r.size() && k < CTR_CFG_PODIUM_RUNG_COUNT; k++)
						{
							if (!r[k].is_number_integer())
								continue; // null / absent -> keep -1
							try { *slot[k] = r[k].get<int>(); } catch (...) {}
						}
					}
					else if (r.is_object())
					{
						// Named form: same five keys. json_int returns -1 for a null
						// or missing key -> absent rung.
						pr.held_1st      = json_int(r, "held_1st", -1);
						pr.held_3rd      = json_int(r, "held_3rd", -1);
						pr.held_5th      = json_int(r, "held_5th", -1);
						pr.finish_podium = json_int(r, "finish_podium", -1);
						pr.finish_any    = json_int(r, "finish_any", -1);
					}
				}
				else if (r.is_object())
				{
					// Legacy schema <= 5 shape: {first, podium, any}. "first" is
					// retired (the trophy check covers a 1st-place finish); map the
					// other two onto the new struct; held_* stay absent.
					pr.finish_podium = json_int(r, "podium", -1);
					pr.finish_any    = json_int(r, "any", -1);
				}
			}
		}
	}

	// Activate LAST -- a partial parse never flips ctr_cfg_active() true.
	ctr_cfg.schema_version = schema;

	// Visibility: dump the parsed per-seed requirements so a tester can confirm
	// the contract round-tripped (string-key std::stoi correctness included).
	std::fprintf(stderr,
	             "[AP CFG] slot_data parsed: schema=%d goal=%d warppad_mode=%d "
	             "boss_mode=%d shuffle=%d oxide_final_mode=%d oxide_final_count=%d%s\n",
	             ctr_cfg.schema_version, ctr_cfg.goal, ctr_cfg.warppad_unlock_mode,
	             ctr_cfg.bossgarage_mode, ctr_cfg.shuffle_warp_pads,
	             ctr_cfg.oxide_final_unlock, ctr_cfg.oxide_final_count,
	             ctr_cfg.schema_newer ? " [SCHEMA NEWER THAN BUILD]" : "");
	for (int i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		const ctr_warp_unlock &u = ctr_cfg.warp_pad_unlock[i];
		if (u.stage1.type != 0 || u.stage2.type != 0)
			std::fprintf(stderr,
			             "[AP CFG] warp_pad_unlock[%d] = stage1{type=%d count=%d colour=%d} "
			             "stage2{type=%d count=%d colour=%d} (dest=%d)\n",
			             i, u.stage1.type, u.stage1.count, u.stage1.colour,
			             u.stage2.type, u.stage2.count, u.stage2.colour,
			             ctr_cfg.warp_pad_map[i]);
	}
	for (int b = 0; b < CTR_CFG_BOSS_COUNT; b++)
		std::fprintf(stderr, "[AP CFG] boss_req[%d] = {type=%d count=%d}\n", b,
		             ctr_cfg.boss_req[b].type, ctr_cfg.boss_req[b].count);
	for (int c = 0; c < 5; c++)
	{
		const ctr_warp_unlock &u = ctr_cfg.gem_cup_unlock[c];
		int dest = ctr_cfg.gem_cup_map[c];
		// Print when the cup carries a randomized requirement OR its destination was
		// remapped (slot_data v3 cup destination shuffle) -- either makes it non-vanilla.
		if (u.stage1.type != 0 || u.stage2.type != 0 || dest != 100 + c)
			std::fprintf(stderr,
			             "[AP CFG] gem_cup[%d] (phys LevelID %d) = stage1{type=%d count=%d colour=%d} "
			             "stage2{type=%d count=%d colour=%d} (dest=%d)\n",
			             c, 100 + c, u.stage1.type, u.stage1.count, u.stage1.colour,
			             u.stage2.type, u.stage2.count, u.stage2.colour, dest);
	}
	std::fprintf(stderr, "[AP CFG] podium_checks: enabled=%d any_position=%d\n",
	             ctr_cfg.podium_enabled, ctr_cfg.podium_any_position);
	if (ctr_cfg.podium_enabled)
		for (int i = 0; i < CTR_CFG_PODIUM_TRACK_COUNT; i++)
		{
			const ctr_podium_rungs &pr = ctr_cfg.podium[i];
			if (pr.held_1st >= 0 || pr.held_3rd >= 0 || pr.held_5th >= 0 ||
			    pr.finish_podium >= 0 || pr.finish_any >= 0)
				std::fprintf(stderr,
				             "[AP CFG] podium[LevelID %d] = held_1st=%ld held_3rd=%ld "
				             "held_5th=%ld finish_podium=%ld finish_any=%ld\n",
				             i, pr.held_1st, pr.held_3rd, pr.held_5th,
				             pr.finish_podium, pr.finish_any);
		}
}

extern "C" int ctr_cfg_active(void)
{
	return ctr_cfg.schema_version >= 1;
}

extern "C" int ctr_cfg_warp_dest(int physPadLevelID)
{
	if (ctr_cfg.schema_version < 1)
		return physPadLevelID;
	// Dense pads 0..27 in warp_pad_map; cup pads 100..104 in gem_cup_map. Any other
	// LevelID has no map entry -> identity (safe to call unconditionally).
	if (physPadLevelID >= 0 && physPadLevelID < CTR_CFG_PAD_COUNT)
		return ctr_cfg.warp_pad_map[physPadLevelID];
	if (physPadLevelID >= 100 && physPadLevelID <= 104)
		return ctr_cfg.gem_cup_map[physPadLevelID - 100];
	return physPadLevelID;
}

extern "C" int ctr_cfg_warp_phys(int destTrackLevelID)
{
	if (ctr_cfg.schema_version < 1 || !ctr_valid_dest(destTrackLevelID))
		return destTrackLevelID;
	// Linear scan for the physical pad whose destination is destTrackLevelID, over
	// BOTH maps. The union of warp_pad_map (0..27) and gem_cup_map (100..104) is a
	// permutation over the participating pool (identity for non-shuffled pads), so
	// at most one physical pad maps here. If none does, return identity.
	for (int phys = 0; phys < CTR_CFG_PAD_COUNT; phys++)
		if (ctr_cfg.warp_pad_map[phys] == destTrackLevelID)
			return phys;
	for (int c = 0; c < 5; c++)
		if (ctr_cfg.gem_cup_map[c] == destTrackLevelID)
			return 100 + c;
	return destTrackLevelID;
}
