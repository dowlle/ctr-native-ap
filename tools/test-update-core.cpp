// g++ -m32 -std=c++17 -Wall -Wextra -pthread -I ap -I ap/vendor/json/include -o /tmp/test-update-core tools/test-update-core.cpp ap/ap_update_core.cpp -lcrypto -lz
// /tmp/test-update-core
//
// Host assertions for the signed in-game updater trust core (issue #301,
// ap/ap_update_core.cpp). Exit 0 = every assertion held.
//
// Temporary signed releases are built here: a throwaway Ed25519 key signs
// manifests in minisign format (both algorithms), and ZIP / tar.gz client
// archives are written in memory. No network, no files outside a temp dir.
//
// Covers:
//   * interop: the published v0.2.4 manifest and its minisign signature verify
//     against the pinned release key; one flipped byte anywhere fails
//   * signature failure: wrong key, foreign key id, flipped manifest, flipped
//     trusted comment, malformed signature files
//   * hash mismatch and partial download: a flipped or truncated archive
//     fails after a good signature; an HTTP body shorter than its
//     Content-Length or an unfinished chunked body is never complete
//   * downgrade and replay: equal, older and prerelease versions, and a signed
//     manifest for a different tag than the one offered, are refused; a
//     declined version is not offered again but a newer one is
//   * offer visibility: drafts, prereleases, a missing .minisig and a missing
//     platform archive produce no offer
//   * manifest strictness: duplicate keys, extra keys and entries, uppercase
//     hashes, prerelease versions
//   * archive validation: traversal, absolute, backslash and foreign-root
//     paths, links, duplicate executables, pax size overrides, CRC damage;
//     a valid ZIP and tar.gz yield exactly the executable bytes
//   * install set: the executable, ctr.apworld and the files directly in
//     assets/tracks/cortex-vortex/; never config.ini, ap-config.txt, other
//     assets, memory cards or docs
//   * multi-file rollback: a failure at any swap step (moving a file aside or
//     moving its new copy in) restores every file; an undo that itself fails
//     leaves that file's .old copy; short or corrupted staging writes, a
//     changed or missing staged file and a hash/bytes mismatch move nothing;
//     a linked assets folder is refused; journal and marker cleanup never
//     reaches files outside the install list; .old copies stay until the new
//     build confirms its start
//   * the redirect and host allowlist

#include "ap_update_core.h"

#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

using namespace ap_update;

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                                  \
	do                                                                               \
	{                                                                                \
		g_checks++;                                                                  \
		if (!(cond))                                                                 \
		{                                                                            \
			g_failures++;                                                            \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
		}                                                                            \
	} while (0)

#define CHECK_EQ(a, b)                                                               \
	do                                                                               \
	{                                                                                \
		g_checks++;                                                                  \
		const long long va_ = (long long)(a), vb_ = (long long)(b);                  \
		if (va_ != vb_)                                                              \
		{                                                                            \
			g_failures++;                                                            \
			std::printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, \
			            #a, #b, va_, vb_);                                           \
		}                                                                            \
	} while (0)

static Bytes B(const std::string &s) { return Bytes(s.begin(), s.end()); }

// ── the published v0.2.4 release (public files) ──

static const char kManifest024[] =
    "{\n"
    "  \"artifacts\": {\n"
    "    \"ctr-archipelago-v0.2.4-linux-x86.tar.gz\": \"79d0d430946a27bbf939aceb875f1f20037aef093fb283599ea4d015fa4f5b8e\",\n"
    "    \"ctr-archipelago-v0.2.4-windows-x86.zip\": \"2a8fe4a9594e3ba77230716ff4f722bf44f62c6269a3d28f5818f91a9c54d9cf\"\n"
    "  },\n"
    "  \"version\": \"v0.2.4\"\n"
    "}\n";

static const char kSig024[] =
    "untrusted comment: signature from minisign secret key\n"
    "RUQz1vK+2CnxRmSt7dseFctuRfGzfn77YwgryB4sMHRPAexgsaJ9K2xh5efJYvECSFYEJ9PpJ5ktOUOKORQxf6KctNFPuBBimgc=\n"
    "trusted comment: timestamp:1791883611\tfile:manifest.json\thashed\n"
    "kDqPF9H8nvT55W6hE6ZebBsjFswuBWkBbtChovAVF0Gjw/lh6YRJuZ6NF1Pll4T6t8UqYDVS54aLr1s4eIFICw==\n";

// ── test signing key ──

static std::string B64(const Bytes &in)
{
	static const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	size_t i = 0;
	for (; i + 2 < in.size(); i += 3)
	{
		const unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
		out += a[v >> 18];
		out += a[(v >> 12) & 63];
		out += a[(v >> 6) & 63];
		out += a[v & 63];
	}
	if (i + 1 == in.size())
	{
		const unsigned v = in[i] << 16;
		out += a[v >> 18];
		out += a[(v >> 12) & 63];
		out += "==";
	}
	else if (i + 2 == in.size())
	{
		const unsigned v = (in[i] << 16) | (in[i + 1] << 8);
		out += a[v >> 18];
		out += a[(v >> 12) & 63];
		out += a[(v >> 6) & 63];
		out += '=';
	}
	return out;
}

struct TestKey
{
	EVP_PKEY *pkey;
	unsigned char keyId[8];
	std::string pubB64;
};

static TestKey MakeKey(unsigned char idByte)
{
	TestKey k;
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
	k.pkey = nullptr;
	EVP_PKEY_keygen_init(ctx);
	EVP_PKEY_keygen(ctx, &k.pkey);
	EVP_PKEY_CTX_free(ctx);
	for (int i = 0; i < 8; i++)
		k.keyId[i] = (unsigned char)(idByte + i);
	unsigned char pk[32];
	size_t len = 32;
	EVP_PKEY_get_raw_public_key(k.pkey, pk, &len);
	Bytes raw = {'E', 'd'};
	raw.insert(raw.end(), k.keyId, k.keyId + 8);
	raw.insert(raw.end(), pk, pk + 32);
	k.pubB64 = B64(raw);
	return k;
}

static Bytes Ed25519Sign(EVP_PKEY *key, const Bytes &msg)
{
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	Bytes sig(64);
	size_t len = 64;
	EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key);
	EVP_DigestSign(ctx, sig.data(), &len, msg.data(), msg.size());
	EVP_MD_CTX_free(ctx);
	return sig;
}

static std::string Sign(const TestKey &k, const Bytes &msg, bool prehashed = true,
                        const std::string &comment = "timestamp:1\tfile:manifest.json\thashed")
{
	Bytes input = msg;
	if (prehashed)
	{
		unsigned char d[64];
		unsigned int len = 0;
		EVP_MD_CTX *ctx = EVP_MD_CTX_new();
		EVP_DigestInit_ex(ctx, EVP_blake2b512(), nullptr);
		EVP_DigestUpdate(ctx, msg.data(), msg.size());
		EVP_DigestFinal_ex(ctx, d, &len);
		EVP_MD_CTX_free(ctx);
		input.assign(d, d + 64);
	}
	const Bytes sig = Ed25519Sign(k.pkey, input);
	Bytes block = {'E', prehashed ? (unsigned char)'D' : (unsigned char)'d'};
	block.insert(block.end(), k.keyId, k.keyId + 8);
	block.insert(block.end(), sig.begin(), sig.end());
	Bytes globalMsg = sig;
	globalMsg.insert(globalMsg.end(), comment.begin(), comment.end());
	const Bytes global = Ed25519Sign(k.pkey, globalMsg);
	return "untrusted comment: test\n" + B64(block) + "\ntrusted comment: " + comment + "\n" + B64(global) + "\n";
}

