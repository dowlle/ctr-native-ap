// ap_hooks_feed.c -- part of the ap_hooks unit (issue #305).
// On-screen messages: the AP item feed (hub, race, results), the schema warning
// and the pair-version update notice.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// AP ITEM FEED (display-only)
//
// A feed of the Archipelago items you receive, and of the ones you send to other
// players. It started as a HUB-ONLY feed; issue #192 makes it render IN RACES
// too, because the sanity checks a race is full of (crates, lap placements) were
// firing with nothing on screen to show for them -- the player only found out
// what they had sent by leaving the race. Each visible line lives
// AP_FEED_LIFETIME_FRAMES starting when it first becomes VISIBLE (time spent
// queued while no surface is drawing it does not count against it). New lines
// enter at the bottom and push older lines up; a deep queue drains oldest-first,
// one line per stagger interval.
//
// Two draw surfaces, never both in the same frame, both anchored at the SAME
// bottom-left point: the original race anchor placed the feed in a mid-screen
// band clear of the race HUD, but that put it mid-screen above the kart,
// directly in the sightline (finding from a live test, N. Gin Labs screenshot,
// 2026-08-12). Reusing the hub's own anchor is deliberate, not a placeholder
// -- the ruling was to reuse the hub anchor/behavior verbatim in race mode
// rather than invent a new position.
// RenderAllHUD picks exactly one of the hub and race branches per frame
// (MainFrame_RenderFrame.c: the ADVENTURE_ARENA if/else), the two hub sites are
// themselves mutually exclusive (LOAD_IsOpen_AdvHub), and the race sites are the
// racing/crystal-challenge pair, so the shared tick below still runs exactly
// once per frame and lifetimes stay honest.
//
// Replay suppression: on every (re)connect the server resends the full received
// list from index 0. The feed absorbs that initial inventory SILENTLY -- while
// unprimed every drained item is swallowed and its server index tracked as a
// high-water mark -- and only primes (goes live) after a short quiet window with
// no further items. Past priming, only items with a strictly higher index toast,
// so a reconnect's replay (same indices) never re-toasts. All display-only and
// gated on ctr_cfg_active(), so a clean / vanilla-config run is byte-identical.
// ---------------------------------------------------------------------------
#define AP_FEED_LIFETIME_FRAMES    240 // per-line visible dwell (~4s), tunable
#define AP_FEED_MAX_VISIBLE        5   // lines on screen at once
#define AP_FEED_QUEUE_MAX          24  // pending (not-yet-visible) ring capacity
#define AP_FEED_STAGGER_FRAMES     12  // min frames between promotions (cascade)
#define AP_FEED_PRIME_QUIET_FRAMES 30  // no-new-item frames before going live
#define AP_FEED_LINE_H             0xC // vertical spacing between lines
#define AP_FEED_X                  0x10
#define AP_FEED_BASE_Y             0xC8 // bottom (newest) line anchor
#define AP_FEED_RESULTS_BASE_Y     0xAC // same anchor on the results screens: clears the prompt
#define AP_FEED_MAX_CHARS          28   // DeathLink lines stay clear of the minimap (small font: ~13 px a character)

// ── RACE anchor (bottom-left, matches the hub) ──────────────────────────────
// Previously the race surface used its own mid-screen band (upper-left of the
// track view) to clear the 1P race HUD -- but that put the feed above the kart,
// in the sightline (finding from a live test, N. Gin Labs screenshot, 2026-08-12). Ruled
// fix: reuse the hub's own bottom-left anchor (AP_FEED_X / AP_FEED_BASE_Y)
// verbatim instead of inventing a new position, same as the hub surface below.
// 2P/3P/4P stay NOT drawn (see AP_FeedDrawRace): AP_FEED_X/AP_FEED_BASE_Y are
// still 1P-viewport coordinates, and the split-screen HUD tables move each
// element per viewport. Local multiplayer keeps queueing lines (nothing is
// lost) and shows them on the next 1P surface.
// Storage cap: big enough that the "%s FROM %s" format below (item<=31 +
// " FROM " + player<=23) can never be truncated, so no -Wformat-truncation.
#define AP_FEED_TEXT_CAP           96
#define AP_FEED_ITEM_CAP           32
#define AP_FEED_PLAYER_CAP         24
// Wording lives here (retunable), consistent with the ceremony block above.
#define AP_FEED_FMT_OWN      "%s"           // own slot / server item
#define AP_FEED_FMT_FOREIGN  "%s FROM %s"   // someone else sent it
#define AP_FEED_FMT_SENT     "%s TO %s"     // we checked a location that feeds someone else
#define AP_FEED_ITEM_UNKNOWN "AP ITEM"

