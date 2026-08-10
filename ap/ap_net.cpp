// Archipelago networking for CTR-Native.
//
// Compiled as an isolated C++17 static library (see the CTR_AP block in
// CMakeLists.txt) and called from the C unity build (ap_hooks.c) through the C
// API in ap_net.h. Supports ws:// and wss:// (TLS) with permessage-deflate
// compression; the transport is picked from the URI scheme, so the caller only
// varies the uri passed to ap_net_init(). Schema validation stays disabled
// (AP_NO_SCHEMA). TLS uses OpenSSL; compression uses zlib.
//
// apclientpp's poll() is single-threaded: handlers fire inline during the
// ap_net_poll() call on the game thread, so no locking is needed around the
// shared item queue.

#include "apclient.hpp" // pulls wswrap + websocketpp + asio + nlohmann/json
#include "ap_net.h"
#include "ap_seedcfg.h"   // ap_seedcfg_parse_json() -- per-seed slot_data (Phase 2)
#include "ap_locations.h" // AP_LOCATION_TABLE -- the 99 CTR codes to scout on connect

#include <deque>
#include <list>
#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#ifndef _WIN32
#include <openssl/x509.h> // X509_get_default_cert_file/dir: the compiled-in OPENSSLDIR
#include <unistd.h>       // access(): the overflow-free existence test, see below
#include <dirent.h>
#endif

// ctr-ap.log shim from ap_hooks (resolved at final link). Connection-health
// events must reach the persistent log, not just stderr: the Steam launch
// path does not capture stderr, and support bundles read ctr-ap.log (#103).
extern "C" void AP_LogLine(const char *msg);

static APClient            *g_ap = nullptr;
static std::deque<long long> g_items;        // received item ids, drained by the game
static std::deque<int>       g_items_player; // parallel: sending player slot
static std::deque<long long> g_items_index;  // parallel: server ReceivedItems index
static std::deque<long long> g_items_location; // parallel: source location (<=0 = starting inv)
static std::deque<unsigned>  g_items_flags;  // parallel: NetworkItem.flags (#195 feed colours)

// Metadata of the most recent ap_net_drain_items() batch, positionally aligned
// with its output and valid until the next drain. Lets the hub item feed resolve
// each drained item's sender + server index without changing the drain signature
// (the shared item-id drain loop in ap_hooks stays a plain both-add seam).
static int       g_recv_batch_player[64];
static long long g_recv_batch_index[64];
static long long g_recv_batch_location[64]; // #85: source location for foreign classifier
static unsigned  g_recv_batch_flags[64];    // #195: NetworkItem.flags for feed colours
static int       g_recv_batch_n = 0;
static std::string           g_slot;
static std::string           g_password;
static bool                  g_connected = false;

// Coarse status + last refusal reason for the in-game connection manager (read
// through ap_net_status / ap_net_last_error). Maintained entirely from the socket
// / slot handlers below so it needs no apclientpp State enum names.
static int                   g_status = AP_NET_STATUS_IDLE;
static std::string           g_last_error;

// Issue #146: the status could only ever leave CONNECTING towards CONNECTED or a
// slot refusal, so an unreachable host, a wrong port, a firewall/DNS problem or a
// server that is not up yet all rendered as an indefinite "Connecting...". Count
// failed connect attempts; past the threshold the status names the host it cannot
// reach. apclientpp keeps retrying exactly as before -- only the message changes.
//
// The count comes from the socket ERROR handler, not the disconnect handler: a
// connect that never opens leaves apclientpp in SOCKET_CONNECTING, and its
// onclose() only calls the disconnect handler from a state past that, so a
// refused or unroutable host fires the error handler alone. Cleared whenever the
// socket does come up, on a slot connect, and on fresh connect parameters.
// Attempts back off 1.5s / 3s / 6s / ... (capped at 15s), so three failures is
// at least nine seconds of trying before the line changes -- longer when the
// attempts themselves have to time out rather than being refused outright.
static const int             AP_NET_UNREACHABLE_FAILS = 3;
static std::string           g_host;
static int                   g_socket_fails = 0;

// "ws://host:port/path" -> "host". The connection menu's uri row already carries
// the full address, and the status row it shares only has room for a short host,
// so scheme, port and path are dropped here. Carries apclientpp's own uri caveat:
// a bare (bracket-less) IPv6 literal is mangled by the port strip.
static std::string ap_net_host_of(const std::string &uri)
{
	std::string h = uri;
	const auto scheme = h.find("://");
	if (scheme != std::string::npos)
		h.erase(0, scheme + 3);
	const auto slash = h.find('/');
	if (slash != std::string::npos)
		h.erase(slash);
	const auto colon = h.rfind(':');
	if (colon != std::string::npos)
		h.erase(colon);
	return h;
}

// Set true on every fresh slot-connect (new seed, reconnect, or server switch).
// ap_hooks polls it via ap_net_take_recv_reset() and zeroes its received-item
// tallies before draining, so counts rebuild from the server's authoritative
// ReceivedItems list (resent from index 0 on connect) instead of accumulating on
// top of a previous connection's items. Keyed on the connect event, NOT on
// items_received, so it also fires when the new slot has zero received items.
static bool                  g_recv_reset = false;

// Reward-glow / pad-state support: scouted item placed at each CTR location,
// keyed by AP location_code. Filled by the LocationInfo handler after the
// LocationScouts sent on slot-connect. The warp-pad render reads this to show
// the actual reward in each pad's glow (own CTR item -> its model; foreign item
// -> an "AP" marker). Checked-state is read live from apclientpp instead
// (get_checked_locations), so it needs no separate store here.
static std::unordered_map<int64_t, APClient::NetworkItem> g_scouts;

// True once the connect-time LocationScouts reply has been fully processed (issue
// #85). The server answers a LocationScouts request with exactly one LocationInfo,
// so a single reply completes the cache. ap_net_scouts_ready returns this instead
// of the weaker "cache non-empty" predicate: the verifier now banks own items from
// the scout cache, so reasoning over a partially-filled cache would drop banked
// items. Set in the location_info handler; cleared on slot-connect and shutdown.
static bool g_scouts_done = false;

// Locations whose LocationChecks was sent but whose server ReceivedItems echo has
// not yet been drained (issue #85). In solo every own-world check produces a
// ReceivedItems reply, so this drains to empty; the verifier withholds the
// player-facing "not completable" banner while it is non-empty so a transient
// send->receive snapshot cannot flash a false warning. Read via
// ap_net_checks_in_flight. Cleared on slot-connect and shutdown.
static std::set<int64_t> g_pending_checks;

