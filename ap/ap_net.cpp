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

static APClient            *g_ap = nullptr;
static std::deque<long long> g_items; // received item ids, drained by the game
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
		g_scouts.clear();
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
				if (ctr_cfg.podium[t].first >= 0)
					locs.push_back((int64_t)ctr_cfg.podium[t].first);
				if (ctr_cfg.podium[t].podium >= 0)
					locs.push_back((int64_t)ctr_cfg.podium[t].podium);
				if (ctr_cfg.podium[t].any >= 0)
					locs.push_back((int64_t)ctr_cfg.podium[t].any);
			}
		}
		g_ap->LocationScouts(locs, 0);
		std::fprintf(stderr, "[AP NET] slot connected; scouting %d locations\n",
		             AP_LOCATION_TABLE_LEN);
	});
	g_ap->set_location_info_handler([](const std::list<APClient::NetworkItem> &items) {
		for (const auto &it : items)
			g_scouts[it.location] = it;
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
			std::fprintf(stderr, "[AP NET] received item %lld (index %d)\n",
			             (long long)it.item, it.index);
		}
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

extern "C" void ap_net_poll(void)
{
	if (g_ap)
		g_ap->poll();
}

extern "C" int ap_net_is_connected(void)
{
	return (g_ap && g_ap->get_state() == APClient::State::SLOT_CONNECTED) ? 1 : 0;
}

extern "C" void ap_net_send_location(long long location_code)
{
	if (g_ap && g_connected)
		g_ap->LocationChecks({(int64_t)location_code});
}

extern "C" void ap_net_send_goal(void)
{
	if (g_ap && g_connected)
		g_ap->StatusUpdate(APClient::ClientStatus::GOAL);
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
	while (n < max && !g_items.empty())
	{
		out[n++] = g_items.front();
		g_items.pop_front();
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
	const std::string key = ap_diff_key();
	g_ap->SetNotify({key});
	g_ap->Get({key});
}

extern "C" void ap_net_difficulty_set(int value)
{
	if (!g_ap || !g_connected)
		return;
	// replace: write value unconditionally; default seeds the key if it is unset.
	APClient::DataStorageOperation op;
	op.operation = "replace";
	op.value = value;
	g_ap->Set(ap_diff_key(), value, false, {op});
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

extern "C" void ap_net_shutdown(void)
{
	if (g_ap)
	{
		delete g_ap;
		g_ap = nullptr;
	}
	g_connected = false;
	g_items.clear();
	g_scouts.clear();
	g_diff_known = false; // a difficulty override must not survive a server switch
	g_status = AP_NET_STATUS_IDLE; // set last: delete g_ap may fire the disconnect handler
	g_last_error.clear();
}
