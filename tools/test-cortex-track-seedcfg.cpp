// cortex_vortex_track (schema 15) parser: acceptance, refusal conventions, the
// virtual destination 110 through warp_pad_map / cup map / gem_cup_legs and the
// phys<->dest lookups, and the widened oxide_final_venue Wumpa rule.
//
//   g++ -m32 -std=c++17 -DCTR_AP -I ap -I ap/vendor/json/include
//      tools/test-cortex-track-seedcfg.cpp ap/ap_seedcfg.cpp -o /tmp/test-cortex-track-seedcfg
//      && /tmp/test-cortex-track-seedcfg
//
// Also parses the generated slot_data fixtures in tools/fixtures/cortex-vortex/
// (apworld feat/cortex-vortex-track @ 8fac02df2, convert_to_base_types output),
// and optionally CTR_CORTEX_SLOT_DATA=<slot_data.json>.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
#define EXPECT(g, w, n) do { checks++; long _g = (long)(g), _w = (long)(w); \
	if (_g != _w) { failures++; std::printf("FAIL %s (%ld != %ld)\n", n, _g, _w); } } while (0)

static nlohmann::json block()
{
	return {
		{"version", 1}, {"destination_id", 110}, {"host_level_id", 13},
		{"lev_sha256", CTR_CFG_CORTEX_LEV_SHA256}, {"vrm_sha256", CTR_CFG_CORTEX_VRM_SHA256},
		{"dropped_destination", 7},
		{"locations", {
			{"trophy", 35026000}, {"relic", {35026001, 35026002, 35026003}}, {"ctr_token", 35026004},
			{"podium", {{"held_1st", 35026010}, {"held_3rd", 35026011}, {"held_5th", -1},
			            {"finish_podium", 35026013}, {"finish_any", 35026014}}},
			{"letters", {-1, -1, -1}}, {"wumpa", -1}}},
		{"letter_items", {35010200, 35010201, 35010202}},
	};
}

static nlohmann::json seed(int option = 1)
{
	nlohmann::json map = nlohmann::json::object();
	for (int p = 0; p < 28; p++) map[std::to_string(p)] = p;
	for (int c = 100; c <= 104; c++) map[std::to_string(c)] = c;
	map["7"] = 110; // no shuffle: Cortex Vortex sits on the dropped destination's pad
	nlohmann::json d = {
		{"ctr_options", {{"schema_version", 15}, {"cortex_vortex_track", option}, {"oxide_final_track", 0}}},
		{"warp_pad_map", map},
		{"cortex_vortex_track", block()},
		{"oxide_final_venue", {
			{"version", 1}, {"track", "cortex_vortex"}, {"opponent", "nitros_oxide"},
			{"location", 35011105}, {"host_level_id", 13}, {"wumpa_location", -1},
			{"lev_sha256", CTR_CFG_CORTEX_LEV_SHA256}, {"vrm_sha256", CTR_CFG_CORTEX_VRM_SHA256}}},
	};
	if (!option) { d.erase("cortex_vortex_track"); d["warp_pad_map"]["7"] = 7; }
	return d;
}

static void refused(nlohmann::json d, const char *name)
{
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 0, name);
	EXPECT(ctr_cfg.cortex_track.trophy, -1, "refused block carries no code");
	EXPECT(ctr_cfg.cortex_track.wumpa, -1, "refused block carries no Wumpa");
}

// Retail data.advCupTrackIDs (game/zGlobal_DATA.c), as ap_hooks.c pushes it.
static const int kVanillaLegs[20] = {3, 9, 2, 5, 6, 14, 12, 10, 4, 8, 1, 11, 0, 15, 7, 13, 6, 5, 1, 7};

static bool load(const char *path, nlohmann::json &out)
{
	std::ifstream in(path);
	if (!in) return false;
	out = nlohmann::json::parse(in);
	if (out.contains("slot_data")) out = out["slot_data"];
	return true;
}

