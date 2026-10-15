// Signed in-game updater, trust core (issue #301). See ap_update_core.h.

#include "ap_update_core.h"
#include "ap_version_cmp.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ap_update
{

const char *ResultText(Result r)
{
	switch (r)
	{
	case OK: return "ok";
	case ERR_KEY: return "built-in public key unusable";
	case ERR_SIG_FORMAT: return "signature file malformed";
	case ERR_SIG_KEY_ID: return "signature made by another key";
	case ERR_SIGNATURE: return "manifest signature invalid";
	case ERR_COMMENT_SIGNATURE: return "trusted comment signature invalid";
	case ERR_MANIFEST: return "manifest malformed";
	case ERR_TAG_MISMATCH: return "signed version is not the offered release";
	case ERR_NOT_NEWER: return "signed version is not newer than this build";
	case ERR_HASH: return "archive SHA-256 mismatch";
	case ERR_ARCHIVE: return "archive contents refused";
	case ERR_SIZE: return "size limit exceeded";
	case ERR_IO: return "file operation failed";
	case ERR_STAGED_CHANGED: return "staged file changed before install";
	}
	return "unknown";
}

// ── primitives ──

bool Base64Decode(const std::string &text, Bytes &out)
{
	static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	out.clear();
	if (text.empty() || text.size() % 4 != 0)
		return false;
	unsigned int acc = 0;
	int bits = 0;
	size_t pad = 0;
	for (size_t i = 0; i < text.size(); i++)
	{
		const char c = text[i];
		if (c == '=')
		{
			// Padding only in the last two positions, and nothing after it.
			if (i + 2 < text.size())
				return false;
			pad++;
			continue;
		}
		if (pad)
			return false;
		const char *p = std::strchr(alphabet, c);
		if (!p || c == '\0')
			return false;
		acc = (acc << 6) | (unsigned int)(p - alphabet);
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back((unsigned char)((acc >> bits) & 0xFF));
		}
	}
	if (pad > 2)
		return false;
	// Leftover bits must be zero (canonical encoding).
	if (bits > 0 && (acc & ((1u << bits) - 1)) != 0)
		return false;
	return true;
}

std::string Sha256Hex(const unsigned char *data, size_t size)
{
	unsigned char digest[32];
	unsigned int len = 0;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	bool ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
	          EVP_DigestUpdate(ctx, data, size) == 1 && EVP_DigestFinal_ex(ctx, digest, &len) == 1 &&
	          len == 32;
	if (ctx)
		EVP_MD_CTX_free(ctx);
	if (!ok)
		return std::string();
	static const char hex[] = "0123456789abcdef";
	std::string s(64, '0');
	for (int i = 0; i < 32; i++)
	{
		s[i * 2] = hex[digest[i] >> 4];
		s[i * 2 + 1] = hex[digest[i] & 15];
	}
	return s;
}

static bool IsLowerHex64(const std::string &s)
{
	if (s.size() != 64)
		return false;
	for (char c : s)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}

bool IsStableTag(const std::string &tag)
{
	if (tag.size() < 6 || tag.size() > 32 || tag[0] != 'v')
		return false;
	int parts = 0;
	size_t i = 1;
	while (i < tag.size())
	{
		size_t digits = 0;
		while (i < tag.size() && tag[i] >= '0' && tag[i] <= '9')
		{
			i++;
			digits++;
		}
		if (digits == 0 || digits > 6)
			return false;
		parts++;
		if (i == tag.size())
			break;
		if (tag[i] != '.')
			return false;
		i++;
		if (i == tag.size())
			return false;
	}
	return parts == 3;
}

std::string ArchiveName(const std::string &tag, Platform platform)
{
	return "ctr-archipelago-" + tag + (platform == PLATFORM_WINDOWS ? "-windows-x86.zip" : "-linux-x86.tar.gz");
}

const char *ExecutableName(Platform platform)
{
	return platform == PLATFORM_WINDOWS ? "ctr_native_ap.exe" : "ctr_native_ap";
}

// ── signature ──

bool ParsePublicKey(const std::string &base64, PublicKey &out)
{
	Bytes raw;
	if (!Base64Decode(base64, raw) || raw.size() != 42 || raw[0] != 'E' || raw[1] != 'd')
		return false;
	std::memcpy(out.keyId, &raw[2], 8);
	std::memcpy(out.key, &raw[10], 32);
	return true;
}

static bool Ed25519Verify(const unsigned char key[32], const unsigned char sig[64],
                          const unsigned char *msg, size_t len)
{
	EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key, 32);
	if (!pkey)
		return false;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1 &&
	          EVP_DigestVerify(ctx, sig, 64, msg, len) == 1;
	if (ctx)
		EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	return ok;
}

static bool Blake2b512(const Bytes &msg, unsigned char out[64])
{
	unsigned int len = 0;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	bool ok = ctx && EVP_DigestInit_ex(ctx, EVP_blake2b512(), nullptr) == 1 &&
	          EVP_DigestUpdate(ctx, msg.data(), msg.size()) == 1 &&
	          EVP_DigestFinal_ex(ctx, out, &len) == 1 && len == 64;
	if (ctx)
		EVP_MD_CTX_free(ctx);
	return ok;
}

static bool SplitLines(const std::string &text, std::vector<std::string> &lines)
{
	lines.clear();
	size_t start = 0;
	while (start < text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.find('\r') != std::string::npos || line.find('\0') != std::string::npos)
			return false;
		lines.push_back(line);
		start = end + 1;
	}
	return true;
}

