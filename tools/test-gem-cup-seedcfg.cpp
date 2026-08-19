// Native parser acceptance for issue #166's schema-7 gem_cup_legs wire.
//
// Build with C++17, CTR_AP, 32-bit layout and ap/vendor/json/include.
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

static const int vanilla[20] = {
	3, 9, 2, 5,
	6, 14, 12, 10,
	4, 8, 1, 11,
	0, 15, 7, 13,
	6, 5, 1, 7,
};

static const int alpha3_matrix[20] = {
	1, 7, 4, 2,
	2, 15, 6, 14,
	9, 12, 7, 12,
	12, 12, 9, 2,
	15, 10, 11, 8,
};

static nlohmann::json base(void)
{
	return {{"ctr_options", {{"schema_version", 7}}}};
}

static void test_exact_alpha3_matrix_wire(void)
{
	nlohmann::json doc = base();
	doc["gem_cup_legs"] = {
		{"100", {1, 7, 4, 2}},
		{"101", {2, 15, 6, 14}},
		{"102", {9, 12, 7, 12}},
		{"103", {12, 12, 9, 2}},
		{"104", {15, 10, 11, 8}},
	};
	ap_seedcfg_parse_json(doc);

	for (int cup = 0; cup < 5; cup++)
		for (int leg = 0; leg < 4; leg++)
			expect_eq(ctr_cfg_cup_leg(cup, leg), alpha3_matrix[cup * 4 + leg],
			          "exact Alpha3Matrix cup leg reaches native accessor");

	// Duplicates are legal independent draws and must not be normalized away.
	expect_eq(ctr_cfg_cup_leg(2, 1), 12, "Blue leg 2 is Polar Pass");
	expect_eq(ctr_cfg_cup_leg(2, 3), 12, "Blue leg 4 repeats Polar Pass");
	expect_eq(ctr_cfg_cup_leg(3, 0), 12, "Yellow leg 1 is Polar Pass");
	expect_eq(ctr_cfg_cup_leg(3, 1), 12, "Yellow leg 2 repeats Polar Pass");
}

static void test_absent_block_resets_to_vanilla(void)
{
	ap_seedcfg_parse_json(base());
	for (int cup = 0; cup < 5; cup++)
		for (int leg = 0; leg < 4; leg++)
			expect_eq(ctr_cfg_cup_leg(cup, leg), vanilla[cup * 4 + leg],
			          "absent block restores the cached vanilla leg");
}

static void test_malformed_elements_fall_back_individually(void)
{
	nlohmann::json doc = base();
	doc["gem_cup_legs"] = {
		{"100", {15, -1, "bad", 16, 7}},
		{"101", "wrong shape"},
		{"104", {0, 1}},
		{"105", {1, 2, 3, 4}},
		{"not-a-cup", {1, 2, 3, 4}},
	};
	ap_seedcfg_parse_json(doc);

	expect_eq(ctr_cfg_cup_leg(0, 0), 15, "valid replacement lands");
	expect_eq(ctr_cfg_cup_leg(0, 1), vanilla[1], "negative leg keeps vanilla");
	expect_eq(ctr_cfg_cup_leg(0, 2), vanilla[2], "non-integer leg keeps vanilla");
	expect_eq(ctr_cfg_cup_leg(0, 3), vanilla[3], "track 16 keeps vanilla");
	expect_eq(ctr_cfg_cup_leg(1, 0), vanilla[4], "bad cup shape keeps vanilla");
	expect_eq(ctr_cfg_cup_leg(4, 0), 0, "partial cup first leg lands");
	expect_eq(ctr_cfg_cup_leg(4, 1), 1, "partial cup second leg lands");
	expect_eq(ctr_cfg_cup_leg(4, 2), vanilla[18], "missing third leg keeps vanilla");
	expect_eq(ctr_cfg_cup_leg(4, 3), vanilla[19], "missing fourth leg keeps vanilla");
	expect_eq(ctr_cfg_cup_leg(-1, 0), -1, "negative cup accessor fails closed");
	expect_eq(ctr_cfg_cup_leg(5, 0), -1, "cup past range fails closed");
	expect_eq(ctr_cfg_cup_leg(0, 4), -1, "leg past range fails closed");
}

int main(void)
{
	ctr_cfg_set_vanilla_cup_legs(vanilla);
	test_exact_alpha3_matrix_wire();
	test_absent_block_resets_to_vanilla();
	test_malformed_elements_fall_back_individually();
	std::printf("%s gem-cup seedcfg (%d checks, %d failures)\n",
	            failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
