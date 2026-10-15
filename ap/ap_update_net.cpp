// Signed in-game updater, network worker (issue #301). See ap_update.h.
//
// HTTPS only, with certificate and host name verification, and only to the
// release topology in ap_update_core.h (AllowedUrl). Redirects are followed by
// hand so each hop is checked. Downloads stay in memory until the trust core
// accepts them; the only file this worker writes is the verified executable,
// staged beside the installed one. A cancelled job's result is discarded by
// generation number, so the menu never waits on a slow socket.

#include "ap_update.h"
#include "ap_update_core.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
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
#include <openssl/x509.h>
#include <spawn.h>
#include <unistd.h>
extern char **environ;
#endif

using namespace ap_update;

namespace
{

#ifdef _WIN32
const Platform kPlatform = PLATFORM_WINDOWS;
#else
const Platform kPlatform = PLATFORM_LINUX;
#endif

const char kUserAgent[] = "ctr-native-ap-updater";

struct State
{
	std::mutex mutex;
	int status = AP_UPD_IDLE;
	std::string tag;          // offered release
	std::string running;      // CTR_AP_COMPAT_VERSION
	std::string detail;
	int progress = -1;
	std::string exe;          // installed executable, resolved once at startup
	std::vector<StagedFile> staged;
	std::atomic<unsigned> generation{0};
};

// Process-lifetime on purpose: a detached worker may still be unwinding a
// socket read when the game exits.
State &S()
{
	static State *s = new State();
	return *s;
}

void SetStatus(unsigned gen, int status, const std::string &detail, int progress = -1)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (gen != s.generation.load())
		return; // cancelled or superseded: discard
	s.status = status;
	s.detail = detail;
	s.progress = progress;
}

bool Cancelled(unsigned gen)
{
	return gen != S().generation.load();
}

std::string ExecutablePath()
{
#ifdef _WIN32
	wchar_t wide[32768];
	const DWORD n = GetModuleFileNameW(nullptr, wide, (DWORD)(sizeof wide / sizeof wide[0]));
	if (n == 0 || n >= sizeof wide / sizeof wide[0])
		return std::string();
	const int len = WideCharToMultiByte(CP_UTF8, 0, wide, (int)n, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return std::string();
	std::string out((size_t)len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide, (int)n, &out[0], len, nullptr, nullptr);
	return out;
#else
	char buf[4096];
	const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
	if (n <= 0 || n >= (ssize_t)sizeof buf - 1)
		return std::string();
	buf[n] = '\0';
	std::string out(buf);
	if (out.size() > 10 && out.compare(out.size() - 10, 10, " (deleted)") == 0)
		return std::string();
	return out;
#endif
}

typedef void (*ProgressFn)(unsigned gen, size_t got, long long total);

void ArchiveProgress(unsigned gen, size_t got, long long total)
{
	if (total <= 0)
		return;
	const long long permille = (long long)got * 1000 / total;
	SetStatus(gen, AP_UPD_DOWNLOADING, "", (int)(permille > 1000 ? 1000 : permille));
}

#ifdef _WIN32
std::wstring WideAscii(const std::string &s)
{
	return std::wstring(s.begin(), s.end());
}

// One GET without automatic redirects. Fills status, location and body.
bool HttpsGet(const std::string &host, const std::string &path, size_t maxBody, unsigned gen,
              ProgressFn progress, int &status, std::string &location, Bytes &body, std::string &error)
{
	body.clear();
	location.clear();
	status = 0;
	HINTERNET session = WinHttpOpen(L"ctr-native-ap-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
	{
		error = "https unavailable";
		return false;
	}
	WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000);
	HINTERNET connection = WinHttpConnect(session, WideAscii(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", WideAscii(path).c_str(), nullptr,
	                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                                    WINHTTP_FLAG_SECURE)
	                               : nullptr;
	bool ok = request != nullptr;
	if (ok)
	{
		// Every hop is checked by AllowedUrl, so WinHTTP must not follow any.
		DWORD feature = WINHTTP_DISABLE_REDIRECTS;
		ok = WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof feature) != FALSE;
	}
	ok = ok && WinHttpSendRequest(request,
	                              L"Accept: application/vnd.github+json, application/octet-stream\r\n"
	                              L"Accept-Encoding: identity\r\n",
	                              (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	     WinHttpReceiveResponse(request, nullptr);
	DWORD code = 0, size = sizeof code;
	if (ok)
		ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                         WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
	status = (int)code;
	if (ok && code >= 300 && code < 400)
	{
		wchar_t loc[4096];
		DWORD locSize = sizeof loc;
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &locSize,
		                        WINHTTP_NO_HEADER_INDEX))
		{
			for (DWORD i = 0; i < locSize / sizeof(wchar_t) && loc[i]; i++)
				location.push_back(loc[i] < 0x80 ? (char)loc[i] : '\x7f');
		}
	}
	long long total = -1;
	if (ok && code == 200)
	{
		wchar_t lenText[32];
		DWORD lenSize = sizeof lenText;
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenText,
		                        &lenSize, WINHTTP_NO_HEADER_INDEX))
			total = _wtoi64(lenText);
		if (total > (long long)maxBody)
		{
			error = "response too large";
			ok = false;
		}
	}
	while (ok && code == 200)
	{
		if (Cancelled(gen))
		{
			error = "cancelled";
			ok = false;
			break;
		}
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
		{
			error = "connection lost";
			ok = false;
			break;
		}
		if (available == 0)
			break;
		if (body.size() + available > maxBody)
		{
			error = "response too large";
			ok = false;
			break;
		}
		const size_t old = body.size();
		body.resize(old + available);
		DWORD got = 0;
		if (!WinHttpReadData(request, body.data() + old, available, &got))
		{
			error = "connection lost";
			ok = false;
			break;
		}
		body.resize(old + got);
		if (progress)
			progress(gen, body.size(), total);
	}
	// A body shorter than its announced length is a partial download.
	if (ok && code == 200 && total >= 0 && (long long)body.size() != total)
	{
		error = "partial download";
		ok = false;
	}
	if (!ok && error.empty())
		error = code == 0 ? "could not reach GitHub" : "unexpected response";
	if (request)
		WinHttpCloseHandle(request);
	if (connection)
		WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return ok;
}
#else

