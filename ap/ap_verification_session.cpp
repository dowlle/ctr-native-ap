#include "ap_verification_session.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
using json = nlohmann::json;
void fail(char *out, int cap, const std::string &message) {
	if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", message.c_str());
}
bool hex64(const std::string &value) {
	if (value.size() != 64) return false;
	for (unsigned char c : value) if (!std::isxdigit(c)) return false;
	return true;
}
bool copy_text(const json &root, const char *key, char *out, size_t cap, bool hex) {
	auto it = root.find(key);
	if (it == root.end() || !it->is_string()) return false;
	const std::string value = it->get<std::string>();
	if (value.empty() || value.size() >= cap || value.find('\0') != std::string::npos || (hex && !hex64(value))) return false;
	std::memcpy(out, value.c_str(), value.size() + 1);
	return true;
}
std::string executable_path() {
#if defined(_WIN32)
	std::array<char, 32768> path{};
	DWORD n = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
	return (n && n < path.size()) ? std::string(path.data(), n) : std::string();
#else
	std::array<char, 4096> path{};
	ssize_t n = readlink("/proc/self/exe", path.data(), path.size() - 1);
	return n > 0 ? std::string(path.data(), static_cast<size_t>(n)) : std::string();
#endif
}
std::string sha256_file(const std::string &path) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) return {};
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) { EVP_MD_CTX_free(context); return {}; }
	std::array<char, 128 * 1024> buffer{};
	while (stream) {
		stream.read(buffer.data(), buffer.size());
		if (stream.gcount() > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<size_t>(stream.gcount())) != 1) { EVP_MD_CTX_free(context); return {}; }
	}
	if (!stream.eof()) { EVP_MD_CTX_free(context); return {}; }
	unsigned char digest[SHA256_DIGEST_LENGTH];
	unsigned int digest_bytes = 0;
	if (EVP_DigestFinal_ex(context, digest, &digest_bytes) != 1 || digest_bytes != sizeof digest) { EVP_MD_CTX_free(context); return {}; }
	EVP_MD_CTX_free(context);
	static const char digits[] = "0123456789abcdef";
	std::string result(64, '0');
	for (size_t i = 0; i < sizeof digest; ++i) { result[i * 2] = digits[digest[i] >> 4]; result[i * 2 + 1] = digits[digest[i] & 15]; }
	return result;
}
std::string identity_hash(std::initializer_list<std::string> parts) {
	std::string encoded;
	for (auto it = parts.begin(); it != parts.end(); ++it) {
		if (it != parts.begin()) encoded.push_back('\0');
		encoded += *it;
	}
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256(reinterpret_cast<const unsigned char *>(encoded.data()), encoded.size(), digest);
	static const char digits[] = "0123456789abcdef";
	std::string result(64, '0');
	for (size_t i = 0; i < sizeof digest; ++i) { result[i * 2] = digits[digest[i] >> 4]; result[i * 2 + 1] = digits[digest[i] & 15]; }
	return result;
}
bool confined_media_path(const char *text) {
	try {
		const auto root = std::filesystem::weakly_canonical(std::filesystem::current_path() / "custom-content-lab" / "media");
		const auto candidate = std::filesystem::weakly_canonical(std::filesystem::path(text));
		auto relative = candidate.lexically_relative(root);
		if (relative.empty() || *relative.begin() == "..") return false;
		for (auto cursor = candidate.parent_path(); cursor != root; cursor = cursor.parent_path()) {
			if (cursor.empty() || std::filesystem::is_symlink(std::filesystem::symlink_status(cursor))) return false;
		}
		return true;
	} catch (...) { return false; }
}
bool atomic_json_file(const std::filesystem::path &destination, const json &value) {
	std::filesystem::path temporary = destination; temporary += ".tmp";
	const std::string encoded = value.dump(2) + "\n";
	FILE *stream = std::fopen(temporary.string().c_str(), "wb");
	if (!stream) return false;
	bool ok = std::fwrite(encoded.data(), 1, encoded.size(), stream) == encoded.size() && std::fflush(stream) == 0;
#if defined(_WIN32)
	if (ok) ok = _commit(_fileno(stream)) == 0;
#else
	if (ok) ok = fsync(fileno(stream)) == 0;
#endif
	if (std::fclose(stream) != 0) ok = false;
	if (!ok) { std::filesystem::remove(temporary); return false; }
#if defined(_WIN32)
	if (!MoveFileExA(temporary.string().c_str(), destination.string().c_str(),
	                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		std::filesystem::remove(temporary); return false;
	}
#else
	if (std::rename(temporary.string().c_str(), destination.string().c_str()) != 0) {
		std::filesystem::remove(temporary); return false;
	}
	int directory = open(destination.parent_path().string().c_str(), O_RDONLY | O_DIRECTORY);
	if (directory < 0 || fsync(directory) != 0) { if (directory >= 0) close(directory); return false; }
	close(directory);
#endif
	return true;
}
}

