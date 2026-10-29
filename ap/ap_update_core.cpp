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
#include <dirent.h>
#include <fcntl.h>
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
	case ERR_UNDO_FAILED: return "install failed and the previous files could not all be restored";
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
const char kRuntimeDir[] = "solo-runtime";
const char kRuntimeIdRel[] = "solo-runtime/RUNTIME.txt";
// The runtime as one journal and marker entry (a folder, not a file).
static const char kRuntimeEntry[] = "solo-runtime/";

bool IsRuntimePath(const std::string &rel)
{
	const size_t n = sizeof kRuntimeEntry - 1;
	return rel.size() > n && rel.compare(0, n, kRuntimeEntry) == 0 && rel.back() != '/';
}

bool IsInstallPath(const std::string &rel, Platform platform)
{
	if (rel == ExecutableName(platform) || rel == "ctr.apworld")
		return true;
	// The whole bundled runtime, at any depth. Never the player's solo/ folder.
	if (IsRuntimePath(rel))
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
	size_t exeCount = 0, total = 0, runtime = 0, runtimeId = 0;
	for (InstallFile &f : out)
	{
		exeCount += f.rel == ExecutableName(platform);
		runtime += IsRuntimePath(f.rel);
		runtimeId += f.rel == kRuntimeIdRel;
		total += f.data.size();
		if (f.data.size() > kMaxExecutable)
			return ERR_SIZE;
		if (f.rel == ExecutableName(platform))
			f.executable = true;
	}
	if (exeCount != 1)
		return ERR_ARCHIVE;
	// The runtime is told apart from an older one by its RUNTIME.txt.
	if (runtime && runtimeId != 1)
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
	// One allocation for the unpacked tar where the gzip trailer says its size
	// (mod 2^32, so only a hint): no doubling of a 70 MB buffer in a 32-bit process.
	if (a.size() >= 18)
	{
		const uint32_t isize = Le32(&a[a.size() - 4]);
		if (isize % 512 == 0 && isize <= kMaxUnpacked)
			tar.reserve(isize);
	}
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
			uint64_t mode = 0;
			f.executable = OctalField(h + 100, 8, mode) && (mode & 0111) != 0;
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
		// Compared by subtraction: on a 32-bit client `size + chunk` wraps for
		// a chunk near FFFFFFFF. cur <= raw.size() holds (the line end was found).
		if (chunk > maxBody - out.body.size())
			return HTTP_TOO_LARGE;
		if (chunk > raw.size() - cur || raw.size() - cur - chunk < 2)
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
std::string SwapJournalPath(const std::string &exe) { return exe + ".update-swap"; }

static std::string g_assetDir;
void SetAssetDir(const std::string &dir)
{
	g_assetDir = dir;
	while (g_assetDir.size() > 1 && (g_assetDir.back() == '/' || g_assetDir.back() == '\\'))
		g_assetDir.pop_back();
}

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
	if (!g_assetDir.empty() && (rel == "assets" || rel.compare(0, 7, "assets/") == 0))
	{
		const std::string rest = rel.size() > 7 ? native.substr(7) : std::string();
		return rest.empty() ? g_assetDir : g_assetDir + sep + rest;
	}
	const std::string dir = InstallDir(exe);
	return dir.empty() ? native : dir + sep + native;
}

// The file a release path installs to. The executable always replaces the
// running one, whatever the player named it.
static std::string InstallTarget(const std::string &exe, const std::string &rel)
{
	return IsExecutableRel(rel) ? exe : TargetPath(exe, rel);
}