// SteamOS keeps its CA store outside the build host's OPENSSLDIR, and a 32-bit
// stat() cannot see overlayfs files there; stdio can (see ap_net.cpp, #170).
bool LoadCaStore(asio::ssl::context &context)
{
	asio::error_code ec;
	const char *envFile = std::getenv("SSL_CERT_FILE");
	const char *envDir = std::getenv("SSL_CERT_DIR");
	if (envFile && *envFile)
	{
		context.load_verify_file(envFile, ec);
		if (ec)
			return false;
	}
	if (envDir && *envDir)
	{
		context.add_verify_path(envDir, ec);
		if (ec)
			return false;
	}
	if ((envFile && *envFile) || (envDir && *envDir))
		return true;
	const char *const candidates[] = {X509_get_default_cert_file(), "/etc/ssl/cert.pem",
	                                  "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
	                                  "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem"};
	for (const char *candidate : candidates)
	{
		if (!candidate || !*candidate)
			continue;
		FILE *file = std::fopen(candidate, "rb");
		if (!file)
			continue;
		const bool populated = std::fgetc(file) != EOF;
		std::fclose(file);
		if (!populated)
			continue;
		ec.clear();
		context.load_verify_file(candidate, ec);
		if (!ec)
			return true;
	}
	ec.clear();
	context.set_default_verify_paths(ec);
	return !ec;
}

// Run one asynchronous operation with a deadline and the cancel flag.
template <typename Start>
bool RunOp(asio::io_context &io, unsigned gen, std::chrono::seconds limit, Start start, asio::error_code &ec,
           size_t *bytes)
{
	bool done = false;
	ec.clear();
	start([&](const asio::error_code &e, size_t n) {
		ec = e;
		if (bytes)
			*bytes = n;
		done = true;
	});
	const auto deadline = std::chrono::steady_clock::now() + limit;
	while (!done)
	{
		io.restart();
		io.run_for(std::chrono::milliseconds(200));
		if (done)
			break;
		if (Cancelled(gen) || std::chrono::steady_clock::now() >= deadline)
		{
			io.stop();
			return false;
		}
	}
	return true;
}

