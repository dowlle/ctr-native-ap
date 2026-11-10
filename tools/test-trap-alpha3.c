// Host harness for the seven alpha3 data package traps (ruled 2026-11-06):
// their scheduler rows (ap/ap_trap_sched_logic.h) and their arithmetic
// (ap/ap_trap_alpha3_logic.h). No engine; the engine call sites are thin
// wrappers around these functions in ap/ap_traps.c.
//
//   cc -std=c99 -Wall -Wextra -o /tmp/ta3 tools/test-trap-alpha3.c && /tmp/ta3

#include <stdio.h>
#include <string.h>

#include "../ap/ap_trap_items.h"
#include "../ap/ap_trap_alpha3_logic.h"

static int failures;
static int checks;

static void expect(const char *name, int got, int want)
{
	checks++;
	if (got != want)
	{
		printf("FAIL %-60s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

#define FRAME_MS 100

static AP_TrapWorld world_in(int context)
{
	AP_TrapWorld w;
	memset(&w, 0, sizeof w);
	w.context = context;
	w.mapEpoch = 1;
	w.controlUnlocked = 1;
	w.elapsedMs = FRAME_MS;
	return w;
}

static void run(AP_TrapSched *s, const AP_TrapWorld *w, int frames)
{
	int i;
	for (i = 0; i < frames; i++)
		AP_TrapSchedStep(s, w);
}

static void case_identities(void)
{
	static const struct { int index; int effect; const char *key; const char *name; } rows[] = {
		{208, AP_TRAP_ALWAYS_JUMPING, "always_jumping", "Always Jumping"},
		{209, AP_TRAP_NO_DRIFTING, "no_drifting", "No Drifting"},
		{210, AP_TRAP_RIVAL_SURGE, "rival_surge", "Rival Surge"},
		{211, AP_TRAP_MUD_SEASON, "mud_season", "Mud Season"},
		{212, AP_TRAP_LIMP_MODE, "limp_mode", "Limp Mode"},
		{213, AP_TRAP_STUCK_STEERING, "stuck_steering", "Stuck Steering"},
		{214, AP_TRAP_WUMPA_LEAK, "wumpa_leak", "Wumpa Leak"},
	};
	int i;
	expect("enum continues after Reverse Camera", AP_TRAP_ALWAYS_JUMPING, AP_TRAP_REVERSE_CAMERA + 1);
	expect("29 effects", AP_TRAP_EFFECT_COUNT, 29);
	for (i = 0; i < 7; i++)
	{
		expect(rows[i].name, AP_TrapEffectForItemId(35010000LL + rows[i].index), rows[i].effect);
		expect("key matches", strcmp(AP_TRAP_EFFECT_KEY[rows[i].effect], rows[i].key), 0);
		expect("name matches", strcmp(AP_TRAP_DESC[rows[i].effect].name, rows[i].name), 0);
		expect("active in this build", AP_TRAP_DESC[rows[i].effect].active, 1);
		expect("never in a hub", (AP_TRAP_DESC[rows[i].effect].contexts & AP_TRAP_CTX_HUB) != 0, 0);
	}
	expect("35010215 is not a trap", AP_TrapEffectForItemId(35010215LL), -1);
}

// Decision D3 B: every lasting new trap follows Trap Duration, except Stuck
// Steering, which always runs 7 seconds.
static void case_durations(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 30000);
	AP_TrapSchedReceive(&s, AP_TRAP_STUCK_STEERING);
	AP_TrapSchedReceive(&s, AP_TRAP_LIMP_MODE);
	run(&s, &w, 10); // warning
	run(&s, &w, 1);
	expect("Stuck Steering active", AP_TrapSchedActive(&s, AP_TRAP_STUCK_STEERING), 1);
	expect("Limp Mode active", AP_TrapSchedActive(&s, AP_TRAP_LIMP_MODE), 1);
	run(&s, &w, 69);
	expect("Stuck Steering still on at 6.9 s", AP_TrapSchedActive(&s, AP_TRAP_STUCK_STEERING), 1);
	run(&s, &w, 1);
	expect("Stuck Steering ends at 7 s with Trap Duration 30 s",
	       AP_TrapSchedActive(&s, AP_TRAP_STUCK_STEERING), 0);
	expect("Limp Mode follows Trap Duration", AP_TrapSchedActive(&s, AP_TRAP_LIMP_MODE), 1);
	run(&s, &w, 230);
	expect("Limp Mode ends at 30 s", AP_TrapSchedActive(&s, AP_TRAP_LIMP_MODE), 0);

	// Full Race keeps Limp Mode for the race, Stuck Steering still 7 s.
	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 0);
	AP_TrapSchedReceive(&s, AP_TRAP_STUCK_STEERING);
	AP_TrapSchedReceive(&s, AP_TRAP_NO_DRIFTING);
	run(&s, &w, 11 + 70);
	expect("Full Race: Stuck Steering still 7 s", AP_TrapSchedActive(&s, AP_TRAP_STUCK_STEERING), 0);
	expect("Full Race: No Drifting runs on", AP_TrapSchedActive(&s, AP_TRAP_NO_DRIFTING), 1);
}

// A second Stuck Steering copy refreshes the fixed 7 s window.
static void case_stuck_refresh(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 15000);
	AP_TrapSchedReceive(&s, AP_TRAP_STUCK_STEERING);
	run(&s, &w, 11 + 50);
	AP_TrapSchedReceive(&s, AP_TRAP_STUCK_STEERING);
	run(&s, &w, 1);
	run(&s, &w, 60);
	expect("refreshed copy still running 6 s later", AP_TrapSchedActive(&s, AP_TRAP_STUCK_STEERING), 1);
	run(&s, &w, 10);
	expect("and ends 7 s after the refresh", AP_TrapSchedActive(&s, AP_TRAP_STUCK_STEERING), 0);
}

