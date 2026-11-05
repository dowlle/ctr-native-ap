// Small, release-scoped Project Saphi downloader for manager-light. It lives in
// the isolated C++ AP archive because that target already owns TLS, JSON and a
// real thread runtime; the C unity build only sees the narrow API above.

#include "ap_custom_track_download.h"
#include "ap_seed_content_logic.h" // ap_seed_size_agrees, ap_seed_root_safe (Current Seed, #133 PR 7)
#include "ap_track_registry.h"     // ap_https_get (row 10k)
#ifdef CTR_CUSTOM_PACKAGES
// The Track Manager's Saphi catalogue and package install.
#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_package.h>
#include <zlib.h>
#include <future>
#endif

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <cerrno>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY_NEVER
#define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0
#endif
#ifndef WINHTTP_QUERY_RETRY_AFTER
#define WINHTTP_QUERY_RETRY_AFTER 36
#endif
#ifndef ERROR_WINHTTP_TIMEOUT
#define ERROR_WINHTTP_TIMEOUT 12002
#endif
#include <direct.h>
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
// The only hosts the transport talks to (row 10k): Project Saphi for track
// files, GitHub raw for the public track registry. Redirects are not followed.
constexpr const char *kRegistryHost = "raw.githubusercontent.com";

bool host_allowed(const std::string &host)
{
	return host == kSaphiHost || host == kRegistryHost;
}

std::string host_label(const std::string &host)
{
	return host == kSaphiHost ? "Project Saphi" : "the track registry";
}

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

// What one HTTPS GET learned besides its body: the HTTP status (0 when no
// response arrived), the server's Retry-After in seconds (-1 when absent),
// and whether a 200 body was cut off. Saphi answers 429 with a Retry-After
// of a few seconds once a client asks for files faster than its download
// limit, which a seed install of several tracks reaches.
struct HttpOutcome
{
	int status = 0;
	int retry_after = -1;
	bool interrupted = false;
};

// The player-facing text for a failed transfer. The HTTP status is always
// named; the log line of the request has the path and request number.
std::string transfer_error(const std::string &label, const HttpOutcome &outcome)
{
	const std::string http = "(HTTP " + std::to_string(outcome.status) + ")";
	if (outcome.status == 429)
		return label + " is busy " + http + ". Retry in a minute.";
	if (outcome.status >= 500)
		return label + " is unavailable " + http + ". Retry later.";
	if (outcome.interrupted)
		return label + " stopped sending the file " + http + ". Retry.";
	return label + " refused the file " + http + ".";
}

// Only a busy or briefly unavailable server, or a 200 cut off mid-file, is
// asked again; a refusal, a timeout or a file that fails its checks is not.
constexpr int kTransferAttempts = 4;
bool transfer_retryable(const HttpOutcome &outcome)
{
	return outcome.status == 429 || outcome.status == 502 || outcome.status == 503 ||
	       outcome.status == 504 || outcome.interrupted;
}

// Seconds before attempt `attempt` + 1: the server's Retry-After when given,
// else 2, 4, 6; never under 1 or over 60. An HTTP-date Retry-After is not
// read and falls back to 2, 4, 6 (Saphi sends seconds).
int transfer_retry_delay(const HttpOutcome &outcome, int attempt)
{
	const int seconds = outcome.retry_after >= 0 ? outcome.retry_after : 2 * attempt;
	return std::max(1, std::min(60, seconds));
}

// Runs `fetch` (one GET into a cleared body) up to kTransferAttempts times.
// Every failed attempt is logged with a client request number, the host and
// path, the HTTP status and Retry-After, so a tester's log tells a rate
// limit from a missing file or a network failure. Saphi sends no request id
// of its own.
template <typename Fetch, typename Wait>
bool transfer_with_retries(const std::string &host, const std::string &path, Fetch fetch, Wait wait,
	                       std::string &error)
{
	static std::mutex request_mutex;
	static unsigned next_request = 0;
	for (int attempt = 1;; attempt++)
	{
		unsigned request;
		{
			std::lock_guard<std::mutex> lock(request_mutex);
			request = ++next_request;
		}
		HttpOutcome outcome;
		std::string failure;
		if (fetch(outcome, failure))
			return true;
		const bool again = attempt < kTransferAttempts && transfer_retryable(outcome);
		const int delay = again ? transfer_retry_delay(outcome, attempt) : 0;
		std::fprintf(stderr, "[Saphi] request %u failed: GET %s%s http=%d retry_after=%d attempt=%d/%d: %s%s\n",
		             request, host.c_str(), path.c_str(), outcome.status, outcome.retry_after, attempt,
		             kTransferAttempts, failure.c_str(),
		             again ? (" Retrying in " + std::to_string(delay) + " s.").c_str() : "");
		if (!again)
		{
			error = failure;
			return false;
		}
		wait(delay);
	}
}