Result VerifyMinisign(const PublicKey &key, const Bytes &message, const std::string &sigFile,
                      std::string *trustedComment)
{
	static const char kUntrusted[] = "untrusted comment: ";
	static const char kTrusted[] = "trusted comment: ";
	std::vector<std::string> lines;
	if (sigFile.size() > kMaxSignature || !SplitLines(sigFile, lines))
		return ERR_SIG_FORMAT;
	// Exactly four lines; a trailing empty line from the final newline is fine.
	while (!lines.empty() && lines.back().empty())
		lines.pop_back();
	if (lines.size() != 4)
		return ERR_SIG_FORMAT;
	if (lines[0].compare(0, sizeof kUntrusted - 1, kUntrusted) != 0 ||
	    lines[2].compare(0, sizeof kTrusted - 1, kTrusted) != 0)
		return ERR_SIG_FORMAT;

	Bytes sig, global;
	if (!Base64Decode(lines[1], sig) || sig.size() != 74 || !Base64Decode(lines[3], global) ||
	    global.size() != 64)
		return ERR_SIG_FORMAT;

	const bool prehashed = sig[0] == 'E' && sig[1] == 'D';
	const bool legacy = sig[0] == 'E' && sig[1] == 'd';
	if (!prehashed && !legacy)
		return ERR_SIG_FORMAT;
	if (std::memcmp(&sig[2], key.keyId, 8) != 0)
		return ERR_SIG_KEY_ID;

	if (prehashed)
	{
		unsigned char digest[64];
		if (!Blake2b512(message, digest) || !Ed25519Verify(key.key, &sig[10], digest, 64))
			return ERR_SIGNATURE;
	}
	else if (!Ed25519Verify(key.key, &sig[10], message.data(), message.size()))
	{
		return ERR_SIGNATURE;
	}

	// The global signature covers the file signature and the trusted comment.
	const std::string comment = lines[2].substr(sizeof kTrusted - 1);
	Bytes signedComment(sig.begin() + 10, sig.end());
	signedComment.insert(signedComment.end(), comment.begin(), comment.end());
	if (!Ed25519Verify(key.key, global.data(), signedComment.data(), signedComment.size()))
		return ERR_COMMENT_SIGNATURE;
	if (trustedComment)
		*trustedComment = comment;
	return OK;
}

// ── JSON helpers ──

// nlohmann keeps the last of two equal keys; a strict reader must refuse them.
static bool ParseJsonStrict(const Bytes &raw, nlohmann::json &out)
{
	std::vector<std::set<std::string>> stack;
	bool duplicate = false;
	auto callback = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &parsed) -> bool {
		(void)depth;
		switch (event)
		{
		case nlohmann::json::parse_event_t::object_start:
			stack.emplace_back();
			break;
		case nlohmann::json::parse_event_t::object_end:
			if (!stack.empty())
				stack.pop_back();
			break;
		case nlohmann::json::parse_event_t::key:
			if (!stack.empty() && !stack.back().insert(parsed.get<std::string>()).second)
				duplicate = true;
			break;
		default:
			break;
		}
		return true;
	};
	try
	{
		out = nlohmann::json::parse(raw.begin(), raw.end(), callback, true);
	}
	catch (...)
	{
		return false;
	}
	return !duplicate;
}

Result ParseManifest(const Bytes &raw, Manifest &out)
{
	nlohmann::json j;
	if (raw.empty() || raw.size() > kMaxManifest || !ParseJsonStrict(raw, j) || !j.is_object() ||
	    j.size() != 2 || !j.contains("version") || !j.contains("artifacts"))
		return ERR_MANIFEST;
	const nlohmann::json &version = j["version"];
	const nlohmann::json &artifacts = j["artifacts"];
	if (!version.is_string() || !artifacts.is_object() || artifacts.size() != 2)
		return ERR_MANIFEST;
	const std::string tag = version.get<std::string>();
	// The manifest contract allows prerelease tails; the updater only ever
	// installs a plain stable version, so anything else is not for us.
	if (!IsStableTag(tag))
		return ERR_MANIFEST;
	const std::string win = ArchiveName(tag, PLATFORM_WINDOWS);
	const std::string lin = ArchiveName(tag, PLATFORM_LINUX);
	if (!artifacts.contains(win) || !artifacts.contains(lin) || !artifacts[win].is_string() ||
	    !artifacts[lin].is_string())
		return ERR_MANIFEST;
	out.version = tag;
	out.windowsSha256 = artifacts[win].get<std::string>();
	out.linuxSha256 = artifacts[lin].get<std::string>();
	if (!IsLowerHex64(out.windowsSha256) || !IsLowerHex64(out.linuxSha256))
		return ERR_MANIFEST;
	return OK;
}

