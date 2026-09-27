#ifndef AP_OXIDE_SCENE_SEEN_H
#define AP_OXIDE_SCENE_SEEN_H

// Issue #377: "the Oxide Final Challenge scene has played for this slot".
//
// The scene plays once per seed, so whether it already played is per-slot
// server state, not local save state: it must survive a client restart and a
// change of machine, and a fresh room must start clean. Server data storage is
// kept per room, so a new room built from the same seed starts without the key.
// The key also names the seed, team and slot (the same shape as the hub-door
// history in ap_door_history.h), so two slots in one room never share it.
//
// FAIL SAFE. Until the Get reply for this key has arrived (`known()`), the
// caller must not auto-play the scene: an unknown flag could be a "seen" that
// has not been read yet. A value that is not 0 or 1 is some other tool's key
// and leaves the flag unknown for the whole session, which also means no
// auto-play.
//
// Value: integer 0 or 1. Written with the data storage "or" operation and a
// default of 0, so concurrent writers can only ever set it.
#ifdef __cplusplus
#include <string>
#include <nlohmann/json.hpp>

struct APOxideSceneSeen {
    std::string identity, key;
    bool barrier = false; // the Get reply for this connection has arrived
    bool valid = false;   // the stored value was readable
    bool seen = false;    // played, from the server or recorded locally
    bool pending = false; // recorded locally, server has not confirmed yet
    bool sent = false;    // a Set is in flight for this connection

    static std::string part(const std::string &s) {
        return std::to_string(s.size()) + ":" + s;
    }
    void connect(const std::string &endpoint, const std::string &seed, int team, int slot) {
        std::string k = "ctr_oxide_final_scene_v1:" + part(seed) + ":" +
                        std::to_string(team) + ":" + std::to_string(slot);
        std::string id = part(endpoint) + part(k);
        if (id != identity) { seen = pending = false; }
        identity = id; key = k; barrier = valid = sent = false;
    }
    void disconnected() { barrier = valid = sent = false; }
    static bool accept(const nlohmann::json &v, bool &out) {
        if (!v.is_number_integer()) return false;
        if (v.is_number_unsigned()) {
            auto n = v.get<unsigned long long>();
            if (n > 1) return false;
            out = n != 0;
        } else {
            auto n = v.get<long long>();
            if (n < 0 || n > 1) return false;
            out = n != 0;
        }
        return true;
    }
    void retrieved(const nlohmann::json &v) {
        barrier = true;
        bool b = false;
        if (v.is_null()) { valid = true; return; }
        if (!accept(v, b)) { valid = false; return; }
        valid = true;
        if (b) { seen = true; pending = false; }
    }
    void reply(const nlohmann::json &v) {
        bool b = false;
        if (!accept(v, b)) { valid = false; return; }
        valid = true; sent = false;
        if (b) { seen = true; pending = false; }
    }
    bool known() const { return barrier && valid; }
    // Returns true when this call is the one that recorded the play.
    bool record() {
        if (identity.empty() || seen) return false;
        seen = pending = true;
        return true;
    }
    bool wantsSend() const { return pending && !sent && known(); }
};
#endif
#endif