// #188: locations earned while the client could not send (not yet connected, or
// the guarded LocationChecks call itself threw over a dying socket). Deliberately
// NOT cleared on slot-connect/shutdown like g_pending_checks above -- these are
// the opposite lifecycle: they must SURVIVE a disconnect and flush on the next
// successful slot-connect. A std::set dedupes a location that re-fires its grant
// path on every frame while offline (the callers already guard on
// ap_net_location_checked, but that reflects server-confirmed state, which never
// advances while offline) down to one resend, matching the server's own
// idempotent LocationChecks handling (requirement 2 of the issue).
static std::set<int64_t> g_held_checks;

// Seed name + slot the currently-held g_held_checks were earned under (captured
// from the identity active at hold time; see g_slot / APClient::get_seed()).
// Compared against the identity of the NEXT successful slot-connect before
// flushing: a reconnect to a different room or a different slot in the same
// room must discard held checks rather than deliver them to the wrong world
// (requirement 5 -- wrong-slot delivery is worse than the original bug).
static std::string g_held_seed;
static std::string g_held_slot;

// AI-difficulty option sync. g_diff_value caches the last value learned from the
// server (slot_data default seed, a Get reply, or a SetNotify update); g_diff_known
// gates it. Cleared on shutdown/reconnect so a stale value never survives a server
// switch. Key is per-slot: "ctr_difficulty_<slot name>".
static int         g_diff_value = 0;
static bool        g_diff_known = false;

static std::string ap_diff_key()
{
	return "ctr_difficulty_" + g_slot;
}

// ── DeathLink (issue #6) ──
// Depth-1 inbound latch: the most recent DeathLink bounce not yet handed to the
// game thread. A newer death overwrites an unhandled one, so extras are dropped
// at the network boundary (the game side keeps its own depth-1 queue and fires
// it at the next race window). Written in the bounced handler and drained by
// ap_net_deathlink_take on the SAME poll thread (apclientpp is single-threaded,
// the same guarantee the received-item queue relies on), so no lock is needed.
static bool        g_dl_incoming = false;
static std::string g_dl_incoming_cause;

// ── TLS trust store (issue #170) ──
//
// The shipped client links OpenSSL statically, so the binary carries the
// OPENSSLDIR of the machine it was BUILT on (Debian convention, /usr/lib/ssl).
// wswrap's non-Windows branch only calls set_default_verify_paths()
// (wswrap_websocketpp.hpp:347-351), so on a distribution that keeps its CA store
// somewhere else the client loads zero trust roots and every wss:// certificate
// verification fails. Measured in the field on SteamOS/Arch, whose store is at
// /etc/ssl/cert.pem + /etc/ssl/certs and which has no /usr/lib/ssl at all: no
// wss connection had ever succeeded from that device, while the same box
// verified the AP server fine with `openssl s_client`.
//
// The fix lives in this wrapper rather than in wswrap because the vendored trees
// are pinned by sha AND tree hash in ap/vendor/versions.lock and re-checked at
// configure time (cmake/APVendorCheck.cmake), so a vendor edit fails the build
// gate. apclientpp already exposes the seam: its certStore constructor argument
// (apclient.hpp:139-152) is handed to wswrap, which load_verify_file()s it in
// place of the compiled-in paths (wswrap_websocketpp.hpp:319-322).
//
// Precedence, highest first:
//   1. SSL_CERT_FILE / SSL_CERT_DIR from the environment. Resolves to an empty
//      store path so OpenSSL's own default-paths handling reads them: an
//      explicit choice by the player (or by the Steam Deck run scripts, where
//      this workaround is live today) has to keep winning over anything probed
//      here.
//   2. the compiled-in OPENSSLDIR locations, whenever they actually exist.
//   3. the first readable bundle among the common distribution locations, or
//      failing that a hashed certs directory via SSL_CERT_DIR.
#ifndef _WIN32
// Neither helper below may touch a 32-bit stat or dirent struct, and that is
// the whole point of how they are written.
//
// The client ships as a 32-bit binary built without large-file support, so
// sizeof(ino_t) is 4. SteamOS keeps /etc on overlayfs, which synthesises inode
// numbers around 2^63. glibc's legacy stat() and readdir() cannot represent
// those and fail with EOVERFLOW, which the obvious implementation reads as
// "file does not exist" and "directory is empty". Measured on the device:
// stat("/etc/ssl/cert.pem") and lstat() both return -1 EOVERFLOW, readdir() on
// the 441-entry /etc/ssl/certs returns NULL EOVERFLOW, and access() and fopen()
// on those very same paths succeed. That is why the first version of this probe
// reported "none found" on a machine whose CA store was present and readable
// the whole time, while pointing SSL_CERT_FILE at the same path worked by hand.
// It is not about the path being a symlink: lstat fails there too, and so does
// the fully resolved target.
static bool ap_tls_file_ok(const char *p)
{
	if (!p || !*p)
		return false;
	if (access(p, R_OK) != 0)
		return false;
	// Readable is not enough on its own: a directory also passes access(R_OK),
	// and an empty bundle is useless. One byte through stdio settles both, since
	// reading a directory fails with EISDIR and so yields EOF immediately.
	FILE *f = std::fopen(p, "rb");
	if (!f)
		return false;
	const bool has_content = std::fgetc(f) != EOF;
	std::fclose(f);
	return has_content;
}

static bool ap_tls_dir_ok(const char *p)
{
	if (!p || !*p)
		return false;
	if (access(p, R_OK | X_OK) != 0)
		return false;
	// opendir() cannot overflow because it fills no dirent; readdir() can, and
	// on the device it does. The previous "is it populated" test is deliberately
	// gone: it was the call that failed, and OpenSSL copes with an empty hash
	// directory anyway.
	DIR *d = opendir(p);
	if (!d)
		return false;
	closedir(d);
	return true;
}