bool ParseLatestRelease(const Bytes &json, ReleaseInfo &out)
{
	nlohmann::json j;
	if (json.empty() || json.size() > kMaxReleaseJson || !ParseJsonStrict(json, j) || !j.is_object())
		return false;
	try
	{
		if (!j.contains("tag_name") || !j["tag_name"].is_string())
			return false;
		if (!j.contains("draft") || !j["draft"].is_boolean() || j["draft"].get<bool>())
			return false;
		if (!j.contains("prerelease") || !j["prerelease"].is_boolean() || j["prerelease"].get<bool>())
			return false;
		const std::string tag = j["tag_name"].get<std::string>();
		if (!IsStableTag(tag) || !j.contains("assets") || !j["assets"].is_array())
			return false;
		std::set<std::string> names;
		for (const auto &asset : j["assets"])
			if (asset.is_object() && asset.contains("name") && asset["name"].is_string())
				names.insert(asset["name"].get<std::string>());
		if (!names.count("manifest.json") || !names.count("manifest.json.minisig") ||
		    !names.count(ArchiveName(tag, PLATFORM_WINDOWS)) || !names.count(ArchiveName(tag, PLATFORM_LINUX)))
			return false;
		out.tag = tag;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool ShouldOffer(const std::string &tag, const std::string &running, const std::string &declined)
{
	if (!IsStableTag(tag) || !AP_VersionNewer(tag.c_str(), running.c_str()))
		return false;
	// A declined version stays declined; only a newer one is offered again. An
	// unreadable remembered value (hand-edited config) declines nothing.
	if (IsStableTag(declined) && !AP_VersionNewer(tag.c_str(), declined.c_str()))
		return false;
	return true;
}

Result VerifyRelease(const std::string &publicKeyBase64, const Bytes &manifest,
                     const std::string &signature, const std::string &offeredTag,
                     const std::string &runningVersion, Platform platform, const Bytes &archive)
{
	PublicKey key;
	if (!ParsePublicKey(publicKeyBase64, key))
		return ERR_KEY;
	if (manifest.size() > kMaxManifest)
		return ERR_SIZE;
	// 1. Signature over the exact bytes, before any field is read.
	Result r = VerifyMinisign(key, manifest, signature, nullptr);
	if (r != OK)
		return r;
	// 2. Shape.
	Manifest m;
	r = ParseManifest(manifest, m);
	if (r != OK)
		return r;
	// 3. The offered release, and strictly newer than this build.
	if (m.version != offeredTag)
		return ERR_TAG_MISMATCH;
	if (!AP_VersionNewer(m.version.c_str(), runningVersion.c_str()))
		return ERR_NOT_NEWER;
	// 4. Archive bytes.
	if (archive.size() > kMaxArchive)
		return ERR_SIZE;
	const std::string &want = platform == PLATFORM_WINDOWS ? m.windowsSha256 : m.linuxSha256;
	if (Sha256Hex(archive) != want)
		return ERR_HASH;
	return OK;
}

// ── archive ──

static uint32_t Le16(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t Le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// A member path relative to the archive root. `isDir` members may end in '/'.
static bool MemberPathOk(std::string name, const std::string &root, bool isDir, std::string &rel)
{
	if (name.empty() || name.size() > 512)
		return false;
	for (unsigned char c : name)
		if (c < 0x20 || c == 0x7F || c == '\\' || c == ':')
			return false;
	if (isDir && name.back() == '/')
		name.pop_back();
	if (name == root)
	{
		if (!isDir)
			return false;
		rel.clear();
		return true;
	}
	if (name.compare(0, root.size() + 1, root + "/") != 0)
		return false;
	rel = name.substr(root.size() + 1);
	if (rel.empty() || rel.back() == '/')
		return false;
	size_t start = 0;
	while (start <= rel.size())
	{
		size_t end = rel.find('/', start);
		if (end == std::string::npos)
			end = rel.size();
		const std::string seg = rel.substr(start, end - start);
		if (seg.empty() || seg == "." || seg == "..")
			return false;
		start = end + 1;
	}
	return true;
}

static std::string LowerAscii(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

static bool Inflate(const unsigned char *data, size_t size, int windowBits, size_t maxOut, Bytes &out,
                    size_t *consumed)
{
	z_stream zs;
	std::memset(&zs, 0, sizeof zs);
	if (inflateInit2(&zs, windowBits) != Z_OK)
		return false;
	out.clear();
	zs.next_in = const_cast<Bytes::value_type *>(data);
	zs.avail_in = (uInt)size;
	unsigned char buf[65536];
	int ret = Z_OK;
	while (ret != Z_STREAM_END)
	{
		zs.next_out = buf;
		zs.avail_out = sizeof buf;
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END)
			break;
		const size_t got = sizeof buf - zs.avail_out;
		if (out.size() + got > maxOut)
		{
			ret = Z_MEM_ERROR;
			break;
		}
		out.insert(out.end(), buf, buf + got);
		if (ret == Z_OK && got == 0 && zs.avail_in == 0)
		{
			ret = Z_DATA_ERROR; // truncated stream
			break;
		}
	}
	if (consumed)
		*consumed = size - zs.avail_in;
	inflateEnd(&zs);
	return ret == Z_STREAM_END;
}

static const char kCortexDir[] = "assets/tracks/cortex-vortex/";

bool IsInstallPath(const std::string &rel, Platform platform)
{
	if (rel == ExecutableName(platform) || rel == "ctr.apworld")
		return true;
	// Files directly inside the shipped custom track folder, no deeper.
	const size_t n = sizeof kCortexDir - 1;
	return rel.size() > n && rel.compare(0, n, kCortexDir) == 0 && rel.find('/', n) == std::string::npos;
}

struct PendingMember
{
	std::string rel;
	size_t data, comp, size;
	uint32_t crc, method;
};

// Install set order: executable first, then the rest in archive order.
static Result FinishInstallSet(std::vector<InstallFile> &out, Platform platform)
{
	size_t exeCount = 0, total = 0;
	for (const InstallFile &f : out)
	{
		exeCount += f.rel == ExecutableName(platform);
		total += f.data.size();
		if (f.data.size() > kMaxExecutable)
			return ERR_SIZE;
	}
	if (exeCount != 1)
		return ERR_ARCHIVE;
	if (total > kMaxUnpacked)
		return ERR_SIZE;
	std::stable_partition(out.begin(), out.end(),
	                      [&](const InstallFile &f) { return f.rel == ExecutableName(platform); });
	if (out[0].data.empty())
		return ERR_ARCHIVE;
	for (InstallFile &f : out)
		f.sha256 = Sha256Hex(f.data);
	return OK;
}

static Result ExtractZip(const Bytes &a, const std::string &root, Platform platform, std::vector<InstallFile> &out)
{
	const size_t n = a.size();
	if (n < 22)
		return ERR_ARCHIVE;
	// End of central directory: the comment must run exactly to the end.
	size_t eocd = std::string::npos;
	const size_t lowest = n > 22 + 65535 ? n - 22 - 65535 : 0;
	for (size_t i = n - 22 + 1; i-- > lowest;)
		if (Le32(&a[i]) == 0x06054b50 && i + 22 + Le16(&a[i + 20]) == n)
		{
			eocd = i;
			break;
		}
	if (eocd == std::string::npos)
		return ERR_ARCHIVE;
	const unsigned char *e = &a[eocd];
	const uint32_t entries = Le16(e + 10);
	const uint32_t cdSize = Le32(e + 12);
	const uint32_t cdOff = Le32(e + 16);
	if (Le16(e + 4) != 0 || Le16(e + 6) != 0 || Le16(e + 8) != entries || entries == 0 ||
	    entries == 0xFFFF || cdOff == 0xFFFFFFFFu || entries > kMaxMembers ||
	    (uint64_t)cdOff + cdSize != eocd)
		return ERR_ARCHIVE;

	std::set<std::string> seen;
	size_t unpacked = 0;
	std::vector<PendingMember> pending;
	size_t p = cdOff;
	for (uint32_t k = 0; k < entries; k++)
	{
		if (p + 46 > eocd || Le32(&a[p]) != 0x02014b50)
			return ERR_ARCHIVE;
		const unsigned char *c = &a[p];
		const uint32_t madeBy = Le16(c + 4) >> 8;
		const uint32_t flags = Le16(c + 8);
		const uint32_t method = Le16(c + 10);
		const uint32_t crc = Le32(c + 16);
		const uint32_t comp = Le32(c + 20);
		const uint32_t size = Le32(c + 24);
		const uint32_t nameLen = Le16(c + 28), extraLen = Le16(c + 30), commentLen = Le16(c + 32);
		const uint32_t disk = Le16(c + 34);
		const uint32_t extAttr = Le32(c + 38);
		const uint32_t local = Le32(c + 42);
		if (p + 46 + nameLen + extraLen + commentLen > eocd)
			return ERR_ARCHIVE;
		const std::string name((const char *)c + 46, nameLen);
		p += 46 + nameLen + extraLen + commentLen;

		if ((flags & 0x0001) || (flags & 0x0040) || disk != 0 || comp == 0xFFFFFFFFu || size == 0xFFFFFFFFu ||
		    local == 0xFFFFFFFFu)
			return ERR_ARCHIVE; // encrypted, strong-encrypted, multi-disk or zip64
		if (method != 0 && method != 8)
			return ERR_ARCHIVE;
		if (madeBy == 3) // Unix attributes: only regular files and directories
		{
			const uint32_t type = (extAttr >> 16) & 0170000;
			if (type != 0 && type != 0100000 && type != 0040000)
				return ERR_ARCHIVE;
		}
		const bool isDir = !name.empty() && name.back() == '/';
		if (isDir && size != 0)
			return ERR_ARCHIVE;
		std::string rel;
		if (!MemberPathOk(name, root, isDir, rel))
			return ERR_ARCHIVE;
		if (!seen.insert(LowerAscii(name)).second)
			return ERR_ARCHIVE;
		unpacked += size;
		if (size > kMaxUnpacked || unpacked > kMaxUnpacked)
			return ERR_SIZE;

		// Local header must agree and its data must sit before the directory.
		if ((uint64_t)local + 30 > cdOff || Le32(&a[local]) != 0x04034b50)
			return ERR_ARCHIVE;
		const uint32_t lNameLen = Le16(&a[local + 26]), lExtraLen = Le16(&a[local + 28]);
		const uint64_t data = (uint64_t)local + 30 + lNameLen + lExtraLen;
		if (data + comp > cdOff || lNameLen != nameLen ||
		    std::memcmp(&a[local + 30], name.data(), nameLen) != 0 || Le16(&a[local + 8]) != method)
			return ERR_ARCHIVE;

		if (!isDir && IsInstallPath(rel, platform))
		{
			if (size > kMaxExecutable)
				return ERR_SIZE;
			pending.push_back(PendingMember{rel, (size_t)data, comp, size, crc, method});
		}
	}
	if (p != eocd)
		return ERR_ARCHIVE;

	for (const PendingMember &m : pending)
	{
		InstallFile f;
		f.rel = m.rel;
		if (m.method == 0)
		{
			if (m.comp != m.size)
				return ERR_ARCHIVE;
			f.data.assign(a.begin() + m.data, a.begin() + m.data + m.size);
		}
		else
		{
			size_t used = 0;
			if (!Inflate(&a[m.data], m.comp, -MAX_WBITS, kMaxExecutable, f.data, &used) || used != m.comp)
				return ERR_ARCHIVE;
		}
		if (f.data.size() != m.size || (m.size && crc32(0L, f.data.data(), (uInt)f.data.size()) != m.crc) ||
		    (!m.size && m.crc != 0))
			return ERR_ARCHIVE;
		out.push_back(std::move(f));
	}
	return FinishInstallSet(out, platform);
}

static bool OctalField(const unsigned char *f, size_t len, uint64_t &out)
{
	out = 0;
	size_t i = 0;
	while (i < len && f[i] == ' ')
		i++;
	size_t digits = 0;
	for (; i < len && f[i] >= '0' && f[i] <= '7'; i++, digits++)
	{
		out = (out << 3) | (uint64_t)(f[i] - '0');
		if (out > 0xFFFFFFFFull)
			return false;
	}
	for (; i < len; i++)
		if (f[i] != ' ' && f[i] != '\0')
			return false;
	return digits > 0;
}

static std::string CField(const unsigned char *f, size_t len)
{
	size_t n = 0;
	while (n < len && f[n])
		n++;
	return std::string((const char *)f, n);
}

static bool ParsePax(const unsigned char *d, size_t len, std::string &path)
{
	static const char *allowed[] = {"mtime", "atime", "ctime", "uid", "gid", "uname", "gname"};
	size_t p = 0;
	while (p < len)
	{
		size_t q = p;
		size_t recLen = 0;
		while (q < len && d[q] >= '0' && d[q] <= '9' && q - p < 8)
			recLen = recLen * 10 + (size_t)(d[q++] - '0');
		if (q >= len || d[q] != ' ' || recLen == 0 || p + recLen > len || d[p + recLen - 1] != '\n')
			return false;
		const std::string rec((const char *)d + q + 1, p + recLen - 1 - (q + 1));
		const size_t eq = rec.find('=');
		if (eq == std::string::npos || eq == 0)
			return false;
		const std::string key = rec.substr(0, eq);
		if (key == "path")
			path = rec.substr(eq + 1);
		else if (std::find_if(std::begin(allowed), std::end(allowed),
		                      [&](const char *a) { return key == a; }) == std::end(allowed))
			return false; // size, linkpath, GNU.sparse.* and the rest are refused
		p += recLen;
	}
	return true;
}

static Result ExtractTarGz(const Bytes &a, const std::string &root, Platform platform, std::vector<InstallFile> &out)
{
	Bytes tar;
	size_t used = 0;
	if (!Inflate(a.data(), a.size(), 16 + MAX_WBITS, kMaxUnpacked, tar, &used))
		return tar.size() >= kMaxUnpacked ? ERR_SIZE : ERR_ARCHIVE;
	if (used != a.size())
		return ERR_ARCHIVE; // trailing bytes after the gzip member
	if (tar.size() % 512 != 0)
		return ERR_ARCHIVE;

	std::set<std::string> seen;
	std::string paxPath;
	bool havePax = false;
	size_t members = 0;
	size_t p = 0;
	bool ended = false;
	while (p + 512 <= tar.size())
	{
		const unsigned char *h = &tar[p];
		bool zero = true;
		for (int i = 0; i < 512 && zero; i++)
			zero = h[i] == 0;
		if (zero)
		{
			// End of archive: everything after must be zero too.
			for (size_t i = p; i < tar.size(); i++)
				if (tar[i])
					return ERR_ARCHIVE;
			ended = true;
			break;
		}
		if (++members > kMaxMembers * 2)
			return ERR_ARCHIVE;
		uint64_t sum = 0, want = 0;
		for (int i = 0; i < 512; i++)
			sum += (i >= 148 && i < 156) ? ' ' : h[i];
		if (!OctalField(h + 148, 8, want) || want != sum)
			return ERR_ARCHIVE;
		if (std::memcmp(h + 257, "ustar", 5) != 0)
			return ERR_ARCHIVE;
		uint64_t size = 0;
		if (!OctalField(h + 124, 12, size) || size > kMaxUnpacked)
			return ERR_ARCHIVE;
		const char type = (char)h[156];
		const size_t dataOff = p + 512;
		const size_t padded = (size_t)((size + 511) / 512 * 512);
		if (dataOff + padded > tar.size())
			return ERR_ARCHIVE;

		if (type == 'x')
		{
			if (havePax || size > 65536)
				return ERR_ARCHIVE;
			paxPath.clear();
			if (!ParsePax(&tar[dataOff], (size_t)size, paxPath))
				return ERR_ARCHIVE;
			havePax = true;
			p = dataOff + padded;
			continue;
		}
		if (type != '0' && type != '\0' && type != '5')
			return ERR_ARCHIVE; // links, devices, FIFOs, GNU and global headers
		std::string name = CField(h, 100);
		const std::string prefix = CField(h + 345, 155);
		if (!prefix.empty())
			name = prefix + "/" + name;
		if (havePax && !paxPath.empty())
			name = paxPath;
		havePax = false;
		paxPath.clear();

		const bool isDir = type == '5';
		if (isDir && size != 0)
			return ERR_ARCHIVE;
		std::string rel;
		if (!MemberPathOk(name, root, isDir, rel))
			return ERR_ARCHIVE;
		std::string key = name;
		if (isDir && !key.empty() && key.back() == '/')
			key.pop_back();
		if (!seen.insert(LowerAscii(key)).second)
			return ERR_ARCHIVE;
		if (!isDir && IsInstallPath(rel, platform))
		{
			if (size > kMaxExecutable)
				return ERR_SIZE;
			InstallFile f;
			f.rel = rel;
			f.data.assign(tar.begin() + dataOff, tar.begin() + dataOff + (size_t)size);
			out.push_back(std::move(f));
		}
		p = dataOff + padded;
	}
	if (!ended || havePax)
		return ERR_ARCHIVE;
	return FinishInstallSet(out, platform);
}

Result ExtractInstallSet(const Bytes &archive, Platform platform, const std::string &tag,
                         std::vector<InstallFile> &out)
{
	out.clear();
	if (!IsStableTag(tag))
		return ERR_ARCHIVE;
	if (archive.size() > kMaxArchive)
		return ERR_SIZE;
	const std::string root = "ctr-archipelago-" + tag;
	Result r = platform == PLATFORM_WINDOWS ? ExtractZip(archive, root, platform, out)
	                                        : ExtractTarGz(archive, root, platform, out);
	if (r != OK)
		out.clear();
	return r;
}

Result ExtractExecutable(const Bytes &archive, Platform platform, const std::string &tag, Bytes &exeOut)
{
	std::vector<InstallFile> set;
	exeOut.clear();
	const Result r = ExtractInstallSet(archive, platform, tag, set);
	if (r == OK)
		exeOut.swap(set[0].data);
	return r;
}

// ── network topology ──

static const char kRepoPath[] = "/repos/dowlle/ctr-native-ap/releases/latest";
static const char kDownloadPrefix[] = "/dowlle/ctr-native-ap/releases/download/";

std::string LatestReleaseUrl()
{
	return std::string("https://api.github.com") + kRepoPath;
}

std::string AssetUrl(const std::string &tag, const std::string &name)
{
	return std::string("https://github.com") + kDownloadPrefix + tag + "/" + name;
}

bool AllowedUrl(const std::string &url, bool redirect, std::string &host, std::string &path)
{
	static const char scheme[] = "https://";
	if (url.size() > 4096 || url.compare(0, sizeof scheme - 1, scheme) != 0)
		return false;
	for (unsigned char c : url)
		if (c <= 0x20 || c >= 0x7F || c == '\\')
			return false;
	const size_t hostStart = sizeof scheme - 1;
	const size_t slash = url.find('/', hostStart);
	if (slash == std::string::npos)
		return false;
	host = LowerAscii(url.substr(hostStart, slash - hostStart));
	path = url.substr(slash);
	// No user info, no explicit port, no fragment.
	if (host.empty() || host.find_first_of("@:") != std::string::npos || path.find('#') != std::string::npos)
		return false;
	if (host == "api.github.com")
		return !redirect && path == kRepoPath;
	if (host == "github.com")
		return path.compare(0, sizeof kDownloadPrefix - 1, kDownloadPrefix) == 0 &&
		       path.find("/../") == std::string::npos && path.find("/./") == std::string::npos;
	if (host == "objects.githubusercontent.com" || host == "release-assets.githubusercontent.com")
		return redirect;
	return false;
}

long long HeaderContentLength(const std::string &headers)
{
	const std::string lower = LowerAscii(headers);
	size_t pos = 0;
	long long found = -1;
	while ((pos = lower.find("\r\ncontent-length:", pos)) != std::string::npos)
	{
		pos += 17;
		while (pos < lower.size() && (lower[pos] == ' ' || lower[pos] == '\t'))
			pos++;
		long long v = 0;
		size_t digits = 0;
		while (pos < lower.size() && lower[pos] >= '0' && lower[pos] <= '9' && digits < 15)
		{
			v = v * 10 + (lower[pos++] - '0');
			digits++;
		}
		if (digits == 0 || (found >= 0 && found != v))
			return -2; // malformed or conflicting
		found = v;
	}
	return found;
}

HttpParse ParseHttpResponse(const Bytes &raw, size_t maxBody, HttpResponse &out)
{
	out.status = 0;
	out.location.clear();
	out.contentLength = -1;
	out.body.clear();
	static const char marker[] = "\r\n\r\n";
	auto end = std::search(raw.begin(), raw.end(), marker, marker + 4);
	if (end == raw.end())
		return raw.size() > 65536 ? HTTP_MALFORMED : HTTP_INCOMPLETE;
	const std::string headers(raw.begin(), end);
	if (headers.size() > 65536 || headers.compare(0, 9, "HTTP/1.1 ") != 0 || headers.size() < 12)
		return HTTP_MALFORMED;
	for (int i = 9; i < 12; i++)
		if (headers[i] < '0' || headers[i] > '9')
			return HTTP_MALFORMED;
	out.status = (headers[9] - '0') * 100 + (headers[10] - '0') * 10 + (headers[11] - '0');
	const std::string lower = LowerAscii(headers);
	const size_t loc = lower.find("\r\nlocation:");
	if (loc != std::string::npos)
	{
		size_t s = loc + 11;
		while (s < headers.size() && headers[s] == ' ')
			s++;
		size_t e = headers.find("\r\n", s);
		out.location = headers.substr(s, (e == std::string::npos ? headers.size() : e) - s);
	}
	const bool chunked = lower.find("\r\ntransfer-encoding: chunked") != std::string::npos;
	out.contentLength = HeaderContentLength(headers);
	if (out.contentLength == -2)
		return HTTP_MALFORMED;
	const size_t bodyStart = (size_t)(end - raw.begin()) + 4;

	if (!chunked)
	{
		const size_t have = raw.size() - bodyStart;
		if (out.contentLength < 0)
			return HTTP_MALFORMED; // no way to tell a whole body from a cut one
		if ((unsigned long long)out.contentLength > maxBody)
			return HTTP_TOO_LARGE;
		if (have < (size_t)out.contentLength)
			return HTTP_INCOMPLETE;
		if (have > (size_t)out.contentLength)
			return HTTP_MALFORMED;
		out.body.assign(raw.begin() + bodyStart, raw.end());
		return HTTP_COMPLETE;
	}

	size_t cur = bodyStart;
	for (;;)
	{
		auto lineEnd = std::search(raw.begin() + cur, raw.end(), marker, marker + 2);
		if (lineEnd == raw.end())
			return HTTP_INCOMPLETE;
		std::string sizeText(raw.begin() + cur, lineEnd);
		const size_t semi = sizeText.find(';');
		if (semi != std::string::npos)
			sizeText.resize(semi);
		if (sizeText.empty() || sizeText.size() > 8)
			return HTTP_MALFORMED;
		size_t chunk = 0;
		for (char c : sizeText)
		{
			int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
			                                         : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
			if (v < 0)
				return HTTP_MALFORMED;
			chunk = chunk * 16 + (size_t)v;
		}
		cur = (size_t)(lineEnd - raw.begin()) + 2;
		if (chunk == 0)
			return HTTP_COMPLETE; // trailers, if any, are ignored
		if (out.body.size() + chunk > maxBody)
			return HTTP_TOO_LARGE;
		if (cur + chunk + 2 > raw.size())
			return HTTP_INCOMPLETE;
		out.body.insert(out.body.end(), raw.begin() + cur, raw.begin() + cur + chunk);
		cur += chunk;
		if (raw[cur] != '\r' || raw[cur + 1] != '\n')
			return HTTP_MALFORMED;
		cur += 2;
	}
}

// ── install ──

std::string StagedPath(const std::string &file) { return file + ".update"; }
std::string PartPath(const std::string &file) { return file + ".update.part"; }
std::string OldPath(const std::string &file) { return file + ".old"; }
std::string MarkerPath(const std::string &exe) { return exe + ".update-applied"; }
std::string JournalPath(const std::string &exe) { return exe + ".update-files"; }

static bool IsExecutableRel(const std::string &rel)
{
	return rel == ExecutableName(PLATFORM_WINDOWS) || rel == ExecutableName(PLATFORM_LINUX);
}

static std::string InstallDir(const std::string &exe)
{
	const size_t slash = exe.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : exe.substr(0, slash);
}

std::string TargetPath(const std::string &exe, const std::string &rel)
{
	std::string native = rel;
#ifdef _WIN32
	std::replace(native.begin(), native.end(), '/', '\\');
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	const std::string dir = InstallDir(exe);
	return dir.empty() ? native : dir + sep + native;
}

// The file a release path installs to. The executable always replaces the
// running one, whatever the player named it.
static std::string InstallTarget(const std::string &exe, const std::string &rel)
{
	return IsExecutableRel(rel) ? exe : TargetPath(exe, rel);
}

// A release path read back from the journal or marker. Only install-set paths
// are ever acted on, so a damaged file cannot point the cleanup elsewhere.
static bool SafeLocalRel(const std::string &rel)
{
	if (rel.empty() || rel.size() > 512 || rel[0] == '/')
		return false;
	for (unsigned char c : rel)
		if (c < 0x20 || c == 0x7F || c == '\\' || c == ':')
			return false;
	if (rel.find("/./") != std::string::npos || rel.find("/../") != std::string::npos ||
	    rel.find("//") != std::string::npos)
		return false;
	return IsInstallPath(rel, PLATFORM_WINDOWS) || IsInstallPath(rel, PLATFORM_LINUX);
}

#ifdef _WIN32
static std::wstring Wide(const std::string &utf8)
{
	if (utf8.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), (int)utf8.size(), nullptr, 0);
	if (n <= 0)
		return std::wstring();
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), (int)utf8.size(), &w[0], n);
	return w;
}
#endif

