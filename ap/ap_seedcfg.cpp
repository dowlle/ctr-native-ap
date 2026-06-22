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

void ap_seedcfg_parse_json(const nlohmann::json &j)
{
	// Reset to a clean state; identity warp map; type:0 reqs (= native vanilla).
	ctr_cfg.schema_version = 0;
	for (int i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		ctr_cfg.warp_pad_map[i] = i;
		ctr_cfg.warp_pad_unlock[i].type = 0;
		ctr_cfg.warp_pad_unlock[i].count = 0;
		ctr_cfg.warp_pad_unlock[i].colour = -1;
	}
	for (int i = 0; i < CTR_CFG_BOSS_COUNT; i++)
	{
		ctr_cfg.boss_req[i].type = 0;
		ctr_cfg.boss_req[i].count = 0;
		ctr_cfg.boss_req[i].colour = -1;
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

	ctr_cfg.goal = json_int(opt, "goal", 0);
	ctr_cfg.relic_min_time = json_int(opt, "relic_min_time", 0);
	ctr_cfg.relics_require_perfect = json_int(opt, "relics_require_perfect", 0);
	ctr_cfg.oxide_final_unlock = json_int(opt, "oxide_final_unlock", 0);
	ctr_cfg.shuffle_warp_pads = json_int(opt, "shuffle_warp_pads", 0);
	ctr_cfg.warppad_unlock_mode = json_int(opt, "warppad_unlock_mode", 0);
	ctr_cfg.bossgarage_mode = json_int(opt, "bossgarage_mode", 0);

	// ── warp_pad_map: identity already set; overlay string-keyed entries ──
	auto mapIt = j.find("warp_pad_map");
	if (mapIt != j.end() && mapIt->is_object())
	{
		for (auto it = mapIt->begin(); it != mapIt->end(); ++it)
		{
			int pad;
			try { pad = std::stoi(it.key()); } catch (...) { continue; }
			if (pad < 0 || pad >= CTR_CFG_PAD_COUNT)
				continue;
			try { ctr_cfg.warp_pad_map[pad] = it.value().get<int>(); } catch (...) {}
		}
	}

	// ── warp_pad_unlock: per-pad resolved {type,count,colour} ──
	auto unlIt = j.find("warp_pad_unlock");
	if (unlIt != j.end() && unlIt->is_object())
	{
		for (auto it = unlIt->begin(); it != unlIt->end(); ++it)
		{
			int pad;
			try { pad = std::stoi(it.key()); } catch (...) { continue; }
			if (pad < 0 || pad >= CTR_CFG_PAD_COUNT || !it.value().is_object())
				continue;
			ctr_cfg.warp_pad_unlock[pad] = parse_req(it.value());
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
				ctr_cfg.boss_req[b] = parse_req(*it);
		}
	}

	// Activate LAST -- a partial parse never flips ctr_cfg_active() true.
	ctr_cfg.schema_version = schema;

	// Visibility: dump the parsed per-seed requirements so a tester can confirm
	// the contract round-tripped (string-key std::stoi correctness included).
	std::fprintf(stderr,
	             "[AP CFG] slot_data parsed: schema=%d goal=%d warppad_mode=%d "
	             "boss_mode=%d shuffle=%d oxide_final=%d\n",
	             ctr_cfg.schema_version, ctr_cfg.goal, ctr_cfg.warppad_unlock_mode,
	             ctr_cfg.bossgarage_mode, ctr_cfg.shuffle_warp_pads,
	             ctr_cfg.oxide_final_unlock);
	for (int i = 0; i < CTR_CFG_PAD_COUNT; i++)
	{
		const ctr_req &r = ctr_cfg.warp_pad_unlock[i];
		if (r.type != 0)
			std::fprintf(stderr,
			             "[AP CFG] warp_pad_unlock[%d] = {type=%d count=%d colour=%d} "
			             "(dest=%d)\n",
			             i, r.type, r.count, r.colour, ctr_cfg.warp_pad_map[i]);
	}
	for (int b = 0; b < CTR_CFG_BOSS_COUNT; b++)
		std::fprintf(stderr, "[AP CFG] boss_req[%d] = {type=%d count=%d}\n", b,
		             ctr_cfg.boss_req[b].type, ctr_cfg.boss_req[b].count);
}

extern "C" int ctr_cfg_active(void)
{
	return ctr_cfg.schema_version >= 1;
}

extern "C" int ctr_cfg_warp_dest(int physPadLevelID)
{
	if (ctr_cfg.schema_version < 1 || physPadLevelID < 0 ||
	    physPadLevelID >= CTR_CFG_PAD_COUNT)
		return physPadLevelID;
	return ctr_cfg.warp_pad_map[physPadLevelID];
}
