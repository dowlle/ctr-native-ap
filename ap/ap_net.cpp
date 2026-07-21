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
#include <ctime>

static APClient            *g_ap = nullptr;
static std::deque<long long> g_items;        // received item ids, drained by the game
static std::deque<int>       g_items_player; // parallel: sending player slot
static std::deque<long long> g_items_index;  // parallel: server ReceivedItems index
static std::deque<long long> g_items_location; // parallel: source location (<=0 = starting inv)

// Metadata of the most recent ap_net_drain_items() batch, positionally aligned
// with its output and valid until the next drain. Lets the hub item feed resolve
// each drained item's sender + server index without changing the drain signature
// (the shared item-id drain loop in ap_hooks stays a plain both-add seam).
static int       g_recv_batch_player[64];
static long long g_recv_batch_index[64];
static long long g_recv_batch_location[64]; // #85: source location for foreign classifier
static int       g_recv_batch_n = 0;
static std::string           g_slot;
static std::string           g_password;
static bool                  g_connected = false;

// Coarse status + last refusal reason for the in-game connection manager (read
// through ap_net_status / ap_net_last_error). Maintained entirely from the socket
// / slot handlers below so it needs no apclientpp State enum names.
static int                   g_status = AP_NET_STATUS_IDLE;
static std::string           g_last_error;

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

extern "C" int ap_net_init(const char *uuid, const char *game, const char *uri)
{
	if (g_ap)
		return 0;
	try
	{
		g_ap = new APClient(uuid ? uuid : "ctr-native",
		                    game ? game : "Crash Team Racing",
		                    uri ? uri : "ws://localhost:38281");
	}
	catch (...)
	{
		std::fprintf(stderr, "[AP NET] APClient construction failed\n");
		return -1;
	}

	g_ap->set_socket_connected_handler([]() {
		if (g_status != AP_NET_STATUS_ERROR)
			g_status = AP_NET_STATUS_CONNECTING; // socket up; slot handshake pending
		std::fprintf(stderr, "[AP NET] socket connected\n");
	});
	g_ap->set_socket_disconnected_handler([]() {
		g_connected = false;
		// Not an error unless the slot was refused; apclientpp will auto-retry.
		if (g_status != AP_NET_STATUS_ERROR)
			g_status = AP_NET_STATUS_CONNECTING;
		std::fprintf(stderr, "[AP NET] socket disconnected\n");
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
		// Fresh connect: signal ap_hooks to zero its received-item tallies, and
		// drop any stale queue/scout state from a previous connection (server
		// switch carried the old seed's items into memory otherwise). The server
		// resends the full ReceivedItems list (from index 0) right after this.
		g_recv_reset = true;
		g_items.clear();
		g_items_player.clear();
		g_items_index.clear();
		g_items_location.clear();
		g_scouts.clear();
		g_scouts_done = false;   // #85: fresh scout round pending -> verifier waits
		g_pending_checks.clear(); // #85: no checks in flight on a fresh connect
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
	g_status = AP_NET_STATUS_CONNECTING; // dialing; handlers advance this
	g_last_error.clear();
	return 0;
}

extern "C" void ap_net_connect_slot(const char *slot, const char *password)
{
	g_slot = slot ? slot : "";
	g_password = password ? password : "";
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
		std::fprintf(stderr,
		             "[AP NET] network exception in %s (server connection lost? "
		             "retrying in background): %s (occurrence %u)\n",
		             where, what, count);
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
	if (g_ap && g_connected)
	{
		AP_NET_GUARD("send_location", {
			g_ap->LocationChecks({(int64_t)location_code});
			g_pending_checks.insert((int64_t)location_code); // #85: in flight until its receipt drains
		});
	}
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

extern "C" int ap_net_self_slot(void)
{
	return g_ap ? g_ap->get_player_number() : -1;
}

extern "C" int ap_net_player_count(void)
{
	return g_ap ? (int)g_ap->get_players().size() : 0;
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
		if (n < (int)(sizeof g_recv_batch_player / sizeof g_recv_batch_player[0]))
		{
			g_recv_batch_player[n] = pl;
			g_recv_batch_index[n] = ix;
			g_recv_batch_location[n] = lc;
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
		delete g_ap;
		g_ap = nullptr;
	}
	g_connected = false;
	g_items.clear();
	g_items_player.clear();
	g_items_index.clear();
	g_items_location.clear();
	g_scouts.clear();
	g_scouts_done = false;    // #85: no valid scout cache after shutdown
	g_pending_checks.clear(); // #85: drop any in-flight checks on a server switch
	g_dl_incoming = false; // a pending death must not survive a server switch
	g_diff_known = false; // a difficulty override must not survive a server switch
	g_status = AP_NET_STATUS_IDLE; // set last: delete g_ap may fire the disconnect handler
	g_last_error.clear();
}