static std::string ManifestText(const std::string &tag, const std::string &linSha, const std::string &winSha)
{
	return "{\n  \"artifacts\": {\n    \"ctr-archipelago-" + tag + "-linux-x86.tar.gz\": \"" + linSha +
	       "\",\n    \"ctr-archipelago-" + tag + "-windows-x86.zip\": \"" + winSha + "\"\n  },\n  \"version\": \"" +
	       tag + "\"\n}\n";
}

// ── archive writers ──

struct Member
{
	std::string name;
	Bytes data;
	char type;         // '0' file, '5' dir, '2' symlink (tar); zip uses unixMode
	unsigned unixMode; // zip external attribute high word, 0 = none
	std::string pax;   // raw pax records for a preceding 'x' header
};

static Member File(const std::string &name, const std::string &data)
{
	return Member{name, B(data), '0', 0100644, ""};
}

static void Put16(Bytes &b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void Put32(Bytes &b, unsigned v) { Put16(b, v & 0xFFFF); Put16(b, v >> 16); }

static Bytes RawDeflate(const Bytes &in, int windowBits)
{
	z_stream zs;
	std::memset(&zs, 0, sizeof zs);
	deflateInit2(&zs, 6, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY);
	Bytes out(deflateBound(&zs, in.size()) + 64);
	zs.next_in = const_cast<unsigned char *>(in.data());
	zs.avail_in = (uInt)in.size();
	zs.next_out = out.data();
	zs.avail_out = (uInt)out.size();
	deflate(&zs, Z_FINISH);
	out.resize(zs.total_out);
	deflateEnd(&zs);
	return out;
}

static Bytes Zip(const std::vector<Member> &members, bool deflateIt = true)
{
	Bytes out, cd;
	for (const Member &m : members)
	{
		const bool dir = !m.name.empty() && m.name.back() == '/';
		const unsigned method = (deflateIt && !dir) ? 8 : 0;
		const Bytes payload = method == 8 ? RawDeflate(m.data, -MAX_WBITS) : m.data;
		const unsigned crc = (unsigned)crc32(0L, m.data.data(), (uInt)m.data.size());
		const unsigned off = (unsigned)out.size();
		Put32(out, 0x04034b50); Put16(out, 20); Put16(out, 0); Put16(out, method);
		Put16(out, 0); Put16(out, 0); Put32(out, crc); Put32(out, (unsigned)payload.size());
		Put32(out, (unsigned)m.data.size()); Put16(out, (unsigned)m.name.size()); Put16(out, 0);
		out.insert(out.end(), m.name.begin(), m.name.end());
		out.insert(out.end(), payload.begin(), payload.end());
		Put32(cd, 0x02014b50); Put16(cd, m.unixMode ? (3 << 8) | 20 : 20); Put16(cd, 20); Put16(cd, 0);
		Put16(cd, method); Put16(cd, 0); Put16(cd, 0); Put32(cd, crc); Put32(cd, (unsigned)payload.size());
		Put32(cd, (unsigned)m.data.size()); Put16(cd, (unsigned)m.name.size()); Put16(cd, 0); Put16(cd, 0);
		Put16(cd, 0); Put16(cd, 0); Put32(cd, m.unixMode << 16); Put32(cd, off);
		cd.insert(cd.end(), m.name.begin(), m.name.end());
	}
	const unsigned cdOff = (unsigned)out.size();
	out.insert(out.end(), cd.begin(), cd.end());
	Put32(out, 0x06054b50); Put16(out, 0); Put16(out, 0); Put16(out, (unsigned)members.size());
	Put16(out, (unsigned)members.size()); Put32(out, (unsigned)cd.size()); Put32(out, cdOff); Put16(out, 0);
	return out;
}

static void TarHeader(Bytes &out, const std::string &name, size_t size, char type)
{
	unsigned char h[512];
	std::memset(h, 0, sizeof h);
	std::memcpy(h, name.data(), std::min<size_t>(name.size(), 100));
	std::snprintf((char *)h + 100, 8, "%07o", 0644);
	std::snprintf((char *)h + 108, 8, "%07o", 0);
	std::snprintf((char *)h + 116, 8, "%07o", 0);
	std::snprintf((char *)h + 124, 12, "%011o", (unsigned)size);
	std::snprintf((char *)h + 136, 12, "%011o", 0);
	h[156] = (unsigned char)type;
	std::memcpy(h + 257, "ustar\0" "00", 8);
	std::memset(h + 148, ' ', 8);
	unsigned sum = 0;
	for (int i = 0; i < 512; i++)
		sum += h[i];
	std::snprintf((char *)h + 148, 8, "%06o", sum);
	h[155] = ' ';
	out.insert(out.end(), h, h + 512);
}

static void TarData(Bytes &out, const Bytes &d)
{
	out.insert(out.end(), d.begin(), d.end());
	out.resize((out.size() + 511) / 512 * 512, 0);
}

static Bytes TarGz(const std::vector<Member> &members)
{
	Bytes tar;
	for (const Member &m : members)
	{
		if (!m.pax.empty())
		{
			TarHeader(tar, "././@PaxHeader", m.pax.size(), 'x');
			TarData(tar, B(m.pax));
		}
		TarHeader(tar, m.name, m.type == '0' ? m.data.size() : 0, m.type);
		if (m.type == '0')
			TarData(tar, m.data);
	}
	tar.resize(tar.size() + 1024, 0);
	return RawDeflate(tar, 16 + MAX_WBITS);
}

static std::string PaxRecord(const std::string &key, const std::string &value)
{
	const std::string body = " " + key + "=" + value + "\n";
	size_t len = body.size() + 1;
	while (std::to_string(len).size() + body.size() != len)
		len++;
	return std::to_string(len) + body;
}

static const std::string kTag = "v0.3.1";
static const std::string kRoot = "ctr-archipelago-v0.3.1/";
static const std::string kExeBytes = "NEW-CLIENT-BINARY-\x01\x02\x03-v0.3.1";

static std::vector<Member> GoodMembers(Platform p)
{
	std::vector<Member> m;
	m.push_back(File(kRoot + "LICENSE", "license text"));
	m.push_back(File(kRoot + "ctr.apworld", "world"));
	m.push_back(File(kRoot + "assets/tracks/cortex-vortex/CVortex Arcade All.lev", "lev"));
	m.push_back(File(kRoot + ExecutableName(p), kExeBytes));
	m.push_back(File(kRoot + "versions.txt", "v0.3.1"));
	return m;
}

static Bytes Archive(Platform p, const std::vector<Member> &m)
{
	return p == PLATFORM_WINDOWS ? Zip(m) : TarGz(m);
}

// ── tests ──

static void TestInterop()
{
	PublicKey key;
	CHECK(ParsePublicKey(AP_UPDATE_PUBLIC_KEY, key));
	std::string comment;
	CHECK_EQ(VerifyMinisign(key, B(kManifest024), kSig024, &comment), OK);
	CHECK(comment == "timestamp:1791883611\tfile:manifest.json\thashed");
	// CRLF line endings in the .minisig are accepted.
	std::string crlf;
	for (char c : std::string(kSig024))
		crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
	CHECK_EQ(VerifyMinisign(key, B(kManifest024), crlf, nullptr), OK);
	// Every single flipped manifest byte breaks the signature.
	int broken = 0;
	const Bytes m = B(kManifest024);
	for (size_t i = 0; i < m.size(); i++)
	{
		Bytes f = m;
		f[i] ^= 0x01;
		broken += VerifyMinisign(key, f, kSig024, nullptr) == ERR_SIGNATURE;
	}
	CHECK_EQ(broken, (long long)m.size());
	// Appending a byte (a different file) fails too.
	Bytes longer = m;
	longer.push_back('\n');
	CHECK_EQ(VerifyMinisign(key, longer, kSig024, nullptr), ERR_SIGNATURE);
	// The trusted comment is covered by the global signature.
	std::string sig = kSig024;
	sig.replace(sig.find("1791883611"), 10, "1791883612");
	CHECK_EQ(VerifyMinisign(key, m, sig, nullptr), ERR_COMMENT_SIGNATURE);
	// The parsed manifest is the one the release published.
	Manifest parsed;
	CHECK_EQ(ParseManifest(m, parsed), OK);
	CHECK(parsed.version == "v0.2.4");
	CHECK(parsed.linuxSha256 == "79d0d430946a27bbf939aceb875f1f20037aef093fb283599ea4d015fa4f5b8e");
	// A full release check of v0.2.4 for a v0.2.3 build reaches the archive
	// hash (the archive is not embedded, so any bytes fail there).
	CHECK_EQ(VerifyRelease(AP_UPDATE_PUBLIC_KEY, m, kSig024, "v0.2.4", "v0.2.3", PLATFORM_WINDOWS, B("x")), ERR_HASH);
	// The same signed release is a downgrade or no-op for v0.2.4 and later.
	CHECK_EQ(VerifyRelease(AP_UPDATE_PUBLIC_KEY, m, kSig024, "v0.2.4", "v0.2.4", PLATFORM_WINDOWS, B("x")),
	         ERR_NOT_NEWER);
	CHECK_EQ(VerifyRelease(AP_UPDATE_PUBLIC_KEY, m, kSig024, "v0.2.4", "v0.3.0", PLATFORM_LINUX, B("x")),
	         ERR_NOT_NEWER);
}

static void TestSignatureFailures()
{
	const TestKey good = MakeKey(0x10);
	const TestKey other = MakeKey(0x40);
	PublicKey pk;
	CHECK(ParsePublicKey(good.pubB64, pk));
	const Bytes msg = B("hello manifest");

	CHECK_EQ(VerifyMinisign(pk, msg, Sign(good, msg, true), nullptr), OK);
	CHECK_EQ(VerifyMinisign(pk, msg, Sign(good, msg, false), nullptr), OK); // legacy "Ed"
	CHECK_EQ(VerifyMinisign(pk, msg, Sign(other, msg), nullptr), ERR_SIG_KEY_ID);

	// Another key that claims our key id: the signature itself fails.
	TestKey impostor = MakeKey(0x60);
	std::memcpy(impostor.keyId, good.keyId, 8);
	CHECK_EQ(VerifyMinisign(pk, msg, Sign(impostor, msg), nullptr), ERR_SIGNATURE);

	// Malformed files.
	CHECK_EQ(VerifyMinisign(pk, msg, "", nullptr), ERR_SIG_FORMAT);
	CHECK_EQ(VerifyMinisign(pk, msg, "not a minisign signature\n", nullptr), ERR_SIG_FORMAT);
	std::string sig = Sign(good, msg);
	CHECK_EQ(VerifyMinisign(pk, msg, sig.substr(0, sig.size() / 2), nullptr), ERR_SIG_FORMAT); // truncated
	std::string bad = sig;
	bad.replace(bad.find("trusted comment: "), 17, "trusted-comment: ");
	CHECK_EQ(VerifyMinisign(pk, msg, bad, nullptr), ERR_SIG_FORMAT);
	CHECK_EQ(VerifyMinisign(pk, msg, sig + "extra line\n", nullptr), ERR_SIG_FORMAT);
	CHECK_EQ(VerifyMinisign(pk, msg, std::string(kMaxSignature + 1, 'a'), nullptr), ERR_SIG_FORMAT);
	// Unknown algorithm bytes.
	std::vector<std::string> lines;
	{
		size_t s = 0, e;
		while ((e = sig.find('\n', s)) != std::string::npos)
		{
			lines.push_back(sig.substr(s, e - s));
			s = e + 1;
		}
	}
	Bytes block;
	CHECK(Base64Decode(lines[1], block));
	block[1] = 'X';
	CHECK_EQ(VerifyMinisign(pk, msg, lines[0] + "\n" + B64(block) + "\n" + lines[2] + "\n" + lines[3] + "\n", nullptr),
	         ERR_SIG_FORMAT);

	// The pinned key does not verify a release signed by any other key.
	const Bytes manifest = B(ManifestText(kTag, std::string(64, 'a'), std::string(64, 'b')));
	CHECK_EQ(VerifyRelease(AP_UPDATE_PUBLIC_KEY, manifest, Sign(good, manifest), kTag, "v0.3.0", PLATFORM_LINUX,
	                       B("x")),
	         ERR_SIG_KEY_ID);
	// A broken built-in key refuses everything.
	CHECK_EQ(VerifyRelease("RWQ", manifest, Sign(good, manifest), kTag, "v0.3.0", PLATFORM_LINUX, B("x")), ERR_KEY);

	EVP_PKEY_free(good.pkey);
	EVP_PKEY_free(other.pkey);
	EVP_PKEY_free(impostor.pkey);
}

static void TestBase64()
{
	Bytes out;
	CHECK(Base64Decode("aGk=", out) && out == B("hi"));
	CHECK(!Base64Decode("aGk", out));
	CHECK(!Base64Decode("aG=k", out));
	CHECK(!Base64Decode("aGl=", out)); // non-canonical trailing bits
	CHECK(!Base64Decode("a$k=", out));
	CHECK(!Base64Decode("", out));
}

struct Release
{
	TestKey key;
	Bytes manifest;
	std::string signature;
	Bytes zip, tgz;
};

static Release MakeRelease(const std::string &tag = kTag)
{
	Release r;
	r.key = MakeKey(0x20);
	(void)tag;
	r.zip = Zip(GoodMembers(PLATFORM_WINDOWS));
	r.tgz = TarGz(GoodMembers(PLATFORM_LINUX));
	r.manifest = B(ManifestText(kTag, Sha256Hex(r.tgz), Sha256Hex(r.zip)));
	r.signature = Sign(r.key, r.manifest);
	return r;
}

static void TestHashAndPartial()
{
	Release r = MakeRelease();
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.0", PLATFORM_WINDOWS, r.zip), OK);
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.0", PLATFORM_LINUX, r.tgz), OK);
	// The other platform's archive does not pass for this platform.
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.0", PLATFORM_WINDOWS, r.tgz), ERR_HASH);

	Bytes flipped = r.zip;
	flipped[flipped.size() / 2] ^= 0x80;
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.0", PLATFORM_WINDOWS, flipped), ERR_HASH);
	Bytes partial(r.tgz.begin(), r.tgz.begin() + r.tgz.size() - 1);
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.0", PLATFORM_LINUX, partial), ERR_HASH);
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.0", PLATFORM_LINUX, Bytes()), ERR_HASH);

	// A flipped manifest byte fails at the signature, before any field is read.
	Bytes m = r.manifest;
	m[m.size() - 4] ^= 0x01;
	CHECK_EQ(VerifyRelease(r.key.pubB64, m, r.signature, kTag, "v0.3.0", PLATFORM_WINDOWS, r.zip), ERR_SIGNATURE);

	// HTTP framing: partial bodies are never complete.
	HttpResponse resp;
	const std::string head = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n";
	CHECK_EQ(ParseHttpResponse(B(head + "0123456789"), 100, resp), HTTP_COMPLETE);
	CHECK(resp.body == B("0123456789") && resp.status == 200);
	CHECK_EQ(ParseHttpResponse(B(head + "012345678"), 100, resp), HTTP_INCOMPLETE);
	CHECK_EQ(ParseHttpResponse(B(head + "0123456789X"), 100, resp), HTTP_MALFORMED);
	CHECK_EQ(ParseHttpResponse(B(head + "0123456789"), 5, resp), HTTP_TOO_LARGE);
	CHECK_EQ(ParseHttpResponse(B("HTTP/1.1 200 OK\r\nContent-Le"), 100, resp), HTTP_INCOMPLETE);
	CHECK_EQ(ParseHttpResponse(B("HTTP/1.1 200 OK\r\n\r\nbody without length"), 100, resp), HTTP_MALFORMED);
	CHECK_EQ(ParseHttpResponse(B("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabc"), 100, resp),
	         HTTP_MALFORMED);
	const std::string ch = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n"), 100, resp), HTTP_COMPLETE);
	CHECK(resp.body == B("abcde"));
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\n2\r\nde\r\n"), 100, resp), HTTP_INCOMPLETE); // no final chunk
	CHECK_EQ(ParseHttpResponse(B(ch + "5\r\nabc"), 100, resp), HTTP_INCOMPLETE);
	CHECK_EQ(ParseHttpResponse(B(ch + "zz\r\nabc\r\n0\r\n\r\n"), 100, resp), HTTP_MALFORMED);
	CHECK_EQ(ParseHttpResponse(B("HTTP/1.1 302 Found\r\nLocation: https://x/y\r\nContent-Length: 0\r\n\r\n"), 100, resp),
	         HTTP_COMPLETE);
	CHECK(resp.status == 302 && resp.location == "https://x/y");
	CHECK_EQ(HeaderContentLength("HTTP/1.1 200 OK\r\ncontent-length: 1234"), 1234);
	CHECK_EQ(HeaderContentLength("HTTP/1.1 200 OK\r\nX: 1"), -1);
	EVP_PKEY_free(r.key.pkey);
}

static void TestDowngradeAndReplay()
{
	Release r = MakeRelease();
	// Equal and newer running builds refuse the signed v0.3.1.
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.3.1", PLATFORM_WINDOWS, r.zip),
	         ERR_NOT_NEWER);
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "v0.4.0", PLATFORM_WINDOWS, r.zip),
	         ERR_NOT_NEWER);
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, kTag, "garbage", PLATFORM_WINDOWS, r.zip),
	         ERR_NOT_NEWER);
	// Replay: a validly signed older release served for a newer offer.
	CHECK_EQ(VerifyRelease(r.key.pubB64, r.manifest, r.signature, "v0.3.2", "v0.3.0", PLATFORM_WINDOWS, r.zip),
	         ERR_TAG_MISMATCH);
	// A signed prerelease manifest is not installable.
	const Bytes pre = B(ManifestText("v0.3.2-rc1", Sha256Hex(r.tgz), Sha256Hex(r.zip)));
	CHECK_EQ(VerifyRelease(r.key.pubB64, pre, Sign(r.key, pre), "v0.3.2-rc1", "v0.3.0", PLATFORM_WINDOWS, r.zip),
	         ERR_MANIFEST);

	CHECK(ShouldOffer("v0.3.1", "v0.3.0", ""));
	CHECK(!ShouldOffer("v0.3.0", "v0.3.0", ""));
	CHECK(!ShouldOffer("v0.2.9", "v0.3.0", ""));
	CHECK(!ShouldOffer("v0.3.1-rc1", "v0.3.0", ""));
	CHECK(!ShouldOffer("v0.3.1", "v0.3.0", "v0.3.1"));  // declined
	CHECK(ShouldOffer("v0.3.2", "v0.3.0", "v0.3.1"));   // a newer one is offered again
	CHECK(!ShouldOffer("v0.3.1", "v0.3.0", "v0.3.2"));
	CHECK(ShouldOffer("v1.0.0", "v0.3.0", "garbage"));  // unreadable declined value: offer

	CHECK(IsStableTag("v0.3.1") && IsStableTag("v10.20.30"));
	CHECK(!IsStableTag("0.3.1") && !IsStableTag("v0.3") && !IsStableTag("v0.3.1.") && !IsStableTag("v0.3.1-rc1") &&
	      !IsStableTag("v0.3.1/../x") && !IsStableTag("v0..1") && !IsStableTag("v0.3.1.2"));
	EVP_PKEY_free(r.key.pkey);
}

