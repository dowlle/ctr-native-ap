// g++ -m32 -std=c++17 -Wall -Wextra -DCTR_AP -I ap -I ap/vendor/json/include -o /tmp/test-wumpa-shop-seedcfg tools/test-wumpa-shop-seedcfg.cpp && /tmp/test-wumpa-shop-seedcfg
//
// #249: the `wumpa_shop` block and goal_wumpa parser (ap/ap_wumpa_shop_parser.hpp).
// tools/fixtures/wumpa_shop.json is a byte-identical copy of the apworld's
// worlds/ctr/test/fixtures/wumpa_shop.json. Every refusal refuses the shop only
// and keeps the Wumpa goal.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include "ap_wumpa_shop_parser.hpp"

static int failures;
#define CHECK(label, expr) do { bool ok_ = (expr); std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

static nlohmann::json fixture()
{
    std::ifstream f("tools/fixtures/wumpa_shop.json");
    return nlohmann::json::parse(f);
}

static AP_WumpaShopConfig parse(const nlohmann::json &j, int schema = 18, std::string *log = nullptr)
{
    AP_WumpaShopConfig c;
    std::string line = ap_wumpa_shop_parse(j, j.at("ctr_options"), schema, &c);
    if (log) *log = line;
    return c;
}

static void refused(const char *label, void (*mutate)(nlohmann::json &))
{
    nlohmann::json j = fixture();
    mutate(j);
    std::string log;
    AP_WumpaShopConfig c = parse(j, 18, &log);
    bool ok = c.state == AP_WUMPA_SHOP_REFUSED && c.slots == 0 && c.goal == 300 &&
              log.find("refused") != std::string::npos && !AP_WumpaShopUsable(&c) &&
              AP_WumpaBankingActive(&c);
    CHECK(label, ok);
}

int main()
{
    nlohmann::json j = fixture();
    AP_WumpaShopConfig c = parse(j);
    CHECK("fixture: valid", c.state == AP_WUMPA_SHOP_VALID);
    CHECK("fixture: four slots, goal 300, hints on", c.slots == 4 && c.goal == 300 && c.hints == 1);
    CHECK("fixture: N. Sanity Beach slot 1", c.slot[0][0].code == 35028600LL && c.slot[0][0].price == 15 &&
                                              c.slot[0][0].unlock == 0);
    CHECK("fixture: Citadel City slot 4 unlocks at 150", c.slot[4][3].code == 35028763LL && c.slot[4][3].unlock == 150);
    bool same = true;
    for (int h = 0; h < 5; ++h) {
        const auto &hub = j["wumpa_shop"]["hubs"][h];
        int idx = AP_WumpaShopHubOfLevel(hub["level"].get<int>());
        for (int k = 0; k < 4; ++k)
            same = same && c.slot[idx][k].price == hub["slots"][k]["price"].get<int>() &&
                   c.slot[idx][k].unlock == hub["slots"][k]["unlock_banked"].get<int>();
    }
    CHECK("fixture: every price and unlock as written", same);
    CHECK("hub levels pinned", j["wumpa_shop"]["hubs"][0]["level"] == 26 && j["wumpa_shop"]["hubs"][1]["level"] == 25 &&
                               j["wumpa_shop"]["hubs"][4]["level"] == 29);

    {
        nlohmann::json off = {{"ctr_options", {{"schema_version", 18}}}};
        AP_WumpaShopConfig o = parse(off);
        CHECK("no keys: shop absent, goal off, no banking", o.state == AP_WUMPA_SHOP_ABSENT && o.goal == 0 &&
                                                            !AP_WumpaBankingActive(&o));
    }
    {
        nlohmann::json g = {{"ctr_options", {{"goal_wumpa", 500}, {"wumpa_shop_slots", 0}}}};
        AP_WumpaShopConfig o = parse(g);
        CHECK("goal alone: banking, no shop", o.goal == 500 && o.state == AP_WUMPA_SHOP_ABSENT && AP_WumpaBankingActive(&o));
    }
    {
        nlohmann::json g = {{"ctr_options", {{"goal_wumpa", -5}}}};
        std::string log;
        AP_WumpaShopConfig o = parse(g, 18, &log);
        CHECK("goal: a negative target turns the arm off with a log line", o.goal == 0 && !log.empty());
        g["ctr_options"]["goal_wumpa"] = 5001;
        o = parse(g, 18, &log);
        CHECK("goal: above 5000 turns the arm off", o.goal == 0 && !log.empty());
    }
    {
        nlohmann::json h = fixture();
        h["ctr_options"]["wumpa_shop_hints"] = false;
        CHECK("hints off", parse(h).hints == 0);
    }
    {
        AP_WumpaShopConfig o = parse(fixture(), 17);
        CHECK("schema 17 refuses the block", o.state == AP_WUMPA_SHOP_REFUSED);
    }
    refused("refuse: missing version", [](nlohmann::json &x) { x["wumpa_shop"].erase("version"); });
    refused("refuse: unknown version", [](nlohmann::json &x) { x["wumpa_shop"]["version"] = 2; });
    refused("refuse: four hubs", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"].erase(4); });
    refused("refuse: unknown hub", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][2]["level"] = 13; });
    refused("refuse: hub twice", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][2]["level"] = 26; });
    refused("refuse: code outside the block", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][1]["slots"][0]["code"] = 35028600; });
    refused("refuse: negative price", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][0]["slots"][1]["price"] = -1; });
    refused("refuse: negative unlock", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][0]["slots"][1]["unlock_banked"] = -1; });
    refused("refuse: hubs disagree on the count", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][3]["slots"].erase(3); });
    refused("refuse: option disagrees with the block", [](nlohmann::json &x) { x["ctr_options"]["wumpa_shop_slots"] = 5; });
    refused("refuse: string price", [](nlohmann::json &x) { x["wumpa_shop"]["hubs"][0]["slots"][0]["price"] = "15"; });
    refused("refuse: not an object", [](nlohmann::json &x) { x["wumpa_shop"] = 7; });

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
