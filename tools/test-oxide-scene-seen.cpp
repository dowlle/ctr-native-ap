// g++ -m32 -std=c++17 -Wall -Wextra -I ap -I ap/vendor/json/include -o /tmp/test-oxide-scene-seen tools/test-oxide-scene-seen.cpp
//
// Issue #377: the once-per-seed Oxide Final Challenge scene flag and the
// "Oxide Final Challenge is open" message flag, two bits of one server data
// storage value (ap/ap_oxide_scene_seen.h). The network glue that drives
// this struct is covered by tools/test-oxide-scene-seen-production.py.
#include <cstdio>
#include "ap_oxide_scene_seen.h"

static int failures;

#define CHECK(label, expression) do { \
	bool passed = (expression); \
	std::printf("%s  %s\n", passed ? "ok  " : "FAIL", label); \
	failures += !passed; \
} while (0)

int main()
{
	APOxideSceneSeen f;

	// Fail safe before connect and before the Get reply.
	CHECK("fresh: unknown", !f.known());
	CHECK("record before connect is refused", !f.record() && !f.seen());
	f.connect("host", "seed:a", 0, 1);
	CHECK("key names seed, team and slot",
	      f.key == "ctr_oxide_final_scene_v1:6:seed:a:0:1");
	CHECK("connected, Get not answered: unknown", !f.known());

	// Missing key (fresh room): known, not seen.
	f.retrieved(nullptr);
	CHECK("missing key: known and unseen", f.known() && !f.seen());

	// Play, send, acknowledge.
	CHECK("first record is the play", f.record());
	CHECK("recorded locally at once", f.seen() && f.pending && f.wantsSend());
	CHECK("second record is not a play", !f.record());
	f.sent = true;
	CHECK("in flight: no resend", !f.wantsSend());
	f.reply(1);
	CHECK("acknowledged: seen, nothing pending", f.seen() && !f.pending && !f.wantsSend());

	// Client restart on the same room: the server value comes back.
	APOxideSceneSeen g;
	g.connect("host", "seed:a", 0, 1);
	g.retrieved(1);
	CHECK("restart: seen from the server", g.known() && g.seen());
	g.retrieved(0);
	CHECK("stale 0 cannot clear a seen flag", g.seen());

	// Stored 0 is unseen.
	APOxideSceneSeen z;
	z.connect("host", "seed:a", 0, 1);
	z.retrieved(0);
	CHECK("stored 0: known and unseen", z.known() && !z.seen());

	// Disconnect before the acknowledgement: pending survives, resent after
	// the next Get reply for the same room.
	APOxideSceneSeen d;
	d.connect("host", "seed:a", 0, 1);
	d.retrieved(nullptr);
	d.record();
	d.sent = true;
	d.disconnected();
	CHECK("dropped: unknown, still seen locally", !d.known() && d.seen() && d.pending);
	d.connect("host", "seed:a", 0, 1);
	CHECK("same room reconnect keeps the pending play", d.seen() && d.pending && !d.wantsSend());
	d.retrieved(nullptr);
	CHECK("after Get the pending play is resent", d.wantsSend());
	d.reply(1);
	CHECK("then acknowledged", !d.pending && d.seen());

	// A different room, seed, team or slot starts clean.
	const char *what[] = {"other endpoint", "other seed", "other team", "other slot"};
	for (int i = 0; i < 4; i++)
	{
		APOxideSceneSeen r;
		r.connect("host", "seed:a", 0, 1);
		r.retrieved(1);
		if (i == 0) r.connect("other", "seed:a", 0, 1);
		if (i == 1) r.connect("host", "seed:b", 0, 1);
		if (i == 2) r.connect("host", "seed:a", 1, 1);
		if (i == 3) r.connect("host", "seed:a", 0, 2);
		CHECK(what[i], !r.seen() && !r.pending && !r.known());
	}

	// Values another tool could have written: unknown for the session, which
	// means no auto-play.
	for (auto v : {nlohmann::json(-1), nlohmann::json(4), nlohmann::json(1.5),
	               nlohmann::json(true), nlohmann::json("1"), nlohmann::json::array(),
	               nlohmann::json(18446744073709551615ULL)})
	{
		APOxideSceneSeen b;
		b.connect("host", "seed:a", 0, 1);
		b.retrieved(v);
		if (b.known() || b.seen())
		{
			std::printf("FAIL  malformed value accepted: %s\n", v.dump().c_str());
			failures++;
		}
	}
	std::printf("ok    malformed values leave the flag unknown\n");

	// Second bit: the open message. Independent of the scene bit, same
	// barrier, same "or" merge.
	APOxideSceneSeen m;
	m.connect("host", "seed:a", 0, 1);
	CHECK("before the Get reply: nothing to send", !m.wantsSend());
	m.retrieved(nullptr);
	CHECK("message: fresh room, not shown", !m.has(AP_OXIDE_FLAG_OPEN_MSG));
	CHECK("message recorded once", m.record(AP_OXIDE_FLAG_OPEN_MSG) &&
	      !m.record(AP_OXIDE_FLAG_OPEN_MSG));
	CHECK("message does not mark the scene", !m.seen() && m.pending == AP_OXIDE_FLAG_OPEN_MSG);
	m.sent = true;
	CHECK("scene recorded while the message Set is in flight", m.record());
	CHECK("no second Set while one is in flight", !m.wantsSend());
	m.reply(2);
	CHECK("reply for the message leaves the scene pending",
	      m.has(AP_OXIDE_FLAG_OPEN_MSG) && m.pending == AP_OXIDE_FLAG_SCENE && m.wantsSend());
	m.sent = true;
	m.reply(3);
	CHECK("then both confirmed", m.pending == 0 && m.seen() && m.has(AP_OXIDE_FLAG_OPEN_MSG));

	APOxideSceneSeen r2;
	r2.connect("host", "seed:a", 0, 1);
	r2.retrieved(2);
	CHECK("restart: message shown, scene not seen", r2.known() && !r2.seen() &&
	      r2.has(AP_OXIDE_FLAG_OPEN_MSG));
	r2.retrieved(3);
	CHECK("restart: both bits", r2.seen() && r2.has(AP_OXIDE_FLAG_OPEN_MSG));
	r2.retrieved(1);
	CHECK("a stale lower value cannot clear a bit", r2.has(AP_OXIDE_FLAG_OPEN_MSG));

	// Ambiguous concatenations cannot collide.
	CHECK("length-prefixed key parts",
	      APOxideSceneSeen::part("ab") + APOxideSceneSeen::part("c") !=
	      APOxideSceneSeen::part("a") + APOxideSceneSeen::part("bc"));

	std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