typedef struct
{
	char text[AP_FEED_TEXT_CAP];
	int  color;      // DecalFontStyle index resolved from the item's AP class (#195)
	int  framesLeft; // visible lifetime remaining
	int  playCue;    // received-item cue, consumed only when promoted in a race
} AP_FeedLine;

static AP_FeedLine ap_feed_queue[AP_FEED_QUEUE_MAX]; // FIFO of pending lines
static int ap_feed_qhead = 0;
static int ap_feed_qcount = 0;
static AP_FeedLine ap_feed_vis[AP_FEED_MAX_VISIBLE]; // [0]=oldest(top)..newest(bottom)
static int ap_feed_vcount = 0;
static int ap_feed_stagger = 0;

static int       ap_feed_primed = 0;  // 0 while absorbing the initial inventory
static long long ap_feed_hwm = -1;    // highest already-known server index
static int       ap_feed_quiet = 0;   // consecutive no-new-item frames (priming)

// A self-bound podium rung is shown immediately with its rung reason. The
// server then echoes the same item through ReceivedItems; suppress that one
// generic echo so the player sees one descriptive line and hears one cue.
#define AP_FEED_SELF_RUNG_MAX 8
static long long ap_feed_self_rung_items[AP_FEED_SELF_RUNG_MAX];
static int ap_feed_self_rung_count = 0;

static int ap_hub_feed_on = 1; // ap-config.txt "hub_feed=" (default on; governs
                               // BOTH feed surfaces since #192, key name kept)
int AP_HubFeedOn(void)
{
	return ap_hub_feed_on;
}

// Append a ready-formatted line to the pending queue. Oldest is dropped if full.
static void AP_FeedEnqueue(const char *text, int color, int playCue)
{
	if (ap_feed_qcount >= AP_FEED_QUEUE_MAX)
	{
		ap_feed_qhead = (ap_feed_qhead + 1) % AP_FEED_QUEUE_MAX;
		ap_feed_qcount--;
	}
	int slot = (ap_feed_qhead + ap_feed_qcount) % AP_FEED_QUEUE_MAX;
	snprintf(ap_feed_queue[slot].text, AP_FEED_TEXT_CAP, "%s", text);
	ap_feed_queue[slot].color = color;
	ap_feed_queue[slot].framesLeft = 0;
	ap_feed_queue[slot].playCue = playCue;
	ap_feed_qcount++;
}

static void AP_FeedRememberSelfRung(long long item)
{
	if (ap_feed_self_rung_count >= AP_FEED_SELF_RUNG_MAX)
	{
		memmove(&ap_feed_self_rung_items[0], &ap_feed_self_rung_items[1],
		        sizeof(ap_feed_self_rung_items[0]) * (AP_FEED_SELF_RUNG_MAX - 1));
		ap_feed_self_rung_count--;
	}
	ap_feed_self_rung_items[ap_feed_self_rung_count++] = item;
}

static int AP_FeedConsumeSelfRung(long long item)
{
	for (int i = 0; i < ap_feed_self_rung_count; i++)
	{
		if (ap_feed_self_rung_items[i] != item)
			continue;
		memmove(&ap_feed_self_rung_items[i], &ap_feed_self_rung_items[i + 1],
		        sizeof(ap_feed_self_rung_items[0]) * (ap_feed_self_rung_count - i - 1));
		ap_feed_self_rung_count--;
		return 1;
	}
	return 0;
}

// Clear feed state on a fresh (re)connect: drop stale toasts and re-arm the
// initial-inventory absorb window. Called from the connect-reset block.
static unsigned char ap_feed_letters_ready[CTR_CFG_LETTER_TRACK_COUNT];
static void AP_FeedLetterReadyUpdates(void);
static void AP_FeedOxideFinalOpenUpdate(void);