bool HttpsGet(const std::string &host, const std::string &path, size_t maxBody, unsigned gen,
              ProgressFn progress, int &status, std::string &location, Bytes &body, std::string &error)
{
	body.clear();
	location.clear();
	status = 0;
	try
	{
		asio::io_context io;
		asio::ssl::context context(asio::ssl::context::tls_client);
		if (!LoadCaStore(context))
		{
			error = "no TLS certificate store";
			return false;
		}
		asio::ssl::stream<asio::ip::tcp::socket> stream(io, context);
		if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
		{
			error = "TLS setup failed";
			return false;
		}
		stream.set_verify_mode(asio::ssl::verify_peer);
		stream.set_verify_callback(asio::ssl::host_name_verification(host));

		asio::ip::tcp::resolver resolver(io);
		asio::ip::tcp::resolver::results_type endpoints;
		asio::error_code ec;
		bool finished = false;
		resolver.async_resolve(host, "443", [&](const asio::error_code &e, asio::ip::tcp::resolver::results_type r) {
			ec = e;
			endpoints = r;
			finished = true;
		});
		const auto resolveDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		while (!finished)
		{
			io.restart();
			io.run_for(std::chrono::milliseconds(200));
			if (!finished && (Cancelled(gen) || std::chrono::steady_clock::now() >= resolveDeadline))
			{
				error = Cancelled(gen) ? "cancelled" : "could not reach GitHub";
				return false;
			}
		}
		if (ec)
		{
			error = "could not reach GitHub";
			return false;
		}
		if (!RunOp(io, gen, std::chrono::seconds(15),
		           [&](std::function<void(const asio::error_code &, size_t)> h) {
			           asio::async_connect(stream.next_layer(), endpoints,
			                               [h](const asio::error_code &e, const asio::ip::tcp::endpoint &) { h(e, 0); });
		           },
		           ec, nullptr) ||
		    ec)
		{
			error = "could not reach GitHub";
			return false;
		}
		if (!RunOp(io, gen, std::chrono::seconds(15),
		           [&](std::function<void(const asio::error_code &, size_t)> h) {
			           stream.async_handshake(asio::ssl::stream_base::client,
			                                  [h](const asio::error_code &e) { h(e, 0); });
		           },
		           ec, nullptr) ||
		    ec)
		{
			error = "TLS handshake failed";
			return false;
		}
		const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: " +
		                            kUserAgent +
		                            "\r\nAccept: application/vnd.github+json, application/octet-stream\r\n"
		                            "Accept-Encoding: identity\r\nConnection: close\r\n\r\n";
		if (!RunOp(io, gen, std::chrono::seconds(15),
		           [&](std::function<void(const asio::error_code &, size_t)> h) {
			           asio::async_write(stream, asio::buffer(request), h);
		           },
		           ec, nullptr) ||
		    ec)
		{
			error = "request failed";
			return false;
		}

		Bytes raw;
		unsigned char chunk[16384];
		long long total = -2;
		size_t headerEnd = 0;
		for (;;)
		{
			size_t got = 0;
			if (!RunOp(io, gen, std::chrono::seconds(30),
			           [&](std::function<void(const asio::error_code &, size_t)> h) {
				           stream.async_read_some(asio::buffer(chunk), h);
			           },
			           ec, &got))
			{
				error = Cancelled(gen) ? "cancelled" : "download stalled";
				return false;
			}
			if (got)
			{
				if (raw.size() + got > maxBody + 65536)
				{
					error = "response too large";
					return false;
				}
				raw.insert(raw.end(), chunk, chunk + got);
			}
			if (total == -2)
			{
				static const char marker[] = "\r\n\r\n";
				auto end = std::search(raw.begin(), raw.end(), marker, marker + 4);
				if (end != raw.end())
				{
					headerEnd = (size_t)(end - raw.begin()) + 4;
					total = HeaderContentLength(std::string(raw.begin(), end));
				}
			}
			if (progress && total >= 0 && raw.size() >= headerEnd)
				progress(gen, raw.size() - headerEnd, total);
			// A server that closes without TLS close_notify ends the stream the
			// same way; completeness is then judged by ParseHttpResponse.
			if (ec == asio::error::eof || ec == asio::ssl::error::stream_truncated)
				break;
			if (ec)
			{
				error = "connection lost";
				return false;
			}
		}
		HttpResponse response;
		switch (ParseHttpResponse(raw, maxBody, response))
		{
		case HTTP_COMPLETE:
			break;
		case HTTP_INCOMPLETE:
			error = "partial download";
			return false;
		case HTTP_TOO_LARGE:
			error = "response too large";
			return false;
		default:
			// A redirect without a body length is still a redirect.
			if (response.status >= 300 && response.status < 400 && !response.location.empty())
				break;
			error = "malformed response";
			return false;
		}
		status = response.status;
		location = response.location;
		body.swap(response.body);
		return true;
	}
	catch (const std::exception &)
	{
		error = "could not reach GitHub";
		return false;
	}
}
#endif

