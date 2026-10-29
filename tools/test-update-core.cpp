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

static void TarHeader(Bytes &out, const std::string &name, size_t size, char type, unsigned mode = 0644)
{
	unsigned char h[512];
	std::memset(h, 0, sizeof h);
	std::memcpy(h, name.data(), std::min<size_t>(name.size(), 100));
	std::snprintf((char *)h + 100, 8, "%07o", mode & 07777);
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
		TarHeader(tar, m.name, m.type == '0' ? m.data.size() : 0, m.type, m.unixMode ? m.unixMode : 0644);
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

// The atomic executable path uses link and replace; the same faults apply.
static bool FLink(void *ctx, const std::string &existing, const std::string &newLink)
{
	if (newLink == g_faults.failRenameTo)
		return false;
	return RealFsOps().linkFile(ctx, existing, newLink);
}

static bool FReplace(void *ctx, const std::string &from, const std::string &to)
{
	if (to == g_faults.failRenameTo ||
	    std::find(g_faults.failRenameFrom.begin(), g_faults.failRenameFrom.end(), from) != g_faults.failRenameFrom.end())
		return false;
	return RealFsOps().replaceFile(ctx, from, to);
}

static FsOps FaultOps(bool atomicExe = true)
{
	FsOps ops = RealFsOps();
	ops.writeFile = FWrite;
	ops.renameFile = FRename;
	ops.linkFile = atomicExe ? FLink : nullptr;
	ops.replaceFile = atomicExe ? FReplace : nullptr;
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
	return !fs.exists(nullptr, JournalPath(in.exe)) && !fs.exists(nullptr, SwapJournalPath(in.exe));
}

static void Clean(const Install &in)
{
	const FsOps fs = RealFsOps();
	for (const std::string &f : {in.exe, in.apworld, in.lev, in.vrm})
		for (const std::string &g : {f, StagedPath(f), PartPath(f), OldPath(f)})
			fs.removeFile(nullptr, g);
	for (const std::string &f : {in.config, in.apconfig, in.card, in.disc, MarkerPath(in.exe), JournalPath(in.exe),
	                             SwapJournalPath(in.exe)})
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
		for (int strategy = 0; strategy < 2; strategy++)
		for (const std::string &t : targets)
		{
			const FsOps faulty = FaultOps(strategy == 0); // 1 = no link/replace (Windows-style exe swap)
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
				{
					g_faults.failRenameTo = OldPath(t); // target -> .old fails (rename or hard link)
					g_faults.failWriteOf = OldPath(t);  // ... and so does the copy fallback
				}
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
	CHECK_EQ(Apply(faulty, in.exe, staged, "v0.3.1"), ERR_UNDO_FAILED);
	CHECK(ReadText(in.exe) == "OLD EXE" && ReadText(in.lev) == "OLD LEV" && !real.exists(nullptr, in.vrm));
	CHECK(ReadText(OldPath(in.apworld)) == "OLD WORLD" && !real.exists(nullptr, in.apworld));
	CHECK(Untouched(in));
	// The journal stays; the next launch (no fault now) puts the apworld back.
	CHECK(real.exists(nullptr, SwapJournalPath(in.exe)));
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_RECOVERED);
	CHECK(OldState(in) && NoLeftovers(in, false));
	Clean(in);

	// Both names failing for the executable leaves its .old as the manual copy.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	g_faults = Faults();
	{
		const FsOps plain = FaultOps(false); // rename pair: exe moved aside, then replaced
		g_faults.failRenameFrom = {StagedPath(in.exe), OldPath(in.exe)};
		CHECK_EQ(Apply(plain, in.exe, staged, "v0.3.1"), ERR_UNDO_FAILED);
		CHECK(ReadText(OldPath(in.exe)) == "OLD EXE" && !real.exists(nullptr, in.exe));
		CHECK(ReadText(in.apworld) == "OLD WORLD" && ReadText(in.lev) == "OLD LEV" && !real.exists(nullptr, in.vrm));
		CHECK(real.exists(nullptr, SwapJournalPath(in.exe)));
		// The next launch (when an executable exists to start) restores it.
		CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_RECOVERED);
		CHECK(OldState(in) && NoLeftovers(in, false));
	}
	Clean(in);
	// With atomic replace a failed replace never leaves the executable missing.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	g_faults = Faults();
	g_faults.failRenameFrom = {StagedPath(in.exe)};
	CHECK_EQ(Apply(faulty, in.exe, staged, "v0.3.1"), ERR_IO);
	CHECK(OldState(in) && NoLeftovers(in, false));
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

// ── crash consistency: power loss at every step of the swap ──

struct Crash
{
	int limit = -1; // mutating operations allowed before the "crash"
	int count = 0;
	bool dead = false;
};
static Crash g_crash;

static bool Alive()
{
	if (g_crash.dead)
		return false;
	if (++g_crash.count > g_crash.limit)
	{
		g_crash.dead = true; // this operation and every later one never happen
		return false;
	}
	return true;
}
static bool CWrite(void *c, const std::string &p, const Bytes &d) { return Alive() && RealFsOps().writeFile(c, p, d); }
static bool CRename(void *c, const std::string &a, const std::string &b) { return Alive() && RealFsOps().renameFile(c, a, b); }
static bool CRemove(void *c, const std::string &p) { return Alive() && RealFsOps().removeFile(c, p); }
static bool CLink(void *c, const std::string &a, const std::string &b) { return Alive() && RealFsOps().linkFile(c, a, b); }
static bool CReplace(void *c, const std::string &a, const std::string &b) { return Alive() && RealFsOps().replaceFile(c, a, b); }

static FsOps CrashOps(bool atomicExe)
{
	FsOps ops = RealFsOps();
	ops.writeFile = CWrite;
	ops.renameFile = CRename;
	ops.removeFile = CRemove;
	ops.linkFile = atomicExe ? CLink : nullptr;
	ops.replaceFile = atomicExe ? CReplace : nullptr;
	return ops;
}

static bool NewState(const Install &in)
{
	return ReadText(in.exe) == "NEW EXE" && ReadText(in.apworld) == "NEW WORLD" && ReadText(in.lev) == "NEW LEV" &&
	       ReadText(in.vrm) == "NEW VRM" && Untouched(in);
}

static void TestCrashRecovery()
{
	char tmpl[] = "/tmp/ctr-update-crash-XXXXXX";
	const char *dirp = mkdtemp(tmpl);
	CHECK(dirp != nullptr);
	if (!dirp)
		return;
	const std::string dir = dirp;
	const FsOps real = RealFsOps();
	int crashPoints = 0;
	for (int strategy = 0; strategy < 2; strategy++)
	{
		const bool atomicExe = strategy == 0;
		for (int limit = 0; limit < 64; limit++)
		{
			Install in = MakeInstall(dir);
			std::vector<StagedFile> staged;
			CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
			g_crash = Crash();
			g_crash.limit = limit;
			const Result r = Apply(CrashOps(atomicExe), in.exe, staged, "v0.3.1");
			if (!g_crash.dead)
			{
				CHECK_EQ(r, OK); // ran past the last operation: every point was covered
				CHECK(NewState(in));
				Clean(in);
				break;
			}
			crashPoints++;
			// Power is back. With an atomic executable step the game can always start.
			if (atomicExe)
				CHECK(real.exists(nullptr, in.exe));
			const bool newExe = ReadText(in.exe) == "NEW EXE";
			const StartupState st = StartupCheck(real, in.exe, newExe ? "v0.3.1" : "v0.3.0");
			if (newExe)
			{
				// The swap had reached the new executable: finished, not undone.
				CHECK(st == STARTUP_UPDATED);
				CHECK(NewState(in));
				CHECK(!real.exists(nullptr, SwapJournalPath(in.exe)));
				ConfirmStarted(real, in.exe);
				CHECK(NoLeftovers(in, false));
			}
			else
			{
				// Every other cut-off point returns to the previous install exactly.
				CHECK(st == STARTUP_RECOVERED || st == STARTUP_CLEAN);
				CHECK(OldState(in));
				CHECK(NoLeftovers(in, false));
				CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
			}
			// A second launch finds nothing to do.
			CHECK(StartupCheck(real, in.exe, newExe ? "v0.3.1" : "v0.3.0") == STARTUP_CLEAN);
			Clean(in);
		}
	}
	CHECK(crashPoints > 20);

	// A journal cut off while being written names no completed swap: ignored.
	{
		Install in = MakeInstall(dir);
		WriteText(SwapJournalPath(in.exe), "applying\nv0.3.1\nctr.apworld\t0123");
		CHECK(StartupCheck(real, in.exe, "v0.3.0") == STARTUP_CLEAN);
		CHECK(OldState(in) && NoLeftovers(in, false));
		// A journal that names a file outside the install list cannot move it.
		WriteText(OldPath(in.config), "KEEP");
		WriteText(SwapJournalPath(in.exe),
		          "applying\nv0.3.1\nconfig.ini\t" + std::string(64, 'a') + "\t1\n");
		CHECK(StartupCheck(real, in.exe, "v0.3.0") == STARTUP_CLEAN);
		CHECK(ReadText(OldPath(in.config)) == "KEEP" && ReadText(in.config) == "CONFIG");
		real.removeFile(nullptr, OldPath(in.config));
		Clean(in);
	}
	// A journal next to a finished marker is stale cleanup only.
	{
		Install in = MakeInstall(dir);
		std::vector<StagedFile> staged;
		CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
		CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
		WriteText(SwapJournalPath(in.exe),
		          "applying\nv0.3.1\nctr.apworld\t" + Sha256Hex(B("NEW WORLD")) + "\t1\n");
		CHECK(StartupCheck(real, in.exe, "v0.3.1") == STARTUP_UPDATED);
		CHECK(ReadText(in.apworld) == "NEW WORLD" && !real.exists(nullptr, SwapJournalPath(in.exe)));
		ConfirmStarted(real, in.exe);
		Clean(in);
	}
	// A kept-back file that is neither the new copy nor the old one is not touched.
	{
		Install in = MakeInstall(dir);
		WriteText(OldPath(in.apworld), "OLD WORLD");
		WriteText(in.apworld, "SOMETHING ELSE");
		WriteText(SwapJournalPath(in.exe),
		          "applying\nv0.3.1\nctr.apworld\t" + Sha256Hex(B("NEW WORLD")) + "\t1\n");
		CHECK(StartupCheck(real, in.exe, "v0.3.0") == STARTUP_RESTORE_FAILED);
		CHECK(ReadText(in.apworld) == "SOMETHING ELSE" && ReadText(OldPath(in.apworld)) == "OLD WORLD");
		CHECK(real.exists(nullptr, SwapJournalPath(in.exe)));
		real.removeFile(nullptr, OldPath(in.apworld));
		Clean(in);
	}
	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	rmdir((dir + "/assets").c_str());
	rmdir((dir + "/memcards").c_str());
	CHECK(rmdir(dir.c_str()) == 0);
}

// ── staging: executable bit, marker version spelling, asset folder ──

static bool g_failChmod;
static bool FChmod(void *c, const std::string &p) { return !g_failChmod && RealFsOps().makeExecutable(c, p); }

static void TestPartialUndoRecovery()
{
	char tmpl[] = "/tmp/ctr-update-undo-XXXXXX";
	const char *dirp = mkdtemp(tmpl);
	CHECK(dirp != nullptr);
	if (!dirp)
		return;
	const std::string dir = dirp;
	const FsOps real = RealFsOps();
	Install in = MakeInstall(dir);
	std::vector<StagedFile> staged;
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	// The marker write fails after all files were swapped. Undo then restores
	// the world and assets, but the executable's atomic restore fails.
	g_faults = Faults();
	g_faults.failWriteOf = MarkerPath(in.exe);
	g_faults.failRenameFrom = {OldPath(in.exe)};
	CHECK_EQ(Apply(FaultOps(), in.exe, staged, "v0.3.1"), ERR_UNDO_FAILED);
	CHECK(ReadText(in.exe) == "NEW EXE" && ReadText(in.apworld) == "OLD WORLD");
	CHECK(ReadText(in.lev) == "OLD LEV" && !real.exists(nullptr, in.vrm));
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	const std::string journal = ReadText(SwapJournalPath(in.exe));
	// A second update and menu confirmation cannot consume recovery's files.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), ERR_UNDO_FAILED);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.2"), ERR_UNDO_FAILED);
	ConfirmStarted(real, in.exe);
	CHECK(ReadText(SwapJournalPath(in.exe)) == journal);
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	// The mixed install must be undone, never marked as a successful update.
	g_faults = Faults();
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_RECOVERED);
	CHECK(OldState(in) && NoLeftovers(in, false));
	CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_CLEAN);
	Clean(in);

	// A failed startup restore keeps its journal and backup for a later retry.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	g_faults.failWriteOf = MarkerPath(in.exe);
	g_faults.failRenameFrom = {OldPath(in.exe)};
	CHECK_EQ(Apply(FaultOps(), in.exe, staged, "v0.3.1"), ERR_UNDO_FAILED);
	CHECK_EQ(StartupCheck(FaultOps(), in.exe, "v0.3.1"), STARTUP_RESTORE_FAILED);
	CHECK(ReadText(in.exe) == "NEW EXE"); // failed atomic recovery remains launchable
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	CHECK(real.exists(nullptr, SwapJournalPath(in.exe)));
	CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), ERR_UNDO_FAILED);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.2"), ERR_UNDO_FAILED);
	ConfirmStarted(real, in.exe);
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	g_faults = Faults();
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_RECOVERED);
	CHECK(OldState(in) && NoLeftovers(in, false));
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_CLEAN);
	// Once recovered, staging is available again.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	DiscardStaged(real, in.exe, staged);
	Clean(in);
	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	rmdir((dir + "/assets").c_str());
	rmdir((dir + "/memcards").c_str());
	CHECK(rmdir(dir.c_str()) == 0);
}