void AP_FeedConnectReset(void)
{
	memset(ap_feed_letters_ready, 0, sizeof ap_feed_letters_ready);
	ap_feed_primed = 0;
	ap_feed_hwm = -1;
	ap_feed_quiet = 0;
	ap_feed_qhead = 0;
	ap_feed_qcount = 0;
	ap_feed_vcount = 0;
	ap_feed_stagger = 0;
	ap_feed_self_rung_count = 0;
}

// One drained received item. While unprimed (initial inventory) it is swallowed
// and its index tracked; once primed, only a strictly newer index enqueues a
// toast, so a reconnect's re-sent inventory (same indices) never re-appears.
// `flags` carries the NetworkItem.flags metadata through the drain batch so the
// line can be coloured by AP class (issue #195) instead of re-derived from names.
void AP_FeedOnItemReceived(long long item, int player, long long index, unsigned flags)
{
	if (!ctr_cfg_active())
		return;

	if (!ap_feed_primed)
	{
		if (index > ap_feed_hwm)
			ap_feed_hwm = index; // remember the initial inventory's high-water mark
		return;
	}
	if (index >= 0 && index <= ap_feed_hwm)
		return; // replayed inventory entry -> already known
	if (index > ap_feed_hwm)
		ap_feed_hwm = index;
	if (AP_FeedConsumeSelfRung(item))
		return; // descriptive podium-rung line already queued

	int self = ap_net_self_slot();
	// Display rule: own slot OR the server (slot <= 0) shows just the item; anyone
	// else gets "FROM <PLAYER>".
	int own = (player <= 0 || player == self);

	char itemRaw[64], playerRaw[64];
	char itemS[AP_FEED_ITEM_CAP], playerS[AP_FEED_PLAYER_CAP];
	char line[AP_FEED_TEXT_CAP];
	if (ap_net_item_text(item, player, itemRaw, (int)sizeof itemRaw, playerRaw, (int)sizeof playerRaw))
	{
		AP_CeremonySanitize(itemRaw, itemS, (int)sizeof itemS);
		AP_CeremonySanitize(playerRaw, playerS, (int)sizeof playerS);
	}
	else
	{
		itemS[0] = '\0';
		playerS[0] = '\0';
	}
	{
		char alias[AP_FEED_ITEM_CAP];
		if (AP_ItemDisplayAlias(item, alias, (int)sizeof alias))
			AP_CeremonySanitize(alias, itemS, (int)sizeof itemS);
	}
	if (AP_CeremonyNameIsBlank(itemS))
		snprintf(itemS, sizeof itemS, "%s", AP_FEED_ITEM_UNKNOWN);

	if (own || playerS[0] == '\0')
		snprintf(line, sizeof line, AP_FEED_FMT_OWN, itemS);
	else
		snprintf(line, sizeof line, AP_FEED_FMT_FOREIGN, itemS, playerS);

	// Colour by AP class, not by own/foreign: an own filler reads cyan exactly
	// like a foreign one. own only shaped the wording above.
	AP_FeedEnqueue(line, AP_ClassFontColor(flags), 1);
}

// Trap-state line from the trap scheduler (ap_traps.c): armed, incoming, active.
// Coloured as a trap so a warning reads the same as the receipt that produced it.
// Deliberately outside the initial-inventory absorb window that gates the received
// feed: an armed trap is live gameplay state, not a replayed receipt.
void AP_FeedTrapLine(const char *text)
{
	char line[AP_FEED_TEXT_CAP];
	if (!ctr_cfg_active() || text == 0)
		return;
	AP_CeremonySanitize(text, line, (int)sizeof line);
	if (line[0] == '\0')
		return;
	AP_FeedEnqueue(line, CORTEX_RED, 1);
}

// One received-DeathLink line: a death applied (red) or ignored because it did not
// arrive in a live race (orange). Like the trap line it is live gameplay state, not
// a replayed receipt, so it bypasses the initial-inventory absorb window.
// A one-off client notice in the same feed (orange, like an ignored DeathLink).
void AP_FeedNoticeLine(const char *text)
{
	char line[AP_FEED_TEXT_CAP];
	if (!ctr_cfg_active() || text == 0)
		return;
	AP_CeremonySanitize(text, line, (int)sizeof line);
	line[AP_FEED_MAX_CHARS] = '\0';
	if (line[0] == '\0')
		return;
	AP_FeedEnqueue(line, ORANGE, 1);
}