// GET with redirects checked hop by hop against the release topology.
bool Fetch(const std::string &firstUrl, size_t maxBody, unsigned gen, ProgressFn progress, Bytes &out,
           std::string &error)
{
	std::string url = firstUrl;
	for (int hop = 0; hop <= kMaxRedirects; hop++)
	{
		std::string host, path, location;
		if (!AllowedUrl(url, hop > 0, host, path))
		{
			error = "address outside the release topology refused";
			return false;
		}
		int status = 0;
		Bytes body;
		if (!HttpsGet(host, path, maxBody, gen, progress, status, location, body, error))
			return false;
		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
		{
			url = location;
			continue;
		}
		if (status != 200)
		{
			error = "HTTP " + std::to_string(status);
			return false;
		}
		out.swap(body);
		return true;
	}
	error = "too many redirects";
	return false;
}

void CheckWorker(unsigned gen, std::string running, std::string declined)
{
	Bytes json;
	std::string error;
	ReleaseInfo info;
	if (!Fetch(LatestReleaseUrl(), kMaxReleaseJson, gen, nullptr, json, error))
	{
		SetStatus(gen, AP_UPD_NONE, "check failed: " + error);
		return;
	}
	if (!ParseLatestRelease(json, info))
	{
		SetStatus(gen, AP_UPD_NONE, "latest release is not updater-visible");
		return;
	}
	if (!ShouldOffer(info.tag, running, declined))
	{
		SetStatus(gen, AP_UPD_NONE, "no newer release to offer");
		return;
	}
	{
		State &s = S();
		std::lock_guard<std::mutex> lock(s.mutex);
		if (gen != s.generation.load())
			return;
		s.tag = info.tag;
		s.status = AP_UPD_OFFER;
		s.detail = "offer " + info.tag;
	}
}

void DownloadWorker(unsigned gen, std::string tag, std::string running, std::string exe)
{
	Bytes manifest, signature, archive;
	std::vector<InstallFile> files;
	std::string error;
	SetStatus(gen, AP_UPD_DOWNLOADING, "", 0);
	if (!Fetch(AssetUrl(tag, "manifest.json"), kMaxManifest, gen, nullptr, manifest, error) ||
	    !Fetch(AssetUrl(tag, "manifest.json.minisig"), kMaxSignature, gen, nullptr, signature, error) ||
	    !Fetch(AssetUrl(tag, ArchiveName(tag, kPlatform)), kMaxArchive, gen, ArchiveProgress, archive, error))
	{
		SetStatus(gen, AP_UPD_FAILED_DOWNLOAD, "download failed: " + error);
		return;
	}
	SetStatus(gen, AP_UPD_VERIFYING, "");
	const std::string sigText(signature.begin(), signature.end());
	Result r = VerifyRelease(AP_UPDATE_PUBLIC_KEY, manifest, sigText, tag, running, kPlatform, archive);
	if (r == OK)
		r = ExtractInstallSet(archive, kPlatform, tag, files);
	// Nothing downloaded was written to disk; dropping the buffers deletes it.
	Bytes().swap(manifest);
	Bytes().swap(archive);
	if (r != OK)
	{
		SetStatus(gen, AP_UPD_FAILED_VERIFY, std::string("verification failed: ") + ResultText(r));
		return;
	}
	if (Cancelled(gen))
		return;
	std::vector<StagedFile> staged;
	const FsOps fs = RealFsOps();
	r = Stage(fs, exe, files, staged);
	if (r != OK)
	{
		SetStatus(gen, AP_UPD_FAILED_INSTALL, std::string("staging failed: ") + ResultText(r));
		return;
	}
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (gen != s.generation.load())
	{
		// Cancelled while staging: do not leave staged files behind.
		DiscardStaged(fs, exe, staged);
		return;
	}
	s.staged = staged;
	s.status = AP_UPD_READY;
	s.progress = 1000;
	s.detail = "staged " + tag + " (" + std::to_string(staged.size()) + " files)";
}

} // namespace

