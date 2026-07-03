#ifdef CTR_AP

#include <common.h> // struct Driver / QuadBlock, sdata, DRIVER_COLL_FLAG_*, etc.
#include <stdio.h>

#include "ap_shortcut.h"
#include "ap_hooks.h" // AP_LogLine

// ============================================================================
// SHORTCUTLESS -- implementation. See ap_shortcut.h for the design + the honest
// scoping (retail track data has no shortcut annotation; this ships the reusable
// enforcement hook + a live capture harness to build the annotation from play).
// ============================================================================

int Platform_InputRawKeyDown(int scancode); // native_input.c (CTR_AP only)

#define AP_SC_KEY_TOGGLE 64 // F7: toggle Shortcutless
#define AP_SC_KEY_MARK   65 // F8: mark current quad as a shortcut + log

static int g_shortcutless = 0;   // enforcement toggle (the Shortcut Unlock item)
static int g_capture_log = 0;    // passive per-touch logging (config shortcut_capture=1)

// Runtime captured shortcut blockIDs -- built live via the mark key (F8) so a
// shortcut can be identified + tested against REAL track data without pre-baked
// annotation. A per-track baked table (see handoff) would supersede this.
#define AP_SC_CAPTURE_CAP 64
static s16 g_captured[AP_SC_CAPTURE_CAP];
static int g_captured_n = 0;

// Passive-log dedup: remember the last blockID we logged so a quad the player sits
// on for many frames only logs once.
static int g_last_logged_block = -1;

int AP_ShortcutlessActive(void) { return g_shortcutless; }

void AP_ShortcutlessSet(int on)
{
	if (g_shortcutless != (on ? 1 : 0))
	{
		g_shortcutless = on ? 1 : 0;
		AP_LogLine(g_shortcutless ? "[AP SHORTCUT] Shortcutless ON\n"
		                          : "[AP SHORTCUT] Shortcutless OFF\n");
	}
}

static struct Driver *AP_ScLocalDriver(void)
{
	if (sdata == 0 || sdata->gGT == 0)
		return 0;
	return sdata->gGT->drivers[0];
}

static int AP_ScBlockCaptured(s16 blockID)
{
	int i;
	for (i = 0; i < g_captured_n; i++)
		if (g_captured[i] == blockID)
			return 1;
	return 0;
}

int AP_QuadIsShortcut(struct QuadBlock *quad)
{
	if (quad == 0)
		return 0;
	if ((quad->quadFlags & AP_QUADBLOCK_FLAG_SHORTCUT) != 0)
		return 1; // baked annotation (future asset/track-data pass)
	return AP_ScBlockCaptured(quad->blockID); // live-captured set
}

int AP_ShortcutCheck(struct Driver *driver, struct QuadBlock *quad)
{
	if (driver == 0 || quad == 0)
		return 0;
	if (driver != AP_ScLocalDriver())
		return 0; // enforce on the local player only

	// Passive capture logging: record each distinct quad the player touches so a
	// shortcut's blockIDs can be read off the log while driving it.
	if (g_capture_log && quad->blockID != g_last_logged_block)
	{
		char msg[128];
		snprintf(msg, sizeof msg,
		         "[AP SHORTCUT] touch blockID=%d checkpoint=%d flags=0x%04x\n",
		         (int)quad->blockID, (int)quad->checkpointIndex,
		         (unsigned)quad->quadFlags);
		AP_LogLine(msg);
		g_last_logged_block = quad->blockID;
	}

	if (!g_shortcutless)
		return 0;
	return AP_QuadIsShortcut(quad) ? 1 : 0;
}

// Mark the local player's current quad as a shortcut (adds its blockID to the
// runtime captured set) -- the live POC/authoring workflow: drive onto a shortcut,
// press F8, then with Shortcutless ON re-driving it triggers the reset.
static void AP_ScMarkCurrentQuad(void)
{
	struct Driver *d = AP_ScLocalDriver();
	struct QuadBlock *q;
	char msg[128];

	if (d == 0)
		return;
	q = d->currBlockTouching;
	if (q == 0)
		q = d->lastValid;
	if (q == 0)
	{
		AP_LogLine("[AP SHORTCUT] mark: no current quad\n");
		return;
	}
	if (AP_ScBlockCaptured(q->blockID))
	{
		snprintf(msg, sizeof msg, "[AP SHORTCUT] blockID=%d already marked\n",
		         (int)q->blockID);
		AP_LogLine(msg);
		return;
	}
	if (g_captured_n >= AP_SC_CAPTURE_CAP)
	{
		AP_LogLine("[AP SHORTCUT] capture set full\n");
		return;
	}
	g_captured[g_captured_n++] = q->blockID;
	snprintf(msg, sizeof msg,
	         "[AP SHORTCUT] MARKED shortcut blockID=%d checkpoint=%d (set size %d)\n",
	         (int)q->blockID, (int)q->checkpointIndex, g_captured_n);
	AP_LogLine(msg);
}

void AP_ShortcutKeys(void)
{
	static int prevToggle = 0, prevMark = 0;
	int toggle = Platform_InputRawKeyDown(AP_SC_KEY_TOGGLE);
	int mark = Platform_InputRawKeyDown(AP_SC_KEY_MARK);

	if (toggle && !prevToggle)
		AP_ShortcutlessSet(!g_shortcutless);
	if (mark && !prevMark)
		AP_ScMarkCurrentQuad();

	prevToggle = toggle;
	prevMark = mark;
}

int AP_ShortcutConfigLine(const char *line)
{
	if (!strncmp(line, "shortcutless=", 13))
	{
		AP_ShortcutlessSet(line[13] == '1');
		return 1;
	}
	if (!strncmp(line, "shortcut_capture=", 17))
	{
		g_capture_log = (line[17] == '1');
		return 1;
	}
	return 0;
}

#endif // CTR_AP