// No empty, "." or ".." segment, no backslash, colon or control character.
static bool CleanRel(const std::string &rel)
{
	if (rel.empty() || rel.size() > 512 || rel[0] == '/')
		return false;
	for (unsigned char c : rel)
		if (c < 0x20 || c == 0x7F || c == '\\' || c == ':')
			return false;
	size_t start = 0;
	while (start < rel.size())
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

// A release path of an install file. Only install-set paths are ever acted on.
static bool SafeLocalRel(const std::string &rel)
{
	return CleanRel(rel) && (IsInstallPath(rel, PLATFORM_WINDOWS) || IsInstallPath(rel, PLATFORM_LINUX));
}

// An entry read back from the journal or marker: an install file outside the
// runtime, or the runtime folder as a whole. A damaged file cannot point the
// cleanup elsewhere.
static bool SafeEntryRel(const std::string &rel)
{
	if (rel == kRuntimeEntry)
		return true;
	return SafeLocalRel(rel) && !IsRuntimePath(rel);
}

static bool IsRuntimeEntry(const std::string &rel) { return rel == kRuntimeEntry; }

// The installed runtime folder, and where a runtime file is staged.
static std::string RuntimeTarget(const std::string &exe) { return TargetPath(exe, kRuntimeDir); }
static std::string StagedFileOf(const std::string &exe, const std::string &rel)
{
	if (IsRuntimePath(rel))
		return TargetPath(exe, std::string(kRuntimeDir) + ".update" + rel.substr(sizeof kRuntimeDir - 1));
	return StagedPath(InstallTarget(exe, rel));
}
static std::string PartFileOf(const std::string &exe, const std::string &rel)
{
	return IsRuntimePath(rel) ? StagedFileOf(exe, rel) + ".part" : PartPath(InstallTarget(exe, rel));
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

#ifndef _WIN32
// Flush the folder entry of `path` so a rename or a new file survives power
// loss. False when the folder cannot be opened or flushed.
static bool SyncParentDir(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	const std::string dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
	const int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return false;
	const bool ok = fsync(fd) == 0;
	close(fd);
	return ok;
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
	// Every staged file, the journals and the marker are written here, each in
	// a folder a later rename uses. A folder that cannot be flushed fails the
	// write, so the update stops before anything is replaced when durability
	// cannot be had. The renames themselves then only flush folders that
	// already passed (their result is the rename's).
	return ok && SyncParentDir(path);
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
	if (std::rename(from.c_str(), to.c_str()) != 0)
		return false;
	SyncParentDir(to);
	return true;
#endif
}

#ifndef _WIN32
static bool RealLink(void *, const std::string &existing, const std::string &newLink)
{
	if (link(existing.c_str(), newLink.c_str()) != 0)
		return false;
	SyncParentDir(newLink);
	return true;
}

// rename() replaces the target atomically: the executable path never vanishes.
static bool RealReplace(void *, const std::string &from, const std::string &to)
{
	if (std::rename(from.c_str(), to.c_str()) != 0)
		return false;
	SyncParentDir(to);
	return true;
}
#endif

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

#ifndef _WIN32
// Remove `name` inside the folder `dirfd`: a link or file is unlinked, a
// folder emptied through its own descriptor and then removed.
static void RemoveTreeAt(int parent, const std::string &name)
{
	const int fd = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
	{
		unlinkat(parent, name.c_str(), 0); // a file or a link: never followed
		return;
	}
	DIR *d = fdopendir(fd);
	if (!d)
	{
		close(fd);
		return;
	}
	// readdir64: the 32-bit client has no large-file support, and a plain
	// readdir fails with EOVERFLOW on 64-bit inode numbers (btrfs, overlayfs;
	// see ap_net.cpp). Names only, no stat.
	std::vector<std::string> children;
#ifdef __GLIBC__
	while (struct dirent64 *e = readdir64(d))
#else
	while (struct dirent *e = readdir(d))
#endif
	{
		const std::string child = e->d_name;
		if (child != "." && child != "..")
			children.push_back(child);
	}
	for (const std::string &child : children)
		RemoveTreeAt(dirfd(d), child);
	closedir(d);
	unlinkat(parent, name.c_str(), AT_REMOVEDIR);
}
#endif

static bool RealRemoveTree(void *ctx, const std::string &path)
{
#ifdef _WIN32
	const std::wstring w = Wide(path);
	const DWORD attr = GetFileAttributesW(w.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES)
		return true;
	if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		if (attr & FILE_ATTRIBUTE_READONLY)
			SetFileAttributesW(w.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
		DeleteFileW(w.c_str());
		return !RealExists(ctx, path);
	}
	// A junction or folder symlink: remove the link, never what it points at.
	if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((w + L"\\*").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				const std::wstring name = fd.cFileName;
				if (name == L"." || name == L"..")
					continue;
				const int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(), nullptr, 0, nullptr, nullptr);
				std::string child((size_t)(n > 0 ? n : 0), '\0');
				if (n > 0)
					WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(), &child[0], n, nullptr, nullptr);
				RealRemoveTree(ctx, path + "\\" + child);
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
	}
	RemoveDirectoryW(w.c_str());
	return !RealExists(ctx, path);
#else
	// Every step goes through folder descriptors opened with O_NOFOLLOW, so a
	// folder swapped for a link while this runs is unlinked, never entered.
	const size_t slash = path.find_last_of('/');
	const std::string parent = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
	const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	const int pfd = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
	if (pfd < 0)
		return !RealExists(ctx, path);
	RemoveTreeAt(pfd, name);
	close(pfd);
	char buf[8];
	const bool gone = !RealExists(ctx, path) && readlink(path.c_str(), buf, sizeof buf) < 0;
	if (gone)
		SyncParentDir(path);
	return gone;
#endif
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
	return RealExists(ctx, path);
#else
	// A new folder's own entry is flushed too, so a staged tree is durable.
	const bool made = mkdir(path.c_str(), 0755) == 0;
	return RealExists(ctx, path) && (!made || SyncParentDir(path));
#endif
}