static void TestStagingAndMarker()
{
	char tmpl[] = "/tmp/ctr-update-stage-XXXXXX";
	const char *dirp = mkdtemp(tmpl);
	CHECK(dirp != nullptr);
	if (!dirp)
		return;
	const std::string dir = dirp;
	const FsOps real = RealFsOps();
	std::vector<StagedFile> staged;
	Install in = MakeInstall(dir);

	// The staged executable is executable before it is swapped in.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	struct stat st;
	CHECK(stat(StagedPath(in.exe).c_str(), &st) == 0 && (st.st_mode & 0111) == 0111);
	CHECK(stat(StagedPath(in.apworld).c_str(), &st) == 0 && (st.st_mode & 0111) == 0);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(stat(in.exe.c_str(), &st) == 0 && (st.st_mode & 0111) == 0111);
	CHECK(stat(OldPath(in.exe).c_str(), &st) == 0 && ReadText(OldPath(in.exe)) == "OLD EXE");
	ConfirmStarted(real, in.exe);
	Clean(in);

	// A failed chmod fails staging and leaves nothing behind.
	in = MakeInstall(dir);
	FsOps noChmod = real;
	noChmod.makeExecutable = FChmod;
	g_failChmod = true;
	CHECK_EQ(Stage(noChmod, in.exe, NewSet(), staged), ERR_IO);
	g_failChmod = false;
	CHECK(staged.empty() && OldState(in) && NoLeftovers(in, false));

	// The marker's tag and the running version compare as versions, not text.
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(StartupCheck(real, in.exe, "0.3.1") == STARTUP_UPDATED);
	CHECK(real.exists(nullptr, MarkerPath(in.exe)));
	CHECK(StartupCheck(real, in.exe, "V0.3.1") == STARTUP_UPDATED);
	CHECK(StartupCheck(real, in.exe, "0.3.2") == STARTUP_ROLLED_BACK);
	CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	Clean(in);

	// The assets folder the game reads (parent folder, case variant) receives
	// the Cortex Vortex files; the apworld stays beside the executable.
	{
		const std::string game = dir + "/game";
		const std::string alt = dir + "/Assets";
		mkdir(game.c_str(), 0755);
		mkdir(alt.c_str(), 0755);
		const std::string exe = game + "/ctr_native_ap";
		WriteText(exe, "OLD EXE");
		SetAssetDir(alt + "/");
		CHECK(TargetPath(exe, kLev) == alt + "/tracks/cortex-vortex/CVortex Arcade All.lev");
		CHECK(TargetPath(exe, "ctr.apworld") == game + "/ctr.apworld");
		CHECK_EQ(Stage(real, exe, NewSet(), staged), OK);
		CHECK(ReadText(StagedPath(alt + "/tracks/cortex-vortex/CVortex Arcade All.lev")) == "NEW LEV");
		CHECK(!real.exists(nullptr, game + "/assets"));
		CHECK_EQ(Apply(real, exe, staged, "v0.3.1"), OK);
		CHECK(ReadText(alt + "/tracks/cortex-vortex/CVortex Arcade All.vrm") == "NEW VRM");
		CHECK(!real.exists(nullptr, game + "/assets"));
		SetAssetDir("");
		CHECK(TargetPath(exe, kLev) == game + "/" + kLev);
		const std::string rm = "rm -rf '" + dir + "/game' '" + alt + "'";
		CHECK(std::system(rm.c_str()) == 0);
	}
	Clean(in);
	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	rmdir((dir + "/assets").c_str());
	rmdir((dir + "/memcards").c_str());
	CHECK(rmdir(dir.c_str()) == 0);
}

