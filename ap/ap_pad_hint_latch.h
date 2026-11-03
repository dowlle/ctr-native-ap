// ap_pad_hint_latch.h -- Pad Approach Hints (#124): the once-ever latch.
//
// The room's data storage holds every location code this slot has hinted by
// approaching a pad, as an object {"<code>": 1}, under a key scoped like the
// Wumpa Shop wallet (endpoint, seed, team, slot). The client reads it at
// connect and hints nothing until the read arrives, so a reconnect, a second
// computer or a restarted client never sends the same location again. A value
// it cannot read (another tool wrote it) stops hints for this connection
// rather than risk sending them twice. See ap_pad_hints_logic.h.
#ifndef AP_PAD_HINT_LATCH_H
#define AP_PAD_HINT_LATCH_H

#include <set>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

struct APPadHintLatch {
    std::string key;
    bool barrier = false; // the Get reply arrived on this connection
    bool valid = false;   // and it could be read
    std::set<int64_t> codes;

    static std::string part(const std::string &s) { return std::to_string(s.size()) + ":" + s; }

    void connect(const std::string &seed, int team, int slot) {
        key = "ctr_pad_hints_v1:" + part(seed) + ":" + std::to_string(team) + ":" + std::to_string(slot);
        barrier = valid = false;
        codes.clear();
    }
    void disconnected() { barrier = valid = false; }
    bool ready() const { return barrier && valid; }
    bool has(int64_t code) const { return codes.count(code) != 0; }

    // null (never written) is an empty latch. Anything but an object of
    // decimal location codes is unreadable.
    static bool accept(const nlohmann::json &v, std::set<int64_t> &out) {
        out.clear();
        if (v.is_null()) return true;
        if (!v.is_object()) return false;
        for (auto it = v.begin(); it != v.end(); ++it) {
            const std::string &k = it.key();
            if (k.empty() || k.size() > 18 || k.find_first_not_of("0123456789") != std::string::npos) return false;
            long long code = std::stoll(k);
            if (code <= 0) return false;
            out.insert((int64_t)code);
        }
        return true;
    }
    void retrieved(const nlohmann::json &v) {
        std::set<int64_t> got;
        barrier = true;
        valid = accept(v, got);
        if (valid) codes.insert(got.begin(), got.end());
    }
    // SetReply or SetNotify: another client of this slot may have hinted more.
    void reply(const nlohmann::json &v) {
        std::set<int64_t> got;
        if (accept(v, got)) codes.insert(got.begin(), got.end());
    }
    // Latch the codes about to be sent; returns the data storage update value.
    nlohmann::json mark(const int64_t *sent, int n) {
        nlohmann::json update = nlohmann::json::object();
        for (int i = 0; i < n; ++i) {
            codes.insert(sent[i]);
            update[std::to_string((long long)sent[i])] = 1;
        }
        return update;
    }
};

#endif
