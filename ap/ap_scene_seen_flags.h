#ifndef AP_SCENE_SEEN_FLAGS_H
#define AP_SCENE_SEEN_FLAGS_H

// Issue #377: a small per-slot bitmask of "this scene has played" flags kept in
// the room's server data storage. Shared by the Oxide Final Challenge scene
// flag (ap_oxide_scene_seen.h, key ctr_oxide_final_scene_v1) and the boss-door
// scene flag (ap_boss_door_scene_seen.h, key ctr_boss_door_scene_v1). Each key
// is its own value, so a client that only knows one key never sees the other's
// bits.
//
// The key names the seed, team and slot, and server data storage is kept per
// room, so a fresh room of the same seed starts clean and two slots in one
// room never share a value.
//
// FAIL SAFE. Until the Get reply for the key has arrived (`known()`), callers
// must not auto-play a scene: an unknown flag could be a "seen" that has not
// been read yet. A value outside 0..all is some other tool's key and leaves the
// flag unknown for the whole session.
//
// Written with the data storage "or" operation and a default of 0, so
// concurrent writers can only ever set bits.
#ifdef __cplusplus
#include <string>
#include <nlohmann/json.hpp>

struct APSceneSeenFlags {
    const char *prefix;    // data storage key prefix, e.g. "ctr_oxide_final_scene_v1"
    unsigned all;          // every bit this key may hold
    std::string identity, key;
    bool barrier = false;  // the Get reply for this connection has arrived
    bool valid = false;    // the stored value was readable
    unsigned bits = 0;     // set, from the server or recorded locally
    unsigned pending = 0;  // recorded locally, server has not confirmed yet
    bool sent = false;     // a Set is in flight for this connection

    APSceneSeenFlags(const char *keyPrefix, unsigned allBits)
        : prefix(keyPrefix), all(allBits) {}

    static std::string part(const std::string &s) {
        return std::to_string(s.size()) + ":" + s;
    }
    void connect(const std::string &endpoint, const std::string &seed, int team, int slot) {
        std::string k = std::string(prefix) + ":" + part(seed) + ":" +
                        std::to_string(team) + ":" + std::to_string(slot);
        std::string id = part(endpoint) + part(k);
        if (id != identity) { bits = pending = 0; }
        identity = id; key = k; barrier = valid = sent = false;
    }
    void disconnected() { barrier = valid = sent = false; }
    bool accept(const nlohmann::json &v, unsigned &out) const {
        if (!v.is_number_integer()) return false;
        if (v.is_number_unsigned()) {
            auto n = v.get<unsigned long long>();
            if (n > all) return false;
            out = (unsigned)n;
        } else {
            auto n = v.get<long long>();
            if (n < 0 || n > (long long)all) return false;
            out = (unsigned)n;
        }
        return true;
    }
    void merge(unsigned b) { bits |= b; pending &= ~b; }
    void retrieved(const nlohmann::json &v) {
        barrier = true;
        unsigned b = 0;
        if (v.is_null()) { valid = true; return; }
        if (!accept(v, b)) { valid = false; return; }
        valid = true;
        merge(b);
    }
    void reply(const nlohmann::json &v) {
        unsigned b = 0;
        if (!accept(v, b)) { valid = false; return; }
        valid = true; sent = false;
        merge(b);
    }
    bool known() const { return barrier && valid; }
    bool has(unsigned bit) const { return (bits & bit) != 0; }
    // Returns true when this call is the one that recorded the bit.
    bool record(unsigned bit) {
        if (identity.empty() || bit == 0 || (bit & ~all) != 0 || has(bit)) return false;
        bits |= bit; pending |= bit;
        return true;
    }
    bool wantsSend() const { return pending != 0 && !sent && known(); }
};
#endif
#endif
