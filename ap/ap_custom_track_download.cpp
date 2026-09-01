// Small, release-scoped Project Saphi downloader for manager-light. It lives in
// the isolated C++ AP archive because that target already owns TLS, JSON and a
// real thread runtime; the C unity build only sees the narrow API above.

#include "ap_custom_track_download.h"

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
		asio::connect(stream.next_layer(), resolver.resolve(kSaphiHost, "443"));
		stream.handshake(asio::ssl::stream_base::client);
		const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + kSaphiHost +
		                            "\r\nUser-Agent: CTR-AP-alpha7/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
		asio::write(stream, asio::buffer(request));
		std::vector<unsigned char> raw;
		unsigned char chunk[16384];
		asio::error_code ec;
		for (;;)
		{
			size_t got = stream.read_some(asio::buffer(chunk), ec);
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