// Single-file CA bundles, which is what apclientpp's certStore takes. Hashed
// directories cannot go through that argument and are handled by SSL_CERT_DIR.
static const char *const AP_TLS_CA_FILES[] = {
    "/etc/ssl/cert.pem",                  // Arch, SteamOS
    "/etc/ssl/certs/ca-certificates.crt", // Debian, Ubuntu
    "/etc/pki/tls/certs/ca-bundle.crt",   // Fedora, RHEL
    "/etc/ssl/ca-bundle.pem",             // openSUSE
    "/etc/pki/tls/cacert.pem",
    "/etc/ssl/certs/ca-bundle.crt",
};
static const char *const AP_TLS_CA_DIRS[] = {
    "/etc/ssl/certs",
    "/etc/pki/tls/certs",
};
#endif

static std::string g_cert_store;      // passed to APClient; "" = OpenSSL's own defaults
static std::string g_cert_store_desc; // one line for the log and the failure diagnostic
static bool        g_cert_store_ok = true; // false once nothing usable was found at all
static bool        g_cert_store_done = false;
static bool        g_cert_store_logged = false;

static void ap_tls_resolve_cert_store(void)
{
	if (g_cert_store_done)
		return;
	g_cert_store_done = true;
#ifdef _WIN32
	// wswrap snapshots the Windows ROOT store straight into the OpenSSL context
	// (wswrap_websocketpp.hpp:325-346), so there is nothing to resolve here and
	// the store path stays empty exactly as before.
	g_cert_store_desc = "Windows ROOT store";
#else
	const char *envFile = std::getenv("SSL_CERT_FILE");
	const char *envDir = std::getenv("SSL_CERT_DIR");
	if ((envFile && *envFile) || (envDir && *envDir))
	{
		g_cert_store_desc = "environment (SSL_CERT_FILE=";
		g_cert_store_desc += (envFile && *envFile) ? envFile : "unset";
		g_cert_store_desc += ", SSL_CERT_DIR=";
		g_cert_store_desc += (envDir && *envDir) ? envDir : "unset";
		g_cert_store_desc += ")";
		return;
	}

	const char *defFile = X509_get_default_cert_file();
	const char *defDir = X509_get_default_cert_dir();
	const std::string builtIn =
	    std::string(defFile ? defFile : "?") + " / " + (defDir ? defDir : "?");
	if (ap_tls_file_ok(defFile) || ap_tls_dir_ok(defDir))
	{
		g_cert_store_desc = "built-in " + builtIn;
		return;
	}

	for (size_t i = 0; i < sizeof AP_TLS_CA_FILES / sizeof AP_TLS_CA_FILES[0]; i++)
		if (ap_tls_file_ok(AP_TLS_CA_FILES[i]))
		{
			g_cert_store = AP_TLS_CA_FILES[i];
			g_cert_store_desc = g_cert_store + " (built-in " + builtIn + " missing)";
			return;
		}

	for (size_t i = 0; i < sizeof AP_TLS_CA_DIRS / sizeof AP_TLS_CA_DIRS[0]; i++)
		if (ap_tls_dir_ok(AP_TLS_CA_DIRS[i]))
		{
			// No single bundle anywhere, but a hashed directory exists. It cannot
			// travel through certStore, so hand it to OpenSSL the way OpenSSL
			// takes directories; set_default_verify_paths reads this at handshake
			// time. Only reachable when the player set neither variable, so this
			// never overwrites an explicit choice.
			setenv("SSL_CERT_DIR", AP_TLS_CA_DIRS[i], 1);
			g_cert_store_desc = std::string("SSL_CERT_DIR=") + AP_TLS_CA_DIRS[i] +
			                    " (built-in " + builtIn + " missing)";
			return;
		}

	g_cert_store_ok = false;
	// Name what was probed, not just that it failed. The first field failure of
	// this resolver looked identical to "this machine has no CA store" when the
	// truth was that the probe could not see one that was there.
	g_cert_store_desc =
	    "none found (built-in " + builtIn + " missing; probed " +
	    std::to_string(sizeof AP_TLS_CA_FILES / sizeof AP_TLS_CA_FILES[0]) +
	    " bundle paths and " +
	    std::to_string(sizeof AP_TLS_CA_DIRS / sizeof AP_TLS_CA_DIRS[0]) +
	    " certs directories)";
#endif
}

// One line per process, and only when a TLS target is actually dialled: a
// bare ws:// room has no use for it. Says which store the client settled on, so
// a field log answers "was this a certificate problem?" without a repro.
static void ap_tls_log_cert_store(void)
{
	if (g_cert_store_logged)
		return;
	g_cert_store_logged = true;
	char line[512];
	if (g_cert_store_ok)
		std::snprintf(line, sizeof line, "[AP NET] TLS trust store: %s\n",
		              g_cert_store_desc.c_str());
	else
		std::snprintf(line, sizeof line,
		              "[AP NET] TLS trust store empty: %s, and no CA bundle exists at "
		              "any known distribution path -- wss:// certificate verification "
		              "will fail until SSL_CERT_FILE names a CA bundle\n",
		              g_cert_store_desc.c_str());
	std::fprintf(stderr, "%s", line);
	AP_LogLine(line);
}

// True when the target is, or will become, wss://. apclientpp upgrades a
// scheme-less uri to wss:// (apclient.hpp:164-179; AP_PREFER_UNENCRYPTED is not
// defined in this build), so a bare "host:port" is a TLS target too.
static bool ap_net_uri_is_secure(const std::string &uri)
{
	if (uri.rfind("wss://", 0) == 0)
		return true;
	if (uri.rfind("ws://", 0) == 0)
		return false;
	return !uri.empty() && uri.find("://") == std::string::npos;
}

// websocketpp reports a failed handshake as "TLS handshake failed"; OpenSSL's
// own detail ("certificate verify failed") reaches the same string on other
// paths. Substring matching keeps this independent of the transport's wording.
static bool ap_net_error_is_tls(const std::string &m)
{
	return m.find("TLS") != std::string::npos || m.find("tls") != std::string::npos ||
	       m.find("SSL") != std::string::npos || m.find("ssl") != std::string::npos ||
	       m.find("certificate") != std::string::npos ||
	       m.find("handshake") != std::string::npos;
}

static bool g_uri_secure = false;      // this client dials a TLS target
static bool g_tls_diag_logged = false; // the TLS verdict below is logged once per client