// ── chunked bodies that would overflow a 32-bit size ──

static void TestChunkOverflow()
{
	HttpResponse resp;
	const std::string ch = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
	const size_t huge = (size_t)0xFFFFFFFFu;
	// The wrap-around cases: a chunk near FFFFFFFF after a first chunk, with a
	// cap that admits it. Each must be refused or incomplete, never walk past the buffer.
	for (const char *size : {"FFFFFFFF", "FFFFFFFE", "FFFFFFFD", "FFFFFFFC", "FFFFFFF0", "80000000"})
	{
		const HttpParse r = ParseHttpResponse(B(ch + "3\r\nabc\r\n" + size + "\r\nxx"), huge, resp);
		CHECK(r == HTTP_TOO_LARGE || r == HTTP_INCOMPLETE);
		const HttpParse first = ParseHttpResponse(B(ch + size + "\r\nxx"), huge, resp);
		CHECK(first == HTTP_TOO_LARGE || first == HTTP_INCOMPLETE);
	}
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\nFFFFFFFF\r\nxx"), huge, resp), HTTP_TOO_LARGE);
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\nFFFFFFFE\r\nxx"), huge, resp), HTTP_TOO_LARGE);
	// A chunk that fits the cap but not the data, close to the end of the buffer.
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\n5\r\nxxxxx"), 100, resp), HTTP_INCOMPLETE);
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\n5\r\nxxxxx\r"), 100, resp), HTTP_INCOMPLETE);
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\n5\r\nxxxxx\r\n0\r\n\r\n"), 100, resp), HTTP_COMPLETE);
	CHECK_EQ(ParseHttpResponse(B(ch + "3\r\nabc\r\n98\r\n"), 100, resp), HTTP_TOO_LARGE);
	CHECK(kMaxArchive <= 64u * 1024u * 1024u);
}

