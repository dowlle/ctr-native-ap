// Schema-18 arena-off parser and physical-pad policy, using the production
// ap_seedcfg parser and the same pad predicate as the hub, gate and ceremony.
// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include -o /tmp/test-arena-pads-seedcfg tools/test-arena-pads-seedcfg.cpp ap/ap_seedcfg.cpp
#include <cstdio>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"

extern "C" void AP_LogLine(const char *) {}

static int failures;
static void expect(int value, const char *name)
{
    if (!value) { failures++; std::printf("FAIL %s\n", name); }
}
static nlohmann::json seed(int schema, nlohmann::json arenas)
{
    return {{"ctr_options", {{"schema_version", schema},
                              {"include_battle_arenas", arenas}}},
            {"warp_pad_unlock", nlohmann::json::object()}};
}
int main()
{
    const int pads[] = {18, 19, 21, 23};
    ap_seedcfg_parse_json(seed(18, false));
    expect(ctr_cfg_active() && ctr_cfg.include_battle_arenas == 0, "off parsed");
    for (int pad : pads)
        expect(ctr_cfg_arena_pad_disabled(pad), "arena physical pad closed");
    expect(!ctr_cfg_arena_pad_disabled(20) && !ctr_cfg_arena_pad_disabled(100),
           "other pads remain available");
    ap_seedcfg_parse_json(seed(18, true));
    for (int pad : pads)
        expect(!ctr_cfg_arena_pad_disabled(pad), "arena on reopens pad");
    ap_seedcfg_parse_json(seed(17, false));
    expect(!ctr_cfg_arena_pad_disabled(18), "older schema preserves arena behavior");
    ap_seedcfg_parse_json({{"ctr_options", {{"schema_version", 18}}}});
    expect(ctr_cfg.include_battle_arenas == 1 && !ctr_cfg_arena_pad_disabled(18),
           "earlier schema-18 seed defaults on");
    ap_seedcfg_parse_json(seed(18, "off"));
    expect(ctr_cfg.include_battle_arenas == 1, "malformed value defaults on");
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