void AP_FeedDeathLinkLine(const char *text, int ignored)
{
	char line[AP_FEED_TEXT_CAP];
	if (!ctr_cfg_active() || text == 0)
		return;
	AP_CeremonySanitize(text, line, (int)sizeof line);
	line[AP_FEED_MAX_CHARS] = '\0';
	if (line[0] == '\0')
		return;
	AP_FeedEnqueue(line, ignored ? ORANGE : CORTEX_RED, 0);
}

// A location WE just checked that feeds SOMEONE ELSE. The received feed only ever
// shows items we RECEIVE, so a nonlocal send (gem cups, cup-leg podium rungs, any
// ceremony a player skips past) would otherwise surface nothing (issue #63).
// Resolve the scouted destination + item and toast "<ITEM> TO <PLAYER>". Own-slot
// sends are skipped: the server echoes them back through ReceivedItems, so
// AP_FeedOnItemReceived already toasts them -- toasting here too would double the
// line. Display-only, self-gated on ctr_cfg_active(); vanilla builds are
// byte-identical. The caller must only invoke this for a genuinely NEW check (it
// captures ap_net_location_checked BEFORE the send) so a re-win never re-toasts.
static void AP_FeedOnLocationSent(long code)
{
	if (!ctr_cfg_active())
		return;

	// Every one of the 99 CTR locations is scouted on connect, so a known scout is
	// expected; if it is somehow absent we cannot attribute the item -> stay silent.
	// The scout also carries the destination item's AP flags, so a sent line gets
	// the same classification colouring as a received one (#195).
	long long item = 0;
	int player = -1;
	unsigned flags = 0;
	int scoutKnown = ap_net_scout_known(code, &item, &player, &flags);
	if (!AP_LocationSentShouldToastPure(scoutKnown, player == ap_net_self_slot()))
		return; // unscouted: cannot attribute; own item: the ReceivedItems echo already toasts it

	char itemRaw[64], playerRaw[64];
	char itemS[AP_FEED_ITEM_CAP], playerS[AP_FEED_PLAYER_CAP];
	char line[AP_FEED_TEXT_CAP];
	if (ap_net_scout_text(code, itemRaw, (int)sizeof itemRaw, playerRaw, (int)sizeof playerRaw))
	{
		AP_CeremonySanitize(itemRaw, itemS, (int)sizeof itemS);
		AP_CeremonySanitize(playerRaw, playerS, (int)sizeof playerS);
	}
	else
	{
		itemS[0] = '\0';
		playerS[0] = '\0';
	}
	{
		char alias[AP_FEED_ITEM_CAP];
		if (AP_ItemDisplayAlias(item, alias, (int)sizeof alias))
			AP_CeremonySanitize(alias, itemS, (int)sizeof itemS);
	}
	if (AP_CeremonyNameIsBlank(itemS))
		snprintf(itemS, sizeof itemS, "%s", AP_FEED_ITEM_UNKNOWN);
	if (playerS[0] == '\0')
		snprintf(playerS, sizeof playerS, "%s", "PLAYER");

	// "%s TO %s": item (<=31) + " TO " + player (<=23) = 58 < AP_FEED_TEXT_CAP (64),
	// so it never truncates. Class-coloured like a received line; a sent item is
	// never our own, but that no longer drives the colour.
	snprintf(line, sizeof line, AP_FEED_FMT_SENT, itemS, playerS);
	AP_FeedEnqueue(line, AP_ClassFontColor(flags), 0);
}