static bool RealWrite(void *, const std::string &path, const Bytes &data)
{
#ifdef _WIN32
	const std::wstring w = Wide(path);
	HANDLE h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	size_t off = 0;
	bool ok = true;
	while (ok && off < data.size())
	{
		DWORD wrote = 0;
		const DWORD chunk = (DWORD)std::min<size_t>(data.size() - off, 1 << 20);
		ok = WriteFile(h, data.data() + off, chunk, &wrote, nullptr) && wrote == chunk;
		off += wrote;
	}
	ok = ok && FlushFileBuffers(h);
	CloseHandle(h);
	return ok;
#else
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = std::fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = std::fclose(f) == 0 && ok;
	return ok;
#endif
}

static bool RealRead(void *, const std::string &path, size_t maxBytes, Bytes &out)
{
	out.clear();
#ifdef _WIN32
	FILE *f = _wfopen(Wide(path).c_str(), L"rb");
#else
	FILE *f = std::fopen(path.c_str(), "rb");
#endif
	if (!f)
		return false;
	unsigned char buf[65536];
	size_t got;
	bool ok = true;
	while ((got = std::fread(buf, 1, sizeof buf, f)) > 0)
	{
		if (out.size() + got > maxBytes)
		{
			ok = false;
			break;
		}
		out.insert(out.end(), buf, buf + got);
	}
	ok = ok && !std::ferror(f);
	std::fclose(f);
	return ok;
}