extern "C" int ap_net_init(const char *uuid, const char *game, const char *uri)
{
	if (g_ap)
		return 0;
	// Resolve the trust store before the client exists: apclientpp takes it as a
	// constructor argument and hands it to every socket it later builds (#170).
	ap_tls_resolve_cert_store();
	g_uri_secure = ap_net_uri_is_secure(uri ? uri : "");
	g_tls_diag_logged = false;
	if (g_uri_secure)
		ap_tls_log_cert_store();
	try
	{
		g_ap = new APClient(uuid ? uuid : "ctr-native",
		                    game ? game : "Crash Team Racing",
		                    uri ? uri : "ws://localhost:38281",
		                    g_cert_store);
	}
	catch (...)
	{
		// Leave a status behind instead of the IDLE the failed dial inherits from
		// shutdown: IDLE renders as "not connected", which reads as "nobody tried"
		// and is what a reconnect into a broken client used to look like (#146).
		g_status = AP_NET_STATUS_ERROR;
		g_last_error = "client init failed";
		std::fprintf(stderr, "[AP NET] APClient construction failed\n");
		AP_LogLine("[AP NET] APClient construction failed\n");
		return -1;
	}

	g_ap->set_socket_connected_handler([]() {
		// The room answered a websocket handshake, so it is reachable: any
		// unreachable verdict is stale and the failure run starts over (#146).
		g_socket_fails = 0;
		if (g_status != AP_NET_STATUS_ERROR)
			g_status = AP_NET_STATUS_CONNECTING; // socket up; slot handshake pending
		std::fprintf(stderr, "[AP NET] socket connected\n");
		AP_LogLine("[AP NET] socket connected\n");
	});
	g_ap->set_socket_disconnected_handler([]() {
		g_connected = false;
		// Not an error unless the slot was refused; apclientpp will auto-retry.
		if (g_status != AP_NET_STATUS_ERROR)
			g_status = AP_NET_STATUS_CONNECTING;
		std::fprintf(stderr, "[AP NET] socket disconnected\n");
		AP_LogLine("[AP NET] socket disconnected (auto-retrying)\n");
	});
	// One call per failed connect attempt, with the transport's own reason
	// ("Connection refused", a TLS or DNS failure, ...). Until #146 nothing
	// consumed this, so a room that was never reachable produced no ctr-ap.log
	// trace at all and no status change. Attempts back off to one every 15s, so
	// logging each of them cannot flood the file.
	g_ap->set_socket_error_handler([](const std::string &msg) {
		if (g_socket_fails < AP_NET_UNREACHABLE_FAILS)
			g_socket_fails++;
		char line[256];
		std::snprintf(line, sizeof line, "[AP NET] connect attempt failed: %s\n",
		              msg.empty() ? "unknown error" : msg.c_str());
		std::fprintf(stderr, "%s", line);
		AP_LogLine(line);
		// #170: an unreachable host and a dead trust store used to produce the
		// same "connect attempt failed" line, so a field log could not tell a
		// certificate problem from a network problem -- which is how a Linux
		// client that had never once completed a wss handshake read as a flaky
		// server. Name it once per client; the retry loop repeats the same
		// message every 15s and this must not flood the log.
		if (g_uri_secure && !g_tls_diag_logged && ap_net_error_is_tls(msg))
		{
			g_tls_diag_logged = true;
			std::snprintf(line, sizeof line,
			              "[AP NET] TLS trust store empty/verification failed: %s "
			              "(trust store: %s)\n",
			              msg.c_str(), g_cert_store_desc.c_str());
			std::fprintf(stderr, "%s", line);
			AP_LogLine(line);
		}
		// Past the threshold, say what cannot be reached instead of sitting on
		// "Connecting..." forever. Retrying is unchanged; only the message moves.
		if (g_status == AP_NET_STATUS_ERROR || g_status == AP_NET_STATUS_CONNECTED)
			return; // a refusal reason / a live slot outranks a stray socket error
		if (g_socket_fails >= AP_NET_UNREACHABLE_FAILS && !g_host.empty() &&
		    g_status != AP_NET_STATUS_UNREACHABLE)
		{
			g_status = AP_NET_STATUS_UNREACHABLE;
			std::snprintf(line, sizeof line,
			              "[AP NET] cannot reach %s after %d attempts (still retrying)\n",
			              g_host.c_str(), g_socket_fails);
			AP_LogLine(line);
		}
	});
	g_ap->set_room_info_handler([]() {
		std::fprintf(stderr, "[AP NET] RoomInfo; connecting slot '%s'\n", g_slot.c_str());
		// items_handling 0b111 = remote items + own world + starting inventory.
		g_ap->ConnectSlot(g_slot, g_password, 7, {"AP"});
	});
	g_ap->set_slot_connected_handler([](const nlohmann::json &slotData) {
		g_connected = true;
		g_status = AP_NET_STATUS_CONNECTED;
		g_last_error.clear();
		g_socket_fails = 0; // #146: the run of failures ended here
		// Fresh connect: signal ap_hooks to zero its received-item tallies, and
		// drop any stale queue/scout state from a previous connection (server
		// switch carried the old seed's items into memory otherwise). The server
		// resends the full ReceivedItems list (from index 0) right after this.
		g_recv_reset = true;
		g_items.clear();
		g_items_player.clear();
		g_items_index.clear();
		g_items_location.clear();
		g_items_flags.clear();
		g_recv_batch_n = 0; // invalidate metadata from the previous connection
		g_scouts.clear();
		g_scouts_done = false;   // #85: fresh scout round pending -> verifier waits
		g_pending_checks.clear(); // #85: no checks in flight on a fresh connect
		// #188: flush checks retained across a disconnect, but ONLY into the same
		// seed+slot they were earned under. Runs AFTER the g_pending_checks.clear()
		// above -- resending here so the checks we just re-sent land back in
		// g_pending_checks and the #85 in-flight bookkeeping still tracks them,
		// instead of being wiped by that clear immediately after. get_seed() is
		// populated from RoomInfo (fires before ConnectSlot, so it is already
		// valid here) and is cleared to "" on every socket close, so it uniquely
		// names the room this slot just joined; g_slot is the name we asked to
		// join. Both apclientpp's own automatic reconnect (transient drop
		// mid-play, the field-reported case -- same g_ap, same
		// RoomInfo/ConnectSlot replay) and a manual same-room "Reconnect"
		// (AP_Net_Reconnect tears g_ap down and rebuilds it, but does not touch
		// g_held_checks) match here and flush. A reconnect to a DIFFERENT room or
		// a different slot in the same room mismatches and the held checks are
		// discarded rather than risk delivering them to the wrong world
		// (requirement 5).
		if (!g_held_checks.empty())
		{
			char line[96];
			if (g_ap->get_seed() == g_held_seed && g_slot == g_held_slot)
			{
				std::snprintf(line, sizeof line,
				              "[AP NET] resending %d check(s) held since the last disconnect\n",
				              (int)g_held_checks.size());
				AP_LogLine(line);
				// Iterate a snapshot, not g_held_checks itself: ap_net_send_location
				// re-inserts into g_held_checks on failure (guard exception / socket
				// dead again immediately), which must not be lost -- swap it out
				// first so a failed resend re-arms itself for the NEXT reconnect
				// instead of being silently dropped here.
				std::set<int64_t> retry;
				retry.swap(g_held_checks);
				for (int64_t code : retry)
					ap_net_send_location((long long)code); // re-enters the connected path above
			}
			else
			{
				std::snprintf(line, sizeof line,
				              "[AP NET] discarding %d held check(s) from a different seed/slot\n",
				              (int)g_held_checks.size());
				AP_LogLine(line);
				g_held_checks.clear();
			}
		}
		g_held_seed = g_ap->get_seed();
		g_held_slot = g_slot;
		ap_seedcfg_parse_json(slotData); // Phase 2: per-seed reqs -> ctr_cfg
		// Scout every CTR location so the warp pads can show the actual AP reward
		// placed at each (and recolour pads whose location is already checked).
		// One LocationScouts on connect; results arrive via the info handler.
		std::list<int64_t> locs;
		for (int i = 0; i < AP_LOCATION_TABLE_LEN; i++)
			locs.push_back((int64_t)AP_LOCATION_TABLE[i].location_code);
		// Podium-ladder rungs carry no AdvProgress bit, so they are absent from
		// AP_LOCATION_TABLE -- scout them explicitly from the parsed per-seed config
		// so the ceremony can resolve the item + player placed on each rung (else a
		// foreign rung reward renders as the generic fallback). ctr_cfg is populated
		// by ap_seedcfg_parse_json() just above.
		if (ctr_cfg.podium_enabled)
		{
			for (int t = 0; t < CTR_CFG_PODIUM_TRACK_COUNT; t++)
			{
				const ctr_podium_rungs &pr = ctr_cfg.podium[t];
				const long rung[CTR_CFG_PODIUM_RUNG_COUNT] = {
				    pr.held_1st, pr.held_3rd, pr.held_5th,
				    pr.finish_podium, pr.finish_any};
				for (int k = 0; k < CTR_CFG_PODIUM_RUNG_COUNT; k++)
					if (rung[k] >= 0)
						locs.push_back((int64_t)rung[k]);
			}
		}
		g_ap->LocationScouts(locs, 0);
		std::fprintf(stderr, "[AP NET] slot connected; scouting %d locations\n",
		             AP_LOCATION_TABLE_LEN);
	});
	g_ap->set_location_info_handler([](const std::list<APClient::NetworkItem> &items) {
		for (const auto &it : items)
			g_scouts[it.location] = it;
		g_scouts_done = true; // #85: single LocationInfo reply completes the scout cache
		std::fprintf(stderr, "[AP NET] scout info received for %d locations\n",
		             (int)items.size());
	});
	g_ap->set_slot_refused_handler([](const std::list<std::string> &errors) {
		g_connected = false;
		std::string e;
		for (const auto &s : errors)
		{
			e += s;
			e += ' ';
		}
		g_status = AP_NET_STATUS_ERROR;
		g_last_error = e;
		std::fprintf(stderr, "[AP NET] slot refused: %s\n", e.c_str());
	});
	// Data-storage replies for the AI-difficulty override. Get -> Retrieved (a
	// key->value map); SetNotify + Set(want_reply) -> SetReply (key, new value).
	// Both fire inline on the poll thread, same as every other handler.
	g_ap->set_retrieved_handler([](const std::map<std::string, nlohmann::json> &keys) {
		auto it = keys.find(ap_diff_key());
		if (it != keys.end() && it->second.is_number_integer())
		{
			g_diff_value = it->second.get<int>();
			g_diff_known = true;
			std::fprintf(stderr, "[AP NET] difficulty override retrieved: %d\n", g_diff_value);
		}
	});
	g_ap->set_set_reply_handler([](const std::string &key, const nlohmann::json &value,
	                               const nlohmann::json &) {
		if (key == ap_diff_key() && value.is_number_integer())
		{
			g_diff_value = value.get<int>();
			g_diff_known = true;
			std::fprintf(stderr, "[AP NET] difficulty override changed: %d\n", g_diff_value);
		}
	});
	g_ap->set_items_received_handler([](const std::list<APClient::NetworkItem> &items) {
		for (const auto &it : items)
		{
			g_items.push_back((long long)it.item);
			g_items_player.push_back((int)it.player);
			g_items_index.push_back((long long)it.index);
			g_items_location.push_back((long long)it.location);
			g_items_flags.push_back(it.flags);
			g_pending_checks.erase(it.location); // #85: this receipt settles its own check
			std::fprintf(stderr, "[AP NET] received item %lld (index %d)\n",
			             (long long)it.item, it.index);
		}
	});
	// DeathLink: incoming deaths arrive as a tagged Bounce. The handler fires
	// inline on the poll thread like every other handler. It filters to the
	// DeathLink tag, ignores our own death echoed back by the server (we carry the
	// tag too), and latches the most recent death (depth 1) for the game thread.
	g_ap->set_bounced_handler([](const nlohmann::json &packet) {
		if (!packet.contains("tags") || !packet["tags"].is_array())
			return;
		bool isDeath = false;
		for (const auto &t : packet["tags"])
			if (t.is_string() && t.get<std::string>() == "DeathLink")
			{
				isDeath = true;
				break;
			}
		if (!isDeath)
			return;
		if (!packet.contains("data") || !packet["data"].is_object())
			return;
		const auto &d = packet["data"];
		std::string source = (d.contains("source") && d["source"].is_string())
		                         ? d["source"].get<std::string>()
		                         : "";
		// Ignore the server's echo of our own death (no ping-pong with ourselves).
		if (!source.empty() && g_ap && source == g_ap->get_slot())
			return;
		std::string cause = (d.contains("cause") && d["cause"].is_string())
		                        ? d["cause"].get<std::string>()
		                        : "";
		g_dl_incoming = true; // depth-1: overwrite any death not yet drained
		g_dl_incoming_cause = cause;
		std::fprintf(stderr, "[AP NET] deathlink received from '%s': %s\n",
		             source.c_str(), cause.c_str());
	});
	g_host = ap_net_host_of(uri ? uri : "localhost"); // #146: named by the status line
	g_socket_fails = 0;
	g_status = AP_NET_STATUS_CONNECTING; // dialing; handlers advance this
	g_last_error.clear();
	return 0;
}

