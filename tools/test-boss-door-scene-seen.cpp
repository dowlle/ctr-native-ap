// g++ -m32 -std=c++17 -Wall -Wextra -I ap -I ap/vendor/json/include -o /tmp/test-boss-door-scene-seen tools/test-boss-door-scene-seen.cpp
//
// Issue #377: the once-per-hub boss-door scene flag, one bit per boss hub in a
// server data storage value (ap/ap_boss_door_scene_seen.h). The storage rules
// are shared with the Oxide scene flag (ap/ap_scene_seen_flags.h, also covered
// by tools/test-oxide-scene-seen.cpp). The network glue is covered by
// tools/test-boss-door-scene-production.py.
#include <cstdio>
#include "ap_boss_door_scene_seen.h"
#include "ap_oxide_scene_seen.h"

static int failures;

#define CHECK(label, expression) do { \
	bool passed = (expression); \
	std::printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

int main()
{
	APBossDoorSceneSeen f;

	CHECK("fresh: unknown", !f.known());
	CHECK("record before connect is refused", !f.recordHub(0) && !f.seenHub(0));
	f.connect("host", "seed:a", 0, 1);
	CHECK("key names seed, team and slot",
	      f.key == "ctr_boss_door_scene_v1:6:seed:a:0:1");
	CHECK("connected, Get not answered: unknown", !f.known());
	f.retrieved(nullptr);
	CHECK("missing key: known, no hub seen",
	      f.known() && !f.seenHub(0) && !f.seenHub(1) && !f.seenHub(2) && !f.seenHub(3));

	// One bit per hub.
	CHECK("hub 1 recorded", f.recordHub(1));
	CHECK("hub 1 seen, others not", f.seenHub(1) && !f.seenHub(0) && !f.seenHub(2) && !f.seenHub(3));
	CHECK("hub 1 pending as bit 1", f.pending == 2u && f.wantsSend());
	CHECK("hub 1 second record is not a play", !f.recordHub(1));
	CHECK("out of range hubs refused",
	      !f.recordHub(-1) && !f.recordHub(4) && !f.seenHub(-1) && !f.seenHub(4));
	f.sent = true;
	CHECK("hub 3 recorded while a Set is in flight", f.recordHub(3));
	CHECK("no second Set while one is in flight", !f.wantsSend());
	f.reply(2);
	CHECK("reply for hub 1 leaves hub 3 pending", f.pending == 8u && f.wantsSend());
	f.sent = true;
	f.reply(10);
	CHECK("then both confirmed", f.pending == 0 && f.seenHub(1) && f.seenHub(3));

	// Restart on the same room: the server value comes back; stale lower
	// values cannot clear a hub.
	APBossDoorSceneSeen g;
	g.connect("host", "seed:a", 0, 1);
	g.retrieved(15);
	CHECK("restart: all four hubs seen",
	      g.seenHub(0) && g.seenHub(1) && g.seenHub(2) && g.seenHub(3));
	g.retrieved(0);
	CHECK("stale 0 cannot clear", g.seenHub(0) && g.seenHub(3));

	// Disconnect before the acknowledgement: pending survives and is resent
	// after the next Get reply for the same room.
	APBossDoorSceneSeen d;
	d.connect("host", "seed:a", 0, 1);
	d.retrieved(nullptr);
	d.recordHub(2);
	d.sent = true;
	d.disconnected();
	CHECK("dropped: unknown, still seen locally", !d.known() && d.seenHub(2) && d.pending);
	d.connect("host", "seed:a", 0, 1);
	CHECK("same room reconnect: nothing sent before Get", !d.wantsSend());
	d.retrieved(nullptr);
	CHECK("after Get the pending hub is resent", d.wantsSend() && d.pending == 4u);

	// A different room, seed, team or slot starts clean.
	const char *what[] = {"other endpoint", "other seed", "other team", "other slot"};
	for (int i = 0; i < 4; i++)
	{
		APBossDoorSceneSeen r;
		r.connect("host", "seed:a", 0, 1);
		r.retrieved(15);
		if (i == 0) r.connect("other", "seed:a", 0, 1);
		if (i == 1) r.connect("host", "seed:b", 0, 1);
		if (i == 2) r.connect("host", "seed:a", 1, 1);
		if (i == 3) r.connect("host", "seed:a", 0, 2);
		CHECK(what[i], !r.seenHub(0) && !r.pending && !r.known());
	}

	// Values another tool could have written: unknown for the session.
	for (auto v : {nlohmann::json(-1), nlohmann::json(16), nlohmann::json(1.5),
	               nlohmann::json(true), nlohmann::json("1"), nlohmann::json::array(),
	               nlohmann::json(18446744073709551615ULL)})
	{
		APBossDoorSceneSeen b;
		b.connect("host", "seed:a", 0, 1);
		b.retrieved(v);
		if (b.known() || b.seenHub(0))
		{
			std::printf("FAIL  malformed value accepted: %s\n", v.dump().c_str());
			failures++;
		}
	}
	std::printf("ok    malformed values leave the flag unknown\n");

	// The two flags are separate keys with separate ranges: the boss-door
	// value 15 is out of range for the Oxide key, and the keys never collide.
	APOxideSceneSeen o;
	o.connect("host", "seed:a", 0, 1);
	CHECK("separate keys", o.key != f.key);
	o.retrieved(15);
	CHECK("oxide key rejects boss-door values", !o.known());

	std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
