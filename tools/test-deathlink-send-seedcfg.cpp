// Native parser acceptance for the DeathLink `death_link_send` key. Compiles the
// REAL parser (ap/ap_seedcfg.cpp is linked in).
//
//   c++ -m32 -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include
//     tools/test-deathlink-send-seedcfg.cpp ap/ap_seedcfg.cpp
//     -o /tmp/test-deathlink-send-seedcfg && /tmp/test-deathlink-send-seedcfg
//
// What this pins: death_link_send is an additive bitmask (1 mask_grab, 2
// weapon_hit, 4 race_loss). Absent, negative or non-integer reads as -1 (legacy
// coupling from death_link). Only the low three bits are kept. A second seed does
// not inherit the first one's value.

#include <cstdio>
#include <nlohmann/json.hpp>

#include "../ap/ap_seedcfg.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
extern "C" void AP_LogLine(const char *) {}

static int checks;
static int failures;

static void expect_int(int got, int want, const char *name)
{
	checks++;
	if (got != want)
	{
		failures++;
		std::printf("FAIL %s (got %d, want %d)\n", name, got, want);
	}
}

static nlohmann::json seed(nlohmann::json opts)
{
	opts["schema_version"] = 8;
	return {{"ctr_options", opts}};
}

int main(void)
{
	ap_seedcfg_parse_json(seed({{"death_link", 3}}));
	expect_int(ctr_cfg.death_link, 3, "death_link still parses");
	expect_int(ctr_cfg.death_link_send, -1, "absent key -> -1 (legacy coupling)");

	ap_seedcfg_parse_json(seed({{"death_link", 1}, {"death_link_send", 4}}));
	expect_int(ctr_cfg.death_link_send, 4, "4 = race_loss only, independent of death_link");

	ap_seedcfg_parse_json(seed({{"death_link_send", 0}}));
	expect_int(ctr_cfg.death_link_send, 0, "0 = sends nothing (not the same as absent)");

	ap_seedcfg_parse_json(seed({{"death_link_send", 7}}));
	expect_int(ctr_cfg.death_link_send, 7, "7 = every trigger");

	ap_seedcfg_parse_json(seed({{"death_link_send", 15}}));
	expect_int(ctr_cfg.death_link_send, 7, "unknown future bits are dropped");

	ap_seedcfg_parse_json(seed({{"death_link_send", -3}}));
	expect_int(ctr_cfg.death_link_send, -1, "negative -> -1");

	ap_seedcfg_parse_json(seed({{"death_link_send", "x"}}));
	expect_int(ctr_cfg.death_link_send, -1, "non-integer -> -1");

	ap_seedcfg_parse_json(seed({{"death_link_send", 5}}));
	ap_seedcfg_parse_json(seed({{"death_link", 2}}));
	expect_int(ctr_cfg.death_link_send, -1, "a second seed does not inherit the first");

	std::printf("%s: %d checks, %d failure(s)\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