// Called once after each frame's received-item drain (n = items drained this
// frame). Handles the go-live transition: once connected and the initial dump
// has gone quiet for AP_FEED_PRIME_QUIET_FRAMES, the feed primes and starts
// toasting. Any item that arrives while unprimed keeps resetting the window (and
// is absorbed by AP_FeedOnItemReceived), so a chunked initial sync stays silent.
void AP_FeedEndDrain(int drainedThisFrame)
{
	AP_FeedLetterReadyUpdates();
	AP_FeedOxideFinalOpenUpdate();
	if (ap_feed_primed)
		return;
	if (drainedThisFrame > 0 || !ap_net_is_connected())
	{
		ap_feed_quiet = 0;
		return;
	}
	if (++ap_feed_quiet >= AP_FEED_PRIME_QUIET_FRAMES)
		ap_feed_primed = 1;
}

// Tick + render the feed at one anchor: promote one pending line per stagger into
// a free visible slot (oldest first), age + expire visible lines from the top,
// and draw newest at baseY with older lines stacked upward.
//
// Shared by both surfaces so the queue, the stagger, the lifetimes and the
// eviction order are literally the same code in a race as on the hub; the only
// per-surface value is where the block is anchored. Callers guarantee it runs at
// most once per frame (see the block comment above).
static void AP_FeedTickAndDraw(int x, int baseY, int playableRace)
{
	// Promote a pending line into a free slot, rate-limited so a deep queue
	// cascades in rather than snapping full in one frame.
	if (ap_feed_vcount < AP_FEED_MAX_VISIBLE && ap_feed_qcount > 0)
	{
		if (ap_feed_stagger > 0)
		{
			ap_feed_stagger--;
		}
		else
		{
			AP_FeedLine *q = &ap_feed_queue[ap_feed_qhead];
			ap_feed_qhead = (ap_feed_qhead + 1) % AP_FEED_QUEUE_MAX;
			ap_feed_qcount--;
			AP_FeedLine *v = &ap_feed_vis[ap_feed_vcount];
			snprintf(v->text, AP_FEED_TEXT_CAP, "%s", q->text);
			v->color = q->color; // classification colour follows the line through the queue
			v->framesLeft = AP_FEED_LIFETIME_FRAMES; // lifetime starts now (visible)
			v->playCue = 0;
			if (playableRace && q->playCue)
				OtherFX_Play(0x41, 1); // retail 10-Wumpa / juiced-up pickup cue
			ap_feed_vcount++;
			ap_feed_stagger = AP_FEED_STAGGER_FRAMES;
		}
	}

	// Age, then compact out expired lines (oldest reach 0 first).
	int w = 0;
	for (int i = 0; i < ap_feed_vcount; i++)
	{
		if (ap_feed_vis[i].framesLeft > 0)
			ap_feed_vis[i].framesLeft--;
		if (ap_feed_vis[i].framesLeft > 0)
		{
			if (w != i)
				ap_feed_vis[w] = ap_feed_vis[i];
			w++;
		}
	}
	ap_feed_vcount = w;

	// Draw: newest (last) at the bottom anchor, older lines stacked upward.
	// Each line renders in its stored classification colour (issue #195); the
	// old own/foreign orange-vs-white mapping is gone.
	for (int i = 0; i < ap_feed_vcount; i++)
	{
		int fromBottom = ap_feed_vcount - 1 - i;
		int y = baseY - fromBottom * AP_FEED_LINE_H;
		DecalFont_DrawLine(ap_feed_vis[i].text, x, y, FONT_SMALL,
		                   ap_feed_vis[i].color);
	}
}

// Adventure-hub surface: the shipped anchor, bottom-left, unchanged.
void AP_FeedDrawHub(void)
{
	if (!ctr_cfg_active() || !ap_hub_feed_on)
		return;

	AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_BASE_Y, 0);
}

// In-race surface (issue #192): the same feed, on the race HUD, so an item sent
// or received mid-race is seen when it happens instead of on the next hub entry.
// Anchored at the SAME bottom-left point as the hub surface (see the RACE
// anchor comment above); this is not a coincidence, it is the fix.
//
// Two extra gates the hub surface does not need:
//   * 1P ONLY. AP_FEED_X/AP_FEED_BASE_Y are 1P-viewport coordinates. In
//     split-screen the line would land in an arbitrary place in an arbitrary
//     viewport, so the feed stays queued instead.
//   * NOT WHILE PAUSED. The pause menu draws over the HUD, and a feed printing
//     through it would be both ugly and unreadable. Skipping the whole tick (not
//     just the draw) also FREEZES the lifetimes: a line does not burn its four
//     seconds behind a pause menu, it resumes where it was. That mirrors how a
//     line queued in a menu keeps its full dwell for later.
void AP_FeedDrawRace(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!ctr_cfg_active() || !ap_hub_feed_on)
		return;
	if (gGT->numPlyrCurrGame != 1)
		return;
	if ((gGT->gameMode1 & PAUSE_ALL) != 0)
		return;

	AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_BASE_Y, 1);
}

