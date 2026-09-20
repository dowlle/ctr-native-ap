#ifndef CTR_HARNESS_APCLIENT_MOCK_HPP
#define CTR_HARNESS_APCLIENT_MOCK_HPP

// Minimal deterministic stand-in for apclientpp's APClient, used ONLY by
// tools/test-net-admission.cpp. The harness #includes the PRODUCTION
// ap/ap_net.cpp; this header replaces the websocket transport so the test can
// drive the real slot-connected/ReceivedItems handlers synchronously and assert
// the production admission ordering (parse-before-side-effects, deferred
// rejection teardown, packet-tail guards) without a live server.
//
// This is a mock TRANSPORT, not a reimplementation of ap_net's logic: every
// decision under test lives in the production translation unit.

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Harness-settable room identity, read by the constructor. ap_net.cpp constructs
// APClient from ap_net_init(); the harness sets these before calling it.
inline std::string &ap_mock_seed()
{
	static std::string s = "2101";
	return s;
}
inline std::string &ap_mock_slot()
{
	static std::string s = "Harness";
	return s;
}

class APClient
{
public:
	enum class State
	{
		DISCONNECTED,
		SOCKET_CONNECTING,
		SOCKET_CONNECTED,
		SLOT_CONNECTING,
		SLOT_CONNECTED,
	};

	enum class ClientStatus
	{
		CLIENT_UNKNOWN = 0,
		CLIENT_READY,
		CLIENT_PLAYING,
		CLIENT_GOAL = 30,
		GOAL = 30,
	};

	struct NetworkItem
	{
		int64_t  item = 0;
		int64_t  location = 0;
		int      player = 0;
		unsigned flags = 0;
		int      index = -1;
	};

	struct DataStorageOperation
	{
		std::string   operation;
		nlohmann::json value;
	};

	// Everything the test needs to observe about outbound traffic.
	struct Calls
	{
		int location_checks = 0;
		int location_scouts = 0;
		int data_set = 0;
		int data_get = 0;
		int set_notify = 0;
		int connect_update = 0;
		int status_update = 0;
		int bounce = 0;
		int connect_slot = 0;
		std::vector<long long> checked_codes;
		std::vector<std::string> set_keys;
		std::vector<std::string> get_keys;
		void reset()
		{
			location_checks = location_scouts = data_set = data_get = 0;
			set_notify = connect_update = status_update = bounce = connect_slot = 0;
			checked_codes.clear();
			set_keys.clear();
			get_keys.clear();
		}
	};
	Calls calls;

	APClient(const std::string &, const std::string &, const std::string &,
	         const std::string &)
	{
	}

	void poll() {}

	// ── handler registration ──
	void set_socket_connected_handler(std::function<void()> h) { socket_connected_ = std::move(h); }
	void set_socket_disconnected_handler(std::function<void()> h) { socket_disconnected_ = std::move(h); }
	void set_socket_error_handler(std::function<void(const std::string &)> h) { socket_error_ = std::move(h); }
	void set_room_info_handler(std::function<void()> h) { room_info_ = std::move(h); }
	void set_slot_connected_handler(std::function<void(const nlohmann::json &)> h) { slot_connected_ = std::move(h); }
	void set_location_info_handler(std::function<void(const std::list<NetworkItem> &)> h) { location_info_ = std::move(h); }
	void set_slot_refused_handler(std::function<void(const std::list<std::string> &)> h) { slot_refused_ = std::move(h); }
	void set_retrieved_handler(std::function<void(const std::map<std::string, nlohmann::json> &)> h) { retrieved_ = std::move(h); }
	void set_set_reply_handler(std::function<void(const std::string &, const nlohmann::json &, const nlohmann::json &)> h) { set_reply_ = std::move(h); }
	void set_items_received_handler(std::function<void(const std::list<NetworkItem> &)> h) { items_received_ = std::move(h); }
	void set_bounced_handler(std::function<void(const nlohmann::json &)> h) { bounced_ = std::move(h); }

