// g++ -m32 -std=c++17 -Wall -Wextra -I include tools/test-lev-normalize.cpp -o /tmp/test-lev-normalize
// Synthetic data only. Optional input/output arguments normalize a local file.
#include <platform/native_lev_normalize.hpp>
#include <cstdio>
#include <fstream>
#include <iterator>
using Bytes = std::vector<unsigned char>;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static void put(Bytes &b, size_t at, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>(v >> (i * 8));
}
static Bytes make(const std::vector<uint32_t> &deltas, uint32_t map = 0x200)
{
    Bytes b(map + 8, 0);
    put(b, 0, map);
    put(b, map + 4, 0x80000000u | uint32_t(deltas.size() * 4));
    for (auto d : deltas)
    {
        if (d >= 0xffff) { b.push_back(0xff); b.push_back(0xff); NativeLev::append(b, d); }
        else { b.push_back(static_cast<unsigned char>(d)); b.push_back(static_cast<unsigned char>(d >> 8)); }
    }
    return b;
}
static void bad(Bytes b, const char *reason, size_t cap = 16u * 1024u * 1024u)
{
    const auto before = b;
    std::string why;
    CHECK(NativeLev::normalize(b, why, cap) == NativeLev::Result::Refused);
    CHECK(why == reason);
    CHECK(b == before);
}
static void good(Bytes b, const std::vector<uint32_t> &slots)
{
    const uint32_t map = NativeLev::word(b.data());
    Bytes reference(b.begin(), b.begin() + map + 4);
    NativeLev::append(reference, uint32_t(slots.size() * 4));
    for (auto s : slots) NativeLev::append(reference, s);
    std::string why;
    CHECK(NativeLev::normalize(b, why) == NativeLev::Result::Normalized);
    CHECK(why.empty());
    CHECK(b == reference);
    CHECK(NativeLev::normalize(b, why) == NativeLev::Result::Ordinary);
    CHECK(b == reference);
}
int main(int argc, char **argv)
{
    if (argc == 3)
    {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) return 2;
        Bytes b((std::istreambuf_iterator<char>(in)), {});
        std::string why;
        if (NativeLev::normalize(b, why) != NativeLev::Result::Normalized) { std::puts(why.c_str()); return 1; }
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char *>(b.data()), b.size());
        return out ? 0 : 2;
    }
    good(make({}), {});
    good(make({0}), {0});
    good(make({4, 8}), {4, 12});
    good(make({65536}, 0x10100), {65536});
    bad(make({4, 0}), "duplicate pointer slot");
    bad(make({3}), "unaligned pointer slot");
    bad(make({0x200}), "pointer slot overlaps map or exceeds payload");
    auto b = make({4}); put(b, 8, 0x200); bad(b, "pointer target outside payload");
    put(b, 8, 0x80000100); bad(b, "absolute pointer target");
    b = make({4}); b.pop_back(); bad(b, "truncated delta stream");
    b = make({65536}, 0x10100); b.pop_back(); bad(b, "truncated escaped delta");
    b = make({4}); b.push_back(0); bad(b, "trailing bytes or count mismatch");
    b = make({4}); put(b, 0x204, 0x40000004); bad(b, "unknown map encoding");
    b = make({4}); put(b, 0, 0x201); bad(b, "invalid map offset");
    /* Additional allocation, arithmetic, boundary and target-tag cases. */
    b = make({4}); put(b, 0x204, 0x80000204); bad(b, "count exceeds payload capacity");
    b = make({4}); put(b, 0x204, 0x80000005); bad(b, "unaligned map byte count");
    bad(Bytes(7), "truncated map header");
    b = make({4}); b.resize(0x205); bad(b, "truncated map header");
    bad(make({4}), "input exceeds LEV limit", 0x200);
    bad(make({0, 4}), "output exceeds LEV limit", 0x20c);
    bad(make({4, UINT32_MAX}), "cumulative slot overflow");
    b = make({4}); put(b, 8, 0x1ff); good(b, {4});
    good(make({0x1fc}), {0x1fc});
    b = make({4}); put(b, 0x204, 4); const auto before = b;
    std::string why;
    CHECK(NativeLev::normalize(b, why) == NativeLev::Result::Ordinary);
    CHECK(b == before);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