// End-of-race screens: the race HUD pass returns before AP_FeedDrawRace, so the
// results and cup standings call this instead. Same gates, higher anchor.
void AP_FeedDrawResults(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!ctr_cfg_active() || !ap_hub_feed_on)
		return;
	if (gGT->numPlyrCurrGame != 1)
		return;
	if ((gGT->gameMode1 & PAUSE_ALL) != 0)
		return;

	AP_FeedTickAndDraw(AP_FEED_X, AP_FEED_RESULTS_BASE_Y, 1);
}

// Persistent "this seed is from a newer apworld -- update the client" banner
// (issue #8). Drawn every adventure-hub frame while ctr_cfg.schema_newer is set,
// in RED at the top-left so it cannot be mistaken for the transient item feed at
// the bottom. Unlike the feed it does NOT age out: as long as the mismatched
// seed is connected, the warning stays up. No-op on matching / older seeds, so a
// normal session is byte-identical.
void AP_DrawSchemaWarning(void)
{
	if (!ctr_cfg.schema_newer)
		return;
	// Static so the strings are not rebuilt per frame; DrawLine takes char*.
	static char warn1[] = "!! CTR-AP CLIENT OUT OF DATE !!";
	static char warn2[] = "This seed needs a newer client. Gates may be wrong.";
	DecalFont_DrawLine(warn1, AP_FEED_X, 0x14, FONT_SMALL, RED);
	DecalFont_DrawLine(warn2, AP_FEED_X, 0x14 + AP_FEED_LINE_H, FONT_SMALL, RED);
}

// ── Pair-version update notice (issue #150) ─────────────────────────────────
// The apworld stamps its own world_version into ctr_options.world_version, and
// under the release policy that string IS the pair version -- so a seed built by
// a newer apworld is proof that a newer client exists. That makes this the exact
// mirror of the schema_newer channel above, with no network call anywhere: the
// whole comparison is the seed's own string against the compiled CTR_AP_VERSION.
//
// It is INFORMATIONAL, and the difference from AP_DrawSchemaWarning matters.
// schema_newer means a compatibility break (gates may be enforced wrong), so it
// is RED, permanent, and worded as a fault. This one means "there is a newer
// release": the session is fine, nothing is mis-enforced, and the player may
// finish this seed and update whenever. So it is ORANGE (the title line's own
// colour), it never uses the words required / must / unsupported / out of date,
// and it does not touch the RED banner in any way.
//
// Silence is the default answer to every uncertainty: no slot_data, a seed from
// before the key existed (empty string), a version either side cannot parse, an
// equal or older seed, or update_check switched off -- all draw nothing at all.

// Wide enough for the longest line plus a full-length world_version (33 chars of
// fixed text + CTR_CFG_VERSION_CAP), so no line can ever be truncated.
#define AP_UPD_LINE_CAP 80