// ── one staging worker at a time ──

static void TestWorkGate()
{
	WorkSession s;
	s.generation = 1;
	CHECK(CancelAllowed(s) && DownloadAllowed(s));
	CHECK(ClaimStaging(s, 1));
	// While worker 1 stages: no second claim, no cancel, no new download.
	CHECK(!ClaimStaging(s, 1));
	CHECK(!CancelAllowed(s) && !DownloadAllowed(s));
	ReleaseStaging(s);
	CHECK(CancelAllowed(s) && DownloadAllowed(s));
	// A worker that was cancelled (generation moved on) cannot claim staging.
	s.generation = 2;
	CHECK(!ClaimStaging(s, 1));
	CHECK(!s.staging);
	CHECK(ClaimStaging(s, 2));
	ReleaseStaging(s);
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

// ── Linux executable swap without hard links, marker and folder-sync faults ──

static bool NoLink(void *, const std::string &, const std::string &) { return false; }
static std::string g_failRemoveOf;
static bool FRemove(void *c, const std::string &p)
{
	return p != g_failRemoveOf && RealFsOps().removeFile(c, p);
}

static void TestExecutableSwapFallbacks()
{
	char tmpl[] = "/tmp/ctr-update-nolink-XXXXXX";
	const char *dirp = mkdtemp(tmpl);
	CHECK(dirp != nullptr);
	if (!dirp)
		return;
	const std::string dir = dirp;
	const FsOps real = RealFsOps();
	std::vector<StagedFile> staged;
	struct stat st;

	// No hard links (exFAT, some shares): the running executable is copied to
	// .old and the new one renamed over it. The path never goes missing.
	FsOps noLink = real;
	noLink.linkFile = NoLink;
	Install in = MakeInstall(dir);
	chmod(in.exe.c_str(), 0755);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	CHECK_EQ(Apply(noLink, in.exe, staged, "v0.3.1"), OK);
	CHECK(NewState(in));
	CHECK(ReadText(OldPath(in.exe)) == "OLD EXE");
	CHECK(stat(OldPath(in.exe).c_str(), &st) == 0 && (st.st_mode & 0111) == 0111); // the rollback copy runs
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_UPDATED);
	ConfirmStarted(real, in.exe);
	CHECK(NoLeftovers(in, false));
	Clean(in);

	// Neither a link nor a copy: the update stops, everything as before, and
	// the executable is never moved aside (the old two-rename fallback).
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	{
		FsOps ops = FaultOps();
		ops.linkFile = NoLink;
		g_faults = Faults();
		g_faults.failWriteOf = OldPath(in.exe);
		CHECK_EQ(Apply(ops, in.exe, staged, "v0.3.1"), ERR_IO);
		CHECK(OldState(in) && NoLeftovers(in, false));
		CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	}
	Clean(in);

	// Power loss at every step with the copy fallback: the executable path
	// always holds a file, and recovery lands on the old or the new install.
	int points = 0;
	for (int limit = 0; limit < 64; limit++)
	{
		in = MakeInstall(dir);
		CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
		FsOps ops = CrashOps(true);
		ops.linkFile = NoLink;
		g_crash = Crash();
		g_crash.limit = limit;
		const Result r = Apply(ops, in.exe, staged, "v0.3.1");
		if (!g_crash.dead)
		{
			CHECK_EQ(r, OK);
			Clean(in);
			break;
		}
		points++;
		CHECK(real.exists(nullptr, in.exe));
		const bool newExe = ReadText(in.exe) == "NEW EXE";
		const StartupState stt = StartupCheck(real, in.exe, newExe ? "v0.3.1" : "v0.3.0");
		if (newExe)
			CHECK(stt == STARTUP_UPDATED && NewState(in));
		else
			CHECK((stt == STARTUP_RECOVERED || stt == STARTUP_CLEAN) && OldState(in) && NoLeftovers(in, false));
		Clean(in);
	}
	CHECK(points > 15);

	// A marker from an earlier update that cannot be removed stops the swap
	// before the journal is written: nothing changes.
	in = MakeInstall(dir);
	WriteText(MarkerPath(in.exe), "v0.3.0\nctr.apworld\n");
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	{
		FsOps ops = real;
		ops.removeFile = FRemove;
		g_failRemoveOf = MarkerPath(in.exe);
		CHECK_EQ(Apply(ops, in.exe, staged, "v0.3.1"), ERR_IO);
		g_failRemoveOf.clear();
		CHECK(OldState(in));
		CHECK(!real.exists(nullptr, SwapJournalPath(in.exe)));
		CHECK(!real.exists(nullptr, StagedPath(in.exe)) && !real.exists(nullptr, JournalPath(in.exe)));
	}
	real.removeFile(nullptr, MarkerPath(in.exe));
	Clean(in);

	// A marker of another release beside a journal is not this swap's finish:
	// the cut-short swap is undone, not accepted.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	{
		g_crash = Crash();
		g_crash.limit = 4; // journal written, some files swapped
		Apply(CrashOps(true), in.exe, staged, "v0.3.1");
		CHECK(g_crash.dead && real.exists(nullptr, SwapJournalPath(in.exe)));
		WriteText(MarkerPath(in.exe), "v0.3.0\n");
		CHECK(ReadText(in.exe) == "OLD EXE");
		CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_RECOVERED);
		CHECK(OldState(in) && NoLeftovers(in, false));
		CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	}
	Clean(in);
	// Even this release's tag does not count when the files are not all new.
	in = MakeInstall(dir);
	CHECK_EQ(Stage(real, in.exe, NewSet(), staged), OK);
	{
		g_crash = Crash();
		g_crash.limit = 4;
		Apply(CrashOps(true), in.exe, staged, "v0.3.1");
		WriteText(MarkerPath(in.exe), "v0.3.1\n");
		CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_RECOVERED);
		CHECK(OldState(in) && NoLeftovers(in, false));
		CHECK(!real.exists(nullptr, MarkerPath(in.exe)));
	}
	Clean(in);

	// A folder that cannot be flushed fails the write (here: it cannot even
	// be opened for reading), so staging stops before anything is replaced.
	if (geteuid() != 0)
	{
		const std::string locked = dir + "/locked";
		mkdir(locked.c_str(), 0700);
		chmod(locked.c_str(), 0300); // write and search, no read: open(O_DIRECTORY) fails
		CHECK(!real.writeFile(nullptr, locked + "/f", B("x")));
		chmod(locked.c_str(), 0700);
		real.removeFile(nullptr, locked + "/f");
		rmdir(locked.c_str());
	}

	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	rmdir((dir + "/assets").c_str());
	rmdir((dir + "/memcards").c_str());
	CHECK(rmdir(dir.c_str()) == 0);
}

