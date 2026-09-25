// Small, release-scoped Project Saphi downloader for manager-light. It lives in
// the isolated C++ AP archive because that target already owns TLS, JSON and a
// real thread runtime; the C unity build only sees the narrow API above.

#include "ap_custom_track_download.h"
#ifdef CTR_CUSTOM_PACKAGES
// Authoring build only: the Track Manager's Saphi catalogue and package install.
#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_package.h>
#include <zlib.h>
#include <chrono>
#include <future>
#endif

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <openssl/ssl.h>
#endif

namespace
{
constexpr size_t kMetadataMax = 256 * 1024;
constexpr size_t kTrackFileMax = 8 * 1024 * 1024;
constexpr const char *kSaphiHost = "www.projectsaphi.com";

struct DownloadState
{
	std::mutex mutex;
	int state = AP_CT_DOWNLOAD_IDLE;
	std::string detail = "Ready to download from Project Saphi.";
};

// Intentionally process-lifetime. A detached request must not race a static
// destructor if the player closes the game while Windows is still unwinding an
// HTTPS read.
DownloadState &download_state()
{
	static DownloadState *state = new DownloadState();
	return *state;
}

void set_state(int state, const std::string &detail)
{
	DownloadState &shared = download_state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	shared.state = state;
	shared.detail = detail;
}

bool saphi_url_path(const std::string &url, std::string &path)
{
	const std::string prefix = std::string("https://") + kSaphiHost;
	if (url.compare(0, prefix.size(), prefix) != 0)
		return false;
	path = url.substr(prefix.size());
	return !path.empty() && path[0] == '/' && path.find_first_of("\r\n") == std::string::npos;
}

std::string lower_ascii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

bool decode_http_response(const std::vector<unsigned char> &raw, size_t max_bytes,
	                      std::vector<unsigned char> &body, std::string &error)
{
	const std::string marker = "\r\n\r\n";
	auto header_end = std::search(raw.begin(), raw.end(), marker.begin(), marker.end());
	if (header_end == raw.end())
	{
		error = "Saphi returned an invalid HTTP response.";
		return false;
	}
	const size_t header_bytes = static_cast<size_t>(header_end - raw.begin());
	const std::string headers(reinterpret_cast<const char *>(raw.data()), header_bytes);
	const size_t line_end = headers.find("\r\n");
	if (line_end == std::string::npos || headers.substr(0, line_end).find(" 200 ") == std::string::npos)
	{
		error = "Project Saphi did not return the requested file.";
		return false;
	}

	const std::string lower = lower_ascii(headers);
	const size_t body_start = header_bytes + marker.size();
	if (body_start > raw.size())
		return false;
	if (lower.find("transfer-encoding: chunked") == std::string::npos)
	{
		if (raw.size() - body_start > max_bytes)
		{
			error = "Project Saphi returned a file larger than this client accepts.";
			return false;
		}
		body.assign(raw.begin() + body_start, raw.end());
		return true;
	}

	size_t cursor = body_start;
	body.clear();
	while (cursor < raw.size())
	{
		auto size_end = std::search(raw.begin() + cursor, raw.end(), marker.begin(), marker.begin() + 2);
		if (size_end == raw.end())
			break;
		std::string size_text(reinterpret_cast<const char *>(&raw[cursor]),
		                      static_cast<size_t>(size_end - (raw.begin() + cursor)));
		const size_t semicolon = size_text.find(';');
		if (semicolon != std::string::npos)
			size_text.resize(semicolon);
		char *end = nullptr;
		unsigned long chunk = std::strtoul(size_text.c_str(), &end, 16);
		if (end == size_text.c_str() || *end != '\0')
			break;
		cursor = static_cast<size_t>(size_end - raw.begin()) + 2;
		if (chunk == 0)
			return true;
		if (chunk > max_bytes || body.size() + chunk > max_bytes || cursor + chunk + 2 > raw.size())
			break;
		body.insert(body.end(), raw.begin() + cursor, raw.begin() + cursor + chunk);
		cursor += chunk;
		if (raw[cursor] != '\r' || raw[cursor + 1] != '\n')
			break;
		cursor += 2;
	}
	error = "Project Saphi returned an incomplete download.";
	return false;
}

#ifdef _WIN32
std::wstring widen_ascii(const std::string &text)
{
	return std::wstring(text.begin(), text.end());
}

bool https_get(const std::string &path, size_t max_bytes,
	           std::vector<unsigned char> &body, std::string &error)
{
	HINTERNET session = WinHttpOpen(L"CTR-AP-alpha7/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
	{
		error = "Could not start Windows HTTPS.";
		return false;
	}
	WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
	HINTERNET connection = WinHttpConnect(session, L"www.projectsaphi.com",
	                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
	const std::wstring wide_path = widen_ascii(path);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", wide_path.c_str(),
	                                                    nullptr, WINHTTP_NO_REFERER,
	                                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                                    WINHTTP_FLAG_SECURE) : nullptr;
	bool ok = request && WinHttpSendRequest(request, L"Accept-Encoding: identity\r\n", -1,
	                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	          WinHttpReceiveResponse(request, nullptr);
	DWORD status = 0;
	DWORD status_size = sizeof status;
	if (ok)
		ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
		                         WINHTTP_NO_HEADER_INDEX) && status == 200;
	while (ok)
	{
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
		{
			ok = false;
			break;
		}
		if (available == 0)
			break;
		if (body.size() + available > max_bytes)
		{
			error = "Project Saphi returned a file larger than this client accepts.";
			ok = false;
			break;
		}
		const size_t old_size = body.size();
		body.resize(old_size + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, body.data() + old_size, available, &read))
		{
			ok = false;
			break;
		}
		body.resize(old_size + read);
	}
	if (!ok && error.empty())
		error = status == 0 ? "Could not reach Project Saphi." : "Project Saphi did not return the requested file.";
	if (request) WinHttpCloseHandle(request);
	if (connection) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return ok;
}
#else
#ifdef CTR_CUSTOM_PACKAGES
// The authoring build carries the CA-trust and timeout fixes from the custom
// content campaign (SteamOS lacks the build host's OPENSSLDIR, and a stalled
// catalogue request must not hold the Track Manager forever). The player build
// keeps the resolver it was tested with, below.
bool load_ca_store(asio::ssl::context &context)
{
	asio::error_code ec;
	const char *env = std::getenv("SSL_CERT_FILE");
	const char *envDir = std::getenv("SSL_CERT_DIR");
	/* Honour explicit trust configuration; do not silently replace an invalid
	   override with system roots. The file API loads roots now, unlike the
	   lazy default-directory lookup that can report success with no usable CA.
	   In particular SteamOS does not have the build host's OPENSSLDIR. */
	if (env && *env)
	{
		context.load_verify_file(env, ec);
		if (ec) return false;
	}
	if (envDir && *envDir)
	{
		context.add_verify_path(envDir, ec);
		if (ec) return false;
	}
	if ((env && *env) || (envDir && *envDir)) return true;
	const char *const candidates[] = {
		X509_get_default_cert_file(),
		"/etc/ssl/cert.pem",
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/ca-bundle.pem"
	};
	for (const char *candidate : candidates)
	{
		if (!candidate || !*candidate)
			continue;
		/* stdio works for SteamOS overlayfs inode numbers that overflow
		   32-bit stat/readdir. Same strategy as the AP transport resolver. */
		FILE *file = std::fopen(candidate, "rb");
		if (!file) continue;
		const bool populated = std::fgetc(file) != EOF;
		std::fclose(file);
		if (!populated) continue;
		ec.clear();
		context.load_verify_file(candidate, ec);
		if (!ec)
		{
			std::fprintf(stderr, "[CustomTracks] HTTPS trust bundle: %s\n", candidate);
			return true;
		}
	}
	/* Directory-only distributions can still use OpenSSL's defaults. Never
	   treat this as proof of connectivity: hostname/peer verification remains
	   mandatory at the actual handshake. */
	context.set_default_verify_paths(ec);
	return !ec;
}

template<typename T> T saphi_await(asio::io_context &io, std::future<T> result,
                                  std::chrono::steady_clock::time_point deadline)
{
    io.restart();
    io.run_until(deadline);
    if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        throw std::runtime_error("Project Saphi request timed out; check connection and retry");
    return result.get();
}

#else
bool load_ca_store(asio::ssl::context &context)
{
	asio::error_code ec;
	context.set_default_verify_paths(ec);
	if (!ec)
		return true;
	const char *env = std::getenv("SSL_CERT_FILE");
	const char *const candidates[] = {
		env,
		"/etc/ssl/cert.pem",
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/ca-bundle.pem"
	};
	for (const char *candidate : candidates)
	{
		if (!candidate || !*candidate)
			continue;
		ec.clear();
		context.load_verify_file(candidate, ec);
		if (!ec)
			return true;
	}
	return false;
}

#endif

bool https_get(const std::string &path, size_t max_bytes,
	           std::vector<unsigned char> &body, std::string &error)
{
	try
	{
		asio::io_context io;
		asio::ssl::context context(asio::ssl::context::tls_client);
		if (!load_ca_store(context))
		{
			error = "No usable TLS certificate store was found.";
			return false;
		}
		asio::ssl::stream<asio::ip::tcp::socket> stream(io, context);
		if (!SSL_set_tlsext_host_name(stream.native_handle(), kSaphiHost))
			throw std::runtime_error("TLS host setup failed");
		stream.set_verify_mode(asio::ssl::verify_peer);
		stream.set_verify_callback(asio::ssl::host_name_verification(kSaphiHost));
		asio::ip::tcp::resolver resolver(io);
#ifdef CTR_CUSTOM_PACKAGES
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
		auto endpoints = saphi_await(io, resolver.async_resolve(kSaphiHost, "443", asio::use_future), deadline);
		saphi_await(io, asio::async_connect(stream.next_layer(), endpoints, asio::use_future), deadline);
		saphi_await(io, stream.async_handshake(asio::ssl::stream_base::client, asio::use_future), deadline);
#else
		asio::connect(stream.next_layer(), resolver.resolve(kSaphiHost, "443"));
		stream.handshake(asio::ssl::stream_base::client);
#endif
		const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + kSaphiHost +
		                            "\r\nUser-Agent: CTR-AP-alpha7/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
#ifdef CTR_CUSTOM_PACKAGES
		saphi_await(io, asio::async_write(stream, asio::buffer(request), asio::use_future), deadline);
#else
		asio::write(stream, asio::buffer(request));
#endif
		std::vector<unsigned char> raw;
		unsigned char chunk[16384];
		asio::error_code ec;
		for (;;)
		{
#ifdef CTR_CUSTOM_PACKAGES
			size_t got = saphi_await(io, stream.async_read_some(asio::buffer(chunk),
			                         asio::redirect_error(asio::use_future, ec)), deadline);
#else
			size_t got = stream.read_some(asio::buffer(chunk), ec);
#endif
			if (got)
			{
				if (raw.size() + got > max_bytes + 64 * 1024)
				{
					error = "Project Saphi returned a file larger than this client accepts.";
					return false;
				}
				raw.insert(raw.end(), chunk, chunk + got);
			}
			if (ec == asio::error::eof || ec == asio::ssl::error::stream_truncated)
				break;
			if (ec)
				throw asio::system_error(ec);
		}
		return decode_http_response(raw, max_bytes, body, error);
	}
	catch (const std::exception &e)
	{
		error = std::string("Could not reach Project Saphi: ") + e.what();
		return false;
	}
}
#endif

bool hash_matches(const std::vector<unsigned char> &bytes, const std::string &expected)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digest_size = 0;
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool ok = context && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
	          EVP_DigestUpdate(context, bytes.data(), bytes.size()) == 1 &&
	          EVP_DigestFinal_ex(context, digest, &digest_size) == 1 && digest_size == 32;
	if (context)
		EVP_MD_CTX_free(context);
	if (!ok || expected.size() != 64)
		return false;
	static const char hex[] = "0123456789abcdef";
	for (unsigned int i = 0; i < digest_size; i++)
		if (hex[digest[i] >> 4] != std::tolower(static_cast<unsigned char>(expected[i * 2])) ||
		    hex[digest[i] & 15] != std::tolower(static_cast<unsigned char>(expected[i * 2 + 1])))
			return false;
	return true;
}