	// ── outbound (recorded) ──
	void ConnectSlot(const std::string &, const std::string &, int, std::vector<std::string>)
	{
		calls.connect_slot++;
		state_ = State::SLOT_CONNECTED;
	}
	void ConnectUpdate(bool, int, bool, std::vector<std::string>) { calls.connect_update++; }
	void LocationChecks(std::list<int64_t> codes)
	{
		calls.location_checks++;
		for (int64_t c : codes)
			calls.checked_codes.push_back((long long)c);
	}
	void LocationScouts(std::list<int64_t>, int) { calls.location_scouts++; }
	void StatusUpdate(ClientStatus) { calls.status_update++; }
	bool Set(const std::string &key, const nlohmann::json &, bool, std::list<DataStorageOperation>)
	{
		calls.data_set++;
		calls.set_keys.push_back(key);
		return true;
	}
	void Get(std::list<std::string> keys)
	{
		calls.data_get++;
		for (auto &k : keys)
			calls.get_keys.push_back(k);
	}
	void SetNotify(std::list<std::string> keys)
	{
		calls.set_notify++;
		for (auto &k : keys)
			calls.get_keys.push_back(k);
	}
	void Bounce(nlohmann::json, std::vector<int>, std::vector<int>, std::vector<std::string>) { calls.bounce++; }

	// ── inbound state ──
	State get_state() const { return state_; }
	std::string get_seed() const { return ap_mock_seed(); }
	std::string get_slot() const { return ap_mock_slot(); }
	int get_team_number() const { return 0; }
	int get_player_number() const { return 1; }
	const std::set<int64_t> &get_checked_locations() const { return checked_; }
	const std::set<int64_t> &get_missing_locations() const { return missing_; }
	const std::map<int, std::string> &get_players() const { return players_; }
	std::string get_player_game(int) const { return "Crash Team Racing"; }
	std::string get_player_alias(int) const { return "Harness"; }
	std::string get_item_name(long long, const std::string &) const { return "Item"; }

	// ── test drivers (mock-only; never called by production) ──
	void emit_socket_connected()
	{
		state_ = State::SOCKET_CONNECTED;
		if (socket_connected_)
			socket_connected_();
	}
	void emit_socket_disconnected()
	{
		state_ = State::DISCONNECTED;
		if (socket_disconnected_)
			socket_disconnected_();
	}
	void emit_socket_error(const std::string &msg)
	{
		if (socket_error_)
			socket_error_(msg);
	}
	void emit_slot_refused(const std::list<std::string> &errors)
	{
		if (slot_refused_)
			slot_refused_(errors);
	}
	void emit_room_info()
	{
		if (room_info_)
			room_info_();
	}
	void emit_slot_connected(const nlohmann::json &sd)
	{
		state_ = State::SLOT_CONNECTED;
		if (slot_connected_)
			slot_connected_(sd);
	}
	void emit_items_received(const std::list<NetworkItem> &items)
	{
		if (items_received_)
			items_received_(items);
	}
	void emit_location_info(const std::list<NetworkItem> &items)
	{
		if (location_info_)
			location_info_(items);
	}
	void emit_retrieved(const std::map<std::string, nlohmann::json> &keys)
	{
		if (retrieved_)
			retrieved_(keys);
	}
	void emit_set_reply(const std::string &key, const nlohmann::json &v)
	{
		if (set_reply_)
			set_reply_(key, v, nlohmann::json());
	}
	void emit_bounced(const nlohmann::json &packet)
	{
		if (bounced_)
			bounced_(packet);
	}
	void test_set_checked(const std::set<int64_t> &s) { checked_ = s; }
	void test_set_missing(const std::set<int64_t> &s) { missing_ = s; }

private:
	State state_ = State::DISCONNECTED;
	std::set<int64_t> checked_;
	std::set<int64_t> missing_;
	std::map<int, std::string> players_{{1, "Harness"}};

	std::function<void()> socket_connected_;
	std::function<void()> socket_disconnected_;
	std::function<void(const std::string &)> socket_error_;
	std::function<void()> room_info_;
	std::function<void(const nlohmann::json &)> slot_connected_;
	std::function<void(const std::list<NetworkItem> &)> location_info_;
	std::function<void(const std::list<std::string> &)> slot_refused_;
	std::function<void(const std::map<std::string, nlohmann::json> &)> retrieved_;
	std::function<void(const std::string &, const nlohmann::json &, const nlohmann::json &)> set_reply_;
	std::function<void(const std::list<NetworkItem> &)> items_received_;
	std::function<void(const nlohmann::json &)> bounced_;
};

#endif