// ── solo mode (#89): solo-runtime/ replaced whole, solo/ never touched ──

static const char kRtId[] = "solo-runtime/RUNTIME.txt";
static const char kRtPy[] = "solo-runtime/python/bin/python3.12";
static const char kRtMs[] = "solo-runtime/archipelago/MultiServer.py";

static void MakeDirs(const std::string &path)
{
	for (size_t i = 1; i <= path.size(); i++)
		if (i == path.size() || path[i] == '/')
			mkdir(path.substr(0, i).c_str(), 0755);
}

static void Put(const std::string &dir, const std::string &rel, const std::string &text)
{
	const size_t slash = (dir + "/" + rel).find_last_of('/');
	MakeDirs((dir + "/" + rel).substr(0, slash));
	WriteText(dir + "/" + rel, text);
}

// An install with an older runtime (one file the new runtime no longer has)
// and the player's solo folder.
static void AddRuntime(const Install &in)
{
	Put(in.dir, kRtId, "OLD RT");
	Put(in.dir, kRtPy, "OLD PY");
	Put(in.dir, "solo-runtime/stale.py", "STALE");
	chmod((in.dir + "/" + kRtPy).c_str(), 0755);
	Put(in.dir, "solo/rooms/r1/room.json", "ROOM");
	Put(in.dir, "solo/saved/me.yaml", "YAML");
}

static std::vector<InstallFile> RuntimeSet()
{
	std::vector<InstallFile> set = NewSet();
	set.push_back(IF(kRtId, "NEW RT"));
	InstallFile py = IF(kRtPy, "NEW PY");
	py.executable = true;
	set.push_back(py);
	set.push_back(IF(kRtMs, "NEW MS"));
	return set;
}

static bool SoloUntouched(const Install &in)
{
	return ReadText(in.dir + "/solo/rooms/r1/room.json") == "ROOM" && ReadText(in.dir + "/solo/saved/me.yaml") == "YAML" &&
	       !RealFsOps().exists(nullptr, in.dir + "/solo.old") && !RealFsOps().exists(nullptr, in.dir + "/solo.update");
}