bool write_file(const std::string &path, const std::vector<unsigned char> &bytes)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	file.flush();
	return file.good();
}

bool replace_file(const std::string &source, const std::string &target)
{
#ifdef _WIN32
	return MoveFileExA(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return std::rename(source.c_str(), target.c_str()) == 0;
#endif
}

std::string join_path(const std::string &left, const char *right)
{
	if (left.empty())
		return {};
	return left + ((left.back() == '/' || left.back() == '\\') ? "" : "/") + right;
}

struct MediaFile
{
	std::string path;
	size_t size = 0;
};

bool find_media(const nlohmann::json &root, const std::string &type,
	            const std::string &version, MediaFile &media)
{
	if (!root.contains("data") || !root["data"].is_array())
		return false;
	int matches = 0;
	for (const auto &row : root["data"])
	{
		if (!row.is_object() || row.value("type", "") != type || row.value("version", "") != version)
			continue;
		const std::string path = row.value("download_url", "");
		const long long size = row.value("file_size", 0LL);
		if (path.rfind("/api/v2/tracks/101/downloads/", 0) != 0 || size <= 0 || size > static_cast<long long>(kTrackFileMax))
			continue;
		media.path = path;
		media.size = static_cast<size_t>(size);
		matches++;
	}
	return matches == 1;
}

void worker(std::string package_root, std::string api_url, std::string version,
	        std::string lev_hash, std::string vrm_hash)
{
	try
	{
		std::string api_path;
		if (!saphi_url_path(api_url, api_path))
			throw std::runtime_error("The release registry has an invalid Saphi API URL.");
		set_state(AP_CT_DOWNLOAD_RUNNING, "Contacting Project Saphi...");
		std::vector<unsigned char> metadata;
		std::string error;
		if (!https_get(api_path, kMetadataMax, metadata, error))
			throw std::runtime_error(error);
		nlohmann::json root = nlohmann::json::parse(metadata.begin(), metadata.end());
		MediaFile lev;
		MediaFile vrm;
		if (!find_media(root, "lev", version, lev) || !find_media(root, "vrm", version, vrm))
			throw std::runtime_error("Saphi does not list exactly one matching LEV and VRM for this release.");

		std::vector<unsigned char> lev_bytes;
		std::vector<unsigned char> vrm_bytes;
		set_state(AP_CT_DOWNLOAD_RUNNING, "Downloading Baby T Park LEV from Saphi...");
		if (!https_get(lev.path, kTrackFileMax, lev_bytes, error) || lev_bytes.size() != lev.size)
			throw std::runtime_error(error.empty() ? "The Saphi LEV download was incomplete." : error);
		if (!hash_matches(lev_bytes, lev_hash))
			throw std::runtime_error("The downloaded LEV does not match this release registry.");

		set_state(AP_CT_DOWNLOAD_RUNNING, "Downloading Baby T Park VRM from Saphi...");
		if (!https_get(vrm.path, kTrackFileMax, vrm_bytes, error) || vrm_bytes.size() != vrm.size)
			throw std::runtime_error(error.empty() ? "The Saphi VRM download was incomplete." : error);
		if (!hash_matches(vrm_bytes, vrm_hash))
			throw std::runtime_error("The downloaded VRM does not match this release registry.");

		const std::string original = join_path(package_root, "original");
		const std::string lev_temp = join_path(original, ".saphi-track.lev.part");
		const std::string vrm_temp = join_path(original, ".saphi-track.vrm.part");
		const std::string lev_target = join_path(original, "track.lev");
		const std::string vrm_target = join_path(original, "track.vrm");
		if (!write_file(lev_temp, lev_bytes) || !write_file(vrm_temp, vrm_bytes))
		{
			std::remove(lev_temp.c_str());
			std::remove(vrm_temp.c_str());
			throw std::runtime_error("Could not write the downloaded files to the track folder.");
		}
		if (!replace_file(lev_temp, lev_target) || !replace_file(vrm_temp, vrm_target))
		{
			std::remove(lev_temp.c_str());
			std::remove(vrm_temp.c_str());
			throw std::runtime_error("Could not finish installing the verified Saphi files.");
		}
		set_state(AP_CT_DOWNLOAD_SUCCEEDED, "Download verified. Finishing local setup...");
	}
	catch (const std::exception &e)
	{
		set_state(AP_CT_DOWNLOAD_FAILED, e.what());
	}
}
} // namespace