bool decode_http_response(const std::vector<unsigned char> &raw, size_t max_bytes,
	                      std::vector<unsigned char> &body, std::string &error,
	                      const std::string &label = "Project Saphi", HttpOutcome *outcome = nullptr)
{
	HttpOutcome ignored;
	HttpOutcome &result = outcome ? *outcome : ignored;
	const std::string marker = "\r\n\r\n";
	auto header_end = std::search(raw.begin(), raw.end(), marker.begin(), marker.end());
	if (header_end == raw.end())
	{
		error = label + " returned an invalid HTTP response.";
		return false;
	}
	const size_t header_bytes = static_cast<size_t>(header_end - raw.begin());
	const std::string headers(reinterpret_cast<const char *>(raw.data()), header_bytes);
	const size_t line_end = headers.find("\r\n");
	const std::string status_line = headers.substr(0, line_end == std::string::npos ? headers.size() : line_end);
	const size_t status_at = status_line.find(' ');
	if (status_at != std::string::npos && status_line.compare(0, 5, "HTTP/") == 0)
	{
		char *end = nullptr;
		const long status = std::strtol(status_line.c_str() + status_at + 1, &end, 10);
		if (end && end != status_line.c_str() + status_at + 1 && status >= 100 && status <= 999 &&
		    (*end == ' ' || *end == '\0'))
			result.status = static_cast<int>(status);
	}
	const std::string lower = lower_ascii(headers);
	const size_t retry_at = lower.find("\r\nretry-after:");
	if (retry_at != std::string::npos)
	{
		const char *value = lower.c_str() + retry_at + 14;
		while (*value == ' ') value++;
		char *end = nullptr;
		const long seconds = std::strtol(value, &end, 10);
		if (end != value && seconds >= 0)
			result.retry_after = static_cast<int>(std::min(seconds, 3600L));
	}
	if (result.status != 200)
	{
		error = result.status ? transfer_error(label, result) : label + " returned an invalid HTTP response.";
		return false;
	}

	const size_t body_start = header_bytes + marker.size();
	if (body_start > raw.size())
		return false;
	if (lower.find("transfer-encoding: chunked") == std::string::npos)
	{
		if (raw.size() - body_start > max_bytes)
		{
			error = label + " returned a file larger than this client accepts.";
			return false;
		}
		// A connection closed before Content-Length is a cut-off file.
		const size_t length_at = lower.find("\r\ncontent-length:");
		if (length_at != std::string::npos)
		{
			char *end = nullptr;
			const unsigned long long length = std::strtoull(lower.c_str() + length_at + 17, &end, 10);
			if (end != lower.c_str() + length_at + 17 && length != raw.size() - body_start)
			{
				result.interrupted = length > raw.size() - body_start;
				error = label + (result.interrupted ? " returned an incomplete download." : " returned an invalid HTTP response.");
				return false;
			}
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
	result.interrupted = true;
	error = label + " returned an incomplete download.";
	return false;
}

#ifdef _WIN32
std::wstring widen_ascii(const std::string &text)
{
	return std::wstring(text.begin(), text.end());
}

bool https_get_host(const std::string &host, const std::string &path, size_t max_bytes,
	                std::vector<unsigned char> &body, std::string &error, HttpOutcome &outcome)
{
	if (!host_allowed(host))
	{
		error = "This client does not download from " + host + ".";
		return false;
	}
	const std::string label = host_label(host);
	HINTERNET session = WinHttpOpen(L"CTR-AP-alpha7/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
	{
		error = "Could not start Windows HTTPS.";
		return false;
	}
	if (!WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000))
	{
		WinHttpCloseHandle(session);
		error = "Could not set up Windows HTTPS safely.";
		return false;
	}
	// The host allow-list holds only if a redirect cannot leave the host:
	// without that policy, no request.
	DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	if (!WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects))
	{
		WinHttpCloseHandle(session);
		error = "Could not set up Windows HTTPS safely.";
		return false;
	}
	// The timeouts above are per operation; this bounds the read loop like
	// the Linux transport's 35 seconds. Connect, send and the response
	// headers keep their own timeouts, so the worst case is about a minute.
	const ULONGLONG deadline = GetTickCount64() + 35000;
	const std::wstring wide_host = widen_ascii(host);
	HINTERNET connection = WinHttpConnect(session, wide_host.c_str(),
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
	{
		ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
		                         WINHTTP_NO_HEADER_INDEX) && status == 200;
		outcome.status = static_cast<int>(status);
		DWORD retry_after = 0;
		DWORD retry_size = sizeof retry_after;
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER,
		                        WINHTTP_HEADER_NAME_BY_INDEX, &retry_after, &retry_size,
		                        WINHTTP_NO_HEADER_INDEX))
			outcome.retry_after = static_cast<int>(std::min<DWORD>(retry_after, 3600));
	}
	while (ok)
	{
		DWORD available = 0;
		if (GetTickCount64() > deadline)
		{
			error = label + " request timed out; check connection and retry.";
			ok = false;
			break;
		}
		if (!WinHttpQueryDataAvailable(request, &available))
		{
			outcome.interrupted = GetLastError() != ERROR_WINHTTP_TIMEOUT;
			ok = false;
			break;
		}
		if (available == 0)
			break;
		if (body.size() + available > max_bytes)
		{
			error = label + " returned a file larger than this client accepts.";
			ok = false;
			break;
		}
		const size_t old_size = body.size();
		body.resize(old_size + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, body.data() + old_size, available, &read))
		{
			outcome.interrupted = GetLastError() != ERROR_WINHTTP_TIMEOUT;
			ok = false;
			break;
		}
		body.resize(old_size + read);
	}
	// A body shorter than its Content-Length is a cut-off file.
	DWORD length = 0;
	DWORD length_size = sizeof length;
	if (ok && WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
	                              WINHTTP_HEADER_NAME_BY_INDEX, &length, &length_size,
	                              WINHTTP_NO_HEADER_INDEX) && body.size() != length)
	{
		outcome.interrupted = body.size() < length;
		ok = false;
	}
	if (!ok && error.empty())
		error = status == 0 ? "Could not reach " + label + "." : transfer_error(label, outcome);
	if (request) WinHttpCloseHandle(request);
	if (connection) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return ok;
}
#else
#ifdef CTR_CUSTOM_PACKAGES
// Package builds carry the CA-trust and timeout fixes from the custom content
// campaign (SteamOS lacks the build host's OPENSSLDIR, and a stalled catalogue
// request must not hold the Track Manager forever). A build without packages
// keeps the plain resolver below.
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

