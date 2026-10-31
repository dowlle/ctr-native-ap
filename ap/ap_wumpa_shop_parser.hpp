// The `wumpa_shop` block and `goal_wumpa` (#249), slot_data schema 18.
// Included by ap_seedcfg.cpp and tools/test-wumpa-shop-seedcfg.cpp.
//
// A bad block refuses the SHOP only, never the seed: the shop row then shows
// SHOP UNAVAILABLE and banking still runs for a Wumpa goal. One log line
// names the reason. `goal_wumpa` is read from ctr_options; a value outside
// 0..5000 or a non-integer makes the arm unreadable (-1, never met) with a log
// line: auto-goal then never completes on it, even when it is the only arm.
#include <set>
#include "ap_wumpa_shop.h"

static const char *ap_wumpa_shop_parse_block(const nlohmann::json &j, const nlohmann::json &opt,
                                             int schema, AP_WumpaShopConfig *out)
{
    const nlohmann::json &b = j.at("wumpa_shop");
    if (schema < 18) return "block on a schema below 18";
    if (!b.is_object() || !b.contains("version") || !b["version"].is_number_integer())
        return "missing version";
    if (b["version"] != AP_WUMPA_SHOP_BLOCK_VERSION) return "unknown version";
    if (!b.contains("hubs") || !b["hubs"].is_array() || b["hubs"].size() != AP_WUMPA_SHOP_HUBS)
        return "hubs is not a five-entry list";
    int slots = -1;
    std::set<int> seen;
    for (const auto &h : b["hubs"]) {
        if (!h.is_object() || !h.contains("level") || !h["level"].is_number_integer() ||
            !h.contains("slots") || !h["slots"].is_array())
            return "malformed hub";
        int hub = AP_WumpaShopHubOfLevel(h["level"].get<int>());
        if (hub < 0) return "unknown hub level";
        if (!seen.insert(hub).second) return "hub listed twice";
        int n = (int)h["slots"].size();
        if (n < 1 || n > AP_WUMPA_SHOP_MAX_SLOTS) return "slot count out of range";
        if (slots >= 0 && n != slots) return "hubs disagree on the slot count";
        slots = n;
        for (int k = 0; k < n; ++k) {
            const auto &s = h["slots"][k];
            if (!s.is_object() || !s.contains("code") || !s["code"].is_number_integer() ||
                !s.contains("price") || !s["price"].is_number_integer() ||
                !s.contains("unlock_banked") || !s["unlock_banked"].is_number_integer())
                return "malformed slot";
            long long code = s["code"].get<long long>();
            long long price = s["price"].get<long long>();
            long long unlock = s["unlock_banked"].get<long long>();
            if (code != AP_WumpaShopCode(hub, k + 1)) return "code outside the minted shop block";
            if (price < 0 || price > AP_WUMPA_SHOP_PRICE_MAX) return "price out of range";
            if (unlock < 0 || unlock > AP_WUMPA_SHOP_UNLOCK_MAX) return "unlock out of range";
            out->slot[hub][k].code = code;
            out->slot[hub][k].price = (int)price;
            out->slot[hub][k].unlock = (int)unlock;
        }
    }
    auto declared = opt.find("wumpa_shop_slots");
    if (declared != opt.end() && (!declared->is_number_integer() || *declared != slots))
        return "wumpa_shop_slots disagrees with the block";
    out->slots = slots;
    return nullptr;
}

// Parse into `out`. Never refuses the seed. Returns the log line to print, or
// an empty string when there is nothing to say.
static std::string ap_wumpa_shop_parse(const nlohmann::json &j, const nlohmann::json &opt,
                                       int schema, AP_WumpaShopConfig *out)
{
    std::string log;
    std::memset(out, 0, sizeof *out);
    out->hints = 1;
    auto goal = opt.find("goal_wumpa");
    if (goal != opt.end()) {
        if (goal->is_number_integer() && *goal >= 0 && *goal <= AP_WUMPA_GOAL_MAX)
            out->goal = goal->get<int>();
        else
        {
            out->goal = -1;
            log += "[AP WUMPA SHOP] goal_wumpa unreadable: the goal will not complete on its own\n";
        }
    }
    auto hints = opt.find("wumpa_shop_hints");
    if (hints != opt.end() && hints->is_boolean())
        out->hints = hints->get<bool>() ? 1 : 0;
    if (!j.contains("wumpa_shop"))
        return log;
    const char *why = nullptr;
    try {
        why = ap_wumpa_shop_parse_block(j, opt, schema, out);
    } catch (...) {
        why = "malformed block";
    }
    if (why) {
        int keepGoal = out->goal, keepHints = out->hints;
        std::memset(out, 0, sizeof *out);
        out->goal = keepGoal; out->hints = keepHints;
        out->state = AP_WUMPA_SHOP_REFUSED;
        log += std::string("[AP WUMPA SHOP] wumpa_shop refused: ") + why + "; shop unavailable\n";
        return log;
    }
    out->state = AP_WUMPA_SHOP_VALID;
    return log;
}