static bool RealRename(void *, const std::string &from, const std::string &to)
{
#ifdef _WIN32
	// No REPLACE_EXISTING: every rename here targets a path that was cleared
	// first, so a surprise file at the target fails rather than being lost. A
	// running executable may be renamed on Windows, never overwritten.
	return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
	// rename() replaces silently on POSIX; refuse an existing target to match.
	if (access(to.c_str(), F_OK) == 0)
		return false;
	return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

static bool RealExists(void *, const std::string &path)
{
#ifdef _WIN32
	return GetFileAttributesW(Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
	return access(path.c_str(), F_OK) == 0;
#endif
}

static bool RealRemove(void *ctx, const std::string &path)
{
#ifdef _WIN32
	DeleteFileW(Wide(path).c_str());
#else
	std::remove(path.c_str());
#endif
	return !RealExists(ctx, path);
}

static bool RealMakeExecutable(void *, const std::string &path)
{
#ifdef _WIN32
	(void)path;
	return true;
#else
	return chmod(path.c_str(), 0755) == 0;
#endif
}

static bool RealMakeDir(void *ctx, const std::string &path)
{
#ifdef _WIN32
	CreateDirectoryW(Wide(path).c_str(), nullptr);
#else
	mkdir(path.c_str(), 0755);
#endif
	return RealExists(ctx, path);
}

static bool RealIsLink(void *, const std::string &path)
{
#ifdef _WIN32
	const DWORD attr = GetFileAttributesW(Wide(path).c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
	// readlink, not lstat: a 32-bit lstat fails with EOVERFLOW on SteamOS
	// overlayfs (see ap_net.cpp, #170), readlink fills no stat struct.
	char buf[8];
	return readlink(path.c_str(), buf, sizeof buf) >= 0;
#endif
}

FsOps RealFsOps()
{
	FsOps ops;
	ops.ctx = nullptr;
	ops.writeFile = RealWrite;
	ops.readFile = RealRead;
	ops.renameFile = RealRename;
	ops.removeFile = RealRemove;
	ops.exists = RealExists;
	ops.makeExecutable = RealMakeExecutable;
	ops.makeDir = RealMakeDir;
	ops.isLink = RealIsLink;
	return ops;
}

static std::vector<std::string> ReadLines(const FsOps &fs, const std::string &path)
{
	std::vector<std::string> lines;
	Bytes raw;
	if (!fs.readFile(fs.ctx, path, 64 * 1024, raw))
		return lines;
	std::string cur;
	for (unsigned char c : raw)
	{
		if (c == '\n')
		{
			if (!cur.empty() && cur.back() == '\r')
				cur.pop_back();
			lines.push_back(cur);
			cur.clear();
		}
		else
			cur.push_back((char)c);
	}
	if (!cur.empty())
		lines.push_back(cur);
	return lines;
}

static Bytes JoinLines(const std::vector<std::string> &lines)
{
	Bytes out;
	for (const std::string &l : lines)
	{
		out.insert(out.end(), l.begin(), l.end());
		out.push_back('\n');
	}
	return out;
}

// Remove the staging files of every release path, and the journal.
static void DropStaged(const FsOps &fs, const std::string &exe, const std::vector<std::string> &rels)
{
	for (const std::string &rel : rels)
	{
		const std::string t = InstallTarget(exe, rel);
		fs.removeFile(fs.ctx, PartPath(t));
		fs.removeFile(fs.ctx, StagedPath(t));
	}
	fs.removeFile(fs.ctx, JournalPath(exe));
}

// Create the folders of `rel` under the install directory. A folder on the way
// that is a link or junction is refused: its files would land in a shared place.
static bool EnsureFolders(const FsOps &fs, const std::string &exe, const std::string &rel)
{
	size_t slash = 0;
	while ((slash = rel.find('/', slash)) != std::string::npos)
	{
		const std::string dir = TargetPath(exe, rel.substr(0, slash));
		if (fs.exists(fs.ctx, dir))
		{
			if (fs.isLink(fs.ctx, dir))
				return false;
		}
		else if (!fs.makeDir(fs.ctx, dir) || fs.isLink(fs.ctx, dir))
			return false;
		slash++;
	}
	return true;
}

Result Stage(const FsOps &fs, const std::string &exe, const std::vector<InstallFile> &files,
             std::vector<StagedFile> &staged)
{
	staged.clear();
	std::vector<std::string> rels;
	size_t exes = 0;
	for (const InstallFile &f : files)
	{
		if (!SafeLocalRel(f.rel) || f.data.size() > kMaxExecutable)
			return ERR_ARCHIVE;
		exes += IsExecutableRel(f.rel);
		rels.push_back(f.rel);
	}
	if (exes != 1)
		return ERR_ARCHIVE;
	// The journal names what is being staged, so a crash in the middle is
	// cleaned up at the next launch.
	DropStaged(fs, exe, rels);
	if (!fs.writeFile(fs.ctx, JournalPath(exe), JoinLines(rels)))
	{
		DropStaged(fs, exe, rels);
		return ERR_IO;
	}
	for (const InstallFile &f : files)
	{
		const std::string target = InstallTarget(exe, f.rel);
		const std::string part = PartPath(target);
		Bytes back;
		if (Sha256Hex(f.data) != f.sha256 || !EnsureFolders(fs, exe, f.rel) ||
		    !fs.writeFile(fs.ctx, part, f.data) || !fs.readFile(fs.ctx, part, kMaxExecutable, back) ||
		    Sha256Hex(back) != f.sha256 || !fs.renameFile(fs.ctx, part, StagedPath(target)))
		{
			DropStaged(fs, exe, rels);
			staged.clear();
			return ERR_IO;
		}
		staged.push_back(StagedFile{f.rel, f.sha256});
	}
	return OK;
}

void DiscardStaged(const FsOps &fs, const std::string &exe, const std::vector<StagedFile> &staged)
{
	std::vector<std::string> rels;
	for (const StagedFile &f : staged)
		if (SafeLocalRel(f.rel))
			rels.push_back(f.rel);
	DropStaged(fs, exe, rels);
}

namespace
{
struct Step
{
	std::string target;
	bool hadOld;
};
} // namespace

// Undo swap steps newest first: the new file goes, the previous one returns.
static bool Undo(const FsOps &fs, const std::vector<Step> &steps)
{
	bool ok = true;
	for (size_t i = steps.size(); i-- > 0;)
	{
		const Step &st = steps[i];
		if (!fs.removeFile(fs.ctx, st.target))
		{
			ok = false;
			continue;
		}
		if (st.hadOld && !fs.renameFile(fs.ctx, OldPath(st.target), st.target))
			ok = false;
	}
	return ok;
}

Result Apply(const FsOps &fs, const std::string &exe, const std::vector<StagedFile> &staged,
             const std::string &newTag)
{
	std::vector<std::string> rels;
	for (const StagedFile &f : staged)
		rels.push_back(f.rel);
	size_t exes = 0;
	for (const StagedFile &f : staged)
		exes += IsExecutableRel(f.rel) && SafeLocalRel(f.rel);
	bool good = IsStableTag(newTag) && exes == 1;
	for (const StagedFile &f : staged)
	{
		Bytes check;
		if (!good || !SafeLocalRel(f.rel) ||
		    !fs.readFile(fs.ctx, StagedPath(InstallTarget(exe, f.rel)), kMaxExecutable, check) ||
		    Sha256Hex(check) != f.sha256)
		{
			good = false;
			break;
		}
	}
	if (!good)
	{
		DropStaged(fs, exe, rels);
		return ERR_STAGED_CHANGED;
	}
	if (!fs.exists(fs.ctx, exe))
		return ERR_IO;

	// Everything else first, the executable last.
	std::vector<std::string> order;
	for (const StagedFile &f : staged)
		if (!IsExecutableRel(f.rel))
			order.push_back(f.rel);
	for (const StagedFile &f : staged)
		if (IsExecutableRel(f.rel))
			order.push_back(f.rel);

	std::vector<Step> steps;
	std::vector<std::string> kept; // release paths that now have a .old copy
	fs.removeFile(fs.ctx, MarkerPath(exe));
	for (const std::string &rel : order)
	{
		const std::string target = InstallTarget(exe, rel);
		const std::string old = OldPath(target);
		// An older rollback copy belongs to a finished update; this one replaces it.
		bool ok = fs.removeFile(fs.ctx, old);
		const bool hadOld = ok && fs.exists(fs.ctx, target);
		if (ok && hadOld)
			ok = fs.renameFile(fs.ctx, target, old);
		if (ok && !fs.renameFile(fs.ctx, StagedPath(target), target))
		{
			if (hadOld && !fs.renameFile(fs.ctx, old, target))
			{
				// Neither name works: the previous file stays as .old, the
				// manual rollback copy. Undo the rest and report.
				Undo(fs, steps);
				DropStaged(fs, exe, rels);
				return ERR_IO;
			}
			ok = false;
		}
		if (!ok)
		{
			Undo(fs, steps);
			DropStaged(fs, exe, rels);
			return ERR_IO;
		}
		steps.push_back(Step{target, hadOld});
		if (hadOld)
			kept.push_back(rel);
	}
	fs.makeExecutable(fs.ctx, exe);
	fs.removeFile(fs.ctx, JournalPath(exe));
	// The marker is advisory (it only decides when the .old copies may go); a
	// failure to write it leaves them in place, which is the safe direction.
	std::vector<std::string> marker(1, newTag);
	marker.insert(marker.end(), kept.begin(), kept.end());
	fs.writeFile(fs.ctx, MarkerPath(exe), JoinLines(marker));
	return OK;
}

StartupState StartupCheck(const FsOps &fs, const std::string &exe, const std::string &runningTag)
{
	std::vector<std::string> rels;
	for (const std::string &rel : ReadLines(fs, JournalPath(exe)))
		if (SafeLocalRel(rel))
			rels.push_back(rel);
	rels.push_back(ExecutableName(PLATFORM_WINDOWS)); // the executable maps to `exe` either way
	DropStaged(fs, exe, rels);

	const std::string marker = MarkerPath(exe);
	if (!fs.exists(fs.ctx, marker))
		return STARTUP_CLEAN;
	const std::vector<std::string> lines = ReadLines(fs, marker);
	if (!lines.empty() && lines[0] == runningTag)
		return STARTUP_UPDATED;
	// Another version runs: the player restored files by hand. Keep whatever
	// files are there and forget the marker.
	fs.removeFile(fs.ctx, marker);
	return STARTUP_ROLLED_BACK;
}

void ConfirmStarted(const FsOps &fs, const std::string &exe)
{
	const std::vector<std::string> lines = ReadLines(fs, MarkerPath(exe));
	for (size_t i = 1; i < lines.size(); i++)
		if (SafeLocalRel(lines[i]))
			fs.removeFile(fs.ctx, OldPath(InstallTarget(exe, lines[i])));
	fs.removeFile(fs.ctx, MarkerPath(exe));
}

} // namespace ap_update
