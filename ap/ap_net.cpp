// Archipelago networking for CTR-Native.
//
// Compiled as an isolated C++17 static library (see the CTR_AP block in
// CMakeLists.txt) and called from the C unity build (ap_hooks.c) through the C
// API in ap_net.h. Plain ws:// only -- no TLS, no compression, no schema --
// which reduces the dependency footprint to header-only libs + ws2_32.
//
// apclientpp's poll() is single-threaded: handlers fire inline during the
// ap_net_poll() call on the game thread, so no locking is needed around the
// shared item queue.

#include "apclient.hpp" // pulls wswrap + websocketpp + asio + nlohmann/json
#include "ap_net.h"

#include <deque>
#include <list>
#include <string>
#include <cstdio>
#include <cstdint>

static APClient            *g_ap = nullptr;
static std::deque<long long> g_items; // received item ids, drained by the game
static std::string           g_slot;
static std::string           g_password;
static bool                  g_connected = false;

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
		std::fprintf(stderr, "[AP NET] socket connected\n");
	});
	g_ap->set_socket_disconnected_handler([]() {
		g_connected = false;
		std::fprintf(stderr, "[AP NET] socket disconnected\n");
	});
	g_ap->set_room_info_handler([]() {
		std::fprintf(stderr, "[AP NET] RoomInfo; connecting slot '%s'\n", g_slot.c_str());
		// items_handling 0b111 = remote items + own world + starting inventory.
		g_ap->ConnectSlot(g_slot, g_password, 7, {"AP"});
	});
	g_ap->set_slot_connected_handler([](const nlohmann::json &slotData) {
		g_connected = true;
		(void)slotData;
		std::fprintf(stderr, "[AP NET] slot connected\n");
	});
	g_ap->set_slot_refused_handler([](const std::list<std::string> &errors) {
		g_connected = false;
		std::string e;
		for (const auto &s : errors)
		{
			e += s;
			e += ' ';
		}
		std::fprintf(stderr, "[AP NET] slot refused: %s\n", e.c_str());
	});
	g_ap->set_items_received_handler([](const std::list<APClient::NetworkItem> &items) {
		for (const auto &it : items)
		{
			g_items.push_back((long long)it.item);
			std::fprintf(stderr, "[AP NET] received item %lld (index %d)\n",
			             (long long)it.item, it.index);
		}
	});
	return 0;
}

extern "C" void ap_net_connect_slot(const char *slot, const char *password)
{
	g_slot = slot ? slot : "";
	g_password = password ? password : "";
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

extern "C" void ap_net_shutdown(void)
{
	if (g_ap)
	{
		delete g_ap;
		g_ap = nullptr;
	}
	g_connected = false;
	g_items.clear();
}