static void fixture(const char *name, int expectOn, int dropped, int pad)
{
	char path[256];
	nlohmann::json real;
	std::snprintf(path, sizeof path, "tools/fixtures/cortex-vortex/%s.slot_data.json", name);
	checks++;
	if (!load(path, real)) { failures++; std::printf("FAIL fixture missing: %s\n", path); return; }
	ap_seedcfg_parse_json(real);
	EXPECT(ctr_cfg.schema_newer, 0, "fixture schema is known");
	EXPECT(ctr_cfg.cortex_track.option, expectOn, "fixture option");
	EXPECT(ctr_cfg.cortex_track.valid, expectOn, "fixture block accepted");
	EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "fixture venue still valid");
	if (!expectOn)
	{
		EXPECT(ctr_cfg_warp_phys(110), 110, "option off: 110 hosted nowhere");
		return;
	}
	EXPECT(ctr_cfg.cortex_track.dropped_destination, dropped, "fixture dropped destination");
	EXPECT(ctr_cfg_warp_phys(110), pad, "fixture pad hosting 110");
	EXPECT(ctr_cfg_warp_phys(dropped) == dropped && ctr_cfg_warp_dest(dropped) != dropped, 1,
	       "dropped destination is hosted by no pad");
	EXPECT(ctr_cfg.cortex_track.trophy, 35026000, "fixture trophy");
	EXPECT(ctr_cfg.cortex_track.ctr_token, 35026004, "fixture token");
	for (int t = 0; t < 3; t++)
		EXPECT(ctr_cfg.cortex_track.relic[t] == -1 || ctr_cfg.cortex_track.relic[t] == 35026001 + t, 1,
		       "fixture relic tier is exact or absent");
}