extern "C" void ap_net_connect_slot(const char *slot, const char *password)
{
	g_slot = slot ? slot : "";
	g_password = password ? password : "";
	// Fresh parameters: re-arm the unreachable threshold so a new attempt starts
	// from "Connecting..." rather than inheriting the previous verdict (#146).
	g_socket_fails = 0;
	if (g_status == AP_NET_STATUS_UNREACHABLE)
		g_status = AP_NET_STATUS_CONNECTING;
}

// Returns 1 once after each fresh slot-connect (then clears the flag). ap_hooks
// uses this to reset its received-item tallies before draining the resent list.
extern "C" int ap_net_take_recv_reset(void)
{
	int r = g_recv_reset ? 1 : 0;
	g_recv_reset = false;
	return r;
}

// Issue #103: every seam that drives socket I/O must swallow exceptions. The
// websocket stack throws on abrupt server loss, these functions are called from
// C frames with no unwind tables, and an escaping exception is std::terminate ->
// abort -- which also bypasses the SEH crash reporter, so the death is silent.
// Catch, log (rate-limited: a dead socket would otherwise log every frame), and
// keep going; apclientpp's own retry machinery recovers the connection.
static void ap_net_note_net_exception(const char *where, const char *what)
{
	static unsigned count = 0;
	if ((count++ & 63u) == 0)
	{
		char line[256];
		std::snprintf(line, sizeof line,
		              "[AP NET] network exception in %s (server connection lost? "
		              "retrying in background): %s (occurrence %u)\n",
		              where, what, count);
		std::fprintf(stderr, "%s", line);
		AP_LogLine(line);
	}
}