// Title-screen anchor: centred, in the clear band BELOW the CTR logo art and the
// main-menu block.
//
// 0xAA shipped first and was wrong (rc3 playtest, 2026-08-06): anchoring one line
// under the trademark line at y 0x9C drew the notice straight through the "CRASH
// TEAM RACING" banner, and because the line is ~350 px wide and centred on 0x100
// its tail also ran into the menu block's last row. Shifting it sideways cannot
// fix that -- it is too wide to clear a menu box whose left edge is x 0x126 -- so
// it goes below both instead.
//
// The arithmetic, in the 1P viewport (rect.h 0xd8, PushBuffer.c:9-13):
//   * the main menu is CENTRED on posY_curr 0x6c (D230.c) with rows of
//     font_charPixHeight[FONT_BIG] + 3 = 20 px (RECTMENU.c:364), so it grows
//     symmetrically and its BOTTOM moves with the row count. Seven rows put it
//     at ~0xba; the scrapbook unlock adds an eighth row
//     (s_rowsMainMenuWithSBConfig, MM_ConfigMenu.c:28) and pushed it to ~0xc3
//     (a measured +9 px/row) -- 0xc3 was the number to clear, NOT the 0xba a
//     basic-save screenshot shows.
//   * the logo art ends at about the same height.
// #211 appended a QUIT row to both variants (MM_ConfigMenu.c), so the tallest
// case is now nine rows (scrapbook + options + quit): extrapolating the same
// +9 px/row puts the bottom at ~0xcc. 0xD3 keeps the original ~7 px clearance
// over that new bottom (0xCA cleared 0xc3 the same way); the FONT_SMALL line is
// 8 px, so it now ends at ~0xdb. This is arithmetic extrapolated from the
// existing measured constant, NOT re-measured in game -- flagged on the rolling
// testing list for a render check now that a ninth row exists.
#define AP_UPD_TITLE_CENTRE_X 0x100
#define AP_UPD_TITLE_Y        0xD3

// The TITLE surface is one line and carries NO version numbers. The title screen
// only announces that an update exists; the numbers belong on the Connection
// screen, which is where a player goes when they want to know which version.
// Static (not const) because DecalFont_DrawLineOT takes a char*.
static char ap_upd_title_line[] = "A new version is available!";

static char ap_upd_line1[AP_UPD_LINE_CAP];
static char ap_upd_line2[AP_UPD_LINE_CAP];
// Detail line 3 substitutes nothing, so it stays a literal rather than a
// formatted buffer -- which also keeps it findable in the shipped binary.
static char ap_upd_line3[] = "Update the client and apworld.";
static char ap_upd_built_for[CTR_CFG_VERSION_CAP]; // seed version the detail lines describe
static int  ap_upd_armed = 0;
static int  ap_upd_title_show = -1; // -1 undecided, 0 already shown once, 1 show

// Refresh the notice state for the currently connected seed; 1 when it is armed.
// Safe to call every frame: the strings are rebuilt only when the seed version
// itself changes, which is once per connect.
static int AP_UpdateNoticeRefresh(void)
{
	const char *seed = ctr_cfg.world_version;

	if (!g_config.updateCheck || !ctr_cfg_active() || seed[0] == '\0' ||
	    !AP_VersionNewer(seed, CTR_AP_COMPAT_VERSION))
	{
		ap_upd_armed = 0;
		ap_upd_built_for[0] = '\0';
		ap_upd_title_show = -1;
		return 0;
	}

	if (!ap_upd_armed || strcmp(ap_upd_built_for, seed) != 0)
	{
		char msg[128];

		snprintf(ap_upd_built_for, sizeof ap_upd_built_for, "%s", seed);
		// The DETAIL lines, drawn on the Connection screen only. Both versions are
		// substituted WITHOUT their "v" (the strings print their own), so a seed
		// that spells its version "v0.1.5" cannot render as "vv0.1.5". The explicit
		// %.*s precision is the world_version buffer's own bound: it changes nothing
		// at runtime (a parsed version is a dozen bytes) but lets the compiler see
		// that these lines cannot be truncated.
		snprintf(ap_upd_line1, sizeof ap_upd_line1,
		         "THIS SEED WAS BUILT WITH CTR-AP v%.*s",
		         (int)(sizeof ctr_cfg.world_version - 1), AP_VersionDigits(seed));
		// Lines 2 and 3 are ONE sentence pair, split only because FONT_SMALL bills
		// 13 px per character (zGlobal_DATA.c font_charPixWidth) and the UI space is
		// 512 px wide: the pair on one line measures ~630 px and would run off both
		// edges centred. The words are untouched.
		snprintf(ap_upd_line2, sizeof ap_upd_line2,
		         "You are on v%s.", AP_VersionDigits(CTR_AP_COMPAT_VERSION));

		ap_upd_armed = 1;
		ap_upd_title_show = -1; // a version we have not shown yet gets its own showing

		snprintf(msg, sizeof msg, "[AP UPD] seed pair version %.*s > client %s\n",
		         (int)(sizeof ctr_cfg.world_version - 1), AP_VersionDigits(seed),
		         AP_VersionDigits(CTR_AP_COMPAT_VERSION));
		AP_LogLine(msg);
	}

	return 1;
}