static bool OldRuntime(const Install &in)
{
	return ReadText(in.dir + "/" + kRtId) == "OLD RT" && ReadText(in.dir + "/" + kRtPy) == "OLD PY" &&
	       ReadText(in.dir + "/solo-runtime/stale.py") == "STALE" &&
	       !RealFsOps().exists(nullptr, in.dir + "/" + kRtMs);
}

static bool NewRuntime(const Install &in)
{
	struct stat st;
	return ReadText(in.dir + "/" + kRtId) == "NEW RT" && ReadText(in.dir + "/" + kRtPy) == "NEW PY" &&
	       ReadText(in.dir + "/" + kRtMs) == "NEW MS" &&
	       !RealFsOps().exists(nullptr, in.dir + "/solo-runtime/stale.py") &&
	       stat((in.dir + "/" + kRtPy).c_str(), &st) == 0 && (st.st_mode & 0111) != 0;
}

static bool NoRuntimeLeftovers(const Install &in, bool allowOld)
{
	const FsOps fs = RealFsOps();
	return !fs.exists(nullptr, in.dir + "/solo-runtime.update") &&
	       (allowOld || !fs.exists(nullptr, in.dir + "/solo-runtime.old"));
}

static void RemoveAll(const std::string &path)
{
	const std::string rm = "rm -rf '" + path + "'";
	CHECK(std::system(rm.c_str()) == 0);
}

static void CleanRuntime(const Install &in)
{
	Clean(in);
	for (const char *d : {"solo-runtime", "solo-runtime.old", "solo-runtime.update", "solo"})
		RemoveAll(in.dir + "/" + d);
}

