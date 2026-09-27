#ifndef AP_OXIDE_SCENE_SEEN_H
#define AP_OXIDE_SCENE_SEEN_H

// Issue #377: "the Oxide Final Challenge scene has played for this slot", and
// "the Oxide Final Challenge is open message has been shown for this slot".
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
// has not been read yet, and must not show the open message either. A value
// outside 0..3 is some other tool's key and leaves the flag unknown for the
// whole session, which also means no auto-play and no message.
//
// Value: an integer bitmask, 0..3. Bit 0 (AP_OXIDE_FLAG_SCENE): the scene
// has played, or was skipped by the local Skip Cutscenes option. Bit 1
// (AP_OXIDE_FLAG_OPEN_MSG): the "Oxide Final Challenge is open" message has
// been shown. Written with the data storage "or" operation and a default of 0,
// so concurrent writers can only ever set bits.
#define AP_OXIDE_FLAG_SCENE    1u
#define AP_OXIDE_FLAG_OPEN_MSG 2u
#define AP_OXIDE_FLAG_ALL      (AP_OXIDE_FLAG_SCENE | AP_OXIDE_FLAG_OPEN_MSG)
#ifdef __cplusplus
#include <string>
#include <nlohmann/json.hpp>

struct APOxideSceneSeen {
    std::string identity, key;
    bool barrier = false;  // the Get reply for this connection has arrived
    bool valid = false;    // the stored value was readable
    unsigned bits = 0;     // set, from the server or recorded locally
    unsigned pending = 0;  // recorded locally, server has not confirmed yet
    bool sent = false;     // a Set is in flight for this connection

    static std::string part(const std::string &s) {
        return std::to_string(s.size()) + ":" + s;
    }
    void connect(const std::string &endpoint, const std::string &seed, int team, int slot) {
        std::string k = "ctr_oxide_final_scene_v1:" + part(seed) + ":" +
                        std::to_string(team) + ":" + std::to_string(slot);
        std::string id = part(endpoint) + part(k);
        if (id != identity) { bits = pending = 0; }
        identity = id; key = k; barrier = valid = sent = false;
    }
    void disconnected() { barrier = valid = sent = false; }
    static bool accept(const nlohmann::json &v, unsigned &out) {
        if (!v.is_number_integer()) return false;
        if (v.is_number_unsigned()) {
            auto n = v.get<unsigned long long>();
            if (n > AP_OXIDE_FLAG_ALL) return false;
            out = (unsigned)n;
        } else {
            auto n = v.get<long long>();
            if (n < 0 || n > (long long)AP_OXIDE_FLAG_ALL) return false;
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
    bool seen() const { return has(AP_OXIDE_FLAG_SCENE); }
    // Returns true when this call is the one that recorded the bit.
    bool record(unsigned bit) {
        if (identity.empty() || has(bit)) return false;
        bits |= bit; pending |= bit;
        return true;
    }
    bool record() { return record(AP_OXIDE_FLAG_SCENE); }
    bool wantsSend() const { return pending != 0 && !sent && known(); }
};
#endif
#endif
