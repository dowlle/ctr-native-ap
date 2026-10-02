// Native parser acceptance for the lowered vanilla Slide Coliseum gate
// (slot_data Contract, 2026-10-01 entry; ruling of 2026-10-01).
//
// A vanilla-mode seed that created fewer than 10 Sapphire Relics emits pad 16's
// stage-1 requirement: {type 4, count N, colour 0}, or the free convention
// {type 1, count 0} when it created none. ctr_cfg_slide_coliseum_req() is the
// single accessor the pad gate (AP_PadStage1Met), the LInB birth display and
// the retail "10 relics" hint resolve through. It must return the emitted
// record, and NULL (the retail 10) for a seed without the entry, so a seed
// rolled before the rule keeps 10.
//
// Build with C++17, CTR_AP, 32-bit layout and ap/vendor/json/include; links
// ap/ap_seedcfg.cpp.
#include <cstdio>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect_eq(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %d, want %d)\n", name, got, want);
	}
}

static nlohmann::json vanilla_seed(void)
{
	return {{"ctr_options", {{"schema_version", 16}, {"warppad_unlock_mode", 0}}}};
}

static nlohmann::json req(int type, int count, int colour)
{
	return {{"type", type}, {"count", count}, {"colour", colour}};
}

static nlohmann::json pad16(int type, int count, int colour)
{
	return {{"16", {{"stage1", req(type, count, colour)}, {"stage2", req(0, 0, -1)}}}};
}

// The retail rule when the accessor returns NULL, the emitted rule otherwise:
// what the gate sites compute for a type-4 Sapphire or a type-1 free record.
static int sapphires_needed(void)
{
	const ctr_req *r = ctr_cfg_slide_coliseum_req();
	if (!r)
		return CTR_CFG_SLIDE_COLISEUM_RETAIL_SAPPHIRES;
	if (r->type == 1 && r->count == 0)
		return 0;
	return r->type == 4 ? r->count : -1;
}

static void test_lowered_vanilla_gate_is_returned(void)
{
	nlohmann::json doc = vanilla_seed();
	doc["warp_pad_unlock"] = pad16(4, 6, 0);
	ap_seedcfg_parse_json(doc);
	const ctr_req *r = ctr_cfg_slide_coliseum_req();
	expect_eq(r != 0, 1, "emitted vanilla pad-16 requirement is returned");
	if (!r)
		return;
	expect_eq(r->type, 4, "type 4 relic");
	expect_eq(r->count, 6, "count is the Sapphires the seed created");
	expect_eq(r->colour, 0, "colour 0 is the Sapphire tier");
	expect_eq(ctr_cfg.warppad_unlock_mode, 0, "seed is vanilla mode");
	expect_eq(sapphires_needed(), 6, "pad opens at 6 Sapphires");
}

static void test_zero_sapphires_uses_the_free_convention(void)
{
	nlohmann::json doc = vanilla_seed();
	doc["warp_pad_unlock"] = pad16(1, 0, -1);
	ap_seedcfg_parse_json(doc);
	const ctr_req *r = ctr_cfg_slide_coliseum_req();
	expect_eq(r != 0, 1, "free convention is an emitted requirement");
	expect_eq(sapphires_needed(), 0, "pad opens with no Sapphires");
}

static void test_seed_without_the_entry_keeps_ten(void)
{
	ap_seedcfg_parse_json(vanilla_seed());
	expect_eq(ctr_cfg_slide_coliseum_req() == 0, 1, "absent entry is NULL");
	expect_eq(sapphires_needed(), 10, "absent entry keeps the retail 10");

	nlohmann::json doc = vanilla_seed();
	doc["warp_pad_unlock"] = pad16(0, 0, -1);
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg_slide_coliseum_req() == 0, 1, "type 0 entry is NULL");
	expect_eq(sapphires_needed(), 10, "type 0 entry keeps the retail 10");
}

static void test_previous_seed_does_not_leak(void)
{
	nlohmann::json doc = vanilla_seed();
	doc["warp_pad_unlock"] = pad16(4, 3, 0);
	ap_seedcfg_parse_json(doc);
	expect_eq(sapphires_needed(), 3, "first seed lowered to 3");
	ap_seedcfg_parse_json(vanilla_seed());
	expect_eq(sapphires_needed(), 10, "next seed without the entry is back at 10");
}

static void test_randomized_requirement_is_returned_unchanged(void)
{
	nlohmann::json doc = {{"ctr_options", {{"schema_version", 16}, {"warppad_unlock_mode", 1}}}};
	doc["warp_pad_unlock"] = pad16(1, 7, -1);
	ap_seedcfg_parse_json(doc);
	const ctr_req *r = ctr_cfg_slide_coliseum_req();
	expect_eq(r != 0, 1, "randomized requirement is returned");
	if (r)
	{
		expect_eq(r->type, 1, "randomized trophy type kept");
		expect_eq(r->count, 7, "randomized trophy count kept");
	}
}

static void test_inactive_config_is_retail(void)
{
	nlohmann::json doc = {{"ctr_options", {{"schema_version", 0}}}};
	doc["warp_pad_unlock"] = pad16(4, 2, 0);
	ap_seedcfg_parse_json(doc);
	expect_eq(ctr_cfg_slide_coliseum_req() == 0, 1, "inactive slot_data is NULL");
}

int main(void)
{
	test_lowered_vanilla_gate_is_returned();
	test_zero_sapphires_uses_the_free_convention();
	test_seed_without_the_entry_keeps_ten();
	test_previous_seed_does_not_leak();
	test_randomized_requirement_is_returned_unchanged();
	test_inactive_config_is_retail();
	std::printf("%s slide-coliseum gate seedcfg (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
