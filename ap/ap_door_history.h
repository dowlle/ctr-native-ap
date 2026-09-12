#ifndef AP_DOOR_HISTORY_H
#define AP_DOOR_HISTORY_H

/* Engine hub IDs; only wood hub doors, never garages or race pads. */
static inline unsigned AP_DoorBitPure(int level, int door)
{
    if (level == 26 && door == 5) return 1;
    if ((level == 26 && door == 4) || level == 27) return 2;
    if (level == 28) return 4;
    if (level == 25) return 8;
    return 0;
}
static inline int AP_DoorKeysPure(unsigned bit)
{
    return bit == 1 ? 1 : bit == 2 || bit == 8 ? 2 : bit == 4 ? 3 : 99;
}
static inline unsigned AP_DoorStoryPure(unsigned bits)
{
    return ((bits & 1) ? 0x10u : 0) | ((bits & 2) ? 0xc0u : 0) |
           ((bits & 4) ? 0x100u : 0) | ((bits & 8) ? 0x20u : 0);
}
#ifdef __cplusplus
#include <string>
#include <nlohmann/json.hpp>
struct APDoorHistory {
    std::string identity, key;
    unsigned history = 0, pending = 0, session = 0;
    bool barrier = false, valid = false;
    static std::string part(const std::string &s) {
        return std::to_string(s.size()) + ":" + s;
    }
    void connect(const std::string &endpoint, const std::string &seed, int team, int slot) {
        std::string k = "ctr_doors_v1:" + part(seed) + ":" + std::to_string(team) + ":" + std::to_string(slot);
        std::string id = part(endpoint) + part(k);
        if (id != identity) { history = pending = 0; }
        identity = id; key = k; barrier = valid = false; session = 0;
    }
    void disconnected() { barrier = valid = false; }
    static bool accept(const nlohmann::json &v, unsigned &bits) {
        if (!v.is_number_integer()) return false;
        if (v.is_number_unsigned()) {
            auto n = v.get<unsigned long long>();
            if (n > 15) return false;
            bits = (unsigned)n;
        } else {
            auto n = v.get<long long>();
            if (n < 0 || n > 15) return false;
            bits = (unsigned)n;
        }
        return true;
    }
    void retrieved(const nlohmann::json &v) {
        barrier = true;
        unsigned bits = 0;
        if (v.is_null()) { valid = true; return; }
        if (!accept(v, bits)) { valid = false; return; }
        valid = true; history |= bits; pending &= ~bits;
    }
    void reply(const nlohmann::json &v) {
        unsigned bits;
        if (!accept(v, bits)) { valid = false; return; }
        valid = true; history |= bits; pending &= ~bits;
    }
    void record(unsigned bit) { session |= bit & 15u; pending |= bit & 15u; }
};
#endif
#endif