#define AP_NET_GUARD(where, body)                                   \
	do                                                              \
	{                                                               \
		try                                                         \
		{                                                           \
			body;                                                   \
		}                                                           \
		catch (const std::exception &e)                             \
		{                                                           \
			ap_net_note_net_exception(where, e.what());             \
		}                                                           \
		catch (...)                                                 \
		{                                                           \
			ap_net_note_net_exception(where, "unknown exception");  \
		}                                                           \
	} while (0)

extern "C" void ap_net_poll(void)
{
	if (g_ap)
		AP_NET_GUARD("poll", g_ap->poll());
}

extern "C" int ap_net_is_connected(void)
{
	return (g_ap && g_ap->get_state() == APClient::State::SLOT_CONNECTED) ? 1 : 0;
}

extern "C" void ap_net_send_location(long long location_code)
{
	const int64_t code = (int64_t)location_code;
	if (g_ap && g_connected)
	{
		bool sent = false;
		AP_NET_GUARD("send_location", {
			g_ap->LocationChecks({code});
			g_pending_checks.insert(code); // #85: in flight until its receipt drains
			sent = true;
		});
		if (sent)
			return;
		// #188: the guarded call above threw (dying socket) and was logged by
		// AP_NET_GUARD -- fall through to the same retention path as "not
		// connected" instead of dropping the check silently.
	}
	// #188: no live send path right now. Retain the check instead of losing it;
	// it flushes on the next successful slot-connect for this same seed/slot
	// (see set_slot_connected_handler), and is deduped here if the caller's
	// grant path re-fires while still offline.
	g_held_checks.insert(code);
}

extern "C" void ap_net_send_goal(void)
{
	if (g_ap && g_connected)
		AP_NET_GUARD("send_goal", g_ap->StatusUpdate(APClient::ClientStatus::GOAL));
}

extern "C" int ap_net_scout_known(long long location_code, long long *out_item,
                                  int *out_player, unsigned *out_flags)
{
	auto it = g_scouts.find((int64_t)location_code);
	if (it == g_scouts.end())
		return 0;
	if (out_item)
		*out_item = (long long)it->second.item;
	if (out_player)
		*out_player = it->second.player;
	if (out_flags)
		*out_flags = it->second.flags;
	return 1;
}

extern "C" int ap_net_scout_text(long long location_code, char *item_buf,
                                 int item_n, char *player_buf, int player_n)
{
	if (item_buf && item_n > 0)
		item_buf[0] = '\0';
	if (player_buf && player_n > 0)
		player_buf[0] = '\0';
	if (!g_ap)
		return 0;
	auto it = g_scouts.find((int64_t)location_code);
	if (it == g_scouts.end())
		return 0;
	const APClient::NetworkItem &ni = it->second;
	try
	{
		// The item belongs to the game of the player who RECEIVES it. Names come
		// from the DataPackage apclientpp syncs+caches on connect; a name not yet
		// in the package resolves to "Unknown" (the caller maps that to a generic).
		if (item_buf && item_n > 0)
		{
			std::string name = g_ap->get_item_name(ni.item, g_ap->get_player_game(ni.player));
			std::snprintf(item_buf, (size_t)item_n, "%s", name.c_str());
		}
		if (player_buf && player_n > 0)
		{
			std::string alias = g_ap->get_player_alias(ni.player);
			std::snprintf(player_buf, (size_t)player_n, "%s", alias.c_str());
		}
	}
	catch (...)
	{
		return 0;
	}
	return 1;
}

extern "C" int ap_net_location_checked(long long location_code)
{
	if (!g_ap)
		return 0;
	const std::set<int64_t> &chk = g_ap->get_checked_locations();
	return chk.count((int64_t)location_code) ? 1 : 0;
}

extern "C" int ap_net_location_exists(long long location_code)
{
	if (!g_ap)
		return 0;
	int64_t code = (int64_t)location_code;
	const std::set<int64_t> &chk = g_ap->get_checked_locations();
	if (chk.count(code))
		return 1;
	const std::set<int64_t> &miss = g_ap->get_missing_locations();
	return miss.count(code) ? 1 : 0;
}

extern "C" int ap_net_self_slot(void)
{
	return g_ap ? g_ap->get_player_number() : -1;
}

extern "C" int ap_net_player_count(void)
{
	return g_ap ? (int)g_ap->get_players().size() : 0;
}

