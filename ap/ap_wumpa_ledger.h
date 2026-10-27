#ifndef AP_WUMPA_LEDGER_H
#define AP_WUMPA_LEDGER_H

// The Wumpa Shop wallet in the room's data storage (#249).
//
// Two keys per seed, team and slot, scoped like ctr_boss_won_v1:
//   ctr_wumpa_banked_v1  integer, the LIFETIME banked total, written with
//                        "max" so a resend or a stale client never lowers it;
//   ctr_wumpa_shop_v1    object, bought slot code (decimal string) -> price
//                        paid, written with "update".
// balance = lifetime - sum(prices). Spending is never a separate counter, so it
// cannot drift from the purchases.
//
// BANKING. A credit raises the local lifetime and is flushed with "max", so
// resending a total is harmless. Before this client has ever read the key for
// this seed and slot, the total is unknown: credits collect in `early` and the
// first reply sets lifetime = server + early. After that the local total is
// absolute, so banking while briefly offline is kept in it and the next reply
// merges with max: nothing is lost or counted twice. A different seed or slot
// starts from zero. Two clients banking on one slot at once can lose one side's
// credits to max; one playing client per slot is assumed, as for every other
// personal-progress key.
//
// PURCHASE (one at a time, online only). Cross checks unlock, balance, sold and
// a known wallet, then writes {code: price} with "update" and asks for the
// reply. The reply's original_value is the wallet as the server held it just
// before this write, because the server applies writes to one key in order.
// The purchase stands only if lifetime - sum(original) still covers the price;
// otherwise a second client spent the fruit first, and the entry is removed
// again with "pop" and the player is told to retry. A code already in the
// original is the same purchase made twice and is kept once. Only a standing
// purchase sends its location check, and every connect resends the check of
// any bought code the server has not checked yet. A crash between the write
// and the check therefore leaves either no charge or one paid purchase that
// the next connect completes.
#ifdef __cplusplus
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct APWumpaLedger {
    enum BuyResult { BUY_OK = 0, BUY_NOT_READY, BUY_BUSY, BUY_LOCKED, BUY_TOO_POOR, BUY_SOLD };
    enum ReplyOutcome { REPLY_NONE = 0, REPLY_CONFIRMED, REPLY_ROLLBACK };

    std::string identity, bankKey, shopKey;
    bool bankBarrier = false, bankValid = false, bankSent = false;
    bool everKnown = false; // a reply for this identity has been merged
    bool shopBarrier = false, shopValid = false;
    long long server = 0;   // last lifetime the server reported
    long long lifetime = 0; // local belief, >= server once known
    long long early = 0;    // credits before this connection's Get reply
    std::map<long long, int> bought;
    long long pendingCode = -1;
    int pendingPrice = 0;
    std::vector<long long> rollbacks; // codes to pop

    static std::string part(const std::string &s) { return std::to_string(s.size()) + ":" + s; }

    void connect(const std::string &endpoint, const std::string &seed, int team, int slot) {
        std::string scope = ":" + part(seed) + ":" + std::to_string(team) + ":" + std::to_string(slot);
        std::string id = part(endpoint) + part(scope);
        if (id != identity) {
            server = lifetime = early = 0;
            everKnown = false;
            bought.clear();
        }
        identity = id;
        bankKey = "ctr_wumpa_banked_v1" + scope;
        shopKey = "ctr_wumpa_shop_v1" + scope;
        bankBarrier = bankValid = bankSent = false;
        shopBarrier = shopValid = false;
        pendingCode = -1; pendingPrice = 0;
        rollbacks.clear();
    }
    void disconnected() {
        bankBarrier = bankValid = bankSent = false;
        shopBarrier = shopValid = false;
        pendingCode = -1; pendingPrice = 0;
        rollbacks.clear();
    }

    static bool acceptTotal(const nlohmann::json &v, long long &out) {
        if (v.is_null()) { out = 0; return true; }
        if (!v.is_number_integer()) return false;
        if (v.is_number_unsigned()) {
            auto n = v.get<unsigned long long>();
            if (n > 1000000000ULL) return false;
            out = (long long)n;
            return true;
        }
        auto n = v.get<long long>();
        if (n < 0 || n > 1000000000LL) return false;
        out = n;
        return true;
    }
    static bool acceptShop(const nlohmann::json &v, std::map<long long, int> &out) {
        out.clear();
        if (v.is_null()) return true;
        if (!v.is_object()) return false;
        for (auto it = v.begin(); it != v.end(); ++it) {
            const std::string &k = it.key();
            if (k.empty() || k.size() > 12 || k.find_first_not_of("0123456789") != std::string::npos) return false;
            if (!it.value().is_number_integer()) return false;
            long long price = it.value().get<long long>();
            if (price < 0 || price > 100000) return false;
            out[std::stoll(k)] = (int)price;
        }
        return true;
    }

    // ── banking ──
    bool bankKnown() const { return bankBarrier && bankValid; }
    void credit(long long n) {
        if (identity.empty() || n <= 0) return;
        if (everKnown) lifetime += n; else early += n;
    }
    // Lifetime as the game shows it, including credits not merged yet.
    long long shownLifetime() const { return lifetime + early; }
    void bankRetrieved(const nlohmann::json &v) {
        long long n = 0;
        bankBarrier = true;
        if (!acceptTotal(v, n)) { bankValid = false; return; }
        bankValid = true;
        server = n;
        lifetime = everKnown ? (lifetime > n ? lifetime : n) : n + early;
        early = 0;
        everKnown = true;
    }
    void bankReply(const nlohmann::json &v) {
        long long n = 0;
        bankSent = false;
        if (!acceptTotal(v, n)) return;
        if (n > server) server = n;
        if (server > lifetime) lifetime = server;
    }
    bool bankWantsSend() const { return bankKnown() && !bankSent && lifetime > server; }

    // ── wallet ──
    bool shopKnown() const { return shopBarrier && shopValid; }
    static long long spent(const std::map<long long, int> &m) {
        long long s = 0;
        for (auto &kv : m) s += kv.second;
        return s;
    }
    long long balance() const { return lifetime - spent(bought); }
    bool isBought(long long code) const { return bought.count(code) != 0; }
    void shopRetrieved(const nlohmann::json &v) {
        shopBarrier = true;
        shopValid = acceptShop(v, bought);
    }

    BuyResult tryBuy(long long code, int price, int unlock, bool checked) {
        if (!bankKnown() || !shopKnown()) return BUY_NOT_READY;
        if (pendingCode >= 0 || !rollbacks.empty()) return BUY_BUSY;
        if (checked || isBought(code)) return BUY_SOLD;
        if (lifetime < unlock) return BUY_LOCKED;
        if (balance() < price) return BUY_TOO_POOR;
        pendingCode = code; pendingPrice = price;
        return BUY_OK;
    }
    // A SetReply (our own, or another client's write through SetNotify).
    ReplyOutcome shopReply(const nlohmann::json &value, const nlohmann::json &original) {
        std::map<long long, int> after, before;
        if (!acceptShop(value, after)) { shopValid = false; return REPLY_NONE; }
        shopValid = true;
        bought = after;
        if (pendingCode < 0 || !after.count(pendingCode)) return REPLY_NONE;
        long long code = pendingCode;
        int price = pendingPrice;
        pendingCode = -1; pendingPrice = 0;
        if (!acceptShop(original, before)) before.clear();
        if (before.count(code)) return REPLY_CONFIRMED; // bought twice, kept once
        if (lifetime - spent(before) >= price) return REPLY_CONFIRMED;
        rollbacks.push_back(code);
        return REPLY_ROLLBACK;
    }
    void rollbackSent(long long code) {
        for (auto it = rollbacks.begin(); it != rollbacks.end(); ++it)
            if (*it == code) { rollbacks.erase(it); break; }
        bought.erase(code);
    }
    // Bought codes whose location check the server does not have yet.
    template <class Checked>
    std::vector<long long> unsentPaid(Checked checked) const {
        std::vector<long long> out;
        for (auto &kv : bought)
            if (!checked(kv.first)) out.push_back(kv.first);
        return out;
    }
};
#endif
#endif