static std::string ReleaseJson(const std::string &tag, bool draft, bool pre, const std::vector<std::string> &assets)
{
	std::string s = "{\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") +
	                ",\"prerelease\":" + (pre ? "true" : "false") + ",\"assets\":[";
	for (size_t i = 0; i < assets.size(); i++)
		s += (i ? "," : "") + std::string("{\"name\":\"") + assets[i] + "\",\"browser_download_url\":\"https://evil/\"}";
	return s + "]}";
}

static void TestOfferVisibility()
{
	const std::vector<std::string> all = {"manifest.json", "manifest.json.minisig",
	                                      ArchiveName(kTag, PLATFORM_WINDOWS), ArchiveName(kTag, PLATFORM_LINUX),
	                                      "ctr.apworld"};
	ReleaseInfo info;
	CHECK(ParseLatestRelease(B(ReleaseJson(kTag, false, false, all)), info) && info.tag == kTag);
	CHECK(!ParseLatestRelease(B(ReleaseJson(kTag, true, false, all)), info));
	CHECK(!ParseLatestRelease(B(ReleaseJson(kTag, false, true, all)), info));
	CHECK(!ParseLatestRelease(B(ReleaseJson("v0.3.1-rc1", false, false, all)), info));
	for (size_t drop = 0; drop < 4; drop++)
	{
		std::vector<std::string> less = all;
		less.erase(less.begin() + (long)drop);
		CHECK(!ParseLatestRelease(B(ReleaseJson(kTag, false, false, less)), info));
	}
	CHECK(!ParseLatestRelease(B("{\"tag_name\":\"v0.3.1\",\"tag_name\":\"v9.9.9\",\"draft\":false,"
	                            "\"prerelease\":false,\"assets\":[]}"),
	                          info));
	CHECK(!ParseLatestRelease(B("not json"), info));
	CHECK(!ParseLatestRelease(B("{\"draft\":false}"), info));
}