// #124: the slot-info half of the classified-display gate. The Connected packet
// carries both the player list and the per-slot game names, so once our own slot
// resolves to a game name the table is usable for every slot. Without this gate
// the first glow frames after a connect would resolve every peer's game as
// unknown and flicker between the other-game and CTR-peer presentations.
extern "C" int ap_net_slot_info_ready(void)
{
	if (!g_ap || !g_connected)
		return 0;
	try
	{
		if (g_ap->get_players().empty())
			return 0;
		return g_ap->get_player_game(g_ap->get_player_number()).empty() ? 0 : 1;
	}
	catch (...)
	{
		return 0;
	}
}

// 1 if `player` plays Crash Team Racing (a CTR peer), 0 for any other game and
// for a slot that does not resolve. Callers must gate on ap_net_slot_info_ready()
// first: before slot info lands, an unresolved slot is indistinguishable from a
// genuine other-game slot.
extern "C" int ap_net_player_is_ctr(int player)
{
	if (!g_ap)
		return 0;
	try
	{
		return g_ap->get_player_game(player) == "Crash Team Racing" ? 1 : 0;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" int ap_net_scouts_ready(void)
{
	// True only once the LocationInfo reply to the connect-time LocationScouts has
	// been fully processed (issue #85). The verifier banks own items from the scout
	// cache, so a partially-filled cache would drop banked items -- "cache
	// non-empty" is too weak a predicate now. One request -> one reply, so
	// g_scouts_done flips exactly when the cache is complete.
	return g_scouts_done ? 1 : 0;
}

// Number of own location checks sent whose ReceivedItems echo has not yet drained
// (issue #85). The verifier withholds the solo "not completable" banner while this
// is non-zero so a transient send->receive snapshot cannot flash a false warning.
extern "C" int ap_net_checks_in_flight(void)
{
	return (int)g_pending_checks.size();
}

extern "C" int ap_net_status(void)
{
	return g_status;
}

extern "C" const char *ap_net_last_error(void)
{
	return g_last_error.c_str();
}

extern "C" int ap_net_host(char *buf, int n)
{
	if (!buf || n <= 0)
		return 0;
	std::snprintf(buf, (size_t)n, "%s", g_host.c_str());
	return buf[0] != '\0';
}

extern "C" int ap_net_drain_items(long long *out, int max)
{
	int n = 0;
	g_recv_batch_n = 0;
	while (n < max && !g_items.empty())
	{
		out[n] = g_items.front();
		g_items.pop_front();
		int       pl = -1;
		long long ix = -1;
		long long lc = -1;
		unsigned  fl = 0;
		if (!g_items_player.empty())
		{
			pl = g_items_player.front();
			g_items_player.pop_front();
		}
		if (!g_items_index.empty())
		{
			ix = g_items_index.front();
			g_items_index.pop_front();
		}
		if (!g_items_location.empty())
		{
			lc = g_items_location.front();
			g_items_location.pop_front();
		}
		if (!g_items_flags.empty())
		{
			fl = g_items_flags.front();
			g_items_flags.pop_front();
		}
		if (n < (int)(sizeof g_recv_batch_player / sizeof g_recv_batch_player[0]))
		{
			g_recv_batch_player[n] = pl;
			g_recv_batch_index[n] = ix;
			g_recv_batch_location[n] = lc;
			g_recv_batch_flags[n] = fl;
			g_recv_batch_n = n + 1;
		}
		n++;
	}
	return n;
}

extern "C" void ap_net_difficulty_subscribe(int slot_default)
{
	if (!g_ap || !g_connected)
		return;
	// Seed the slot_data default so it is effective before the Get returns; a
	// stored per-slot override (if any) overwrites it via the retrieved handler.
	if (slot_default >= 0)
	{
		g_diff_value = slot_default;
		g_diff_known = true;
	}
	AP_NET_GUARD("difficulty_subscribe", {
		const std::string key = ap_diff_key();
		g_ap->SetNotify({key});
		g_ap->Get({key});
	});
}

extern "C" void ap_net_difficulty_set(int value)
{
	if (!g_ap || !g_connected)
		return;
	// replace: write value unconditionally; default seeds the key if it is unset.
	AP_NET_GUARD("difficulty_set", {
		APClient::DataStorageOperation op;
		op.operation = "replace";
		op.value = value;
		g_ap->Set(ap_diff_key(), value, false, {op});
	});
	g_diff_value = value;
	g_diff_known = true;
}

extern "C" int ap_net_difficulty_known(int *out)
{
	if (!g_diff_known)
		return 0;
	if (out)
		*out = g_diff_value;
	return 1;
}

// Sender slot / server index for position `pos` of the most recent drain batch
// (valid until the next ap_net_drain_items call). -1 if pos is out of range.
extern "C" int ap_net_recv_batch_player(int pos)
{
	return (pos >= 0 && pos < g_recv_batch_n) ? g_recv_batch_player[pos] : -1;
}

extern "C" long long ap_net_recv_batch_index(int pos)
{
	return (pos >= 0 && pos < g_recv_batch_n) ? g_recv_batch_index[pos] : -1;
}

// Source location for position `pos` of the most recent drain batch (valid until
// the next ap_net_drain_items call). <= 0 = starting inventory / server grant. The
// seed verifier's foreign classifier (issue #85) uses this. -1 if pos out of range.
extern "C" long long ap_net_recv_batch_location(int pos)
{
	return (pos >= 0 && pos < g_recv_batch_n) ? g_recv_batch_location[pos] : -1;
}

// AP classification flags for position `pos` of the most recent drain batch
// (valid until the next ap_net_drain_items call). 0 = filler, matching a
// missing/unknown flags fallback. Used by the hub item feed (issue #195).
extern "C" unsigned ap_net_recv_batch_flags(int pos)
{
	return (pos >= 0 && pos < g_recv_batch_n) ? g_recv_batch_flags[pos] : 0;
}

// Resolve a RECEIVED item into display strings: the item name (in this slot's own
// game, since we are the receiver) and the sending player's alias. Returns 1 on
// success, 0 if not connected (both buffers set to ""). A name missing from the
// synced DataPackage resolves to "Unknown"; the caller substitutes a generic.
extern "C" int ap_net_item_text(long long item_id, int sender_slot, char *item_buf,
                                int item_n, char *player_buf, int player_n)
{
	if (item_buf && item_n > 0)
		item_buf[0] = '\0';
	if (player_buf && player_n > 0)
		player_buf[0] = '\0';
	if (!g_ap)
		return 0;
	try
	{
		if (item_buf && item_n > 0)
		{
			std::string name =
			    g_ap->get_item_name(item_id, g_ap->get_player_game(g_ap->get_player_number()));
			std::snprintf(item_buf, (size_t)item_n, "%s", name.c_str());
		}
		if (player_buf && player_n > 0)
		{
			std::string alias = g_ap->get_player_alias(sender_slot);
			std::snprintf(player_buf, (size_t)player_n, "%s", alias.c_str());
		}
	}
	catch (...)
	{
		return 0;
	}
	return 1;
}

// ── DeathLink (issue #6) ──

// Add the "DeathLink" connection tag on top of the base "AP" tag so the server
// relays deaths to us. ConnectUpdate replaces the whole tag set, so both tags are
// re-declared; items_handling is left unchanged (send_items_handling = false).
// Called by the game side after slot_data is parsed, only when death_link != off.
// Seed + slot name of the connected room. Used as the one-shot-effect dedup
// key (traps/wumpa replay suppression, ap_hooks.c). Empty/0 until connected.
extern "C" int ap_net_seed_name(char *buf, int n)
{
	if (!buf || n <= 0)
		return 0;
	buf[0] = '\0';
	if (!g_ap)
		return 0;
	try
	{
		std::snprintf(buf, (size_t)n, "%s", g_ap->get_seed().c_str());
	}
	catch (...)
	{
		return 0;
	}
	return buf[0] != '\0';
}

extern "C" int ap_net_slot_name(char *buf, int n)
{
	if (!buf || n <= 0)
		return 0;
	buf[0] = '\0';
	if (!g_ap)
		return 0;
	try
	{
		std::snprintf(buf, (size_t)n, "%s", g_ap->get_slot().c_str());
	}
	catch (...)
	{
		return 0;
	}
	return buf[0] != '\0';
}

extern "C" void ap_net_deathlink_enable(void)
{
	if (!g_ap)
		return;
	AP_NET_GUARD("deathlink_enable", {
		g_ap->ConnectUpdate(false, 0, true, {"AP", "DeathLink"});
		std::fprintf(stderr, "[AP NET] DeathLink tag enabled\n");
	});
}

// Remove the "DeathLink" tag again (runtime toggle). ConnectUpdate replaces the
// whole tag set, so the base "AP" tag is re-declared alone.
extern "C" void ap_net_deathlink_disable(void)
{
	if (!g_ap)
		return;
	AP_NET_GUARD("deathlink_disable", {
		g_ap->ConnectUpdate(false, 0, true, {"AP"});
		std::fprintf(stderr, "[AP NET] DeathLink tag disabled\n");
	});
}

// Send a death as a tagged Bounce. cause is a short verb phrase (e.g. "was blown
// up by a bomb"); the slot name is prepended so other clients render the standard
// "<player> <cause>" line. No-op unless slot-connected.
extern "C" void ap_net_deathlink_send(const char *cause)
{
	if (!g_ap || !g_connected)
		return;
	AP_NET_GUARD("deathlink_send", {
		std::string slot = g_ap->get_slot();
		nlohmann::json data;
		data["time"] = (double)std::time(nullptr);
		data["source"] = slot;
		if (cause && cause[0])
			data["cause"] = slot + " " + cause;
		else
			data["cause"] = slot + " wiped out";
		g_ap->Bounce(data, {}, {}, {"DeathLink"});
		std::fprintf(stderr, "[AP NET] deathlink sent: %s\n",
		             data["cause"].get<std::string>().c_str());
	});
}

// Drain the depth-1 inbound death latch. Returns 1 (and copies the cause string,
// truncated to fit) if a death was pending, then clears it; 0 otherwise.
extern "C" int ap_net_deathlink_take(char *cause_buf, int cause_n)
{
	if (!g_dl_incoming)
		return 0;
	g_dl_incoming = false;
	if (cause_buf && cause_n > 0)
		std::snprintf(cause_buf, (size_t)cause_n, "%s", g_dl_incoming_cause.c_str());
	return 1;
}

extern "C" void ap_net_shutdown(void)
{
	if (g_ap)
	{
		// This closes a live socket and polls it down (wswrap_websocketpp.hpp:
		// 428-451), so it looks like it could re-enter a half-destroyed
		// APClient: ~APClient is defaulted (apclient.hpp:223) and therefore
		// destroys every handler std::function (:1807-1823) and _seed (:1833)
		// BEFORE _ws (:1803), while the connection still holds the handler
		// lambdas websocketpp snapshotted at creation (endpoint_impl.hpp:59-68),
		// which cleanup()'s handler clearing does not reach. It does not happen:
		// wswrap nulls impl->second before it polls (wswrap_websocketpp.hpp:431)
		// and every one of those lambdas is gated on impl->second, so none of
		// them ever calls back into APClient. Verified with AddressSanitizer on
		// the 32-bit Linux build over 40 reconnect cycles whose teardowns land
		// across every socket state (pre-connect, TCP connecting, TLS
		// handshaking, established, failed): no error, no callback. Left as a
		// plain delete on purpose -- do not re-derive this.
		delete g_ap;
		g_ap = nullptr;
	}
	g_connected = false;
	g_items.clear();
	g_items_player.clear();
	g_items_index.clear();
	g_items_location.clear();
	g_items_flags.clear();
	g_recv_batch_n = 0; // accessors must not expose the final pre-shutdown batch
	g_scouts.clear();
	g_scouts_done = false;    // #85: no valid scout cache after shutdown
	g_pending_checks.clear(); // #85: drop any in-flight checks on a server switch
	g_dl_incoming = false; // a pending death must not survive a server switch
	g_diff_known = false; // a difficulty override must not survive a server switch
	g_status = AP_NET_STATUS_IDLE; // set last: delete g_ap may fire the disconnect handler
	g_last_error.clear();
	g_host.clear();     // #146: no host to name once the client is gone
	g_socket_fails = 0;
	g_uri_secure = false;
	// Closing marker for field logs. The Steam Deck report of 2026-08-05 turned on
	// "a reconnect line, then a fresh client run start with no shutdown lines",
	// and the log had no way to say whether the teardown had completed. Now it
	// does: no such line after a reconnect means the process died inside it.
	AP_LogLine("[AP NET] client teardown complete\n");
}