extern "C" int ap_custom_track_download_start(const char *package_root,
	                                           const char *download_api_url,
	                                           const char *package_version,
	                                           const char *lev_sha256,
	                                           const char *vrm_sha256)
{
	if (!package_root || !*package_root || !download_api_url || !*download_api_url ||
	    !package_version || !*package_version || !lev_sha256 || !vrm_sha256)
		return 0;
	DownloadState &shared = download_state();
	{
		std::lock_guard<std::mutex> lock(shared.mutex);
		if (shared.state == AP_CT_DOWNLOAD_RUNNING)
			return 0;
		shared.state = AP_CT_DOWNLOAD_RUNNING;
		shared.detail = "Starting Project Saphi download...";
	}
	try
	{
		std::thread(worker, package_root, download_api_url, package_version,
		            lev_sha256, vrm_sha256).detach();
		return 1;
	}
	catch (const std::exception &e)
	{
		set_state(AP_CT_DOWNLOAD_FAILED, std::string("Could not start downloader: ") + e.what());
		return 0;
	}
}

extern "C" int ap_custom_track_download_status(char *detail, int detail_bytes)
{
	DownloadState &shared = download_state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	if (detail && detail_bytes > 0)
		std::snprintf(detail, static_cast<size_t>(detail_bytes), "%s", shared.detail.c_str());
	return shared.state;
}

