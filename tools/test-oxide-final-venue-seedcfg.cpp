#include <cstdio>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"

extern void ap_seedcfg_parse_json(const nlohmann::json &j);
extern "C" void AP_LogLine(const char *) {}
static int checks, failures;
#define EXPECT(g,w,n) do { checks++; long _g=(g),_w=(w); if(_g!=_w){ failures++; std::printf("FAIL %s (%ld != %ld)\n",n,_g,_w); } } while(0)

static nlohmann::json seed(const char *track, int option, const char *opponent = "nitros_oxide")
{
    return {
        {"ctr_options", {{"schema_version", 11}, {"oxide_final_track", option}}},
        {"oxide_final_venue", {
            {"version", 1}, {"track", track}, {"opponent", opponent},
            {"location", 35011105}, {"host_level_id", 13},
            {"wumpa_location", -1},
            {"lev_sha256", "4e3a2daf56c67be3ac645d3bb5375e516c828a0bca24c35ac69b3366c466fe13"},
            {"vrm_sha256", "4131444b9d1d53971befcfd11349efceaf887c20b795c8890fdcb2c36bdff07d"}}}
    };
}

int main()
{
    auto d = seed("cortex_vortex", 0);
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.schema_newer, 0, "schema 11 known");
    EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "Cortex Vortex valid");
    EXPECT(ctr_cfg.oxide_final_venue.track, CTR_CFG_OXIDE_FINAL_CORTEX_VORTEX, "Cortex Vortex selected");
    EXPECT(ctr_cfg.oxide_final_venue.location, 35011105, "Final identity frozen");

    d = seed("oxide_station", 1);
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "Oxide Station valid");
    EXPECT(ctr_cfg.oxide_final_venue.track, CTR_CFG_OXIDE_FINAL_OXIDE_STATION, "retail selected");

    d = seed("cortex_vortex", 1);
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "option mismatch refused");

    d = seed("cortex_vortex", 0, "ripper_roo");
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "opponent substitution refused");

    d = seed("cortex_vortex", 0);
    d["oxide_final_venue"]["location"] = 35011104;
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "Oxide 1 identity refused");

    d = seed("cortex_vortex", 0);
    d["oxide_final_venue"]["lev_sha256"] = "deadbeef";
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "bad hash refused");

    d = seed("cortex_vortex", 0);
    d["oxide_final_venue"]["lev_sha256"] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "alternate well-formed pair refused");

    d = seed("cortex_vortex", 0);
    d["ctr_options"]["wumpa_check"] = CTR_CFG_WUMPA_PER_TRACK;
    d["oxide_final_venue"]["wumpa_location"] = 35016121;
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 1, "independent Wumpa identity valid");
    EXPECT(ctr_cfg.oxide_final_venue.wumpa_location, 35016121, "Wumpa identity exact");

    d["oxide_final_venue"]["wumpa_location"] = 35016113;
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "Oxide Station Wumpa alias refused");

    d = {{"ctr_options", {{"schema_version", 11}, {"oxide_final_track", 0}}}};
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_final_venue.valid, 0, "missing descriptor refused");

    for (int schema = 12; schema <= 13; schema++)
    for (int goal = 0; goal <= 3; goal++)
    for (int flag = 0; flag <= 3; flag++) {
        d = {{"ctr_options", {{"schema_version", schema}, {"goal_oxide", goal},
                               {"oxide_1_optional", flag}}}};
        ap_seedcfg_parse_json(d);
        EXPECT(ctr_cfg.oxide_1_optional,
               schema == 13 && goal == 2 && (flag == 1 || flag == 2) ? flag : 0,
               "optional-first is schema/goal/flag constrained");
    }
    d = {{"ctr_options", {{"schema_version", 13}, {"goal_oxide", 2}}}};
    ap_seedcfg_parse_json(d);
    EXPECT(ctr_cfg.oxide_1_optional, 0, "absent option resets mandatory first");
    for (const auto &bad : {nlohmann::json(true), nlohmann::json(false),
                           nlohmann::json(1.5), nlohmann::json(2.5),
                           nlohmann::json("true_filler"), nlohmann::json(nullptr),
                           nlohmann::json(18446744073709551615ULL),
                           nlohmann::json(4294967297ULL), nlohmann::json(4294967298ULL)}) {
        d["ctr_options"]["oxide_1_optional"] = bad;
        ap_seedcfg_parse_json(d);
        EXPECT(ctr_cfg.oxide_1_optional, 0, "malformed optional-first enum refused");
    }
    std::printf("%s oxide-final venue seedcfg (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