static void TestManifestStrictness()
{
	const std::string a(64, 'a'), b(64, 'b');
	Manifest m;
	CHECK_EQ(ParseManifest(B(ManifestText(kTag, a, b)), m), OK);
	CHECK(m.linuxSha256 == a && m.windowsSha256 == b);
	// Compact formatting is still the same JSON.
	CHECK_EQ(ParseManifest(B("{\"version\":\"v0.3.1\",\"artifacts\":{\"ctr-archipelago-v0.3.1-linux-x86.tar.gz\":\"" + a +
	                         "\",\"ctr-archipelago-v0.3.1-windows-x86.zip\":\"" + b + "\"}}"),
	                       m),
	         OK);
	CHECK_EQ(ParseManifest(B("{\"version\":\"v0.3.1\",\"version\":\"v0.3.1\",\"artifacts\":{}}"), m), ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B("{\"version\":\"v0.3.1\",\"extra\":1,\"artifacts\":{\"ctr-archipelago-v0.3.1-linux-x86.tar.gz\":\"" +
	                         a + "\",\"ctr-archipelago-v0.3.1-windows-x86.zip\":\"" + b + "\"}}"),
	                       m),
	         ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B("{\"version\":\"v0.3.1\",\"artifacts\":{\"ctr-archipelago-v0.3.1-linux-x86.tar.gz\":\"" + a +
	                         "\",\"ctr-archipelago-v0.3.1-windows-x86.zip\":\"" + b + "\",\"x.zip\":\"" + a + "\"}}"),
	                       m),
	         ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B("{\"version\":\"v0.3.1\",\"artifacts\":{\"ctr-archipelago-v0.3.1-linux-x86.tar.gz\":\"" + a +
	                         "\",\"ctr-archipelago-v0.3.1-linux-x86.tar.gz\":\"" + b + "\"}}"),
	                       m),
	         ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B(ManifestText(kTag, std::string(64, 'A'), b)), m), ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B(ManifestText(kTag, a.substr(1), b)), m), ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B(ManifestText("v0.3.1-rc1", a, b)), m), ERR_MANIFEST);
	CHECK_EQ(ParseManifest(B("[]"), m), ERR_MANIFEST);
	CHECK_EQ(ParseManifest(Bytes(), m), ERR_MANIFEST);
}

