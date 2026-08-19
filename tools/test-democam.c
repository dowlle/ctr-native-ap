// Host harness for the Demo Camera prototype state machine.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-democam tools/test-democam.c && /tmp/test-democam
//
// Drives ap/ap_democam_logic.h with no engine present. The acceptance question
// it answers: invoking the retail cinematic camera path without its normal bot /
// end-of-race owner neither changes driver control fields (the mirror carries
// none, by construction) nor leaves camera flags behind after any sequence.

#include <stdio.h>
#include <string.h>

#include "../ap/ap_democam_logic.h"

// Camera flags outside the prototype's owned set, used to prove a concurrent
// engine write survives the release (include/namespace_Camera.h).
#define CAM_FLAG_REVERSE         0x10000u
#define CAM_FLAG_FIRE_SPEED_ZOOM 0x80u
#define CAM_FLAG_TRANSITION_AWAY 0x200u

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-52s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

static void expect_u(const char *name, unsigned got, unsigned want)
{
	if (got != want)
	{
		printf("FAIL %-52s got=0x%x want=0x%x\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

// A camera mid-race: nothing cinematic engaged, but plenty of live state that a
// restore has to reproduce exactly.
static void seed_fields(struct ApDemoCamFields *f)
{
	int i;
	memset(f, 0, sizeof *f);
	f->flags = CAM_FLAG_REVERSE | AP_DEMOCAM_FLAG_TRACK_PATH_FACE_DRIVER;
	f->cameraMode = 0;
	f->cameraModePrev = 0;
	f->currEOR = 0;
	f->action = 0x20000u;
	f->mode = 3;
	f->unk0xC = 0xdeadbeefu;
	f->desiredRotX = -117;
	f->transitionBlend = 0x1000;
	f->transitionFrame = 7;
	f->transitionFrameCount = 0x1e;
	f->spin360Angle = 0x321;
	f->trackPathProgress = 4096;
	f->trackPathNode = 0x80100000ul;
	for (i = 0; i < 3; i++)
	{
		f->transitionToPos[i] = 100 + i;
		f->transitionToRot[i] = 200 + i;
		f->eorEndPos[i] = 300 + i;
		f->driverEyeOffset[i] = 400 + i;
		f->driverLookOffset[i] = 500 + i;
		f->pbPos[i] = 600 + i;
		f->pbRot[i] = 700 + i;
	}
	f->eorSpeed = -64;
	f->angleAxisLerpRatio = 0xa0;
	f->heightSmoothingStart = 33;
	f->blastedLerpFrames = 8;
	f->pbDistanceToScreenPrev = 256;
	f->pbDistanceToScreenCurr = 256;
}

// What CAM_ThTick's end-of-race block does once it selects an authored camera:
// latch the entry, leave the chase camera behind, and start advancing whichever
// mode was authored (CAM.c:1943-2068 plus the per-mode drivers).
static void simulate_cinematic_frames(struct ApDemoCamFields *f)
{
	f->currEOR = 0x80200040ul;
	f->cameraMode = 12;
	f->cameraModePrev = 12;
	f->flags |= AP_DEMOCAM_FLAG_ARCADE_EOR_ACTIVE | AP_DEMOCAM_FLAG_RESET_RAIN_POS |
	            AP_DEMOCAM_FLAG_DIRECTION_CHANGED;
	f->flags &= ~(unsigned)AP_DEMOCAM_FLAG_TRACK_PATH_FACE_DRIVER;
	f->transitionToPos[0] = -900;
	f->transitionToPos[1] = -901;
	f->transitionToPos[2] = -902;
	f->transitionToRot[0] = 11;
	f->transitionToRot[1] = 12;
	f->transitionToRot[2] = 13;
	f->eorEndPos[0] = 77;
	f->eorEndPos[1] = 78;
	f->eorEndPos[2] = 79;
	f->eorSpeed = 24;
	f->trackPathProgress = 999;
	f->trackPathNode = 0x80300000ul;
	f->transitionFrame = 30;
	f->spin360Angle = -12;
	f->driverEyeOffset[0] = 1;
	f->driverLookOffset[2] = 2;
	f->angleAxisLerpRatio = 0xf0;
	f->heightSmoothingStart = 0;
	f->blastedLerpFrames = 0;
	f->action = 1;
	f->mode = 9;
	f->unk0xC = 5;
	f->desiredRotX = 3;
	f->pbPos[0] = -1;
	f->pbRot[2] = 0;
	f->pbDistanceToScreenPrev = 512;
	f->pbDistanceToScreenCurr = 128;
}

static struct ApDemoCamIdentity ident_of(unsigned long cam, unsigned long pbuf,
                                         unsigned long drv, int level)
{
	struct ApDemoCamIdentity id;
	id.cameraDC = cam;
	id.pushBuffer = pbuf;
	id.driver = drv;
	id.levelID = level;
	return id;
}

// Every non-flag member has to come back exactly. Flags are checked separately
// because the reseat pair is deliberately raised on release.
static int fields_equal_except_flags(const struct ApDemoCamFields *a,
                                     const struct ApDemoCamFields *b)
{
	struct ApDemoCamFields ca = *a;
	struct ApDemoCamFields cb = *b;
	ca.flags = 0;
	cb.flags = 0;
	return memcmp(&ca, &cb, sizeof ca) == 0;
}

static struct ApDemoCamGate gate_ok(void)
{
	struct ApDemoCamGate g;
	memset(&g, 0, sizeof g);
	g.cameraPresent = 1;
	g.raceActive = 1;
	g.eorTableUsable = 1;
	return g;
}

// One full engage / cinematic / force-clear cycle, asserted end to end.
static void run_forced_clear(const char *label, int reason, struct ApDemoCamGate gate,
                             int levelChanged, int pointersChanged)
{
	struct ApDemoCamState st;
	struct ApDemoCamFields f, before;
	struct ApDemoCamIdentity id = ident_of(0x1000, 0x2000, 0x3000, 5);
	char name[128];
	int got;

	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;

	expect("forced-clear setup engages", AP_DemoCam_Engage(&st, &id, &f), AP_DEMOCAM_ENGAGE_OK);
	simulate_cinematic_frames(&f);

	got = AP_DemoCam_ForcedClearReason(&gate, levelChanged, pointersChanged);
	snprintf(name, sizeof name, "%s selects its clear reason", label);
	expect(name, got, reason);

	expect("forced clear restores", AP_DemoCam_Release(&st, &id, &f, got),
	       AP_DEMOCAM_RELEASE_RESTORED);

	snprintf(name, sizeof name, "%s leaves non-flag state byte-exact", label);
	expect(name, fields_equal_except_flags(&f, &before), 1);

	snprintf(name, sizeof name, "%s leaves owned flags at the snapshot", label);
	expect_u(name, f.flags & AP_DEMOCAM_FLAG_OWNED_MASK,
	         before.flags & AP_DEMOCAM_FLAG_OWNED_MASK);

	snprintf(name, sizeof name, "%s keeps unrelated flags", label);
	expect_u(name, f.flags & ~(unsigned)(AP_DEMOCAM_FLAG_OWNED_MASK | AP_DEMOCAM_FLAG_RESEAT_MASK),
	         before.flags & ~(unsigned)(AP_DEMOCAM_FLAG_OWNED_MASK | AP_DEMOCAM_FLAG_RESEAT_MASK));

	snprintf(name, sizeof name, "%s ends disengaged", label);
	expect(name, st.engaged, 0);
}

int main(void)
{
	struct ApDemoCamState st;
	struct ApDemoCamFields f, before, held;
	struct ApDemoCamIdentity id = ident_of(0x1000, 0x2000, 0x3000, 5);
	struct ApDemoCamIdentity other = ident_of(0x9000, 0x2000, 0x3000, 5);
	struct ApDemoCamGate g;

	// ── engage writes exactly the declared mutation ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	expect("engage succeeds", AP_DemoCam_Engage(&st, &id, &f), AP_DEMOCAM_ENGAGE_OK);
	expect_u("engage raises only the request bit", f.flags,
	         before.flags | AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED);
	expect("engage clears the last authored entry", (int)f.currEOR, 0);
	f.flags = before.flags;
	f.currEOR = before.currEOR;
	expect("engage touches nothing else", memcmp(&f, &before, sizeof f) == 0, 1);
	expect("engage records the snapshot", st.engaged, 1);

	// ── disengage restores byte-exact after a full cinematic run ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	(void)AP_DemoCam_Engage(&st, &id, &f);
	simulate_cinematic_frames(&f);
	expect("release restores", AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_REQUEST),
	       AP_DEMOCAM_RELEASE_RESTORED);
	expect("release is byte-exact outside flags", fields_equal_except_flags(&f, &before), 1);
	expect_u("release clears the request and active bits",
	         f.flags & (AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED | AP_DEMOCAM_FLAG_ARCADE_EOR_ACTIVE), 0u);
	expect_u("release restores the owned flag bits", f.flags & AP_DEMOCAM_FLAG_OWNED_MASK,
	         before.flags & AP_DEMOCAM_FLAG_OWNED_MASK);
	expect_u("release raises the reseat pair", f.flags & AP_DEMOCAM_FLAG_RESEAT_MASK,
	         (unsigned)AP_DEMOCAM_FLAG_RESEAT_MASK);
	expect_u("release keeps unrelated flags",
	         f.flags & ~(unsigned)(AP_DEMOCAM_FLAG_OWNED_MASK | AP_DEMOCAM_FLAG_RESEAT_MASK),
	         before.flags & ~(unsigned)(AP_DEMOCAM_FLAG_OWNED_MASK | AP_DEMOCAM_FLAG_RESEAT_MASK));
	expect("release records the reason", st.lastClearReason, AP_DEMOCAM_CLEAR_REQUEST);

	// ── a flag another system raises mid-engagement is not rolled back ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	(void)AP_DemoCam_Engage(&st, &id, &f);
	simulate_cinematic_frames(&f);
	f.flags |= CAM_FLAG_FIRE_SPEED_ZOOM;
	(void)AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_REQUEST);
	expect_u("concurrent engine flag survives release", f.flags & CAM_FLAG_FIRE_SPEED_ZOOM,
	         CAM_FLAG_FIRE_SPEED_ZOOM);

	// ── double engage is idempotent and does not re-snapshot ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	(void)AP_DemoCam_Engage(&st, &id, &f);
	simulate_cinematic_frames(&f);
	held = f;
	expect("second engage reports already engaged", AP_DemoCam_Engage(&st, &id, &f),
	       AP_DEMOCAM_ENGAGE_ALREADY);
	expect("second engage counts once", (int)st.engageCount, 1);
	expect("hold keeps the authored entry latched", f.currEOR == held.currEOR, 1);
	held.flags |= AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED;
	expect("hold only re-arms the request bit", memcmp(&f, &held, sizeof f) == 0, 1);
	(void)AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_REQUEST);
	expect("release after double engage is byte-exact", fields_equal_except_flags(&f, &before), 1);

	// ── engaging on a different camera while engaged is refused, not silent ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	(void)AP_DemoCam_Engage(&st, &id, &f);
	held = f;
	expect("engage on another camera is refused", AP_DemoCam_Engage(&st, &other, &f),
	       AP_DEMOCAM_ENGAGE_REFUSED);
	expect("refused engage writes nothing", memcmp(&f, &held, sizeof f) == 0, 1);

	// ── release without engage is a safe no-op ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	expect("release without engage reports not engaged",
	       AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_REQUEST),
	       AP_DEMOCAM_RELEASE_NOT_ENGAGED);
	expect("release without engage writes nothing", memcmp(&f, &before, sizeof f) == 0, 1);
	expect("double release stays a no-op",
	       AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_MAP_RESET),
	       AP_DEMOCAM_RELEASE_NOT_ENGAGED);

	// ── a camera that changed identity is abandoned, never written ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	(void)AP_DemoCam_Engage(&st, &id, &f);
	simulate_cinematic_frames(&f);
	held = f;
	expect("release onto a re-initialized camera abandons",
	       AP_DemoCam_Release(&st, &other, &f, AP_DEMOCAM_CLEAR_MAP_RESET),
	       AP_DEMOCAM_RELEASE_ABANDONED);
	expect("abandoned release writes nothing", memcmp(&f, &held, sizeof f) == 0, 1);
	expect("abandoned release still ends disengaged", st.engaged, 0);

	// ── the force-clear rule, one path at a time ──
	run_forced_clear("map change", AP_DEMOCAM_CLEAR_MAP_CHANGE, gate_ok(), 1, 0);
	run_forced_clear("map reset", AP_DEMOCAM_CLEAR_MAP_RESET, gate_ok(), 0, 1);
	{
		struct ApDemoCamGate loading = gate_ok();
		loading.loading = 1;
		run_forced_clear("load in flight", AP_DEMOCAM_CLEAR_MAP_CHANGE, loading, 0, 0);
	}
	{
		struct ApDemoCamGate scripted = gate_ok();
		scripted.scriptedCamera = 1;
		run_forced_clear("scripted handover", AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA, scripted, 0, 0);
	}
	{
		struct ApDemoCamGate bot = gate_ok();
		bot.driverIsBot = 1;
		run_forced_clear("driver owned elsewhere", AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA, bot, 0, 0);
	}
	{
		struct ApDemoCamGate ended = gate_ok();
		ended.raceActive = 0;
		run_forced_clear("race window closed", AP_DEMOCAM_CLEAR_RACE_ENDED, ended, 0, 0);
	}
	{
		struct ApDemoCamGate gone = gate_ok();
		gone.cameraPresent = 0;
		run_forced_clear("camera gone", AP_DEMOCAM_CLEAR_MAP_RESET, gone, 0, 0);
	}
	// A connection reset is an external event, not a gate reading: nothing about
	// the camera changed, so the release still has to restore it exactly.
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	(void)AP_DemoCam_Engage(&st, &id, &f);
	simulate_cinematic_frames(&f);
	expect("connection reset restores",
	       AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_CONNECT_RESET),
	       AP_DEMOCAM_RELEASE_RESTORED);
	expect("connection reset is byte-exact outside flags",
	       fields_equal_except_flags(&f, &before), 1);
	expect("connection reset records its reason", st.lastClearReason,
	       AP_DEMOCAM_CLEAR_CONNECT_RESET);

	// ── the steady state holds ──
	g = gate_ok();
	expect("a running race with an authored table is clear",
	       AP_DemoCam_ForcedClearReason(&g, 0, 0), AP_DEMOCAM_CLEAR_NONE);

	// ── engage eligibility ──
	g = gate_ok();
	expect("full gate allows engage", AP_DemoCam_CanEngage(&g), 1);
	g = gate_ok();
	g.eorTableUsable = 0;
	expect("no authored camera table refuses engage", AP_DemoCam_CanEngage(&g), 0);
	g = gate_ok();
	g.raceActive = 0;
	expect("outside the race window refuses engage", AP_DemoCam_CanEngage(&g), 0);
	g = gate_ok();
	g.loading = 1;
	expect("a load in flight refuses engage", AP_DemoCam_CanEngage(&g), 0);
	g = gate_ok();
	g.scriptedCamera = 1;
	expect("a scripted camera refuses engage", AP_DemoCam_CanEngage(&g), 0);
	g = gate_ok();
	g.driverIsBot = 1;
	expect("a bot-owned driver refuses engage", AP_DemoCam_CanEngage(&g), 0);
	g = gate_ok();
	g.cameraPresent = 0;
	expect("no camera refuses engage", AP_DemoCam_CanEngage(&g), 0);

	// ── engage / clear / re-engage / clear leaves nothing behind ──
	AP_DemoCam_Reset(&st);
	seed_fields(&f);
	before = f;
	{
		int cycle;
		for (cycle = 0; cycle < 4; cycle++)
		{
			(void)AP_DemoCam_Engage(&st, &id, &f);
			simulate_cinematic_frames(&f);
			(void)AP_DemoCam_Engage(&st, &id, &f); // hold frame
			(void)AP_DemoCam_Release(&st, &id, &f, AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA);
			f.flags &= ~(unsigned)AP_DEMOCAM_FLAG_RESEAT_MASK; // the engine self-clears these
		}
	}
	expect("four cycles leave non-flag state byte-exact", fields_equal_except_flags(&f, &before), 1);
	expect_u("four cycles leave no camera flag behind", f.flags, before.flags);
	expect("four cycles counted four engagements", (int)st.engageCount, 4);

	// Review wave, 2026-08-19: the First Person trap owns cameraMode while
	// active; engaging under it must be refused and an existing engagement
	// must force-clear as a scripted handover.
	{
		struct ApDemoCamGate g = gate_ok();
		g.trapOwnsCamera = 1;
		expect("an active first-person trap refuses engage",
		       AP_DemoCam_CanEngage(&g), 0);
		expect("and force-clears an engagement as a scripted handover",
		       AP_DemoCam_ForcedClearReason(&g, 0, 0),
		       AP_DEMOCAM_CLEAR_SCRIPTED_CAMERA);
		g.trapOwnsCamera = 0;
		expect("the gate reopens when the trap releases the camera",
		       AP_DemoCam_CanEngage(&g), 1);
	}

	printf("%s\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
