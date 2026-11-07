#ifndef NATIVE_LEV_NORMALIZE_HPP
#define NATIVE_LEV_NORMALIZE_HPP

#include <cstdint>
#include <string>
#include <vector>

/* Import-time data conversion only. No relocation or runtime format changes.
   On refusal the caller's bytes stay untouched. Ordinary maps are a no-op;
   their validation remains the cutter/verifier's responsibility. */
namespace NativeLev {
inline uint32_t word(const unsigned char *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void append(std::vector<unsigned char> &b, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i)));
}
enum class Result { Ordinary, Normalized, Refused };
inline Result normalize(std::vector<unsigned char> &b, std::string &why,
                        size_t cap = 16u * 1024u * 1024u)
{
    why.clear();
    auto refuse = [&](const char *s) { why = s; return Result::Refused; };
    if (b.size() > cap) return refuse("input exceeds LEV limit");
    if (b.size() < 8) return refuse("truncated map header");
    const uint32_t map = word(b.data());
    if ((map & 3) || map < 0x190) return refuse("invalid map offset");
    if (map > b.size() - 8) return refuse("truncated map header");
    const uint32_t header = word(b.data() + 4 + map);
    if (!(header & 0xe0000000u)) return Result::Ordinary;
    if (!(header & 0x80000000u)) return refuse("unknown map encoding");
    if (header & 3) return refuse("unaligned map byte count");
    const uint32_t count = (header & 0x7fffffffu) / 4;
    if (count > map / 4) return refuse("count exceeds payload capacity");
    const size_t start = size_t(map) + 8;
    if (start > cap || count > (cap - start) / 4) return refuse("output exceeds LEV limit");
    if (count > (b.size() - start) / 2) return refuse("truncated delta stream");
    std::vector<unsigned char> out(b.begin(), b.begin() + map + 4);
    out.reserve(start + size_t(count) * 4);
    append(out, count * 4);
    size_t at = start;
    uint32_t slot = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (b.size() - at < 2) return refuse("truncated delta stream");
        uint32_t delta = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8;
        at += 2;
        if (delta == 0xffff)
        {
            if (b.size() - at < 4) return refuse("truncated escaped delta");
            delta = word(b.data() + at);
            at += 4;
        }
        if (i && !delta) return refuse("duplicate pointer slot");
        if (delta > UINT32_MAX - slot) return refuse("cumulative slot overflow");
        slot += delta;
        if (slot & 3) return refuse("unaligned pointer slot");
        if (slot > map - 4) return refuse("pointer slot overlaps map or exceeds payload");
        const uint32_t target = word(b.data() + 4 + slot);
        if (target & 0xf0000000u) return refuse("absolute pointer target");
        if (target >= map) return refuse("pointer target outside payload");
        /* Targets may address bytes or carry low-bit tags. Preserve them. */
        append(out, slot);
    }
    if (at != b.size()) return refuse("trailing bytes or count mismatch");
    b.swap(out);
    return Result::Normalized;
}
} // namespace NativeLev
#endif