bool https_get_host(const std::string &host, const std::string &path, size_t max_bytes,
	                std::vector<unsigned char> &body, std::string &error, HttpOutcome &outcome)
{
	if (!host_allowed(host))
	{
		error = "This client does not download from " + host + ".";
		return false;
	}
	const std::string label = host_label(host);
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
		if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
			throw std::runtime_error("TLS host setup failed");
		stream.set_verify_mode(asio::ssl::verify_peer);
		stream.set_verify_callback(asio::ssl::host_name_verification(host));
		asio::ip::tcp::resolver resolver(io);
#ifdef CTR_CUSTOM_PACKAGES
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
		auto endpoints = saphi_await(io, resolver.async_resolve(host, "443", asio::use_future), deadline);
		saphi_await(io, asio::async_connect(stream.next_layer(), endpoints, asio::use_future), deadline);
		saphi_await(io, stream.async_handshake(asio::ssl::stream_base::client, asio::use_future), deadline);
#else
		asio::connect(stream.next_layer(), resolver.resolve(host, "443"));
		stream.handshake(asio::ssl::stream_base::client);
#endif
		const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
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
					error = label + " returned a file larger than this client accepts.";
					return false;
				}
				raw.insert(raw.end(), chunk, chunk + got);
			}
			if (ec == asio::error::eof || ec == asio::ssl::error::stream_truncated)
				break;
			if (ec)
				throw asio::system_error(ec);
		}
		return decode_http_response(raw, max_bytes, body, error, label, &outcome);
	}
	catch (const std::exception &e)
	{
		error = "Could not reach " + label + ": " + e.what();
		return false;
	}
}
#endif

// One allow-listed GET with the retry and logging policy above.
bool https_get_retrying(const std::string &host, const std::string &path, size_t max_bytes,
	                    std::vector<unsigned char> &body, std::string &error)
{
	return transfer_with_retries(host, path,
		[&](HttpOutcome &outcome, std::string &failure) {
			body.clear();
			return https_get_host(host, path, max_bytes, body, failure, outcome);
		},
		[](int seconds) { std::this_thread::sleep_for(std::chrono::seconds(seconds)); }, error);
}

bool https_get(const std::string &path, size_t max_bytes,
	           std::vector<unsigned char> &body, std::string &error)
{
	return https_get_retrying(kSaphiHost, path, max_bytes, body, error);
}

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

// Current Seed installs (#133 PR 7) create the package folder themselves,
// and only after both files are verified, so a refused download leaves no
// trace. Nothing they write may be reached through a link: the package
// folder, its original folder and every file name are checked without
// following links (lstat; a reparse point on Windows).
enum NodeKind { kNodeAbsent, kNodeDirectory, kNodeFile, kNodeOther };