static void TestArchives()
{
	for (int pi = 0; pi < 2; pi++)
	{
		const Platform p = (Platform)pi;
		const std::string exe = kRoot + ExecutableName(p);
		Bytes out;
		CHECK_EQ(ExtractExecutable(Archive(p, GoodMembers(p)), p, kTag, out), OK);
		CHECK(out == B(kExeBytes));
		// Wrong tag: the root folder does not match.
		CHECK_EQ(ExtractExecutable(Archive(p, GoodMembers(p)), p, "v0.3.2", out), ERR_ARCHIVE);
		CHECK(out.empty());

		const char *badNames[] = {"../evil", "ctr-archipelago-v0.3.1/../evil", "/etc/passwd",
		                          "ctr-archipelago-v0.3.1/a\\..\\evil", "ctr-archipelago-v0.3.1/C:/evil",
		                          "ctr-archipelago-v0.3.0/x", "other/x", "ctr-archipelago-v0.3.1//x",
		                          "ctr-archipelago-v0.3.1/./x", "ctr-archipelago-v0.3.1x/y"};
		for (const char *bad : badNames)
		{
			std::vector<Member> m = GoodMembers(p);
			m.push_back(File(bad, "x"));
			CHECK_EQ(ExtractExecutable(Archive(p, m), p, kTag, out), ERR_ARCHIVE);
		}
		// Missing executable, duplicate executable, case-variant duplicate.
		{
			std::vector<Member> m = GoodMembers(p);
			m.erase(m.begin() + 3);
			CHECK_EQ(ExtractExecutable(Archive(p, m), p, kTag, out), ERR_ARCHIVE);
			m = GoodMembers(p);
			m.push_back(File(exe, "second"));
			CHECK_EQ(ExtractExecutable(Archive(p, m), p, kTag, out), ERR_ARCHIVE);
			m = GoodMembers(p);
			std::string upper = kRoot + ExecutableName(p);
			upper[kRoot.size()] = 'C';
			m.push_back(File(upper, "second"));
			CHECK_EQ(ExtractExecutable(Archive(p, m), p, kTag, out), ERR_ARCHIVE);
		}
		// The executable as a directory.
		{
			std::vector<Member> m = GoodMembers(p);
			m.erase(m.begin() + 3);
			Member d = File(p == PLATFORM_WINDOWS ? exe + "/" : exe, "");
			d.type = '5';
			d.unixMode = 040755;
			m.push_back(d);
			CHECK_EQ(ExtractExecutable(Archive(p, m), p, kTag, out), ERR_ARCHIVE);
		}
		// Garbage and truncation.
		Bytes a = Archive(p, GoodMembers(p));
		Bytes cut(a.begin(), a.begin() + a.size() / 2);
		CHECK(ExtractExecutable(cut, p, kTag, out) != OK);
		CHECK(ExtractExecutable(B("garbage"), p, kTag, out) != OK);
	}

	// ZIP specifics: a symlink entry, a damaged CRC, encryption flag, stored.
	{
		Bytes out;
		std::vector<Member> m = GoodMembers(PLATFORM_WINDOWS);
		Member link = File(kRoot + "link", "/etc/passwd");
		link.unixMode = 0120777;
		m.push_back(link);
		CHECK_EQ(ExtractExecutable(Zip(m), PLATFORM_WINDOWS, kTag, out), ERR_ARCHIVE);

		Bytes z = Zip(GoodMembers(PLATFORM_WINDOWS), false);
		CHECK_EQ(ExtractExecutable(z, PLATFORM_WINDOWS, kTag, out), OK);
		CHECK(out == B(kExeBytes));
		// Flip one byte of the stored executable payload: CRC catches it.
		const std::string needle = "NEW-CLIENT";
		auto it = std::search(z.begin(), z.end(), needle.begin(), needle.end());
		CHECK(it != z.end());
		Bytes damaged = z;
		damaged[(size_t)(it - z.begin()) + 3] ^= 1;
		CHECK_EQ(ExtractExecutable(damaged, PLATFORM_WINDOWS, kTag, out), ERR_ARCHIVE);
		// Encrypted flag on the first central entry.
		Bytes enc = z;
		for (size_t i = 0; i + 4 < enc.size(); i++)
			if (enc[i] == 0x50 && enc[i + 1] == 0x4b && enc[i + 2] == 0x01 && enc[i + 3] == 0x02)
			{
				enc[i + 8] |= 1;
				break;
			}
		CHECK_EQ(ExtractExecutable(enc, PLATFORM_WINDOWS, kTag, out), ERR_ARCHIVE);
		// Trailing bytes after the end record.
		Bytes trailing = z;
		trailing.push_back(0);
		CHECK_EQ(ExtractExecutable(trailing, PLATFORM_WINDOWS, kTag, out), ERR_ARCHIVE);
	}

	// tar specifics: links, devices, pax path and pax size override.
	{
		Bytes out;
		for (char type : {'1', '2', '3', '4', '6', 'g', 'L', 'S'})
		{
			std::vector<Member> m = GoodMembers(PLATFORM_LINUX);
			Member odd = File(kRoot + "odd", "");
			odd.type = type;
			m.push_back(odd);
			CHECK_EQ(ExtractExecutable(TarGz(m), PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
		}
		// Python's pax mtime records (the real release shape) are accepted.
		std::vector<Member> m = GoodMembers(PLATFORM_LINUX);
		for (Member &x : m)
			x.pax = PaxRecord("mtime", "1791880397.5697303");
		CHECK_EQ(ExtractExecutable(TarGz(m), PLATFORM_LINUX, kTag, out), OK);
		CHECK(out == B(kExeBytes));
		// A pax path record that escapes the root is refused.
		m = GoodMembers(PLATFORM_LINUX);
		m[0].pax = PaxRecord("path", "../../evil");
		CHECK_EQ(ExtractExecutable(TarGz(m), PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
		// A pax path record can rename an innocent entry onto the executable:
		// that makes two executables, refused.
		m = GoodMembers(PLATFORM_LINUX);
		m[0].pax = PaxRecord("path", kRoot + "ctr_native_ap");
		CHECK_EQ(ExtractExecutable(TarGz(m), PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
		// pax size and linkpath overrides are refused outright.
		m = GoodMembers(PLATFORM_LINUX);
		m[3].pax = PaxRecord("size", "3");
		CHECK_EQ(ExtractExecutable(TarGz(m), PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
		m = GoodMembers(PLATFORM_LINUX);
		m[3].pax = PaxRecord("linkpath", "/etc/passwd");
		CHECK_EQ(ExtractExecutable(TarGz(m), PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
		// Bad header checksum.
		Bytes tgz = TarGz(GoodMembers(PLATFORM_LINUX));
		Bytes tar;
		{
			z_stream zs;
			std::memset(&zs, 0, sizeof zs);
			inflateInit2(&zs, 16 + MAX_WBITS);
			tar.resize(1 << 20);
			zs.next_in = tgz.data();
			zs.avail_in = (uInt)tgz.size();
			zs.next_out = tar.data();
			zs.avail_out = (uInt)tar.size();
			inflate(&zs, Z_FINISH);
			tar.resize(zs.total_out);
			inflateEnd(&zs);
		}
		tar[0] ^= 1;
		CHECK_EQ(ExtractExecutable(RawDeflate(tar, 16 + MAX_WBITS), PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
		// A gzip stream with junk after it.
		Bytes junk = tgz;
		junk.push_back(0x42);
		CHECK_EQ(ExtractExecutable(junk, PLATFORM_LINUX, kTag, out), ERR_ARCHIVE);
	}
}

// ── install set ──

static void TestInstallSet()
{
	for (int pi = 0; pi < 2; pi++)
	{
		const Platform p = (Platform)pi;
		std::vector<Member> m = GoodMembers(p);
		m.push_back(File(kRoot + "assets/tracks/cortex-vortex/CVortex Arcade All.vrm", "vrm"));
		m.push_back(File(kRoot + "assets/tracks/cortex-vortex/deeper/x.lev", "no"));
		m.push_back(File(kRoot + "assets/ctr-u.bin", "disc"));
		m.push_back(File(kRoot + "config.ini", "cfg"));
		m.push_back(File(kRoot + "ap-config.txt", "cfg"));
		m.push_back(File(kRoot + "memcards/card.mcd", "card"));
		std::vector<InstallFile> set;
		CHECK_EQ(ExtractInstallSet(Archive(p, m), p, kTag, set), OK);
		CHECK_EQ(set.size(), 4);
		if (set.size() == 4)
		{
			CHECK(set[0].rel == ExecutableName(p) && set[0].data == B(kExeBytes));
			CHECK(set[1].rel == "ctr.apworld" && set[1].data == B("world"));
			CHECK(set[2].rel == "assets/tracks/cortex-vortex/CVortex Arcade All.lev");
			CHECK(set[3].rel == "assets/tracks/cortex-vortex/CVortex Arcade All.vrm");
			for (const InstallFile &f : set)
				CHECK(f.sha256 == Sha256Hex(f.data));
		}
		CHECK(IsInstallPath(ExecutableName(p), p));
		CHECK(!IsInstallPath(ExecutableName(p == PLATFORM_WINDOWS ? PLATFORM_LINUX : PLATFORM_WINDOWS), p));
		CHECK(!IsInstallPath("config.ini", p) && !IsInstallPath("ap-config.txt", p) &&
		      !IsInstallPath("assets/ctr-u.bin", p) && !IsInstallPath("assets/tracks/cortex-vortex/", p) &&
		      !IsInstallPath("assets/tracks/cortex-vortex/a/b", p) && !IsInstallPath("LICENSE", p) &&
		      !IsInstallPath("Assets/tracks/cortex-vortex/x", p));
		// Two install files that differ only in case are refused.
		m = GoodMembers(p);
		m.push_back(File(kRoot + "CTR.apworld", "world2"));
		CHECK_EQ(ExtractInstallSet(Archive(p, m), p, kTag, set), ERR_ARCHIVE);
		CHECK(set.empty());
	}
	// Real v0.2.4 layout: one root folder, the two Cortex Vortex files sit in
	// assets/tracks/cortex-vortex/ (listing taken from the published archives).
	const char *real024[] = {"LICENSE", "SETUP.md", "THIRD_PARTY_NOTICES.md", "ap-config.example.txt",
	                         "assets/tracks/cortex-vortex/CVortex Arcade All.lev",
	                         "assets/tracks/cortex-vortex/CVortex Arcade All.vrm", "ctr.apworld",
	                         "ctr_native_ap.exe", "extract_assets.py", "support-bundle.bat",
	                         "support-bundle.ps1", "versions.txt"};
	int installed = 0;
	for (const char *rel : real024)
		installed += IsInstallPath(rel, PLATFORM_WINDOWS);
	CHECK_EQ(installed, 4);
}

// ── install with a fault-injecting file table ──

struct Faults
{
	std::string failRenameTo;                // a rename whose target equals this fails
	std::vector<std::string> failRenameFrom; // ... or whose source is one of these
	std::string failWriteOf;    // a write to this path fails after writing half
	std::string corruptWriteOf; // a write to this path stores flipped bytes
};

static Faults g_faults;

static bool FWrite(void *ctx, const std::string &path, const Bytes &d)
{
	if (path == g_faults.failWriteOf)
	{
		FILE *f = std::fopen(path.c_str(), "wb");
		if (f)
		{
			std::fwrite(d.data(), 1, d.size() / 2, f);
			std::fclose(f);
		}
		return false;
	}
	if (path == g_faults.corruptWriteOf && !d.empty())
	{
		Bytes bad = d;
		bad[0] ^= 1;
		return RealFsOps().writeFile(ctx, path, bad);
	}
	return RealFsOps().writeFile(ctx, path, d);
}

static bool FRename(void *ctx, const std::string &from, const std::string &to)
{
	if (to == g_faults.failRenameTo ||
	    std::find(g_faults.failRenameFrom.begin(), g_faults.failRenameFrom.end(), from) != g_faults.failRenameFrom.end())
		return false;
	return RealFsOps().renameFile(ctx, from, to);
}

static FsOps FaultOps()
{
	FsOps ops = RealFsOps();
	ops.writeFile = FWrite;
	ops.renameFile = FRename;
	return ops;
}

static std::string ReadText(const std::string &path)
{
	Bytes b;
	if (!RealFsOps().readFile(nullptr, path, 1 << 20, b))
		return "<missing>";
	return std::string(b.begin(), b.end());
}

static void WriteText(const std::string &path, const std::string &text)
{
	RealFsOps().writeFile(nullptr, path, B(text));
}

static InstallFile IF(const std::string &rel, const std::string &data)
{
	InstallFile f;
	f.rel = rel;
	f.data = B(data);
	f.sha256 = Sha256Hex(f.data);
	return f;
}

struct Install
{
	std::string dir, exe, apworld, lev, vrm, config, apconfig, card, disc;
};

static const char kLev[] = "assets/tracks/cortex-vortex/CVortex Arcade All.lev";
static const char kVrm[] = "assets/tracks/cortex-vortex/CVortex Arcade All.vrm";

// An installed v0.3.0 client: exe, apworld, one Cortex Vortex file (the .vrm
// is new in the update), plus the files the updater must never touch.
static Install MakeInstall(const std::string &dir)
{
	Install in;
	in.dir = dir;
	in.exe = dir + "/ctr_native_ap";
	in.apworld = dir + "/ctr.apworld";
	in.lev = dir + "/" + kLev;
	in.vrm = dir + "/" + kVrm;
	in.config = dir + "/config.ini";
	in.apconfig = dir + "/ap-config.txt";
	in.card = dir + "/memcards/card.mcd";
	in.disc = dir + "/assets/ctr-u.bin";
	mkdir((dir + "/assets").c_str(), 0755);
	mkdir((dir + "/assets/tracks").c_str(), 0755);
	mkdir((dir + "/assets/tracks/cortex-vortex").c_str(), 0755);
	mkdir((dir + "/memcards").c_str(), 0755);
	WriteText(in.exe, "OLD EXE");
	WriteText(in.apworld, "OLD WORLD");
	WriteText(in.lev, "OLD LEV");
	WriteText(in.config, "CONFIG");
	WriteText(in.apconfig, "APCONFIG");
	WriteText(in.card, "CARD");
	WriteText(in.disc, "DISC");
	return in;
}

static std::vector<InstallFile> NewSet()
{
	return {IF("ctr_native_ap", "NEW EXE"), IF("ctr.apworld", "NEW WORLD"), IF(kLev, "NEW LEV"), IF(kVrm, "NEW VRM")};
}

static bool Untouched(const Install &in)
{
	return ReadText(in.config) == "CONFIG" && ReadText(in.apconfig) == "APCONFIG" && ReadText(in.card) == "CARD" &&
	       ReadText(in.disc) == "DISC";
}

static bool OldState(const Install &in)
{
	return ReadText(in.exe) == "OLD EXE" && ReadText(in.apworld) == "OLD WORLD" && ReadText(in.lev) == "OLD LEV" &&
	       !RealFsOps().exists(nullptr, in.vrm) && Untouched(in);
}

// No staging, journal or marker files left anywhere in the install.
static bool NoLeftovers(const Install &in, bool allowOld)
{
	const FsOps fs = RealFsOps();
	for (const std::string &f : {in.exe, in.apworld, in.lev, in.vrm})
	{
		if (fs.exists(nullptr, StagedPath(f)) || fs.exists(nullptr, PartPath(f)))
			return false;
		if (!allowOld && fs.exists(nullptr, OldPath(f)))
			return false;
	}
	return !fs.exists(nullptr, JournalPath(in.exe));
}

static void Clean(const Install &in)
{
	const FsOps fs = RealFsOps();
	for (const std::string &f : {in.exe, in.apworld, in.lev, in.vrm})
		for (const std::string &g : {f, StagedPath(f), PartPath(f), OldPath(f)})
			fs.removeFile(nullptr, g);
	for (const std::string &f : {in.config, in.apconfig, in.card, in.disc, MarkerPath(in.exe), JournalPath(in.exe)})
		fs.removeFile(nullptr, f);
}

static void TestInstall()
{
	char tmpl[] = "/tmp/ctr-update-XXXXXX";
	const char *dirp = mkdtemp(tmpl);
	CHECK(dirp != nullptr);
	if (!dirp)
		return;
	const std::string dir = dirp;
	const FsOps real = RealFsOps();
	const FsOps faulty = FaultOps();
	std::vector<StagedFile> staged;

	// Happy path: stage, apply, start, confirm.
	Install in = MakeInstall(dir);
	g_faults = Faults();
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	CHECK_EQ(staged.size(), 4);
	CHECK(ReadText(StagedPath(in.vrm)) == "NEW VRM" && ReadText(in.lev) == "OLD LEV");
	CHECK(real.exists(nullptr, JournalPath(in.exe)));
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(ReadText(in.exe) == "NEW EXE" && ReadText(in.apworld) == "NEW WORLD" && ReadText(in.lev) == "NEW LEV" &&
	      ReadText(in.vrm) == "NEW VRM");
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE" && ReadText(OldPath(in.apworld)) == "OLD WORLD" &&
	      ReadText(OldPath(in.lev)) == "OLD LEV" && !real.exists(nullptr, OldPath(in.vrm)));
	CHECK(Untouched(in) && NoLeftovers(in, true));
	struct stat st;
	CHECK(stat(in.exe.c_str(), &st) == 0 && (st.st_mode & 0111) != 0);
	CHECK(ReadText(MarkerPath(in.exe)) == "v0.3.1\nctr.apworld\n" + std::string(kLev) + "\nctr_native_ap\n");
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_UPDATED);
	CHECK(real.exists(nullptr, OldPath(in.lev)));
	ConfirmStarted(real, in.exe);
	CHECK(NoLeftovers(in, false) && !real.exists(nullptr, MarkerPath(in.exe)));
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_CLEAN);
	Clean(in);

	// The executable maps to the running file whatever its name.
	{
		const std::string renamed = dir + "/CTR-AP";
		WriteText(renamed, "OLD EXE");
		CHECK_EQ(Stage(real, renamed, {IF("ctr_native_ap", "NEW EXE")}, staged), OK);
		CHECK_EQ(Apply(real, renamed, staged, "v0.3.1"), OK);
		CHECK(ReadText(renamed) == "NEW EXE" && ReadText(OldPath(renamed)) == "OLD EXE");
		CHECK(!real.exists(nullptr, dir + "/ctr_native_ap"));
		ConfirmStarted(real, renamed);
		CHECK(!real.exists(nullptr, OldPath(renamed)));
		real.removeFile(nullptr, renamed);
	}

	// Partial multi-file failure at every swap step: each failure leaves all
	// files exactly as before, and nothing staged behind.
	{
		Install probe = MakeInstall(dir);
		const std::vector<std::string> targets = {probe.apworld, probe.lev, probe.vrm, probe.exe};
		Clean(probe);
		for (const std::string &t : targets)
		{
			for (int mode = 0; mode < 2; mode++)
			{
				if (mode == 1 && t == probe.vrm)
					continue; // a new file has no installed copy to move aside
				in = MakeInstall(dir);
				CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
				g_faults = Faults();
				if (mode == 0)
					g_faults.failRenameFrom = {StagedPath(t)}; // staged -> target fails
				else
					g_faults.failRenameTo = OldPath(t);   // target -> .old fails
				CHECK_EQ(Apply(faulty, in.exe, staged, "v0.3.1"), ERR_IO);
				CHECK(OldState(in));
				CHECK(NoLeftovers(in, false));
				CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
				Clean(in);
			}
		}
	}

	// Undo itself failing for one file: that file's previous copy stays as
	// .old (the manual rollback copy) and the others are restored.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	g_faults = Faults();
	// The last step fails, and the apworld cannot come back.
	g_faults.failRenameFrom = {StagedPath(in.exe), OldPath(in.apworld)};
	CHECK_EQ(Apply(faulty, in.exe, staged, "v0.3.1"), ERR_IO);
	CHECK(ReadText(in.exe) == "OLD EXE" && ReadText(in.lev) == "OLD LEV" && !real.exists(nullptr, in.vrm));
	CHECK(ReadText(OldPath(in.apworld)) == "OLD WORLD" && !real.exists(nullptr, in.apworld));
	CHECK(Untouched(in));
	Clean(in);

	// Both names failing for the executable leaves its .old as the manual copy.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	g_faults = Faults();
	g_faults.failRenameFrom = {StagedPath(in.exe), OldPath(in.exe)};
	CHECK_EQ(Apply(faulty, in.exe, staged, "v0.3.1"), ERR_IO);
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	CHECK(ReadText(in.apworld) == "OLD WORLD" && ReadText(in.lev) == "OLD LEV" && !real.exists(nullptr, in.vrm));
	Clean(in);

	// One staged file changed after verification: nothing moves, all staged go.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	WriteText(StagedPath(in.lev), "TAMPERED");
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), ERR_STAGED_CHANGED);
	CHECK(OldState(in) && NoLeftovers(in, false));
	// A missing staged file is the same.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	real.removeFile(nullptr, StagedPath(in.vrm));
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), ERR_STAGED_CHANGED);
	CHECK(OldState(in) && NoLeftovers(in, false));
	// A non-stable tag is refused before anything moves.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "../x"), ERR_STAGED_CHANGED);
	CHECK(OldState(in) && NoLeftovers(in, false));

	// Staging failures part way: a short write, and a write that reads back
	// different bytes. Everything staged so far is removed.
	g_faults = Faults();
	g_faults.failWriteOf = PartPath(in.lev);
	CHECK_EQ(Stage(faulty, in.exe, NewSet(), staged), ERR_IO);
	CHECK(staged.empty() && OldState(in) && NoLeftovers(in, false));
	g_faults = Faults();
	g_faults.corruptWriteOf = PartPath(in.vrm);
	CHECK_EQ(Stage(faulty, in.exe, NewSet(), staged), ERR_IO);
	CHECK(staged.empty() && OldState(in) && NoLeftovers(in, false));
	// A set whose hash does not match its bytes is refused.
	{
		std::vector<InstallFile> bad = NewSet();
		bad[2].data = B("OTHER");
		CHECK_EQ(Stage(real, in.exe, bad, staged), ERR_IO);
		CHECK(OldState(in) && NoLeftovers(in, false));
	}
	// Sets outside the install list, or without exactly one executable.
	CHECK_EQ(Stage(real, in.exe, {IF("ctr_native_ap", "x"), IF("config.ini", "x")}, staged), ERR_ARCHIVE);
	CHECK_EQ(Stage(real, in.exe, {IF("ctr_native_ap", "x"), IF("../ctr.apworld", "x")}, staged), ERR_ARCHIVE);
	CHECK_EQ(Stage(real, in.exe, {IF("ctr.apworld", "x")}, staged), ERR_ARCHIVE);
	CHECK(OldState(in));

	// An earlier session's staged files, named in the journal, are removed at
	// launch; the journal cannot point the cleanup outside the install list.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	WriteText(PartPath(in.apworld), "LEFT");
	WriteText(dir + "/config.ini.update", "NOT OURS");
	WriteText(JournalPath(in.exe), "ctr_native_ap\nctr.apworld\n" + std::string(kLev) + "\n" + kVrm + "\nconfig.ini\n");
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_CLEAN);
	CHECK(NoLeftovers(in, false) && OldState(in));
	CHECK(ReadText(dir + "/config.ini.update") == "NOT OURS");
	real.removeFile(nullptr, dir + "/config.ini.update");

	// Missing Cortex Vortex folder: created. A folder that is a link: refused,
	// so a shared assets folder is never written through.
	Clean(in);
	in = MakeInstall(dir);
	real.removeFile(nullptr, in.lev);
	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(ReadText(in.lev) == "NEW LEV" && ReadText(in.vrm) == "NEW VRM" && Untouched(in));
	ConfirmStarted(real, in.exe);
	Clean(in);
	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	{
		char shared[] = "/tmp/ctr-update-shared-XXXXXX";
		const char *sh = mkdtemp(shared);
		CHECK(sh != nullptr);
		in = MakeInstall(dir);
		real.removeFile(nullptr, in.lev);
		rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
		CHECK(symlink(sh, (dir + "/assets/tracks/cortex-vortex").c_str()) == 0);
		CHECK_EQ(Stage(real, in.exe, NewSet(), staged), ERR_IO);
		CHECK(ReadText(in.exe) == "OLD EXE" && ReadText(in.apworld) == "OLD WORLD" && NoLeftovers(in, false));
		CHECK(!real.exists(nullptr, std::string(sh) + "/CVortex Arcade All.lev.update.part"));
		unlink((dir + "/assets/tracks/cortex-vortex").c_str());
		rmdir(sh);
		Clean(in);
	}

	// Manual rollback: the player renamed .old back; the marker is dropped and
	// no file is touched.
	in = MakeInstall(dir);
	WriteText(OldPath(in.exe), "NEW EXE");
	WriteText(MarkerPath(in.exe), "v0.3.1\nctr_native_ap\n");
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_ROLLED_BACK);
	CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	CHECK(ReadText(in.exe) == "OLD EXE" && ReadText(OldPath(in.exe)) == "NEW EXE");
	// A damaged marker cannot point ConfirmStarted at other files.
	WriteText(OldPath(in.config), "KEEP");
	WriteText(MarkerPath(in.exe), "v0.3.0\nconfig.ini\n../x\n");
	ConfirmStarted(real, in.exe);
	CHECK(ReadText(OldPath(in.config)) == "KEEP");
	real.removeFile(nullptr, OldPath(in.config));

	// A second update replaces the previous rollback copy, keeping one version.
	WriteText(OldPath(in.exe), "ANCIENT");
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	Clean(in);

	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	rmdir((dir + "/assets").c_str());
	rmdir((dir + "/memcards").c_str());
	CHECK(rmdir(dir.c_str()) == 0);
}

static void TestAllowlist()
{
	std::string host, path;
	CHECK(AllowedUrl(LatestReleaseUrl(), false, host, path) && host == "api.github.com");
	CHECK(AllowedUrl(AssetUrl(kTag, "manifest.json"), false, host, path) && host == "github.com" &&
	      path == "/dowlle/ctr-native-ap/releases/download/v0.3.1/manifest.json");
	CHECK(AllowedUrl("https://objects.githubusercontent.com/github-production-release-asset/1?x=y", true, host, path));
	CHECK(AllowedUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1", true, host, path));
	CHECK(!AllowedUrl("https://objects.githubusercontent.com/x", false, host, path)); // only via redirect
	CHECK(!AllowedUrl("http://github.com/dowlle/ctr-native-ap/releases/download/v0.3.1/x", true, host, path));
	CHECK(!AllowedUrl("https://github.com/someone/ctr-native-ap/releases/download/v0.3.1/x", true, host, path));
	CHECK(!AllowedUrl("https://github.com/dowlle/ctr-native-ap/releases/download/../../x", true, host, path));
	CHECK(!AllowedUrl("https://evil.example/dowlle/ctr-native-ap/releases/download/v1/x", true, host, path));
	CHECK(!AllowedUrl("https://github.com.evil.example/dowlle/ctr-native-ap/releases/download/v1/x", true, host, path));
	CHECK(!AllowedUrl("https://user@github.com/dowlle/ctr-native-ap/releases/download/v1/x", true, host, path));
	CHECK(!AllowedUrl("https://github.com:8443/dowlle/ctr-native-ap/releases/download/v1/x", true, host, path));
	CHECK(!AllowedUrl("https://api.github.com/repos/dowlle/ctr-native-ap/releases/latest", true, host, path));
	CHECK(!AllowedUrl("https://api.github.com/repos/other/repo/releases/latest", false, host, path));
	CHECK(!AllowedUrl("/relative/path", true, host, path));
	CHECK(!AllowedUrl("https://github.com/dowlle/ctr-native-ap/releases/download/v1/a b", true, host, path));
}

int main()
{
	TestInterop();
	TestBase64();
	TestSignatureFailures();
	TestHashAndPartial();
	TestDowngradeAndReplay();
	TestOfferVisibility();
	TestManifestStrictness();
	TestArchives();
	TestInstallSet();
	TestInstall();
	TestAllowlist();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