static void TestSoloRuntime()
{
	// Install set: every runtime file at any depth, never solo/ or look-alikes.
	for (int pi = 0; pi < 2; pi++)
	{
		const Platform p = (Platform)pi;
		std::vector<Member> m = GoodMembers(p);
		m.push_back(File(kRoot + kRtId, "rt"));
		Member py = File(kRoot + kRtPy, "py");
		py.unixMode = 0100755;
		m.push_back(py);
		m.push_back(File(kRoot + "solo-runtime/python/lib/python3.12/os.py", "os"));
		m.push_back(File(kRoot + "solo/rooms/x/room.json", "room"));
		m.push_back(File(kRoot + "solo-runtimeX/a", "no"));
		m.push_back(File(kRoot + "solo-runtime.old/a", "no"));
		std::vector<InstallFile> set;
		CHECK_EQ(ExtractInstallSet(Archive(p, m), p, kTag, set), OK);
		CHECK_EQ(set.size(), 6);
		size_t runtime = 0;
		for (const InstallFile &f : set)
		{
			runtime += IsRuntimePath(f.rel);
			CHECK(f.rel.compare(0, 5, "solo/") != 0 && f.rel.compare(0, 13, "solo-runtimeX") != 0 &&
			      f.rel.compare(0, 16, "solo-runtime.old") != 0);
			if (f.rel == kRtPy && p == PLATFORM_LINUX)
				CHECK(f.executable); // the tar mode carries the interpreter's x bit
			if (f.rel == kRtId)
				CHECK(!f.executable);
		}
		CHECK_EQ(runtime, 3);
		// A runtime without RUNTIME.txt cannot be told apart from an older one.
		m = GoodMembers(p);
		m.push_back(File(kRoot + kRtPy, "py"));
		CHECK_EQ(ExtractInstallSet(Archive(p, m), p, kTag, set), ERR_ARCHIVE);
		CHECK(!IsInstallPath("solo/rooms/r/room.json", p) && !IsInstallPath("solo-runtime", p) &&
		      !IsInstallPath("solo-runtime/", p) && !IsInstallPath("Solo-Runtime/x", p) && IsInstallPath(kRtMs, p));
	}

	char tmpl[] = "/tmp/ctr-update-solo-XXXXXX";
	const char *dirp = mkdtemp(tmpl);
	CHECK(dirp != nullptr);
	if (!dirp)
		return;
	const std::string dir = dirp;
	const FsOps real = RealFsOps();
	std::vector<StagedFile> staged;

	// Happy path: the runtime folder is replaced whole (the stale file goes),
	// solo/ is untouched, the old runtime waits as solo-runtime.old.
	Install in = MakeInstall(dir);
	AddRuntime(in);
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	CHECK(ReadText(dir + "/solo-runtime.update/python/bin/python3.12") == "NEW PY" && OldRuntime(in));
	CHECK(ReadText(JournalPath(in.exe)).find("solo-runtime/\n") != std::string::npos);
	CHECK(ReadText(JournalPath(in.exe)).find(kRtPy) == std::string::npos); // one entry for the folder
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(NewState(in) && NewRuntime(in) && SoloUntouched(in));
	CHECK(ReadText(dir + "/solo-runtime.old/stale.py") == "STALE");
	CHECK(ReadText(MarkerPath(in.exe)).find("solo-runtime/\n") != std::string::npos);
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_UPDATED);
	ConfirmStarted(real, in.exe);
	CHECK(NoLeftovers(in, false) && NoRuntimeLeftovers(in, false) && NewRuntime(in) && SoloUntouched(in));
	CleanRuntime(in);

	// No runtime installed yet (a client older than solo mode): it is added,
	// and an undo removes it again.
	in = MakeInstall(dir);
	Put(dir, "solo/rooms/r1/room.json", "ROOM");
	Put(dir, "solo/saved/me.yaml", "YAML");
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	CHECK(NewRuntime(in) && SoloUntouched(in) && !real.exists(nullptr, dir + "/solo-runtime.old"));
	ConfirmStarted(real, in.exe);
	CleanRuntime(in);

	// A failure at either folder rename, or at the executable after the
	// folder moved: everything back, the old runtime in place, nothing staged.
	{
		const std::string rt = dir + "/solo-runtime";
		struct Case
		{
			std::vector<std::string> from;
			std::string to;
		};
		const Case cases[] = {{{StagedPath(rt)}, ""}, {{}, OldPath(rt)}, {{StagedPath(dir + "/ctr_native_ap")}, ""}};
		for (int strategy = 0; strategy < 2; strategy++)
			for (const Case &c : cases)
			{
				in = MakeInstall(dir);
				AddRuntime(in);
				CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
				g_faults = Faults();
				g_faults.failRenameFrom = c.from;
				g_faults.failRenameTo = c.to;
				CHECK_EQ(Apply(FaultOps(strategy == 0), in.exe, staged, "v0.3.1"), ERR_IO);
				CHECK(OldState(in) && OldRuntime(in) && SoloUntouched(in));
				CHECK(NoLeftovers(in, false) && NoRuntimeLeftovers(in, false));
				CleanRuntime(in);
			}
		g_faults = Faults();
	}

	// A staged runtime file changed after staging: nothing moves.
	in = MakeInstall(dir);
	AddRuntime(in);
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	WriteText(dir + "/solo-runtime.update/archipelago/MultiServer.py", "TAMPERED");
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), ERR_STAGED_CHANGED);
	CHECK(OldState(in) && OldRuntime(in) && NoRuntimeLeftovers(in, false));
	// A staged runtime left by an earlier session is removed at launch.
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_CLEAN);
	CHECK(OldRuntime(in) && NoRuntimeLeftovers(in, false) && NoLeftovers(in, false) && SoloUntouched(in));
	CleanRuntime(in);

	// A linked runtime folder is refused before anything is written, so a
	// shared or outside folder is never replaced or written into.
	{
		char shared[] = "/tmp/ctr-update-rtlink-XXXXXX";
		const char *sh = mkdtemp(shared);
		CHECK(sh != nullptr);
		in = MakeInstall(dir);
		WriteText(std::string(sh) + "/RUNTIME.txt", "SHARED");
		CHECK(symlink(sh, (dir + "/solo-runtime").c_str()) == 0);
		CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), ERR_IO);
		CHECK(ReadText(std::string(sh) + "/RUNTIME.txt") == "SHARED" && OldState(in));
		CHECK(NoLeftovers(in, false) && NoRuntimeLeftovers(in, false));
		unlink((dir + "/solo-runtime").c_str());
		// A link inside an old rollback folder is removed, never followed.
		Put(dir, "solo-runtime/RUNTIME.txt", "OLD RT");
		MakeDirs(dir + "/solo-runtime.old");
		CHECK(symlink(sh, (dir + "/solo-runtime.old/escape").c_str()) == 0);
		CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
		CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
		CHECK(ReadText(std::string(sh) + "/RUNTIME.txt") == "SHARED");
		CHECK(ReadText(dir + "/solo-runtime.old/RUNTIME.txt") == "OLD RT");
		ConfirmStarted(real, in.exe);
		CHECK(!real.exists(nullptr, dir + "/solo-runtime.old"));
		RemoveAll(sh);
		CleanRuntime(in);
	}

	// Journal and marker entries: the runtime folder only as a whole; a
	// runtime file, solo/ or a path that climbs out is never acted on.
	in = MakeInstall(dir);
	Put(dir, "solo.old/keep", "KEEP");
	Put(dir, "solo-runtime.old/x", "KEEP");
	WriteText(MarkerPath(in.exe), "v0.3.0\nsolo/\nsolo-runtime/x\nsolo-runtime/../solo/\n");
	ConfirmStarted(real, in.exe);
	CHECK(ReadText(dir + "/solo.old/keep") == "KEEP" && ReadText(dir + "/solo-runtime.old/x") == "KEEP");
	RemoveAll(dir + "/solo.old");
	CleanRuntime(in);

	// A renamed folder that cannot be flushed: no marker, everything undone,
	// and since the undo cannot be flushed either the journal stays for the
	// next launch, which finds the previous files and closes it.
	in = MakeInstall(dir);
	AddRuntime(in);
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	{
		FsOps ops = real;
		ops.syncDir = [](const std::string &) { return false; };
		CHECK_EQ(Apply(ops, in.exe, staged, "v0.3.1"), ERR_UNDO_FAILED);
		CHECK(OldState(in) && OldRuntime(in) && SoloUntouched(in));
		CHECK(!real.exists(nullptr, MarkerPath(in.exe)) && real.exists(nullptr, SwapJournalPath(in.exe)));
		CHECK_EQ(StartupCheck(real, in.exe, "v0.3.0"), STARTUP_RECOVERED);
		CHECK(OldState(in) && OldRuntime(in) && NoLeftovers(in, false) && NoRuntimeLeftovers(in, false));
		// Only the final flush failing (the undo's own flushes work): ERR_IO, no journal.
		static int calls;
		calls = 0;
		CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
		ops.syncDir = [](const std::string &p) { return ++calls != 1 && RealFsOps().syncDir(p); };
		CHECK_EQ(Apply(ops, in.exe, staged, "v0.3.1"), ERR_IO);
		CHECK(OldState(in) && OldRuntime(in) && NoLeftovers(in, false) && NoRuntimeLeftovers(in, false));
		// A staged folder that cannot be flushed fails staging, nothing left.
		CHECK_EQ(Stage(ops, in.exe, RuntimeSet(), staged), OK); // calls is past 1 now
		ops.syncDir = [](const std::string &) { return false; };
		CHECK_EQ(Stage(ops, in.exe, RuntimeSet(), staged), ERR_IO);
		CHECK(staged.empty() && OldState(in) && NoLeftovers(in, false) && NoRuntimeLeftovers(in, false));
	}
	CleanRuntime(in);

	// A finished update whose runtime rollback copy is still held: the next
	// update clears it first, and if it still cannot, keeps the marker and
	// changes nothing.
	in = MakeInstall(dir);
	AddRuntime(in);
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	{
		const std::vector<InstallFile> next = {IF("ctr_native_ap", "NEWER EXE"), IF(kRtId, "NEWER RT")};
		FsOps held = real;
		held.removeTree = [](void *, const std::string &) { return false; };
		CHECK_EQ(Stage(real, in.exe, next, staged), OK);
		CHECK_EQ(Apply(held, in.exe, staged, "v0.3.2"), ERR_IO);
		CHECK(real.exists(nullptr, MarkerPath(in.exe)) && real.exists(nullptr, dir + "/solo-runtime.old"));
		CHECK(NewState(in) && NewRuntime(in) && !real.exists(nullptr, SwapJournalPath(in.exe)));
		// Released: the next attempt clears the copy and installs.
		CHECK_EQ(Stage(real, in.exe, next, staged), OK);
		CHECK_EQ(Apply(real, in.exe, staged, "v0.3.2"), OK);
		CHECK(ReadText(in.exe) == "NEWER EXE" && ReadText(dir + "/" + kRtId) == "NEWER RT");
		CHECK(ReadText(dir + "/solo-runtime.old/RUNTIME.txt") == "NEW RT");
		ConfirmStarted(real, in.exe);
	}
	CleanRuntime(in);

	// A rollback copy that cannot be removed keeps the marker for a retry.
	in = MakeInstall(dir);
	AddRuntime(in);
	CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
	CHECK_EQ(Apply(real, in.exe, staged, "v0.3.1"), OK);
	{
		FsOps ops = real;
		ops.removeTree = [](void *, const std::string &) { return false; };
		ConfirmStarted(ops, in.exe);
		CHECK(real.exists(nullptr, MarkerPath(in.exe)) && real.exists(nullptr, dir + "/solo-runtime.old"));
		CHECK_EQ(StartupCheck(real, in.exe, "v0.3.1"), STARTUP_UPDATED);
		ConfirmStarted(real, in.exe);
		CHECK(!real.exists(nullptr, MarkerPath(in.exe)) && NoRuntimeLeftovers(in, false) && NewRuntime(in));
	}
	CleanRuntime(in);

	// Tree removal of a path that is itself a link removes only the link.
	{
		char shared[] = "/tmp/ctr-update-rtlink2-XXXXXX";
		const char *sh = mkdtemp(shared);
		CHECK(sh != nullptr);
		Put(sh, "deep/keep.txt", "KEEP");
		CHECK(symlink(sh, (dir + "/solo-runtime.old").c_str()) == 0);
		CHECK(real.removeTree(nullptr, dir + "/solo-runtime.old"));
		CHECK(ReadText(std::string(sh) + "/deep/keep.txt") == "KEEP");
		char lb[8];
		CHECK(readlink((dir + "/solo-runtime.old").c_str(), lb, sizeof lb) < 0);
		RemoveAll(sh);
	}

	// Power loss at every step with the runtime in the set, both executable
	// strategies: the old install (runtime included) or the new one, never a mix.
	int points = 0;
	for (int strategy = 0; strategy < 2; strategy++)
	{
		for (int limit = 0; limit < 80; limit++)
		{
			in = MakeInstall(dir);
			AddRuntime(in);
			CHECK_EQ(Stage(real, in.exe, RuntimeSet(), staged), OK);
			FsOps ops = CrashOps(strategy == 0);
			ops.removeTree = [](void *c, const std::string &p) { return Alive() && RealFsOps().removeTree(c, p); };
			g_crash = Crash();
			g_crash.limit = limit;
			const Result r = Apply(ops, in.exe, staged, "v0.3.1");
			if (!g_crash.dead)
			{
				CHECK_EQ(r, OK);
				CHECK(NewState(in) && NewRuntime(in));
				CleanRuntime(in);
				break;
			}
			points++;
			const bool newExe = ReadText(in.exe) == "NEW EXE";
			const StartupState stt = StartupCheck(real, in.exe, newExe ? "v0.3.1" : "v0.3.0");
			if (newExe)
			{
				CHECK(stt == STARTUP_UPDATED);
				CHECK(NewState(in) && NewRuntime(in));
				ConfirmStarted(real, in.exe);
				CHECK(NoRuntimeLeftovers(in, false));
			}
			else
			{
				CHECK(stt == STARTUP_RECOVERED || stt == STARTUP_CLEAN);
				CHECK(OldState(in) && OldRuntime(in));
				CHECK(NoLeftovers(in, false) && NoRuntimeLeftovers(in, false));
			}
			CHECK(SoloUntouched(in));
			CleanRuntime(in);
		}
	}
	CHECK(points > 25);

	rmdir((dir + "/assets/tracks/cortex-vortex").c_str());
	rmdir((dir + "/assets/tracks").c_str());
	rmdir((dir + "/assets").c_str());
	rmdir((dir + "/memcards").c_str());
	CHECK(rmdir(dir.c_str()) == 0);
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
	TestCrashRecovery();
	TestPartialUndoRecovery();
	TestStagingAndMarker();
	TestExecutableSwapFallbacks();
	TestSoloRuntime();
	TestChunkOverflow();
	TestWorkGate();
	TestAllowlist();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