int main()
{
	ctr_cfg_set_vanilla_cup_legs(kVanillaLegs);

	fixture("on_default_merged_shuffle_seed5", 1, 18, 15);
	fixture("on_no_shuffle_letters_seed8", 1, 9, 9);
	EXPECT(ctr_cfg.lettersanity_mode, 2, "seed8 lettersanity mode 2");
	EXPECT(ctr_cfg_cup_leg(0, 1), 9, "vanilla Red cup still legs the dropped Mystery Caves");
	fixture("on_cup_legs_boost_seed12", 1, 21, 3);
	{
		int legs110 = 0;
		for (int c = 0; c < 5; c++)
			for (int l = 0; l < 4; l++)
				legs110 += ctr_cfg_cup_leg(c, l) == 110;
		EXPECT(legs110 > 0, 1, "seed12 randomized legs include 110");
	}
	fixture("off_default_seed5", 0, -1, -1);

	nlohmann::json d = seed();
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg_active(), 1, "schema 15 active");
	EXPECT(ctr_cfg.schema_newer, 0, "schema 15 is known");
	EXPECT(ctr_cfg.cortex_track.option, 1, "option on");
	EXPECT(ctr_cfg.cortex_track.valid, 1, "block accepted");
	EXPECT(ctr_cfg.cortex_track.dropped_destination, 7, "dropped destination");
	EXPECT(ctr_cfg.cortex_track.trophy, 35026000, "trophy");
	EXPECT(ctr_cfg.cortex_track.relic[2], 35026003, "platinum");
	EXPECT(ctr_cfg.cortex_track.ctr_token, 35026004, "token");
	EXPECT(ctr_cfg.cortex_track.podium.held_5th, -1, "absent rung");
	EXPECT(ctr_cfg.cortex_track.podium.finish_any, 35026014, "finish any");
	EXPECT(ctr_cfg.cortex_track.letter_items[0], -1, "no lettersanity: no letter items");
	EXPECT(ctr_cfg_warp_dest(7), 110, "pad 7 hosts 110");
	EXPECT(ctr_cfg_warp_phys(110), 7, "110 resolves to its physical pad");
	EXPECT(ctr_cfg_warp_phys(13), 13, "Oxide Station keeps its own pad");
	EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "venue unaffected");

	// Cup pad hosting Cortex Vortex, and a Gem Cup legging it.
	d = seed();
	d["warp_pad_map"]["7"] = 103;
	d["warp_pad_map"]["103"] = 110;
	d["gem_cup_legs"] = {{"100", {110, 13, 3, 4}}};
	d["cortex_vortex_track"]["dropped_destination"] = 7;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 1, "cup pad may host 110");
	EXPECT(ctr_cfg_warp_dest(103), 110, "cup pad dest");
	EXPECT(ctr_cfg_warp_phys(110), 103, "110 on a cup pad");
	EXPECT(ctr_cfg_cup_leg(0, 0), 110, "leg 110 stored");
	EXPECT(ctr_cfg_cup_leg(0, 1), 13, "Oxide Station leg is still 13");

	// Crystal Challenge pad hosting Cortex Vortex.
	d = seed();
	d["warp_pad_map"]["7"] = 18;
	d["warp_pad_map"]["18"] = 110;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 1, "crystal pad may host 110");
	EXPECT(ctr_cfg_warp_phys(110), 18, "110 on a crystal pad");

	// Option off: 110 is not a destination; the block may not appear.
	d = seed(0);
	d["warp_pad_map"]["7"] = 110;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg_warp_dest(7), 7, "option off drops 110 to identity");
	EXPECT(ctr_cfg.cortex_track.valid, 0, "option off: nothing valid");
	d = seed(0);
	d["cortex_vortex_track"] = block();
	refused(d, "block while option off refused");
	d = seed(0);
	d["gem_cup_legs"] = {{"100", {110, 1, 2, 3}}};
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg_cup_leg(0, 0) != 110, 1, "option off: leg 110 dropped");

	// Option on, block refused: 110 stays on the map (fail-closed pad).
	d = seed();
	d.erase("cortex_vortex_track");
	refused(d, "missing block refused");
	EXPECT(ctr_cfg_warp_dest(7), 110, "refused block keeps 110 hosted (pad closed, never the dropped track)");

	d = seed(); d["cortex_vortex_track"]["version"] = 2;
	refused(d, "unknown version refused");
	EXPECT(ctr_cfg.schema_newer, 1, "unknown version asks for a client update");
	d = seed(); d["cortex_vortex_track"]["destination_id"] = 13; refused(d, "wrong destination id");
	d = seed(); d["cortex_vortex_track"]["host_level_id"] = 12; refused(d, "wrong host");
	d = seed(); d["cortex_vortex_track"]["lev_sha256"] =
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; refused(d, "wrong LEV hash");
	d = seed(); d["cortex_vortex_track"]["vrm_sha256"] = "x"; refused(d, "malformed VRM hash");
	d = seed(); d["cortex_vortex_track"]["dropped_destination"] = 110; refused(d, "110 cannot be dropped");
	d = seed(); d["cortex_vortex_track"]["dropped_destination"] = 40; refused(d, "dropped out of range");
	d = seed(); d["cortex_vortex_track"]["dropped_destination"] = 3; refused(d, "dropped destination still hosted");
	d = seed(); d["warp_pad_map"]["3"] = 110; refused(d, "110 on two pads");
	d = seed(); d["warp_pad_map"]["7"] = 7; refused(d, "110 hosted by no pad");
	d = seed(); d["gem_cup_legs"] = {{"101", {7, 1, 2, 3}}}; refused(d, "dropped race track as a randomized leg");
	d = seed(); d["warp_pad_map"]["7"] = 7; d["warp_pad_map"]["9"] = 110;
	d["cortex_vortex_track"]["dropped_destination"] = 9;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 1, "vanilla legs may still include the dropped track");
	d = seed(); d["cortex_vortex_track"]["locations"]["trophy"] = 35011015; refused(d, "Oxide Station trophy code refused");
	d = seed(); d["cortex_vortex_track"]["locations"]["trophy"] = -1; refused(d, "trophy is required");
	d = seed(); d["cortex_vortex_track"]["locations"]["relic"][1] = 35012115; refused(d, "foreign relic code");
	d = seed(); d["cortex_vortex_track"]["locations"]["relic"] = {35026001, 35026002}; refused(d, "short relic array");
	d = seed(); d["cortex_vortex_track"]["locations"]["podium"]["held_1st"] = 35015065; refused(d, "Oxide Station rung refused");
	d = seed(); d["cortex_vortex_track"]["locations"]["podium"].erase("finish_any"); refused(d, "missing rung key");
	d = seed(); d["cortex_vortex_track"]["locations"]["ctr_token"] = 35011104; refused(d, "foreign token");
	d = seed(); d["cortex_vortex_track"]["locations"]["letters"] = {35026006, -1, -1};
	refused(d, "letters without lettersanity refused");
	d = seed(); d["cortex_vortex_track"]["letter_items"] = {35010194, 35010201, 35010202}; refused(d, "trial letter item refused");

	// Absent tiers are legal.
	d = seed();
	d["cortex_vortex_track"]["locations"]["relic"] = {35026001, -1, -1};
	d["cortex_vortex_track"]["locations"]["ctr_token"] = -1;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 1, "absent relic tiers and token accepted");
	EXPECT(ctr_cfg.cortex_track.relic[1], -1, "gold absent");

	// Lettersanity with items.
	d = seed();
	d["lettersanity_checks"] = {{"mode", 2}, {"letters_per_track", 2}, {"locations", nlohmann::json::object()}};
	d["cortex_vortex_track"]["locations"]["letters"] = {35026006, -1, 35026008};
	d["cortex_vortex_track"]["letter_items"] = {35010200, -1, 35010202};
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 1, "mode-2 letters accepted");
	EXPECT(ctr_cfg.cortex_track.letters[1], -1, "unselected T");
	EXPECT(ctr_cfg.cortex_track.letter_items[2], 35010202, "R item");

	// Per-track Wumpa: block and venue must both carry 35016121.
	d = seed();
	d["ctr_options"]["wumpa_check"] = CTR_CFG_WUMPA_PER_TRACK;
	d["cortex_vortex_track"]["locations"]["wumpa"] = 35016121;
	d["oxide_final_venue"]["wumpa_location"] = 35016121;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.valid, 1, "per-track Wumpa accepted");
	EXPECT(ctr_cfg.cortex_track.wumpa, 35016121, "Wumpa code");
	EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "venue carries the widened code");
	d["cortex_vortex_track"]["locations"]["wumpa"] = -1;
	refused(d, "per-track Wumpa without the code refused");
	d = seed();
	d["cortex_vortex_track"]["locations"]["wumpa"] = 35016121;
	refused(d, "Wumpa code without per-track Wumpa refused");
	d = seed();
	d["cortex_vortex_track"]["locations"]["wumpa"] = 35016113;
	refused(d, "Oxide Station Wumpa alias refused");

	// Widened venue rule: pad track on + Oxide Station venue + per-track.
	d = seed();
	d["ctr_options"]["wumpa_check"] = CTR_CFG_WUMPA_PER_TRACK;
	d["ctr_options"]["oxide_final_track"] = 1;
	d["oxide_final_venue"]["track"] = "oxide_station";
	d["oxide_final_venue"]["wumpa_location"] = 35016121;
	d["cortex_vortex_track"]["locations"]["wumpa"] = 35016121;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "Oxide Station venue + pad track carries 35016121");
	d["oxide_final_venue"]["wumpa_location"] = -1;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "widened rule enforced on the venue");
	// Option off + CV venue + Oxide disabled: the code is absent.
	d = seed(0);
	d["ctr_options"]["wumpa_check"] = CTR_CFG_WUMPA_PER_TRACK;
	d["ctr_options"]["goal_oxide"] = 3;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "disabled Oxide: no Cortex Vortex Wumpa on the venue");

	// Schema below 15 never reads the option or the block.
	d = seed();
	d["ctr_options"]["schema_version"] = 13;
	ap_seedcfg_parse_json(d);
	EXPECT(ctr_cfg.cortex_track.option, 0, "schema 13: no option");
	EXPECT(ctr_cfg.cortex_track.valid, 0, "schema 13: block ignored");
	EXPECT(ctr_cfg_warp_dest(7), 7, "schema 13: 110 is not a destination");

	// Any option value but 0/1 fails closed.
	d = seed(); d["ctr_options"]["cortex_vortex_track"] = 2;
	refused(d, "option value 2 refused");
	EXPECT(ctr_cfg_warp_dest(7), 110, "value 2 keeps the pad closed on 110");

	if (const char *path = std::getenv("CTR_CORTEX_SLOT_DATA"))
	{
		nlohmann::json real;
		if (!load(path, real)) { std::printf("cannot read %s\n", path); return 1; }
		ap_seedcfg_parse_json(real);
		EXPECT(ctr_cfg.cortex_track.valid, 1, "generated seed block accepted");
		EXPECT(ctr_cfg_warp_phys(110) != 110, 1, "generated seed hosts 110");
		std::printf("real seed: 110 on pad %d, dropped %d\n", ctr_cfg_warp_phys(110),
		            ctr_cfg.cortex_track.dropped_destination);
	}

	std::printf("%s cortex track seedcfg (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