#ifdef CTR_CUSTOM_PACKAGES
// Track Manager: the Saphi catalogue refresh and the explicit install of one
// catalogue revision into the package store. Authoring build only.
namespace {
struct SaphiRefreshState
{
    std::mutex mutex;
    int state = -1;
    CustomSaphiCatalogue *catalogue = nullptr;
    std::string error;
};
SaphiRefreshState &saphi_refresh_state()
{
    /* A game exit cannot race destruction of a detached HTTPS worker. */
    static auto *state = new SaphiRefreshState();
    return *state;
}
void saphi_refresh_worker()
{
    CustomSaphiCatalogue *catalogue = nullptr;
    std::string error;
    try
    {
        nlohmann::json combined = {{"data", nlohmann::json::array()}};
        int pages = 1;
        size_t totalBytes = 0;
        for (int page = 1; page <= pages; page++)
        {
            const std::string path = "/api/v3/tracks?per_page=100&include_standards=0&include_events=0&include_downloads=1&page=" + std::to_string(page);
            std::vector<unsigned char> body;
            if (!https_get(path, 4 * 1024 * 1024, body, error)) throw std::runtime_error(error);
            totalBytes += body.size();
            if (totalBytes > 4 * 1024 * 1024) throw std::runtime_error("Saphi catalogue exceeds this client's capacity");
            auto response = nlohmann::json::parse(body.begin(), body.end());
            const auto &meta = response.at("meta");
            if (!meta.at("current_page").is_number_integer() || meta["current_page"] != page ||
                !meta.at("total_pages").is_number_integer()) throw std::runtime_error("Invalid Saphi pagination");
            int announced = meta["total_pages"].get<int>();
            if (announced < 1 || announced > 11 || (page > 1 && announced != pages))
                throw std::runtime_error("Saphi catalogue changed during refresh; retry");
            pages = announced;
            const auto &rows = response.at("data");
            if (!rows.is_array() || rows.size() > 100 || combined["data"].size() + rows.size() > 1024)
                throw std::runtime_error("Invalid Saphi page size");
            for (const auto &row : rows) combined["data"].push_back(row);
        }
        auto bytes = combined.dump();
        char detail[512];
        if (!CustomSaphi_ParseCatalogue(bytes.data(), bytes.size(), &catalogue, detail, sizeof detail))
            throw std::runtime_error(detail);
    }
    catch (const std::exception &e) { error = e.what(); }
    auto &state = saphi_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.catalogue = catalogue;
    state.error = error;
    state.state = catalogue ? 1 : -2;
}
}
extern "C" int CustomSaphi_StartRefresh(void)
{
    auto &state = saphi_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0;
    state.error.clear();
    try { std::thread(saphi_refresh_worker).detach(); return 1; }
    catch (const std::exception &e) { state.state = -1; state.error = e.what(); return 0; }
}
extern "C" int CustomSaphi_PollRefresh(CustomSaphiCatalogue **out, char *error, size_t errorSize)
{
    auto &state = saphi_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state == 0 || state.state == -1) return state.state;
    if (!out || *out) return 0;
    int result = state.state;
    *out = state.catalogue;
    state.catalogue = nullptr;
    if (error && errorSize) std::snprintf(error, errorSize, "%s", state.error.c_str());
    state.state = -1;
    return result;
}