// Always Jumping and No Drifting share the hop control family: never together.
static void case_hop_family(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	expect("Always Jumping family", AP_TRAP_DESC[AP_TRAP_ALWAYS_JUMPING].family, AP_TRAP_FAMILY_HOP_CONTROL);
	expect("No Drifting family", AP_TRAP_DESC[AP_TRAP_NO_DRIFTING].family, AP_TRAP_FAMILY_HOP_CONTROL);
	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 10000);
	AP_TrapSchedReceive(&s, AP_TRAP_ALWAYS_JUMPING);
	AP_TrapSchedReceive(&s, AP_TRAP_NO_DRIFTING);
	run(&s, &w, 11);
	expect("first in receipt order runs", AP_TrapSchedActive(&s, AP_TRAP_ALWAYS_JUMPING), 1);
	expect("second waits", AP_TrapSchedActive(&s, AP_TRAP_NO_DRIFTING), 0);
	run(&s, &w, 100);
	run(&s, &w, 11);
	expect("second runs after the first", AP_TrapSchedActive(&s, AP_TRAP_NO_DRIFTING), 1);
	expect("first is over", AP_TrapSchedActive(&s, AP_TRAP_ALWAYS_JUMPING), 0);
}

// Contexts: Rival Surge needs AI; none of the seven fires in a hub.
static void case_contexts(void)
{
	AP_TrapSched s;
	AP_TrapWorld hub = world_in(AP_TRAP_CTX_HUB);
	AP_TrapWorld trial = world_in(AP_TRAP_CTX_TIME_TRIAL);
	AP_TrapWorld race = world_in(AP_TRAP_CTX_RACE);
	AP_TrapSchedReset(&s);
	AP_TrapSchedSetDuration(&s, 15000);
	AP_TrapSchedReceive(&s, AP_TRAP_RIVAL_SURGE);
	AP_TrapSchedReceive(&s, AP_TRAP_MUD_SEASON);
	run(&s, &hub, 30);
	expect("hub: Mud Season waits", AP_TrapSchedActive(&s, AP_TRAP_MUD_SEASON), 0);
	expect("hub: Rival Surge waits", AP_TrapSchedActive(&s, AP_TRAP_RIVAL_SURGE), 0);
	run(&s, &trial, 30);
	expect("time trial: Mud Season runs", AP_TrapSchedActive(&s, AP_TRAP_MUD_SEASON), 1);
	expect("time trial: Rival Surge waits (no AI)", AP_TrapSchedActive(&s, AP_TRAP_RIVAL_SURGE), 0);
	run(&s, &race, 30);
	expect("race: Rival Surge runs", AP_TrapSchedActive(&s, AP_TRAP_RIVAL_SURGE), 1);
	expect("Mud Season queues a second copy for a later map",
	       AP_TRAP_DESC[AP_TRAP_MUD_SEASON].duplicate, AP_TRAP_DUP_QUEUE_LATER_MAP);
}

// Wumpa Leak: half, rounded down, waits for 2 fruit, one copy at a time.
static void case_wumpa_leak(void)
{
	AP_TrapSched s;
	AP_TrapWorld w = world_in(AP_TRAP_CTX_RACE);
	expect("11 fruit lose 5", AP_TrapWumpaLeakAmount(11), 5);
	expect("10 fruit lose 5", AP_TrapWumpaLeakAmount(10), 5);
	expect("2 fruit lose 1", AP_TrapWumpaLeakAmount(2), 1);
	expect("1 fruit is left alone", AP_TrapWumpaLeakAmount(1), 0);
	expect("0 fruit is left alone", AP_TrapWumpaLeakAmount(0), 0);
	expect("instant", AP_TRAP_DESC[AP_TRAP_WUMPA_LEAK].timing, AP_TRAP_TIMING_INSTANT);
	expect("copies serialize", AP_TRAP_DESC[AP_TRAP_WUMPA_LEAK].duplicate, AP_TRAP_DUP_SERIALIZE);
	AP_TrapSchedReset(&s);
	AP_TrapSchedReceive(&s, AP_TRAP_WUMPA_LEAK);
	run(&s, &w, 30);
	expect("no fruit: stays armed", AP_TrapSchedStateOf(&s, AP_TRAP_WUMPA_LEAK), AP_TRAP_SLOT_ARMED);
	w.conditions = AP_TRAP_COND_TWO_WUMPA;
	run(&s, &w, 11);
	expect("2 fruit: fires", AP_TrapSchedStateOf(&s, AP_TRAP_WUMPA_LEAK), AP_TRAP_SLOT_ACTIVE);
	expect("condition bit 7, bit 5 stays retired", AP_TRAP_COND_TWO_WUMPA, 1 << 7);
}

