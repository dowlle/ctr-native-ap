// Schema-10 parser acceptance for issue #203 trial-track race identities.
// c++ -std=c++17 -DCTR_AP -Iap -Iap/vendor/json/include tools/test-trial-track-seedcfg.cpp ap/ap_seedcfg.cpp -o /tmp/test-trial-track-seedcfg
#include <cstdio>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
#define EXPECT(g,w,n) do { checks++; long _g=(g),_w=(w); if(_g!=_w){ failures++; std::printf("FAIL %s (%ld != %ld)\n",n,_g,_w); } } while(0)

static nlohmann::json base(int slide, int turbo)
{
    return {{"ctr_options", {{"schema_version", 10},
                              {"slide_coliseum_races", slide},
                              {"turbo_track_races", turbo}}}};
}

int main()
{
    auto d = base(2, 1);
    d["trial_track_checks"] = {{"enabled", true}, {"locations", {
        {"16", {35016200, 35016210}}, {"17", {35016201, -1}}}}};
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.schema_newer, 0, "schema 10 known");
    EXPECT(ctr_cfg.trial_track_valid[0], 1, "Slide valid");
    EXPECT(ctr_cfg.trial_track_locations[0][0], 35016200, "Slide Trophy");
    EXPECT(ctr_cfg.trial_track_locations[0][1], 35016210, "Slide CTR");
    EXPECT(ctr_cfg.trial_track_valid[1], 1, "Turbo valid");
    EXPECT(ctr_cfg.trial_track_locations[1][0], 35016201, "Turbo Trophy");
    EXPECT(ctr_cfg.trial_track_locations[1][1], -1, "Turbo CTR absent");

    d = base(0, 0);
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.trial_track_valid[0], 0, "absence resets Slide");
    EXPECT(ctr_cfg.trial_track_locations[0][0], -1, "absence clears code");

    d = base(2, 2);
    d["trial_track_checks"] = {{"enabled", true}, {"locations", {
        {"16", {35016200, 35016210}}, {"17", {35016201, 35016210}}}}};
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.trial_track_valid[0], 0, "duplicate disables Slide");
    EXPECT(ctr_cfg.trial_track_valid[1], 0, "duplicate disables Turbo");

    d = base(2, 0);
    d["trial_track_checks"] = {{"enabled", true}, {"locations", {
        {"16", {-1, 35016210}}, {"17", {-1, -1}}}}};
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.trial_track_valid[0], 0, "CTR without Trophy refused");

    std::printf("%s trial-track seedcfg (%d checks, %d failures)\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