// The three DETAIL lines (Connection screen). The title screen does not use this.
static void AP_UpdateNoticeDrawDetail(uint32_t *ot, int centreX, int y, int spacing)
{
	DecalFont_DrawLineOT(ap_upd_line1, centreX, y,
	                     FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
	DecalFont_DrawLineOT(ap_upd_line2, centreX, y + spacing,
	                     FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
	DecalFont_DrawLineOT(ap_upd_line3, centreX, y + 2 * spacing,
	                     FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
}

void AP_DrawTitleUpdateNotice(uint32_t *ot)
{
	if (!AP_UpdateNoticeRefresh())
		return;

	if (ap_upd_title_show < 0)
	{
		// First frame this version could appear: decide against the remembered one,
		// then commit that decision straight away. Writing last-seen up front rather
		// than when the notice leaves the screen is what keeps it up for the whole
		// visit -- every later frame reads the latch, not the config value, so the
		// notice cannot suppress itself one frame after it appeared.
		ap_upd_title_show = (strcmp(g_config.updateLastSeen, ap_upd_built_for) != 0);
		if (ap_upd_title_show)
		{
			snprintf(g_config.updateLastSeen, sizeof g_config.updateLastSeen,
			         "%s", ap_upd_built_for);
			NativeConfig_Save();
		}
	}

	if (!ap_upd_title_show)
		return;

	DecalFont_DrawLineOT(ap_upd_title_line, AP_UPD_TITLE_CENTRE_X, AP_UPD_TITLE_Y,
	                     FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
}

// "ARCHIPELAGO" under the red CRASH TEAM RACING banner on the title screen.
//
// Drawn under the same gates as the trademark line (MM_MenuProc_Main: main menu,
// title object alive, fly-in finished), so it appears with the settled logo and
// never over the intro, and only while no submenu is open, so it cannot sit
// under or over another menu box.
//
// Placement, in the 512 x 216 UI space of the 1P viewport (see the
// AP_UPD_TITLE_Y comment above for the menu arithmetic):
//   * centred on x 0x94, the middle of the banner's lettering. FONT_BIG bills
//     17 px per character, so the 11 letters span about x 0x3A..0xEE and end
//     well left of the main-menu box, whose left edge is x 0x126 at any row
//     count.
//   * top at y 0xB1, just under the banner's lower edge (measured in a 4:3
//     render: the ribbon's lettering band ends near y 0xAC). The line is 17 px
//     tall and ends near y 0xC2, above the room-link hint line (y 0xC4) and the
//     update notice (y 0xD3).
// Wider aspect ratios widen the 3D view's field of view, which pulls the logo
// toward the screen centre, while text keeps its UI x. So the line's centre is
// pulled toward x 0x100 by the same factor (Widescreen_CompressXAbout) to stay
// under the banner. At 21:9, the widest, it then ends near x 0x120, still left
// of the menu box. The y placement does not change with the aspect ratio.
#define AP_TITLE_AP_LINE_CENTRE_X 0x94
#define AP_TITLE_AP_LINE_Y        0xB1

static char ap_title_ap_line[] = "ARCHIPELAGO";

void AP_DrawTitleArchipelagoLine(uint32_t *ot)
{
	DecalFont_DrawLineOT(ap_title_ap_line, Widescreen_CompressXAbout(AP_TITLE_AP_LINE_CENTRE_X, 0x100), AP_TITLE_AP_LINE_Y,
	                     FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
}

void AP_DrawConnUpdateNotice(uint32_t *ot, int centreX, int y, int spacing)
{
	// No last-seen check here on purpose: this is the surface a player goes LOOKING
	// for, so it stays as long as the seed wants a newer client. It is also the one
	// that carries the version numbers -- the title screen only announces that an
	// update exists.
	if (!AP_UpdateNoticeRefresh())
		return;

	AP_UpdateNoticeDrawDetail(ot, centreX, y, spacing);
}

int AP_ConnUpdateNoticeActive(void)
{
	return AP_UpdateNoticeRefresh();
}

#endif // CTR_AP