NodeKind node_kind(const std::string &path)
{
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesA(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? kNodeAbsent : kNodeOther;
	if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		return kNodeOther;
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? kNodeDirectory : kNodeFile;
#else
	struct stat st;
	if (lstat(path.c_str(), &st) != 0)
		return errno == ENOENT || errno == ENOTDIR ? kNodeAbsent : kNodeOther;
	if (S_ISDIR(st.st_mode)) return kNodeDirectory;
	if (S_ISREG(st.st_mode)) return kNodeFile;
	return kNodeOther;
#endif
}

bool make_directory(const std::string &path)
{
	const NodeKind kind = node_kind(path);
	if (kind != kNodeAbsent)
		return kind == kNodeDirectory;
#ifdef _WIN32
	_mkdir(path.c_str());
#else
	mkdir(path.c_str(), 0755);
#endif
	return node_kind(path) == kNodeDirectory;
}

// 1 when path does not exist, 2 when it holds exactly `expected`, 0 otherwise
// (another file, a link, or a name that is not a regular file).
int existing_target(const std::string &path, const std::string &expected);

struct MediaFile
{
	std::string path;
	size_t size = 0;
};

// One file in Saphi's listing for a track. `prefix` is the track's own
// downloads path plus '/'; a file elsewhere is never taken. A registry lookup
// names the Saphi download id (row 10k); a compiled profile picks by version.
bool find_media(const nlohmann::json &root, const std::string &prefix, const std::string &type,
	            const std::string &version, int download_id, MediaFile &media)
{
	if (!root.contains("data") || !root["data"].is_array())
		return false;
	int matches = 0;
	for (const auto &row : root["data"])
	{
		if (!row.is_object() || row.value("type", "") != type)
			continue;
		if (download_id > 0)
		{
			if (!row.contains("id") || !row["id"].is_number_integer() || row["id"].get<long long>() != download_id)
				continue;
		}
		else if (row.value("version", "") != version)
			continue;
		const std::string path = row.value("download_url", "");
		const long long size = row.value("file_size", 0LL);
		if (path.rfind(prefix, 0) != 0 || size <= 0 || size > static_cast<long long>(kTrackFileMax) ||
		    (download_id > 0 && path != prefix + std::to_string(download_id)))
			continue;
		media.path = path;
		media.size = static_cast<size_t>(size);
		matches++;
	}
	return matches == 1;
}

// Publish a verified pinned install (Current Seed, #133 PR 7). It never
// writes through a link, never replaces or removes a file it did not write,
// and never takes anything back: each file is written to an unnamed file and
// then given its name with linkat, which fails when the name exists.
//
// POSIX works on directory handles opened without following links:
// <tracks>/<folder> and its original/ are created and opened with
// O_NOFOLLOW. The data goes into an O_TMPFILE file in original/ and is named
// through /proc/self/fd, so no temporary name exists that anything could
// swap. Where the file system has no O_TMPFILE, a fresh exclusive name is
// used: it is linked and later removed only while it still is this install's
// open file (a swap in the instant between that check and the call remains
// possible on such file systems). A name counts as published only
// when it is this install's still-open file, and the bytes are read back
// through that handle. The tracks folder itself is the player's
// configuration and may be a link.
//
// A failure after the LEV is published leaves that verified LEV in place:
// the track then shows Missing, and installing again keeps it and adds the
// VRM. Windows takes the same steps by path (exclusive create of a fresh
// name, a move that never replaces); a folder swapped for a junction between
// the check and the write is not caught there.
// `lev` / `vrm` are null when that file already is the pinned one.
void publish_pinned(const std::string &tracks, const std::string &folder,
                    const std::vector<unsigned char> *lev, const std::vector<unsigned char> *vrm)
{
	static const char *const names[2] = {"track.lev", "track.vrm"};
	const std::vector<unsigned char> *bytes[2] = {lev, vrm};
	bool done[2] = {false, false};
	std::string failure;
#ifdef _WIN32
	const std::string original = join_path(join_path(tracks, folder.c_str()), "original");
	if (!make_directory(join_path(tracks, folder.c_str())) || !make_directory(original))
		throw std::runtime_error("Could not create the track folder. Nothing was installed.");
	for (int i = 0; failure.empty() && i < 2; ++i)
	{
		if (!bytes[i]) continue;
		std::string temp;
		HANDLE h = INVALID_HANDLE_VALUE;
		for (int k = 0; k < 64 && h == INVALID_HANDLE_VALUE; ++k)
		{
			char name[64];
			std::snprintf(name, sizeof name, ".saphi-%lu-%d.part", static_cast<unsigned long>(GetCurrentProcessId()), k);
			temp = join_path(original, name);
			h = CreateFileA(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
		}
		DWORD wrote = 0;
		const bool ok = h != INVALID_HANDLE_VALUE &&
		                WriteFile(h, bytes[i]->data(), static_cast<DWORD>(bytes[i]->size()), &wrote, nullptr) &&
		                wrote == bytes[i]->size() && FlushFileBuffers(h);
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
		if (ok && MoveFileExA(temp.c_str(), join_path(original, names[i]).c_str(), MOVEFILE_WRITE_THROUGH))
			done[i] = true;
		else
		{
			if (h != INVALID_HANDLE_VALUE) std::remove(temp.c_str());
			failure = "Could not finish installing the verified Saphi files.";
		}
	}
#else
	const int tracks_fd = open(tracks.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (tracks_fd < 0)
		throw std::runtime_error("Could not open the tracks folder. Nothing was installed.");
	int root_fd = -1, original_fd = -1;
	if ((mkdirat(tracks_fd, folder.c_str(), 0755) != 0 && errno != EEXIST) ||
	    (root_fd = openat(tracks_fd, folder.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0 ||
	    (mkdirat(root_fd, "original", 0755) != 0 && errno != EEXIST) ||
	    (original_fd = openat(root_fd, "original", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0)
		failure = "Its folder is a link or could not be created.";
	// Each written file stays open until the end, so its inode cannot be
	// reused: a published name is ours only while it is that open file.
	int fds[2] = {-1, -1};
	struct stat mine[2];
	for (int i = 0; failure.empty() && i < 2; ++i)
	{
		if (!bytes[i]) continue;
		char temp[64] = "";
		int fd = -1;
#ifdef O_TMPFILE
		fd = openat(original_fd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0644);
#endif
		for (int k = 0; fd < 0 && k < 64; ++k)
		{
			std::snprintf(temp, sizeof temp, ".saphi-%d-%d.part", static_cast<int>(getpid()), k);
			fd = openat(original_fd, temp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
			if (fd < 0 && errno != EEXIST) break;
		}
		fds[i] = fd;
		// Identify the file before writing, so cleanup after a failed write or
		// fsync still knows which fallback name is this install's own.
		const bool identified = fd >= 0 && fstat(fd, &mine[i]) == 0;
		size_t at = 0;
		while (identified && at < bytes[i]->size())
		{
			const ssize_t n = write(fd, bytes[i]->data() + at, bytes[i]->size() - at);
			if (n <= 0) break;
			at += static_cast<size_t>(n);
		}
		const bool written = identified && at == bytes[i]->size() && fsync(fd) == 0;
		bool linked = false;
		// The fallback's fresh name is used, and later removed, only while it
		// still is this install's open file.
		auto temp_is_mine = [&]() {
			struct stat now;
			return fstatat(original_fd, temp, &now, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(now.st_mode) &&
			       now.st_dev == mine[i].st_dev && now.st_ino == mine[i].st_ino;
		};
		bool temp_swapped = false;
		if (written && temp[0])
		{
			temp_swapped = !temp_is_mine();
			linked = !temp_swapped && linkat(original_fd, temp, original_fd, names[i], 0) == 0;
		}
		else if (written)
		{
			char self[48];
			std::snprintf(self, sizeof self, "/proc/self/fd/%d", fd);
			linked = linkat(AT_FDCWD, self, original_fd, names[i], AT_SYMLINK_FOLLOW) == 0;
		}
		const bool exists_already = written && !linked && !temp_swapped && errno == EEXIST;
		if (fd >= 0 && temp[0] && (!identified || temp_is_mine())) unlinkat(original_fd, temp, 0); // the fallback's own fresh name (unidentified only when fstat failed right after its exclusive create)
		struct stat published;
		if (linked && fstatat(original_fd, names[i], &published, AT_SYMLINK_NOFOLLOW) == 0 &&
		    S_ISREG(published.st_mode) && published.st_dev == mine[i].st_dev && published.st_ino == mine[i].st_ino)
			done[i] = true;
		else if (linked || temp_swapped)
			failure = "Its folder changed during the install. Check tracks/" + folder + "/original.";
		else
			failure = exists_already ? "A track file appeared in its folder during the install."
			                         : "Could not finish installing the verified Saphi files.";
	}
	// Read each published file back through its own handle: bytes changed
	// since verification fail the install.
	for (int i = 0; failure.empty() && i < 2; ++i)
	{
		if (!done[i]) continue;
		std::vector<unsigned char> back(bytes[i]->size());
		size_t at = 0;
		while (at < back.size())
		{
			const ssize_t n = pread(fds[i], back.data() + at, back.size() - at, static_cast<off_t>(at));
			if (n <= 0) break;
			at += static_cast<size_t>(n);
		}
		if (at != back.size() || back != *bytes[i])
			failure = "A track file changed while it was installed. Rescan, then try again.";
	}
	for (int fd : fds)
		if (fd >= 0) close(fd);
	if (original_fd >= 0) close(original_fd);
	if (root_fd >= 0) close(root_fd);
	close(tracks_fd);
#endif
	if (!failure.empty())
		throw std::runtime_error(failure + (done[0] && !done[1] && bytes[1]
		                                    ? " The verified LEV is in place; Install again adds the VRM."
		                                    : done[0] || done[1] ? "" : " Nothing was installed."));
}

// `lev_bytes` / `vrm_bytes` are the seed's size pins. A seed UUID publishes
// the verified pair in tracks/packages; old callers without one retain the
// room-folder path for existing installs and compatibility harnesses.
// `lev_id` / `vrm_id` are the Saphi download ids a registry lookup named
// (row 10k), 0 to pick the files by `version`.
void worker(std::string package_root, std::string api_url, std::string version,
	        std::string lev_hash, std::string vrm_hash,
	        uint32_t lev_pin, uint32_t vrm_pin, std::string tracks, int lev_id, int vrm_id,
	        std::string seed_uuid, std::string seed_title, std::string seed_author, unsigned int seed_laps,
	        int relic_sapphire, int relic_gold, int relic_platinum)
{
	const bool pinned = !tracks.empty();
#ifndef CTR_CUSTOM_PACKAGES
	(void)seed_uuid; (void)seed_title; (void)seed_author; (void)seed_laps;
	(void)relic_sapphire; (void)relic_gold; (void)relic_platinum;
#endif
	try
	{
		std::string api_path;
		if (!saphi_url_path(api_url, api_path))
			throw std::runtime_error("The release registry has an invalid Saphi API URL.");
		if (pinned && !ap_seed_root_safe(tracks.c_str(), package_root.c_str()))
			throw std::runtime_error("The track folder is not a safe place to install. Nothing was changed.");
		set_state(AP_CT_DOWNLOAD_RUNNING, "Contacting Project Saphi...");
		std::vector<unsigned char> metadata;
		std::string error;
		if (!https_get(api_path, kMetadataMax, metadata, error))
			throw std::runtime_error(error);
		nlohmann::json root = nlohmann::json::parse(metadata.begin(), metadata.end());
		MediaFile lev;
		MediaFile vrm;
		if (!find_media(root, api_path + "/", "lev", version, lev_id, lev) ||
		    !find_media(root, api_path + "/", "vrm", version, vrm_id, vrm))
			throw std::runtime_error("Saphi does not list exactly one matching LEV and VRM for this release.");
		if (!ap_seed_size_agrees(lev_pin, lev.size) || !ap_seed_size_agrees(vrm_pin, vrm.size))
			throw std::runtime_error("Saphi lists another file size than this seed pins. Nothing was changed.");

		std::vector<unsigned char> lev_bytes;
		std::vector<unsigned char> vrm_bytes;
		set_state(AP_CT_DOWNLOAD_RUNNING, "Downloading the LEV from Project Saphi...");
		if (!https_get(lev.path, kTrackFileMax, lev_bytes, error) || lev_bytes.size() != lev.size)
			throw std::runtime_error(error.empty() ? "The Saphi LEV download was incomplete." : error);
		if (!hash_matches(lev_bytes, lev_hash))
			throw std::runtime_error("The downloaded LEV does not match the pinned file. Nothing was installed.");

		set_state(AP_CT_DOWNLOAD_RUNNING, "Downloading the VRM from Project Saphi...");
		if (!https_get(vrm.path, kTrackFileMax, vrm_bytes, error) || vrm_bytes.size() != vrm.size)
			throw std::runtime_error(error.empty() ? "The Saphi VRM download was incomplete." : error);
		if (!hash_matches(vrm_bytes, vrm_hash))
			throw std::runtime_error("The downloaded VRM does not match the pinned file. Nothing was installed.");
		if (!ap_seed_size_agrees(lev_pin, lev_bytes.size()) || !ap_seed_size_agrees(vrm_pin, vrm_bytes.size()))
			throw std::runtime_error("The downloaded files are not the size this seed pins. Nothing was changed.");

#ifdef CTR_CUSTOM_PACKAGES
		if (pinned && !seed_uuid.empty())
		{
			const std::string key = package_root.substr(tracks.size() + 1);
			const std::string assets = tracks.substr(0, tracks.size() - 7); // /tracks
			if (tracks.size() < 8 || tracks.compare(tracks.size() - 7, 7, "/tracks"))
				throw std::runtime_error("The shared package store path is invalid");
			const std::string packageTitle = CustomPackage_ValidateTitle(seed_title.data(), seed_title.size()) ? seed_title : key;
			nlohmann::json files = nlohmann::json::array({
				{{"role", "lev"}, {"path", "original/track.lev"}, {"sha256", lev_hash}, {"bytes", lev_bytes.size()}},
				{{"role", "vrm"}, {"path", "original/track.vrm"}, {"sha256", vrm_hash}, {"bytes", vrm_bytes.size()}}});
			const std::string source = nlohmann::json({{"schema_version", 1}, {"provider", "ctr-seed"},
				{"track_key", key}, {"author", seed_author}}).dump();
			std::string settings;
			std::string relicTargets;
			if (seed_laps >= 1 && seed_laps <= 7)
				settings = nlohmann::json({{"schema_version", 1}, {"laps", seed_laps},
					{"lev_sha256", lev_hash}, {"vrm_sha256", vrm_hash}}).dump();
			if (!settings.empty() && relic_sapphire > relic_gold && relic_gold > relic_platinum && relic_platinum > 0)
				relicTargets = nlohmann::json({{"schema_version", 1}, {"laps", seed_laps},
					{"lev_sha256", lev_hash}, {"vrm_sha256", vrm_hash}, {"ticks_per_second", 960},
					{"sapphire", relic_sapphire}, {"gold", relic_gold}, {"platinum", relic_platinum}}).dump();
			auto sha = [](const std::string &bytes) {
				unsigned char hash[32]; unsigned int count = 0;
				if (!EVP_Digest(bytes.data(), bytes.size(), hash, &count, EVP_sha256(), nullptr) || count != 32)
					throw std::runtime_error("Could not hash shared package metadata");
				static const char digits[] = "0123456789abcdef";
				std::string out;
				for (unsigned int i = 0; i < count; i++) { out += digits[hash[i] >> 4]; out += digits[hash[i] & 15]; }
				return out;
			};
			files.insert(files.begin() + 1, {{"role", "presentation"}, {"path", "metadata/seed-source.json"},
				{"sha256", sha(source)}, {"bytes", source.size()}});
			if (!settings.empty())
				files.insert(files.begin() + 2, {{"role", "race_settings"}, {"path", "metadata/race-settings.json"},
					{"sha256", sha(settings)}, {"bytes", settings.size()}});
			if (!relicTargets.empty())
				files.insert(files.end() - 1, {{"role", "relic_targets"}, {"path", "metadata/relic-targets.json"},
					{"sha256", sha(relicTargets)}, {"bytes", relicTargets.size()}});
			const std::string manifest = nlohmann::json({{"schema_version", 1}, {"package_uuid", seed_uuid},
				{"version", version}, {"title", packageTitle},
				{"compatibility", {{"native_contract", 1}, {"apworld_contract", 1}}},
				{"files", files}}).dump();
			unsigned char digest[EVP_MAX_MD_SIZE];
			unsigned int digestSize = 0;
			if (!EVP_Digest(manifest.data(), manifest.size(), digest, &digestSize, EVP_sha256(), nullptr) || digestSize != 32)
				throw std::runtime_error("Could not pin the downloaded package manifest");
			static const char hex[] = "0123456789abcdef";
			std::string pin;
			for (unsigned int i = 0; i < digestSize; i++) { pin += hex[digest[i] >> 4]; pin += hex[digest[i] & 15]; }
			CustomPackageOwned *owned = nullptr;
			std::vector<CustomPackageInput> inputs = {{"lev", lev_bytes.data(), lev_bytes.size()},
				{"presentation", source.data(), source.size()}};
			if (!settings.empty()) inputs.push_back({"race_settings", settings.data(), settings.size()});
			if (!relicTargets.empty()) inputs.push_back({"relic_targets", relicTargets.data(), relicTargets.size()});
			inputs.push_back({"vrm", vrm_bytes.data(), vrm_bytes.size()});
			char detail[512] = {};
			if (!CustomPackage_AcquireBuffers(manifest.data(), manifest.size(), pin.c_str(), inputs.data(), inputs.size(),
			                                   &owned, detail, sizeof detail))
				throw std::runtime_error(detail);
			const int installed = CustomPackage_InstallAssets(owned, assets.c_str(), detail, sizeof detail);
			CustomPackage_Free(&owned);
			if (!installed) throw std::runtime_error(detail);
			set_state(AP_CT_DOWNLOAD_SUCCEEDED, "Download verified. Shared package installed.");
			return;
		}
#endif

		const std::string original = join_path(package_root, "original");
		const std::string lev_temp = join_path(original, ".saphi-track.lev.part");
		const std::string vrm_temp = join_path(original, ".saphi-track.vrm.part");
		const std::string lev_target = join_path(original, "track.lev");
		const std::string vrm_target = join_path(original, "track.vrm");
		bool write_lev = true, write_vrm = true;
		if (pinned)
		{
			const NodeKind root_kind = node_kind(package_root), original_kind = node_kind(original);
			if ((root_kind != kNodeAbsent && root_kind != kNodeDirectory) ||
			    (original_kind != kNodeAbsent && original_kind != kNodeDirectory) ||
			    (root_kind == kNodeAbsent && original_kind != kNodeAbsent))
				throw std::runtime_error("Its folder is a link or not a folder. Move it out, then try again. "
				                         "Nothing was changed.");
			// Never replace a player's file: an existing track.lev / track.vrm
			// must already be the pinned bytes, and is then kept as it is.
			const int lev_existing = existing_target(lev_target, lev_hash);
			const int vrm_existing = existing_target(vrm_target, vrm_hash);
			if (!lev_existing || !vrm_existing)
				throw std::runtime_error("Its folder already has a different track.lev or track.vrm. "
				                         "Move it out, then try again. Nothing was changed.");
			write_lev = lev_existing == 1;
			write_vrm = vrm_existing == 1;
			publish_pinned(tracks, package_root.substr(tracks.size() + 1), write_lev ? &lev_bytes : nullptr,
			               write_vrm ? &vrm_bytes : nullptr);
			set_state(AP_CT_DOWNLOAD_SUCCEEDED, "Download verified. Finishing local setup...");
			return;
		}
		if ((write_lev && !write_file(lev_temp, lev_bytes)) || (write_vrm && !write_file(vrm_temp, vrm_bytes)))
		{
			std::remove(lev_temp.c_str());
			std::remove(vrm_temp.c_str());
			throw std::runtime_error("Could not write the downloaded files to the track folder.");
		}
		if ((write_lev && !replace_file(lev_temp, lev_target)) || (write_vrm && !replace_file(vrm_temp, vrm_target)))
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

int existing_target(const std::string &path, const std::string &expected)
{
	struct stat st;
	const NodeKind kind = node_kind(path);
	if (kind == kNodeAbsent)
		return 1;
	if (kind != kNodeFile || stat(path.c_str(), &st) != 0 || st.st_size <= 0 ||
	    static_cast<uint64_t>(st.st_size) > kTrackFileMax)
		return 0;
	std::ifstream file(path, std::ios::binary);
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return file.good() || file.eof() ? (hash_matches(bytes, expected) ? 2 : 0) : 0;
}

int start_worker(const char *package_root, const char *download_api_url, const char *package_version,
	             const char *lev_sha256, const char *vrm_sha256,
	             uint32_t lev_bytes, uint32_t vrm_bytes, const char *tracks, int lev_id = 0, int vrm_id = 0,
	             const char *seed_uuid = nullptr, const char *seed_title = nullptr,
	             const char *seed_author = nullptr, unsigned int seed_laps = 0,
	             int relic_sapphire = 0, int relic_gold = 0, int relic_platinum = 0)
{
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
		std::thread(worker, std::string(package_root), std::string(download_api_url), std::string(package_version),
		            std::string(lev_sha256), std::string(vrm_sha256), lev_bytes, vrm_bytes,
		            std::string(tracks ? tracks : ""), lev_id, vrm_id,
		            std::string(seed_uuid ? seed_uuid : ""), std::string(seed_title ? seed_title : ""),
		            std::string(seed_author ? seed_author : ""), seed_laps,
		            relic_sapphire, relic_gold, relic_platinum).detach();
		return 1;
	}
	catch (const std::exception &e)
	{
		set_state(AP_CT_DOWNLOAD_FAILED, std::string("Could not start downloader: ") + e.what());
		return 0;
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
	return start_worker(package_root, download_api_url, package_version, lev_sha256, vrm_sha256, 0, 0, nullptr);
}

extern "C" int ap_custom_track_download_start_pinned(const char *tracks_dir,
	                                                  const char *package_root,
	                                                  const char *download_api_url,
	                                                  const char *package_version,
	                                                  const char *lev_sha256, uint32_t lev_bytes,
	                                                  const char *vrm_sha256, uint32_t vrm_bytes)
{
	if (!tracks_dir || !*tracks_dir || !package_root || !*package_root || !download_api_url ||
	    !*download_api_url || !package_version || !*package_version || !lev_sha256 || !vrm_sha256 ||
	    !lev_bytes || !vrm_bytes || !ap_seed_root_safe(tracks_dir, package_root))
		return 0;
	return start_worker(package_root, download_api_url, package_version, lev_sha256, vrm_sha256,
	                    lev_bytes, vrm_bytes, tracks_dir);
}

extern "C" int ap_custom_track_download_start_registry(const char *tracks_dir,
	                                                    const char *package_root,
	                                                    int saphi_track_id, int lev_download_id, int vrm_download_id,
	                                                    const char *lev_sha256, uint32_t lev_bytes,
	                                                    const char *vrm_sha256, uint32_t vrm_bytes)
{
	char api[96];
	if (saphi_track_id <= 0 || lev_download_id <= 0 || vrm_download_id <= 0 || lev_download_id == vrm_download_id)
		return 0;
	std::snprintf(api, sizeof api, "https://%s/api/v2/tracks/%d/downloads", kSaphiHost, saphi_track_id);
	if (!tracks_dir || !*tracks_dir || !package_root || !*package_root || !lev_sha256 || !vrm_sha256 ||
	    !lev_bytes || !vrm_bytes || !ap_seed_root_safe(tracks_dir, package_root))
		return 0;
	// The version is unused when download ids are named.
	return start_worker(package_root, api, "registry", lev_sha256, vrm_sha256, lev_bytes, vrm_bytes, tracks_dir,
	                    lev_download_id, vrm_download_id);
}

extern "C" int ap_custom_track_download_start_shared(const char *tracks_dir, const char *package_root,
    const char *download_api_url, const char *revision, const char *uuid,
    const char *title, const char *author, unsigned int laps,
    int relic_sapphire, int relic_gold, int relic_platinum,
    const char *lev_sha256, uint32_t lev_bytes, const char *vrm_sha256, uint32_t vrm_bytes,
    int saphi_track_id, int lev_download_id, int vrm_download_id)
{
#ifndef CTR_CUSTOM_PACKAGES
    (void)tracks_dir; (void)package_root; (void)download_api_url; (void)revision; (void)uuid;
    (void)title; (void)author; (void)laps; (void)relic_sapphire; (void)relic_gold; (void)relic_platinum;
    (void)lev_sha256; (void)lev_bytes; (void)vrm_sha256; (void)vrm_bytes;
    (void)saphi_track_id; (void)lev_download_id; (void)vrm_download_id;
    return 0;
#else
    char api[96];
    if (!tracks_dir || !package_root || !revision || !*revision || !uuid || !*uuid ||
        !title || !*title || !author || !*author || laps < 1 || laps > 7 ||
        !lev_sha256 || !vrm_sha256 || !lev_bytes || !vrm_bytes ||
        !ap_seed_root_safe(tracks_dir, package_root)) return 0;
    if (saphi_track_id)
    {
        if (saphi_track_id <= 0 || lev_download_id <= 0 || vrm_download_id <= 0 || lev_download_id == vrm_download_id)
            return 0;
        std::snprintf(api, sizeof api, "https://%s/api/v2/tracks/%d/downloads", kSaphiHost, saphi_track_id);
        download_api_url = api;
    }
    if (!download_api_url || !*download_api_url) return 0;
    return start_worker(package_root, download_api_url, revision, lev_sha256, vrm_sha256,
                        lev_bytes, vrm_bytes, tracks_dir, lev_download_id, vrm_download_id,
                        uuid, title, author, laps, relic_sapphire, relic_gold, relic_platinum);
#endif
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
// catalogue revision into the package store.
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
        /* The track's own music, when Saphi lists one current .sca. It is
           pinned like LEV and VRM; the race decides whether it can play it. */
        std::vector<unsigned char> sca;
        if (revision.sca.id) fetch(revision.sca, sca);
        auto levHash = saphi_digest(lev.data(), lev.size(), EVP_sha256());
        auto vrmHash = saphi_digest(vrm.data(), vrm.size(), EVP_sha256());
        nlohmann::json files = nlohmann::json::array();
        files.push_back({{"role","lev"},{"path","original/lev-" + std::to_string(revision.lev.id) + ".lev"},{"sha256",levHash},{"bytes",lev.size()}});
        nlohmann::json source = {{"schema_version",1},{"provider","projectsaphi"},
            {"track_id",revision.trackID},{"lev_media_id",revision.lev.id},{"vrm_media_id",revision.vrm.id},
            {"version",revision.version},{"author",revision.author}};
        if (!sca.empty()) source["sca_media_id"] = revision.sca.id;
        std::string presentation = source.dump();
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
        /* Files stay in role order (the manifest pin is over the sorted list). */
        if (!sca.empty())
            files.push_back({{"role","sca"},{"path","original/sca-" + std::to_string(revision.sca.id) + ".sca"},
                {"sha256",saphi_digest(sca.data(),sca.size(),EVP_sha256())},{"bytes",sca.size()}});
        files.push_back({{"role","vrm"},{"path","original/vrm-" + std::to_string(revision.vrm.id) + ".vrm"},{"sha256",vrmHash},{"bytes",vrm.size()}});
        std::string manifest = nlohmann::json({{"schema_version",1},{"package_uuid",saphi_import_uuid(revision.trackID)},
            {"version",revision.version},{"title",revision.title},
            {"compatibility",{{"native_contract",1},{"apworld_contract",1}}},{"files",files}}).dump();
        pin = saphi_digest(manifest.data(),manifest.size(),EVP_sha256());
        std::vector<CustomPackageInput> inputs = {{"lev",lev.data(),lev.size()},{"vrm",vrm.data(),vrm.size()}};
        inputs.push_back({"presentation",presentation.data(),presentation.size()});
        if (!settings.empty()) inputs.push_back({"race_settings",settings.data(),settings.size()});
        if (!sca.empty()) inputs.push_back({"sca",sca.data(),sca.size()});
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
        !std::memchr(revision->lev.path,0,sizeof revision->lev.path) || !std::memchr(revision->vrm.path,0,sizeof revision->vrm.path) ||
        !std::memchr(revision->sca.path,0,sizeof revision->sca.path)) return 0;
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

// The transport for other units (the track registry, row 10k). Only the
// allow-listed hosts answer; see https_get_host.
bool ap_https_get(const char *host, const std::string &path, size_t max_bytes,
                  std::vector<unsigned char> &body, std::string &error)
{
	body.clear();
	return host && https_get_retrying(host, path, max_bytes, body, error);
}