static bool RealIsLink(void *, const std::string &path)
{
#ifdef _WIN32
	// Only symlinks and junctions redirect a write somewhere else. Other reparse
	// points (OneDrive Files On-Demand, dedup) are ordinary folders to us.
	const std::wstring w = Wide(path);
	const DWORD attr = GetFileAttributesW(w.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
		return false;
	HANDLE h = CreateFileW(w.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                       nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return true; // cannot read the tag: refuse
	FILE_ATTRIBUTE_TAG_INFO info;
	const BOOL got = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof info);
	CloseHandle(h);
	if (!got)
		return true;
	return info.ReparseTag == IO_REPARSE_TAG_SYMLINK || info.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT;
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
	ops.removeTree = RealRemoveTree;
#ifdef _WIN32
	ops.syncDir = nullptr; // MoveFileExW(MOVEFILE_WRITE_THROUGH) is all Windows offers
	ops.linkFile = nullptr;
	ops.replaceFile = nullptr;
#else
	ops.syncDir = SyncParentDir;
	ops.linkFile = RealLink;
	ops.replaceFile = RealReplace;
#endif
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

// Remove the staging files of every release path (the staged runtime folder
// for runtime paths), and the journal.
static void DropStaged(const FsOps &fs, const std::string &exe, const std::vector<std::string> &rels)
{
	bool runtime = false;
	for (const std::string &rel : rels)
	{
		if (IsRuntimeEntry(rel) || IsRuntimePath(rel))
		{
			runtime = true;
			continue;
		}
		const std::string t = InstallTarget(exe, rel);
		fs.removeFile(fs.ctx, PartPath(t));
		fs.removeFile(fs.ctx, StagedPath(t));
	}
	if (runtime)
		fs.removeTree(fs.ctx, StagedPath(RuntimeTarget(exe)));
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
	// Never overwrite the staging journal or backups of an unresolved swap.
	if (fs.exists(fs.ctx, SwapJournalPath(exe)))
		return ERR_UNDO_FAILED;
	staged.clear();
	std::vector<std::string> rels; // journal entries: files, and the runtime once
	size_t exes = 0, runtime = 0, runtimeId = 0;
	for (const InstallFile &f : files)
	{
		if (!SafeLocalRel(f.rel) || f.data.size() > kMaxExecutable)
			return ERR_ARCHIVE;
		exes += IsExecutableRel(f.rel);
		runtimeId += f.rel == kRuntimeIdRel;
		if (IsRuntimePath(f.rel))
		{
			if (runtime++ == 0)
				rels.push_back(kRuntimeEntry);
		}
		else
			rels.push_back(f.rel);
	}
	if (exes != 1 || (runtime && runtimeId != 1))
		return ERR_ARCHIVE;
	// The runtime folder and its staging folder are replaced whole: a link or
	// junction there would send the writes, or a later removal, elsewhere.
	if (runtime && (fs.isLink(fs.ctx, RuntimeTarget(exe)) || fs.isLink(fs.ctx, StagedPath(RuntimeTarget(exe)))))
		return ERR_IO;
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
		const std::string part = PartFileOf(exe, f.rel);
		// Folders on the way: the install folder's own for files, the staged
		// runtime folder's (solo-runtime.update/...) for runtime files.
		const std::string folderRel =
		    IsRuntimePath(f.rel) ? std::string(kRuntimeDir) + ".update" + f.rel.substr(sizeof kRuntimeDir - 1) : f.rel;
		Bytes back;
		if (Sha256Hex(f.data) != f.sha256 || !EnsureFolders(fs, exe, folderRel) ||
		    !fs.writeFile(fs.ctx, part, f.data) || !fs.readFile(fs.ctx, part, kMaxExecutable, back) ||
		    Sha256Hex(back) != f.sha256 ||
		    // Executable before the swap, so a crash never leaves a non-executable game.
		    ((IsExecutableRel(f.rel) || f.executable) && !fs.makeExecutable(fs.ctx, part)) ||
		    !fs.renameFile(fs.ctx, part, StagedFileOf(exe, f.rel)))
		{
			DropStaged(fs, exe, rels);
			staged.clear();
			return ERR_IO;
		}
		staged.push_back(StagedFile{f.rel, f.sha256});
	}
	// Every .part -> staged rename must be durable before Apply trusts the
	// set: flush each folder that received one (once per folder).
	if (fs.syncDir)
	{
		std::set<std::string> folders;
		for (const StagedFile &f : staged)
		{
			const std::string path = StagedFileOf(exe, f.rel);
			const size_t slash = path.find_last_of("/\\");
			if (folders.insert(slash == std::string::npos ? std::string() : path.substr(0, slash)).second &&
			    !fs.syncDir(path))
			{
				DropStaged(fs, exe, rels);
				staged.clear();
				return ERR_IO;
			}
		}
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
	bool atomic; // the previous file was kept as .old (link or copy) and the new one renamed over it
	bool folder; // the runtime folder: undo removes the new folder before the old one returns
};
} // namespace

// Undo swap steps newest first: the new file goes, the previous one returns.
static bool Undo(const FsOps &fs, const std::vector<Step> &steps)
{
	bool ok = true;
	for (size_t i = steps.size(); i-- > 0;)
	{
		const Step &st = steps[i];
		if (st.atomic)
		{
			if (!fs.replaceFile(fs.ctx, OldPath(st.target), st.target))
				ok = false;
			continue;
		}
		if (!(st.folder ? fs.removeTree(fs.ctx, st.target) : fs.removeFile(fs.ctx, st.target)))
		{
			ok = false;
			continue;
		}
		if (st.hadOld && !fs.renameFile(fs.ctx, OldPath(st.target), st.target))
			ok = false;
	}
	return ok;
}

// Keep the running executable as .old while its path stays in place: a hard
// link, or where the file system has none (exFAT, some network shares) a
// verified copy. False when neither worked; nothing is then left at .old.
static bool KeepExecutable(const FsOps &fs, const std::string &target, const std::string &old)
{
	if (fs.linkFile && fs.linkFile(fs.ctx, target, old))
		return true;
	Bytes cur, back;
	if (fs.readFile(fs.ctx, target, kMaxExecutable, cur) && fs.writeFile(fs.ctx, old, cur) &&
	    fs.readFile(fs.ctx, old, kMaxExecutable, back) && back == cur && fs.makeExecutable(fs.ctx, old))
		return true;
	fs.removeFile(fs.ctx, old);
	return false;
}

Result Apply(const FsOps &fs, const std::string &exe, const std::vector<StagedFile> &staged,
             const std::string &newTag)
{
	if (fs.exists(fs.ctx, SwapJournalPath(exe)))
		return ERR_UNDO_FAILED;
	std::vector<std::string> rels;
	bool hasRuntime = false;
	std::string runtimeId; // SHA-256 of the new RUNTIME.txt
	for (const StagedFile &f : staged)
	{
		rels.push_back(f.rel);
		if (IsRuntimePath(f.rel))
			hasRuntime = true;
		if (f.rel == kRuntimeIdRel)
			runtimeId = f.sha256;
	}
	size_t exes = 0;
	for (const StagedFile &f : staged)
		exes += IsExecutableRel(f.rel) && SafeLocalRel(f.rel);
	bool good = IsStableTag(newTag) && exes == 1 && (!hasRuntime || !runtimeId.empty());
	for (const StagedFile &f : staged)
	{
		Bytes check;
		if (!good || !SafeLocalRel(f.rel) || !fs.readFile(fs.ctx, StagedFileOf(exe, f.rel), kMaxExecutable, check) ||
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
	const std::string runtimeDir = RuntimeTarget(exe);
	if (hasRuntime && (fs.isLink(fs.ctx, runtimeDir) || fs.isLink(fs.ctx, StagedPath(runtimeDir))))
	{
		DropStaged(fs, exe, rels);
		return ERR_IO;
	}

	// One entry per swap step: files first, then the runtime folder, the
	// executable last.
	struct Entry
	{
		std::string rel, sha;
	};
	std::vector<Entry> order;
	for (const StagedFile &f : staged)
		if (!IsExecutableRel(f.rel) && !IsRuntimePath(f.rel))
			order.push_back(Entry{f.rel, f.sha256});
	if (hasRuntime)
		order.push_back(Entry{kRuntimeEntry, runtimeId});
	for (const StagedFile &f : staged)
		if (IsExecutableRel(f.rel))
			order.push_back(Entry{f.rel, f.sha256});
	auto targetOf = [&](const std::string &rel) { return IsRuntimeEntry(rel) ? runtimeDir : InstallTarget(exe, rel); };

	// An older marker must be gone before the journal goes down: recovery reads
	// a marker beside a journal as this swap's finish. Its rollback copies go
	// first (ConfirmStarted keeps the marker while one is left), so a copy that
	// cannot be removed keeps its marker for the next retry. Either failing
	// stops here with nothing changed.
	if (fs.exists(fs.ctx, MarkerPath(exe)))
		ConfirmStarted(fs, exe);
	if (fs.exists(fs.ctx, MarkerPath(exe)) || !fs.removeFile(fs.ctx, MarkerPath(exe)))
	{
		DropStaged(fs, exe, rels);
		return ERR_IO;
	}
	// The swap journal goes down, flushed, before the first rename: it names
	// every entry, the hash of the new copy (of RUNTIME.txt for the runtime
	// folder) and whether an installed copy exists.
	std::vector<std::string> journal;
	journal.push_back("applying");
	journal.push_back(newTag);
	for (const Entry &e : order)
		journal.push_back(e.rel + "\t" + e.sha + "\t" + (fs.exists(fs.ctx, targetOf(e.rel)) ? "1" : "0"));
	const std::string swapPath = SwapJournalPath(exe);
	if (!fs.writeFile(fs.ctx, swapPath, JoinLines(journal)))
	{
		fs.removeFile(fs.ctx, swapPath);
		DropStaged(fs, exe, rels);
		return ERR_IO;
	}

	std::vector<Step> steps;
	std::vector<std::string> kept; // entries that now have a .old copy
	// A failed step: undo what was done. When the undo itself fails the swap
	// journal stays so the next launch restores the .old copies.
	// Flush every folder a step renamed in; false if any flush fails.
	auto syncSteps = [&]() {
		bool ok = true;
		if (fs.syncDir)
			for (const Step &st : steps)
				ok = fs.syncDir(st.target) && ok;
		return ok;
	};
	auto fail = [&](bool thisStepUndone) -> Result {
		// The journal is dropped only when the undo is complete and durable;
		// otherwise the next launch checks every entry again.
		const bool undone = Undo(fs, steps) && thisStepUndone && syncSteps();
		DropStaged(fs, exe, rels);
		if (undone)
		{
			fs.removeFile(fs.ctx, swapPath);
			return ERR_IO;
		}
		return ERR_UNDO_FAILED;
	};
	for (const Entry &e : order)
	{
		const std::string &rel = e.rel;
		const bool folder = IsRuntimeEntry(rel);
		const std::string target = targetOf(rel);
		const std::string old = OldPath(target);
		const std::string from = StagedPath(target); // F.update, or solo-runtime.update
		// An older rollback copy belongs to a finished update; this one replaces it.
		bool ok = folder ? fs.removeTree(fs.ctx, old) : fs.removeFile(fs.ctx, old);
		const bool had = ok && fs.exists(fs.ctx, target);
		// Where atomic replace exists (Linux) the executable is never moved
		// aside: its path must hold a launchable file at every moment.
		const bool atomic = IsExecutableRel(rel) && fs.replaceFile != nullptr;
		bool moved = false;
		if (ok && had)
		{
			if (atomic)
				ok = KeepExecutable(fs, target, old);
			else
				ok = fs.renameFile(fs.ctx, target, old);
			moved = ok;
		}
		if (ok)
			ok = atomic ? fs.replaceFile(fs.ctx, from, target) : fs.renameFile(fs.ctx, from, target);
		if (!ok)
		{
			bool stepUndone = true;
			if (moved)
			{
				if (atomic)
					stepUndone = fs.removeFile(fs.ctx, old); // target was never replaced
				else if (!fs.exists(fs.ctx, target))
					stepUndone = fs.renameFile(fs.ctx, old, target);
			}
			return fail(stepUndone);
		}
		steps.push_back(Step{target, moved, atomic && moved, folder});
		if (moved)
			kept.push_back(rel);
	}
	// Every rename must be durable before the marker says the swap finished:
	// flush each folder that had one (the .old name sits in the same folder).
	if (!syncSteps())
		return fail(true);
	// The marker doubles as the journal's "finished" mark: written first, so a
	// crash before the journal is removed is not mistaken for a half swap.
	std::vector<std::string> marker(1, newTag);
	marker.insert(marker.end(), kept.begin(), kept.end());
	if (!fs.writeFile(fs.ctx, MarkerPath(exe), JoinLines(marker)))
	{
		fs.removeFile(fs.ctx, MarkerPath(exe));
		return fail(true);
	}
	fs.removeFile(fs.ctx, swapPath);
	fs.removeFile(fs.ctx, JournalPath(exe));
	return OK;
}

namespace
{
struct SwapEntry
{
	std::string rel;
	std::string sha;
	bool had;
};
} // namespace

static bool SameVersion(const std::string &a, const std::string &b)
{
	int x[4], y[4];
	if (!AP_VersionParse(a.c_str(), x) || !AP_VersionParse(b.c_str(), y))
		return false;
	return std::equal(x, x + 4, y);
}

static bool FileHashIs(const FsOps &fs, const std::string &path, const std::string &sha)
{
	Bytes b;
	return fs.readFile(fs.ctx, path, kMaxExecutable, b) && Sha256Hex(b) == sha;
}

// The file whose hash a journal entry records: the file itself, or the
// runtime folder's RUNTIME.txt.
static std::string EntryTarget(const std::string &exe, const std::string &rel)
{
	return IsRuntimeEntry(rel) ? RuntimeTarget(exe) : InstallTarget(exe, rel);
}
static bool EntryIsNew(const FsOps &fs, const std::string &exe, const std::string &rel, const std::string &sha)
{
	const std::string t = EntryTarget(exe, rel);
	return FileHashIs(fs, IsRuntimeEntry(rel) ? TargetPath(exe, kRuntimeIdRel) : t, sha);
}

static bool ReadSwapJournal(const FsOps &fs, const std::string &exe, std::string &tag, std::vector<SwapEntry> &entries)
{
	const std::vector<std::string> lines = ReadLines(fs, SwapJournalPath(exe));
	tag.clear();
	entries.clear();
	if (lines.size() < 2 || lines[0] != "applying" || !IsStableTag(lines[1]))
		return false;
	tag = lines[1];
	for (size_t i = 2; i < lines.size(); i++)
	{
		const size_t t1 = lines[i].find('\t');
		const size_t t2 = t1 == std::string::npos ? t1 : lines[i].find('\t', t1 + 1);
		if (t2 == std::string::npos)
			continue; // a half-written last line
		SwapEntry e;
		e.rel = lines[i].substr(0, t1);
		e.sha = lines[i].substr(t1 + 1, t2 - t1 - 1);
		const std::string had = lines[i].substr(t2 + 1);
		e.had = had == "1";
		if (!SafeEntryRel(e.rel) || e.sha.size() != 64 || (had != "0" && had != "1"))
			continue;
		entries.push_back(e);
	}
	return !entries.empty();
}

// A swap journal that no valid marker finishes: the swap was cut short.
// 0 = nothing to do, 1 = previous files restored, 2 = every installed file
// matches the new set, so it is finished instead (marker written), 3 = some
// file could not be put back (journal kept for the next launch).
static int RecoverSwap(const FsOps &fs, const std::string &exe)
{
	const std::string swapPath = SwapJournalPath(exe);
	std::string tag;
	std::vector<SwapEntry> entries;
	if (!ReadSwapJournal(fs, exe, tag, entries))
	{
		fs.removeFile(fs.ctx, swapPath); // unreadable: nothing trustworthy to act on
		return 0;
	}
	// Undo can fail for the executable while restoring the other files. A new
	// executable alone therefore does not prove that the full swap finished.
	bool allNew = true;
	bool hasExe = false;
	for (const SwapEntry &e : entries)
	{
		hasExe = hasExe || IsExecutableRel(e.rel);
		if (!EntryIsNew(fs, exe, e.rel, e.sha))
			allNew = false;
	}
	if (hasExe && allNew)
	{
		std::vector<std::string> marker(1, tag);
		for (const SwapEntry &k : entries)
			if (fs.exists(fs.ctx, OldPath(EntryTarget(exe, k.rel))))
				marker.push_back(k.rel);
		if (fs.writeFile(fs.ctx, MarkerPath(exe), JoinLines(marker)))
		{
			fs.removeFile(fs.ctx, swapPath);
			return 2;
		}
		return 3;
	}
	bool complete = true;
	for (const SwapEntry &e : entries)
	{
		const bool folder = IsRuntimeEntry(e.rel);
		const std::string target = EntryTarget(exe, e.rel);
		const std::string old = OldPath(target);
		const bool hasOld = fs.exists(fs.ctx, old);
		const bool hasTarget = fs.exists(fs.ctx, target);
		if (hasOld)
		{
			if (hasTarget)
			{
				Bytes oldBytes;
				if (EntryIsNew(fs, exe, e.rel, e.sha))
				{
					// Keep a launchable executable even if restoring it fails.
					if (IsExecutableRel(e.rel) && fs.replaceFile)
					{
						if (!fs.replaceFile(fs.ctx, old, target))
							complete = false;
						continue;
					}
					// Only the new copy may be cleared to make room for the old one.
					if (!(folder ? fs.removeTree(fs.ctx, target) : fs.removeFile(fs.ctx, target)))
					{
						complete = false;
						continue;
					}
				}
				else if (!folder && fs.readFile(fs.ctx, old, kMaxExecutable, oldBytes) &&
				         FileHashIs(fs, target, Sha256Hex(oldBytes)))
				{
					// Cut short between keeping the executable (link or copy)
					// and the rename: the target is still the previous file
					// and .old is its duplicate.
					if (!fs.removeFile(fs.ctx, old))
						complete = false;
					continue;
				}
				else
				{
					complete = false;
					continue;
				}
			}
			if (!fs.renameFile(fs.ctx, old, target))
				complete = false;
		}
		else if (!e.had && hasTarget && EntryIsNew(fs, exe, e.rel, e.sha))
		{
			// An entry this update added; it did not exist before.
			if (!(folder ? fs.removeTree(fs.ctx, target) : fs.removeFile(fs.ctx, target)))
				complete = false;
		}
	}
	if (!complete)
		return 3;
	// Apply removed any older marker before writing this journal, so a marker
	// here can only be this swap's, written before the undo: it is void.
	fs.removeFile(fs.ctx, MarkerPath(exe));
	fs.removeFile(fs.ctx, swapPath);
	return 1;
}

StartupState StartupCheck(const FsOps &fs, const std::string &exe, const std::string &runningTag)
{
	StartupState recovery = STARTUP_CLEAN;
	const std::string swapPath = SwapJournalPath(exe);
	if (fs.exists(fs.ctx, swapPath))
	{
		// A marker beside the journal means the swap finished and only the
		// journal's removal was cut short, but only when it is this swap's
		// marker (same tag) and every entry really holds the new copy.
		std::string tag;
		std::vector<SwapEntry> entries;
		const std::vector<std::string> marker = ReadLines(fs, MarkerPath(exe));
		bool finished = !marker.empty() && ReadSwapJournal(fs, exe, tag, entries) && marker[0] == tag;
		for (const SwapEntry &e : entries)
			finished = finished && EntryIsNew(fs, exe, e.rel, e.sha);
		if (finished)
			fs.removeFile(fs.ctx, swapPath);
		else
		{
			const int r = RecoverSwap(fs, exe);
			if (r == 1)
				recovery = STARTUP_RECOVERED;
			else if (r == 3)
				recovery = STARTUP_RESTORE_FAILED;
		}
	}

	std::vector<std::string> rels;
	for (const std::string &rel : ReadLines(fs, JournalPath(exe)))
		if (SafeEntryRel(rel))
			rels.push_back(rel);
	rels.push_back(ExecutableName(PLATFORM_WINDOWS)); // the executable maps to `exe` either way
	rels.push_back(kRuntimeEntry);                     // a staged runtime folder is never trusted either
	// A failed restore keeps the staged copies too: nothing more is touched.
	if (recovery != STARTUP_RESTORE_FAILED)
		DropStaged(fs, exe, rels);
	if (recovery != STARTUP_CLEAN)
		return recovery;

	const std::string marker = MarkerPath(exe);
	if (!fs.exists(fs.ctx, marker))
		return STARTUP_CLEAN;
	const std::vector<std::string> lines = ReadLines(fs, marker);
	// "v0.3.1" and "0.3.1" are the same release.
	if (!lines.empty() && (lines[0] == runningTag || SameVersion(lines[0], runningTag)))
		return STARTUP_UPDATED;
	// Another version runs: the player restored files by hand. Keep whatever
	// files are there and forget the marker.
	fs.removeFile(fs.ctx, marker);
	return STARTUP_ROLLED_BACK;
}

void ConfirmStarted(const FsOps &fs, const std::string &exe)
{
	if (fs.exists(fs.ctx, SwapJournalPath(exe)))
		return; // recovery still owns these rollback copies
	const std::vector<std::string> lines = ReadLines(fs, MarkerPath(exe));
	bool allGone = true;
	for (size_t i = 1; i < lines.size(); i++)
	{
		if (!SafeEntryRel(lines[i]))
			continue;
		if (IsRuntimeEntry(lines[i]))
			allGone = fs.removeTree(fs.ctx, OldPath(RuntimeTarget(exe))) && allGone;
		else
			allGone = fs.removeFile(fs.ctx, OldPath(InstallTarget(exe, lines[i]))) && allGone;
	}
	// A rollback copy still held open (Windows) keeps the marker, so the next
	// start of this build tries the cleanup again.
	if (allGone)
		fs.removeFile(fs.ctx, MarkerPath(exe));
}

bool ClaimStaging(WorkSession &s, unsigned gen)
{
	if (gen != s.generation || s.staging)
		return false;
	s.staging = true;
	return true;
}
void ReleaseStaging(WorkSession &s) { s.staging = false; }
bool CancelAllowed(const WorkSession &s) { return !s.staging; }
bool DownloadAllowed(const WorkSession &s) { return !s.staging; }

} // namespace ap_update