namespace {
std::string saphi_digest(const void *bytes, size_t size, const EVP_MD *type)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int count = 0;
    if (!EVP_Digest(bytes, size, digest, &count, type, nullptr)) throw std::runtime_error("Cannot hash Saphi payload");
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned int i = 0; i < count; i++) { result += hex[digest[i] >> 4]; result += hex[digest[i] & 15]; }
    return result;
}
std::string saphi_import_uuid(int trackID)
{
    /* Same importer namespace/UUIDv5 recipe as export_custom_content_library. */
    static const unsigned char ns[] = {0x6b,0xa7,0xb8,0x11,0x9d,0xad,0x11,0xd1,0x80,0xb4,0x00,0xc0,0x4f,0xd4,0x30,0xc8};
    std::string input(reinterpret_cast<const char *>(ns), sizeof ns);
    input += "https://www.projectsaphi.com/tracks/" + std::to_string(trackID);
    auto hex = saphi_digest(input.data(), input.size(), EVP_sha1()).substr(0, 32);
    hex[12] = '5';
    int nibble = hex[16] <= '9' ? hex[16] - '0' : hex[16] - 'a' + 10;
    hex[16] = "89ab"[nibble & 3];
    return hex.substr(0,8) + "-" + hex.substr(8,4) + "-" + hex.substr(12,4) + "-" + hex.substr(16,4) + "-" + hex.substr(20);
}
struct SaphiInstallState
{
    std::mutex mutex;
    int state = -1;
    std::string pin, error;
};
SaphiInstallState &saphi_install_state() { static auto *s = new SaphiInstallState(); return *s; }
void saphi_install_worker(CustomSaphiRevision revision, std::string assets)
{
    CustomPackageOwned *owned = nullptr;
    std::string error, pin;
    try
    {
        std::vector<unsigned char> lev, vrm;
        auto fetch = [&](const CustomSaphiMedia &media, std::vector<unsigned char> &bytes) {
            const std::string expected = "/api/v3/tracks/" + std::to_string(revision.trackID) + "/downloads/" + std::to_string(media.id);
            if (!media.bytes || media.bytes > kTrackFileMax || expected != media.path)
                throw std::runtime_error("Selected Saphi revision has no valid download");
            if (!https_get(expected, kTrackFileMax, bytes, error)) throw std::runtime_error(error);
            if (bytes.size() != media.bytes || crc32(0, bytes.data(), static_cast<uInt>(bytes.size())) != media.crc32)
                throw std::runtime_error("Saphi file differs from the selected source revision; refresh and retry");
        };
        fetch(revision.lev, lev); fetch(revision.vrm, vrm);
        auto levHash = saphi_digest(lev.data(), lev.size(), EVP_sha256());
        auto vrmHash = saphi_digest(vrm.data(), vrm.size(), EVP_sha256());
        nlohmann::json files = nlohmann::json::array();
        files.push_back({{"role","lev"},{"path","original/lev-" + std::to_string(revision.lev.id) + ".lev"},{"sha256",levHash},{"bytes",lev.size()}});
        std::string presentation = nlohmann::json({{"schema_version",1},{"provider","projectsaphi"},
            {"track_id",revision.trackID},{"lev_media_id",revision.lev.id},{"vrm_media_id",revision.vrm.id},
            {"version",revision.version},{"author",revision.author}}).dump();
        files.push_back({{"role","presentation"},{"path","metadata/saphi-source.json"},
            {"sha256",saphi_digest(presentation.data(),presentation.size(),EVP_sha256())},{"bytes",presentation.size()}});
        std::string settings;
        /* Track-level laps are applied only to current source media. An old
           revision is never silently given today's potentially changed rules. */
        if (revision.current && revision.sourceLaps >= 1 && revision.sourceLaps <= 127)
        {
            settings = nlohmann::json({{"schema_version",1},{"laps",revision.sourceLaps},{"lev_sha256",levHash},{"vrm_sha256",vrmHash}}).dump();
            files.push_back({{"role","race_settings"},{"path","metadata/race-settings.json"},
                {"sha256",saphi_digest(settings.data(),settings.size(),EVP_sha256())},{"bytes",settings.size()}});
        }
        files.push_back({{"role","vrm"},{"path","original/vrm-" + std::to_string(revision.vrm.id) + ".vrm"},{"sha256",vrmHash},{"bytes",vrm.size()}});
        std::string manifest = nlohmann::json({{"schema_version",1},{"package_uuid",saphi_import_uuid(revision.trackID)},
            {"version",revision.version},{"title",revision.title},
            {"compatibility",{{"native_contract",1},{"apworld_contract",1}}},{"files",files}}).dump();
        pin = saphi_digest(manifest.data(),manifest.size(),EVP_sha256());
        std::vector<CustomPackageInput> inputs = {{"lev",lev.data(),lev.size()},{"vrm",vrm.data(),vrm.size()}};
        inputs.push_back({"presentation",presentation.data(),presentation.size()});
        if (!settings.empty()) inputs.push_back({"race_settings",settings.data(),settings.size()});
        char detail[512];
        if (!CustomPackage_AcquireBuffers(manifest.data(),manifest.size(),pin.c_str(),inputs.data(),inputs.size(),&owned,detail,sizeof detail))
            throw std::runtime_error(detail);
        if (!CustomPackage_InstallAssets(owned, assets.c_str(), detail, sizeof detail)) throw std::runtime_error(detail);
    }
    catch (const std::exception &e) { error = e.what(); }
    CustomPackage_Free(&owned);
    auto &state = saphi_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.pin = error.empty() ? pin : "";
    state.error = error;
    state.state = error.empty() ? 1 : -2;
}
}
extern "C" int CustomSaphi_StartInstall(const CustomSaphiRevision *revision, const char *assets)
{
    if (!revision || !assets || !*assets || std::strlen(assets) > 31000 || revision->disabledReason[0] ||
        !std::memchr(revision->title,0,sizeof revision->title) || !std::memchr(revision->version,0,sizeof revision->version) ||
        !std::memchr(revision->lev.path,0,sizeof revision->lev.path) || !std::memchr(revision->vrm.path,0,sizeof revision->vrm.path)) return 0;
    auto &state = saphi_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0; state.pin.clear(); state.error.clear();
    try { std::thread(saphi_install_worker, *revision, std::string(assets)).detach(); return 1; }
    catch (const std::exception &e) { state.state = -1; state.error = e.what(); return 0; }
}
extern "C" int CustomSaphi_PollInstall(char pin[65], char *error, size_t errorSize)
{
    auto &state = saphi_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state == 0 || state.state == -1) return state.state;
    int result = state.state;
    if (pin) std::snprintf(pin,65,"%s",state.pin.c_str());
    if (error && errorSize) std::snprintf(error,errorSize,"%s",state.error.c_str());
    state.state = -1;
    return result;
}
#endif // CTR_CUSTOM_PACKAGES