extern "C" int ap_verification_session_load(const char *manifest_path,
	struct ap_verification_session *out, char *error, int error_bytes)
{
	if (!manifest_path || !out) { fail(error, error_bytes, "manifest path and output are required"); return 0; }
	try {
		if (std::filesystem::is_symlink(std::filesystem::symlink_status(manifest_path)) ||
		    !std::filesystem::is_regular_file(std::filesystem::status(manifest_path)))
			throw std::runtime_error("manifest must be a regular non-symlink file");
		const auto size = std::filesystem::file_size(manifest_path);
		if (size == 0 || size > 64 * 1024) throw std::runtime_error("manifest size is outside 1..65536 bytes");
		std::ifstream stream(manifest_path, std::ios::binary);
		json root = json::parse(stream, nullptr, true, true);
		if (!root.is_object()) throw std::runtime_error("manifest must be an object");
		const std::set<std::string> allowed = {"schema_version","nonce","variant_id","content_id","track_id","version",
			"lev_media_id","vrm_media_id","lev_path","vrm_path","lev_sha256","vrm_sha256","lev_bytes","vrm_bytes",
			"host","mode","created_at","expires_at","created_unix","expires_unix"};
		for (auto it = root.begin(); it != root.end(); ++it) if (!allowed.count(it.key())) throw std::runtime_error("unknown manifest field: " + it.key());
		if (!root.contains("schema_version") || !root["schema_version"].is_number_integer() || root["schema_version"] != 2)
			throw std::runtime_error("unsupported manifest schema");
		std::memset(out, 0, sizeof *out);
		if (!copy_text(root,"nonce",out->nonce,sizeof out->nonce,true) ||
		    !copy_text(root,"variant_id",out->variant_id,sizeof out->variant_id,true) ||
		    !copy_text(root,"content_id",out->content_id,sizeof out->content_id,true) ||
		    !copy_text(root,"version",out->version,sizeof out->version,false) ||
		    !copy_text(root,"lev_path",out->lev_path,sizeof out->lev_path,false) ||
		    !copy_text(root,"vrm_path",out->vrm_path,sizeof out->vrm_path,false) ||
		    !copy_text(root,"lev_sha256",out->lev_sha256,sizeof out->lev_sha256,true) ||
		    !copy_text(root,"vrm_sha256",out->vrm_sha256,sizeof out->vrm_sha256,true))
			throw std::runtime_error("manifest contains missing or malformed identity/path/hash text");
		for (const char *key : {"track_id","lev_media_id","vrm_media_id","lev_bytes","vrm_bytes","host","created_unix","expires_unix"})
			if (!root.contains(key) || !root[key].is_number_integer()) throw std::runtime_error(std::string("manifest integer missing or malformed: ") + key);
		out->track_id = root["track_id"].get<long long>(); out->lev_media_id = root["lev_media_id"].get<long long>();
		out->vrm_media_id = root["vrm_media_id"].get<long long>(); out->lev_bytes = root["lev_bytes"].get<long long>(); out->vrm_bytes = root["vrm_bytes"].get<long long>();
		if (out->track_id <= 0 || out->lev_media_id <= 0 || out->vrm_media_id <= 0 || out->lev_bytes <= 0 || out->vrm_bytes <= 0)
			throw std::runtime_error("manifest identity and byte counts must be positive");
		if (out->lev_bytes > 16 * 1024 * 1024 || out->vrm_bytes > 4 * 1024 * 1024)
			throw std::runtime_error("manifest byte counts exceed loader limits");
		if (!std::filesystem::path(out->lev_path).is_absolute() || !std::filesystem::path(out->vrm_path).is_absolute())
			throw std::runtime_error("manifest media paths must be absolute");
		if (!confined_media_path(out->lev_path) || !confined_media_path(out->vrm_path))
			throw std::runtime_error("manifest media path escapes the fixed cache root or crosses a symlink");
		out->host = root["host"].get<int>();
		if (out->host < 0 || out->host > 17) throw std::runtime_error("manifest host is outside race slot range");
		out->expires_unix = root["expires_unix"].get<long long>();
		const std::string mode = root.value("mode", "");
		if (mode == "time_trial") out->mode = 1;
		else if (mode == "relic_race") out->mode = 2;
		else if (mode == "ctr_challenge") out->mode = 3;
		else if (mode == "arcade") out->mode = 4;
		else throw std::runtime_error("manifest mode is unsupported by race review");
		if (identity_hash({out->lev_sha256, out->vrm_sha256}) != out->content_id)
			throw std::runtime_error("manifest content_id does not match pair hashes");
		if (identity_hash({std::to_string(out->track_id), out->version, std::to_string(out->lev_media_id), std::to_string(out->vrm_media_id)}) != out->variant_id)
			throw std::runtime_error("manifest variant_id does not match media identity");
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		const long long created = root["created_unix"].get<long long>();
		if (created > static_cast<long long>(now) + 60 || out->expires_unix <= static_cast<long long>(now) ||
		    out->expires_unix <= created || out->expires_unix - created > 3600)
			throw std::runtime_error("manifest lifetime is invalid or expired");
		fail(error, error_bytes, ""); return 1;
	} catch (const std::exception &exc) { fail(error, error_bytes, exc.what()); std::memset(out, 0, sizeof *out); return 0; }
}

