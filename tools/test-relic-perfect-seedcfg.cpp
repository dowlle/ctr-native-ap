// Native parser acceptance for `relic_perfect_checks` (issue #49). Compiles the
// REAL parser (ap/ap_seedcfg.cpp is linked in) and parses slot_data files the
// apworld generated (tools/fixtures/relic-perfect/, same seed: option on with
// all 18 rows, option off, and option on with Cortex Vortex taking Crash
// Cove's pad, 17 rows), plus hand-made malformed blocks.
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include
//     tools/test-relic-perfect-seedcfg.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-relic-perfect-seedcfg && /tmp/test-relic-perfect-seedcfg
//
// Run from the repository root (the fixture paths are relative to it).
// Exit 0 = every assertion held.

#include <cstdio>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"
#include "../ap/ap_relic_perfect.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);

extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect(long got, long want, const std::string &name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %ld, want %ld)\n", name.c_str(), got, want);
	}
}

static int rows_set(void)
{
	int n = 0;
	for (int i = 0; i < CTR_CFG_RELIC_PERFECT_COUNT; i++)
		n += ctr_cfg.relic_perfect[i] != -1;
	return n;
}

static void expect_off(const std::string &name)
{
	expect(ctr_cfg.relic_perfect_enabled, 0, name + ": disabled");
	expect(rows_set(), 0, name + ": no rows");
}

static nlohmann::json full_block(void)
{
	nlohmann::json loc = nlohmann::json::object();
	for (int lid = 0; lid < CTR_CFG_RELIC_PERFECT_COUNT; lid++)
		loc[std::to_string(lid)] = {AP_RelicPerfectExpectedCode(lid)};
	return {{"enabled", true}, {"locations", loc}};
}

static nlohmann::json seed(const nlohmann::json &block)
{
	nlohmann::json j = {{"ctr_options", {{"schema_version", 16}}}};
	if (!block.is_discarded())
		j["relic_perfect_checks"] = block;
	return j;
}

static void test_hand_made(void)
{
	ap_seedcfg_parse_json(seed(nlohmann::json::value_t::discarded));
	expect_off("absent block");

	ap_seedcfg_parse_json(seed(full_block()));
	expect(ctr_cfg.relic_perfect_enabled, 1, "full block enabled");
	for (int lid = 0; lid < CTR_CFG_RELIC_PERFECT_COUNT; lid++)
		expect(ctr_cfg.relic_perfect[lid], AP_RelicPerfectExpectedCode(lid),
		       "full block row " + std::to_string(lid));

	// Reparse clears: a later seed without the block inherits nothing.
	ap_seedcfg_parse_json(seed(nlohmann::json::value_t::discarded));
	expect_off("reparse without block");
	ap_seedcfg_parse_json(seed(full_block()));
	ap_seedcfg_parse_json(nlohmann::json::object());
	expect_off("reparse without ctr_options");

	nlohmann::json b = {{"enabled", false}, {"locations", nlohmann::json::object()}};
	ap_seedcfg_parse_json(seed(b));
	expect_off("enabled false, no rows");

	struct { const char *name; nlohmann::json block; } bad[] = {
		{"block is an array", nlohmann::json::array()},
		{"block is null", nullptr},
		{"locations missing", {{"enabled", true}}},
		{"locations is an array", {{"enabled", true}, {"locations", nlohmann::json::array()}}},
		{"enabled missing", {{"locations", {{"3", {35012400}}}}}},
		{"enabled is 1", {{"enabled", 1}, {"locations", {{"3", {35012400}}}}}},
		{"enabled true, no rows", {{"enabled", true}, {"locations", nlohmann::json::object()}}},
		{"enabled false with rows", {{"enabled", false}, {"locations", {{"3", {35012400}}}}}},
	};
	for (auto &c : bad)
	{
		ap_seedcfg_parse_json(seed(full_block()));
		ap_seedcfg_parse_json(seed(c.block));
		expect_off(c.name);
	}

	// One bad row refuses the whole block, including the 17 good ones.
	const char *keys[] = {"03", "+3", "-1", "18", "", " 3", "3junk", "3.0", "99999999999"};
	for (const char *k : keys)
	{
		nlohmann::json blk = full_block();
		blk["locations"][k] = {35012400};
		ap_seedcfg_parse_json(seed(blk));
		expect_off(std::string("bad key '") + k + "'");
	}
	nlohmann::json values[] = {
		35012400,                          // bare integer, not an array
		nlohmann::json::array(),           // empty array
		{35012400, 35012400},              // two slots
		{true}, {"35012400"}, {35012400.0}, {nullptr}, {-1},
		{35012401},                        // Roo's Tubes' code on Crash Cove
		{9223372036854775807LL},           // oversized
	};
	for (auto &v : values)
	{
		nlohmann::json blk = full_block();
		blk["locations"]["3"] = v;
		ap_seedcfg_parse_json(seed(blk));
		expect_off("bad value " + v.dump());
	}
	// Every LevelID refuses every other LevelID's code.
	for (int lid = 0; lid < CTR_CFG_RELIC_PERFECT_COUNT; lid++)
	{
		nlohmann::json blk = full_block();
		blk["locations"][std::to_string(lid)] =
			{AP_RelicPerfectExpectedCode((lid + 1) % CTR_CFG_RELIC_PERFECT_COUNT)};
		ap_seedcfg_parse_json(seed(blk));
		expect_off("swapped code at " + std::to_string(lid));
	}
}

static bool load(const char *path, nlohmann::json &out)
{
	std::ifstream in(path);
	if (!in)
		return false;
	out = nlohmann::json::parse(in);
	return true;
}

static void test_fixture(const char *name, int wantRows, int absentLevel)
{
	char path[256];
	nlohmann::json real;

	std::snprintf(path, sizeof path, "tools/fixtures/relic-perfect/%s.slot_data.json", name);
	checks++;
	if (!load(path, real))
	{
		failures++;
		std::printf("FAIL fixture missing: %s\n", path);
		return;
	}
	ap_seedcfg_parse_json(real);
	expect(ctr_cfg_active() ? 1 : 0, 1, std::string(name) + ": active");
	expect(ctr_cfg.schema_newer, 0, std::string(name) + ": schema known");
	expect(ctr_cfg.seed_rejected, 0, std::string(name) + ": admitted");
	expect(rows_set(), wantRows, std::string(name) + ": rows");
	expect(ctr_cfg.relic_perfect_enabled, wantRows > 0, std::string(name) + ": enabled");
	for (int lid = 0; lid < CTR_CFG_RELIC_PERFECT_COUNT && wantRows; lid++)
		expect(ctr_cfg.relic_perfect[lid],
		       lid == absentLevel ? -1 : AP_RelicPerfectExpectedCode(lid),
		       std::string(name) + ": LevelID " + std::to_string(lid));
}

int main(void)
{
	test_hand_made();
	test_fixture("on_seed2026092749", 18, -1);
	test_fixture("off_seed2026092749", 0, -1);
	test_fixture("cortex_crashcove_seed2026092749", 17, 3);
	std::printf("%s relic perfect seedcfg (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
