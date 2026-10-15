// g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I ap/vendor/json/include -o /tmp/test-crystal-checks tools/test-crystal-checks.cpp ap/ap_seedcfg.cpp
#include <cstdio>
#include <nlohmann/json.hpp>
#include "../ap/ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
static int checks,failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
static nlohmann::json seed()
{
    nlohmann::json j={{"ctr_options",{{"schema_version",18},{"include_battle_arenas",true},{"crystalsanity",true}}},
                      {"crystal_checks",{{"version",1},{"arenas",nlohmann::json::object()}}}};
    int levels[4]={21,19,23,18};
    for (int a=0;a<4;++a) {
        auto &r=j["crystal_checks"]["arenas"][std::to_string(levels[a])];
        r["instance_bases"]=nlohmann::json::array(); r["locations"]=nlohmann::json::array();
        for (int p=1;p<=4;++p) r["instance_bases"].push_back(AP_CrystalInstanceBase(a,p));
        for (int n=0;n<20;++n) r["locations"].push_back(35028400+20*a+n);
    }
    return j;
}
int main()
{
    auto j=seed(); ap_seedcfg_parse_json(j); CHECK(ctr_crystal_checks.state==1);
    int levels[4]={21,19,23,18};
    for (int a=0;a<4;++a) for (int p=1;p<=4;++p) {
        int base=AP_CrystalInstanceBase(a,p);
        for (int n=0;n<20;++n) CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,levels[a],p,base+n)==35028400+20*a+n);
        CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,levels[a],p,base-1)==-1);
        CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,levels[a],p,base+20)==-1);
    }
    for(int n=0;n<80;++n) CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,52000+n)==35028400+n);
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,20,1,8)==-1);
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,21,0,8)==-1);
    CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,51999)==-1);
    CHECK(AP_CrystalPseudoCode(&ctr_crystal_checks,52080)==-1);
    auto bad=j;bad["crystal_checks"]["arenas"]["21"]["locations"][1]=35028400;
    ap_seedcfg_parse_json(bad); CHECK(ctr_crystal_checks.state==-1); CHECK(ctr_cfg_arena_pad_disabled(21));
    bad=j;bad["crystal_checks"]["arenas"]["21"]["instance_bases"][3]=8;
    ap_seedcfg_parse_json(bad); CHECK(ctr_crystal_checks.state==-1);
    bad=j;bad["crystal_checks"]["version"]=2;ap_seedcfg_parse_json(bad);CHECK(ctr_crystal_checks.state==-1);
    bad=j;bad.erase("crystal_checks");ap_seedcfg_parse_json(bad);CHECK(ctr_crystal_checks.state==-1);
    bad=j;bad["ctr_options"]["schema_version"]=17;ap_seedcfg_parse_json(bad);CHECK(ctr_crystal_checks.state==0);
    bad=j;bad["ctr_options"]["include_battle_arenas"]=false;ap_seedcfg_parse_json(bad);CHECK(ctr_crystal_checks.state==0);
    bad=j;bad["ctr_options"].erase("crystalsanity");ap_seedcfg_parse_json(bad);CHECK(ctr_crystal_checks.state==0);
    CHECK(AP_CrystalPickupCode(&ctr_crystal_checks,21,1,8)==-1);
    ap_seedcfg_parse_json(j);CHECK(ctr_crystal_checks.state==1);
    ap_seedcfg_parse_json(nlohmann::json::object());CHECK(ctr_crystal_checks.state==0);
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