extern "C" int ap_update_startup(const char *running_tag)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (s.exe.empty())
		s.exe = ExecutablePath();
	if (s.exe.empty())
		return 0;
	return (int)StartupCheck(RealFsOps(), s.exe, running_tag ? running_tag : "");
}

extern "C" void ap_update_confirm_started(void)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (!s.exe.empty())
		ConfirmStarted(RealFsOps(), s.exe);
}

extern "C" void ap_update_check_start(const char *running_tag, const char *declined)
{
	State &s = S();
	unsigned gen;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.status != AP_UPD_IDLE || s.exe.empty())
			return; // one check per launch; no install path means no updater
		s.running = running_tag ? running_tag : "";
		s.status = AP_UPD_CHECKING;
		gen = ++s.generation;
	}
	std::thread(CheckWorker, gen, std::string(running_tag ? running_tag : ""),
	            std::string(declined ? declined : ""))
	    .detach();
}

extern "C" int ap_update_status(char *tag, int tag_bytes, int *progress_permille)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (tag && tag_bytes > 0)
		std::snprintf(tag, (size_t)tag_bytes, "%s", s.tag.c_str());
	if (progress_permille)
		*progress_permille = s.progress;
	return s.status;
}

extern "C" void ap_update_detail(char *out, int out_bytes)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (out && out_bytes > 0)
		std::snprintf(out, (size_t)out_bytes, "%s", s.detail.c_str());
}

extern "C" void ap_update_download_start(void)
{
	State &s = S();
	unsigned gen;
	std::string tag, running, exe;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.status != AP_UPD_OFFER)
			return;
		s.status = AP_UPD_DOWNLOADING;
		s.progress = 0;
		gen = ++s.generation;
		tag = s.tag;
		running = s.running;
		exe = s.exe;
	}
	std::thread(DownloadWorker, gen, tag, running, exe).detach();
}

extern "C" void ap_update_cancel(void)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (s.status != AP_UPD_DOWNLOADING && s.status != AP_UPD_VERIFYING)
		return;
	++s.generation;
	// Back to the offer: the player may start again or decline.
	s.status = AP_UPD_OFFER;
	s.progress = -1;
	s.detail = "download cancelled";
}

extern "C" void ap_update_dismiss(void)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	++s.generation;
	if (s.status == AP_UPD_READY && !s.exe.empty())
	{
		DiscardStaged(RealFsOps(), s.exe, s.staged);
		s.staged.clear();
	}
	s.status = AP_UPD_NONE;
	s.progress = -1;
}

extern "C" int ap_update_apply(void)
{
	State &s = S();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (s.status != AP_UPD_READY || s.exe.empty())
		return 0;
	const Result r = Apply(RealFsOps(), s.exe, s.staged, s.tag);
	if (r != OK)
	{
		s.status = AP_UPD_FAILED_INSTALL;
		s.detail = std::string("install failed: ") + ResultText(r);
		return 0;
	}
	s.status = AP_UPD_INSTALLED;
	s.detail = "installed " + s.tag;
	return 1;
}

extern "C" int ap_update_relaunch(void)
{
	State &s = S();
	std::string exe;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.status != AP_UPD_INSTALLED)
			return 0;
		exe = s.exe;
	}
#ifdef _WIN32
	const int n = MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, nullptr, 0);
	if (n <= 0)
		return 0;
	std::wstring wide((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, &wide[0], n);
	wide.resize((size_t)n - 1);
	std::wstring dir = wide.substr(0, wide.find_last_of(L"\\/"));
	std::wstring cmd = L"\"" + wide + L"\"";
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	std::memset(&si, 0, sizeof si);
	std::memset(&pi, 0, sizeof pi);
	si.cb = sizeof si;
	if (!CreateProcessW(wide.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
		return 0;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 1;
#else
	char *argv[] = {const_cast<char *>(exe.c_str()), nullptr};
	pid_t pid = 0;
	return posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv, environ) == 0 ? 1 : 0;
#endif
}