extern "C" int ap_verification_session_write_receipt(const char *receipt_path,
	const struct ap_verification_session *session, const char *git_commit, int dirty,
	const char *feature_configuration, char *error, int error_bytes)
{
	if (!receipt_path || !session || !git_commit || !feature_configuration) { fail(error,error_bytes,"receipt arguments are required"); return 0; }
	try {
		const std::string exe = executable_path();
		const std::string exe_hash = sha256_file(exe);
		if (exe.empty() || exe_hash.empty()) throw std::runtime_error("could not hash running executable image");
		const long long now = static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
		json receipt = {{"schema_version",2},{"complete",false},{"nonce",session->nonce},{"variant_id",session->variant_id},
			{"content_id",session->content_id},{"track_id",session->track_id},{"version",session->version},
			{"lev_media_id",session->lev_media_id},{"vrm_media_id",session->vrm_media_id},
			{"lev_sha256",session->lev_sha256},{"vrm_sha256",session->vrm_sha256},{"lev_bytes",session->lev_bytes},{"vrm_bytes",session->vrm_bytes},
			{"host",session->host},{"mode",session->mode == 1 ? "time_trial" : session->mode == 2 ? "relic_race" : session->mode == 3 ? "ctr_challenge" : "arcade"},
			{"start_time_unix",now},{"git_commit",git_commit},{"dirty",dirty != 0},{"feature_configuration",feature_configuration},
			{"executable_path",exe},{"executable_sha256",exe_hash}};
		if (!atomic_json_file(receipt_path, receipt)) throw std::runtime_error("could not durably publish receipt");
		fail(error,error_bytes,""); return 1;
	} catch (const std::exception &exc) { fail(error,error_bytes,exc.what()); return 0; }
}

extern "C" int ap_verification_session_finalize_receipt(const char *receipt_path,
	char *error, int error_bytes)
{
	if (!receipt_path) { fail(error,error_bytes,"receipt path is required"); return 0; }
	try {
		std::ifstream input(receipt_path, std::ios::binary);
		json receipt = json::parse(input, nullptr, true, true);
		if (!receipt.is_object() || receipt.value("complete", true)) throw std::runtime_error("receipt is missing or already complete");
		receipt["complete"] = true;
		receipt["end_time_unix"] = static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
		if (!atomic_json_file(receipt_path, receipt)) throw std::runtime_error("could not durably finalize receipt");
		fail(error,error_bytes,""); return 1;
	} catch (const std::exception &exc) { fail(error,error_bytes,exc.what()); return 0; }
}

extern "C" int ap_verification_session_consume_manifest(const char *pending_path,
	const char *armed_path, char *error, int error_bytes)
{
	if (!pending_path || !armed_path) { fail(error,error_bytes,"manifest paths are required"); return 0; }
#if defined(_WIN32)
	if (!MoveFileExA(pending_path, armed_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		fail(error,error_bytes,"could not atomically consume manifest"); return 0;
	}
#else
	if (std::rename(pending_path, armed_path) != 0) { fail(error,error_bytes,"could not atomically consume manifest"); return 0; }
	const auto parent = std::filesystem::path(armed_path).parent_path();
	int directory = open(parent.string().c_str(), O_RDONLY | O_DIRECTORY);
	if (directory < 0 || fsync(directory) != 0) { if (directory >= 0) close(directory); fail(error,error_bytes,"could not sync consumed manifest"); return 0; }
	close(directory);
#endif
	fail(error,error_bytes,""); return 1;
}