static void case_inputs(void)
{
	expect("No Drifting drops L1 and R1", (int)AP_TrapNoDriftButtons(0xc00u | 0x10u), 0x10);
	expect("No Drifting keeps other buttons", (int)AP_TrapNoDriftButtons(0x4040u), 0x4040);
	expect("left dead: full left held at centre", AP_TrapStuckStick(0, 0x80, -1), 0x80);
	expect("left dead: right untouched", AP_TrapStuckStick(0xff, 0x80, -1), 0xff);
	expect("right dead: full right held at centre", AP_TrapStuckStick(0xff, 0x80, 1), 0x80);
	expect("right dead: left untouched", AP_TrapStuckStick(0x10, 0x80, 1), 0x10);
	expect("wheel centre is used", AP_TrapStuckStick(0x70, 0x76, -1), 0x76);
	expect("a clamp twice is the same clamp",
	       AP_TrapStuckStick(AP_TrapStuckStick(0x20, 0x80, -1), 0x80, -1), 0x80);
	{
		int left = 0, right = 0;
		unsigned i;
		for (i = 1; i <= 64; i++)
			if (AP_TrapStuckSidePick(i * 1234u, i) < 0)
				left++;
			else
				right++;
		expect("both sides get picked", left > 8 && right > 8, 1);
	}
}

static void case_speeds(void)
{
	expect("Limp Mode: half of 12000", AP_TrapLimpClamp(15000, 12000), 6000);
	expect("Limp Mode: slower is untouched", AP_TrapLimpClamp(4000, 12000), 4000);
	expect("Limp Mode: reverse held too", AP_TrapLimpClamp(-9000, 12000), -6000);
	expect("Limp Mode: never 0", AP_TrapLimpClamp(5, 1), 1);
	expect("Rival Surge: 3/2", AP_TrapRivalSurgeDelta(100), 150);
	expect("Rival Surge: stopped stays stopped", AP_TrapRivalSurgeDelta(0), 0);
}

static void case_mud_and_jump(void)
{
	expect("Mud Season blocked by Ignore Dirt", AP_TrapMudSeasonBlocked(AP_TRAP_MUD_SEASON, 1), 1);
	expect("Mud Season without Ignore Dirt", AP_TrapMudSeasonBlocked(AP_TRAP_MUD_SEASON, 0), 0);
	expect("other traps never say BLOCKED", AP_TrapMudSeasonBlocked(AP_TRAP_ICY, 1), 0);
	// active, kart state, buffer, coyote, cooldown, lockout
	expect("hop on a grounded chance", AP_TrapAlwaysJumpWants(1, 0, 0, 64, 0, 0), 1);
	expect("hop during a running drift", AP_TrapAlwaysJumpWants(1, 2, 0, 64, 0, 0), 1);
	expect("not when off", AP_TrapAlwaysJumpWants(0, 0, 0, 64, 0, 0), 0);
	expect("not when off, drifting", AP_TrapAlwaysJumpWants(0, 2, 0, 64, 0, 0), 0);
	expect("a player buffer is never replaced", AP_TrapAlwaysJumpWants(1, 0, 7, 64, 0, 0), 0);
	expect("a player buffer is never replaced, drifting", AP_TrapAlwaysJumpWants(1, 2, 7, 64, 0, 0), 0);
	expect("not in the air", AP_TrapAlwaysJumpWants(1, 0, 0, 0, 0, 0), 0);
	expect("not in the air, drifting", AP_TrapAlwaysJumpWants(1, 2, 0, 0, 0, 0), 0);
	expect("not during cooldown", AP_TrapAlwaysJumpWants(1, 0, 0, 64, 0x180, 0), 0);
	expect("not during cooldown, drifting", AP_TrapAlwaysJumpWants(1, 2, 0, 64, 0x180, 0), 0);
	expect("Red Potion cloud wins", AP_TrapAlwaysJumpWants(1, 0, 0, 64, 0, 1), 0);
	expect("Red Potion cloud wins, drifting", AP_TrapAlwaysJumpWants(1, 2, 0, 64, 0, 1), 0);
	{
		// Damage, scripted and freeze states stay out (decomp enum values).
		static const int out[] = {1, 3, 4, 5, 6, 9, 10, 11};
		unsigned i;
		for (i = 0; i < sizeof(out) / sizeof(out[0]); i++)
			expect("no hop in a damage, scripted or freeze state",
			       AP_TrapAlwaysJumpWants(1, out[i], 0, 64, 0, 0), 0);
	}
}

int main(void)
{
	case_identities();
	case_durations();
	case_stuck_refresh();
	case_hop_family();
	case_contexts();
	case_wumpa_leak();
	case_inputs();
	case_speeds();
	case_mud_and_jump();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
