// ap_hooks_ceremony.c -- part of the ap_hooks unit (issue #305).
// Race-end presentation: ceremony award text and ledger, the Relic Race target
// ladder, trophy and custom-track podium ceremonies, and the AP credits section.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// RACE-END CEREMONY AWARD TEXT (option 2: one shared draw block, cycling)
//
// The finish-line ceremonies vanilla-draw a fixed "RELIC AWARDED" / "CTR TOKEN
// AWARDED" string. Under AP the useful thing to show is WHAT the finished race
// actually sent -- own item ("<ITEM> AWARDED") or a foreign one ("<ITEM> FOR
// <PLAYER>"). Two data sources feed the block:
//   * the scout cache (synchronous, item + receiving player for all 99 CTR
//     locations) -- resolved through ap_net_scout_text/_known;
//   * a per-race "ceremony ledger" of the location checks this race sent, so a
//     multi-check race (relic beating 2-3 tiers, a trophy win with podium rungs)
//     can cycle one entry at a time. Relic tiers are recorded at the finish
//     (RR_EndEvent_UnlockAward -> AP_NotifyAdvReward) and podium rungs at the
//     finish (the race listener), so both are present while the ceremony draws.
// Crystal/token/trophy checks are only SENT on the continue-press, i.e. after the
// award text is already on screen, so those callers pass the celebrated reward
// bit directly and the block resolves it from the scout cache without the ledger.
//
// Everything here is display-only and gated on ctr_cfg_active(); when AP is
// inactive AP_CeremonyDraw() returns 0 and the caller draws its vanilla string,
// so a clean/vanilla-config run is byte-identical.
// ---------------------------------------------------------------------------
#define AP_CEREMONY_MAX 8            // max distinct checks shown for one race
#define AP_CEREMONY_CYCLE_FRAMES AP_CEREMONY_DEFAULT_DWELL

typedef struct
{
	long code; // AP location code (scout-cache key)
	int  bit;  // AdvProgress reward bit, or -1 for a podium / non-bit entry
	int  rung; // podium rung tag AP_RUNG_* (0 held_1st..4 finish_any); -1 otherwise
} AP_CeremonyEntry;

static AP_CeremonyEntry ap_ceremony_ledger[AP_CEREMONY_MAX];
static int ap_ceremony_count = 0;
#ifdef CTR_CUSTOM_TRACKS
static ap_custom_trophy_ceremony_state ap_custom_trophy_ceremony;
static int ap_custom_ceremony_bit = AP_CUSTOM_TROPHY_PSEUDO_BIT;
#endif

// Cleared when a new race starts (the finish-flag falling edge in the race
// listener), so each ceremony shows only its own race's just-earned checks.
static void AP_CeremonyLedgerReset(void)
{
	ap_ceremony_count = 0;
}

// Record a just-sent location check for the current race. code < 0 (a reward with
// no AP location) and duplicates are ignored; the buffer caps at AP_CEREMONY_MAX.
static void AP_CeremonyLedgerAdd(long code, int bit, int rung)
{
	if (code < 0)
		return;
	for (int i = 0; i < ap_ceremony_count; i++)
		if (ap_ceremony_ledger[i].code == code)
			return;
	if (ap_ceremony_count >= AP_CEREMONY_MAX)
		return;
	ap_ceremony_ledger[ap_ceremony_count].code = code;
	ap_ceremony_ledger[ap_ceremony_count].bit  = bit;
	ap_ceremony_ledger[ap_ceremony_count].rung = rung;
	ap_ceremony_count++;
}

// Highest relic tier this race sent (0 = Sapphire, 1 = Gold, 2 = Platinum), or -1
// if none. The true "what did this run just win" signal for the relic ceremony's
// tier label + relic colour, which vanilla reads from advProgress.rewards -- bits
// AP_ApplyItems rewrites on every reconcile tick to mirror RECEIVED items, so after
// a local platinum run they read back as the AP inventory, not the run (the SAPPHIRE-label
// bug). The ledger is populated only after a relic finish, so this returns -1
// during the pre-race clock draw and the vanilla path stands.
int AP_CeremonyRelicTier(void)
{
	int best = -1;
	for (int i = 0; i < ap_ceremony_count; i++)
	{
		int bit = ap_ceremony_ledger[i].bit;
		int tier = AP_CortexPseudoRelicTier(bit); // schema 15, -1 otherwise
		if (tier >= 0)
			;
		else if (bit >= ADV_REWARD_FIRST_PLATINUM_RELIC && bit < ADV_REWARD_FIRST_CTR_TOKEN)
			tier = 2;
		else if (bit >= ADV_REWARD_FIRST_GOLD_RELIC && bit < ADV_REWARD_FIRST_PLATINUM_RELIC)
			tier = 1;
		else if (bit >= ADV_REWARD_FIRST_SAPPHIRE_RELIC && bit < ADV_REWARD_FIRST_GOLD_RELIC)
			tier = 0;
		if (tier > best)
			best = tier;
	}
	return best;
}

// ── Relic-race live target ladder (issue #21) ──
// The vanilla race-start tier selector reads advProgress.rewards, which
// AP_ApplyItems rewrites on every reconcile tick to mirror RECEIVED items -- so
// received Gold/Platinum Relic items poison the in-race top-left target (a first
// attempt shows the PLATINUM time, live v0.1.0 report). AP-active seeds
// replace it with a ladder: start at the highest tier still EARNABLE on this
// track (its location exists in the seed and is unchecked), and the moment
// the race clock passes the shown time, step down to the next earnable tier;
// hold at the lowest. Display-only: RR_EndEvent_UnlockAward compares raw time
// against every tier independently, so a late all-crates 10s bonus can still
// award the higher tier after the display stepped down (a happy surprise, by
// design -- see the issue-#21 fix plan).
static int ap_relic_target_tier = -1; // shown tier this relic race; -1 = vanilla selector
static int ap_relic_target_done = 0;  // 1 = ladder exhausted, hold the shown tier

// Current ladder tier for the HUD label/colour pin (UI_Clock.c), -1 = vanilla.
int AP_RelicTargetTier(void)
{
	return ap_relic_target_tier;
}

// Tier still earnable on this track: not yet OWNED, whether that ownership is
// a real AP check (server truth) or a native local grant on a removed slot
// (issue #171/#28 R1 -- AP_RelicRewardOwnedByBit; the location existing at all
// is folded into that same merged answer, since a removed slot's raw bit still
// starts clear and correctly reads "not yet earned"). Unowned tiers are always
// a contiguous run from the top (beating a higher time also sends/grants every
// lower tier), so the ladder never has gaps -- true regardless of which tiers
// on this track are real checks and which are local-grant.
static int AP_RelicTierEarnable(int levelID, int tier)
{
	int bit = ADV_REWARD_FIRST_SAPPHIRE_RELIC + ADV_REWARD_RELIC_TIER_STRIDE * tier + levelID;
	// Schema 15: a Cortex Vortex relic race asks its own tier codes, never
	// Oxide Station's relic bits. An absent tier is not earnable.
	if (AP_CortexTrackActive())
	{
		long code = AP_CortexTrackCode(AP_CV_SLOT_RELIC0 + tier);
		return code > 0 && ap_net_location_exists(code) && !ap_net_location_checked(code);
	}
	return !AP_RelicRewardOwnedByBit(bit);
}

// Break a tier's time into the sdata HUD digit fields (same formulas as the
// vanilla fill in UI_Instance.c, which the AP path replaces).
static void AP_RelicTargetFill(int levelID, int tier)
{
	int relicTime = AP_RelicTimeFor(levelID, tier);
	sdata->relicTime_1min = relicTime / 0xe100;
	sdata->relicTime_10sec = (relicTime / 0x2580) % 6;
	sdata->relicTime_1sec = (relicTime / 0x3c0) % 10;
	sdata->relicTime_10ms = ((relicTime * 100) / 0x3c0) % 10;
	sdata->relicTime_1ms = ((relicTime * 1000) / 0x3c0) % 10;
}

// Race-start hook (UI_Instance.c relic block). Returns 1 when the ladder took
// over (digits filled); 0 keeps the vanilla selector (no slot_data).
int AP_RelicTargetInit(int levelID)
{
	ap_relic_target_tier = -1;
	ap_relic_target_done = 0;
	if (!ctr_cfg_active())
		return 0;
	for (int t = 2; t >= 0; t--)
	{
		if (AP_RelicTierEarnable(levelID, t))
		{
			ap_relic_target_tier = t;
			break;
		}
	}
	if (ap_relic_target_tier < 0)
	{
		// Nothing earnable (replay of a fully-checked track): show Platinum,
		// matching a completed vanilla track, and never step down.
		ap_relic_target_tier = 2;
		ap_relic_target_done = 1;
	}
	AP_RelicTargetFill(levelID, ap_relic_target_tier);
	return 1;
}

// Per-frame downgrade (AP_OnFrame). Mid-race only: after the finish the
// end-of-race digit rewrite (RR_EndEvent_UnlockAward) and the ceremony label
// override (AP_CeremonyRelicTier) own the display.
static void AP_RelicTargetTick(struct GameTracker *gGT)
{
	struct Driver *d;
	int levelID;

	if (ap_relic_target_tier < 0 || ap_relic_target_done)
		return;
	if ((gGT->gameMode1 & RELIC_RACE) == 0)
		return;
	if ((gGT->gameMode1 &
	     (START_OF_RACE | END_OF_RACE | MAIN_MENU | GAME_CUTSCENE | PAUSE_ALL)) != 0 ||
	    gGT->trafficLightsTimer > 0)
		return;
	d = gGT->drivers[0];
	if (d == 0)
		return;
	levelID = (int)gGT->levelID;
	if (d->timeElapsedInRace <= AP_RelicTimeFor(levelID, ap_relic_target_tier))
		return; // shown target still beatable

	// Shown time passed: step down to the next earnable tier, else hold here.
	for (int t = ap_relic_target_tier - 1; t >= 0; t--)
	{
		if (AP_RelicTierEarnable(levelID, t))
		{
			ap_relic_target_tier = t;
			AP_RelicTargetFill(levelID, t);
			return;
		}
	}
	ap_relic_target_done = 1;
}

// Short context line describing WHICH check an entry is (the item line below says
// what was sent). Only the multi-entry contexts get one -- relic tier + podium
// rung -- so a lone trophy/token/crystal reads as just "<ITEM> AWARDED".
static const char *AP_CeremonyPrefix(int bit, int rung)
{
#ifdef CTR_CUSTOM_TRACKS
	if (bit == AP_CUSTOM_TROPHY_PSEUDO_BIT)
		return "CUSTOM TROPHY";
	if (bit == AP_CUSTOM_CTR_PSEUDO_BIT)
		return "CUSTOM CTR";
#endif
	{
		int cvSlot;
		if (AP_CortexPseudoDecode(bit, &cvSlot))
		{
			static const char *const cvRelic[3] = {"SAPPHIRE RELIC", "GOLD RELIC", "PLATINUM RELIC"};
			if (cvSlot == AP_CV_SLOT_TROPHY)
				return "TROPHY WON";
			if (cvSlot >= AP_CV_SLOT_RELIC0 && cvSlot < AP_CV_SLOT_RELIC0 + 3)
				return cvRelic[cvSlot - AP_CV_SLOT_RELIC0];
		}
	}
	if (rung == 0)
		return "HELD 1ST";
	if (rung == 1)
		return "HELD 3RD";
	if (rung == 2)
		return "HELD 5TH";
	if (rung == 3)
		return "PODIUM";
	if (rung == 4)
		return "FINISH";
	if (bit >= ADV_REWARD_FIRST_PLATINUM_RELIC && bit < ADV_REWARD_FIRST_CTR_TOKEN)
		return "PLATINUM RELIC";
	if (bit >= ADV_REWARD_FIRST_GOLD_RELIC && bit < ADV_REWARD_FIRST_PLATINUM_RELIC)
		return "GOLD RELIC";
	if (bit >= ADV_REWARD_FIRST_SAPPHIRE_RELIC && bit < ADV_REWARD_FIRST_GOLD_RELIC)
		return "SAPPHIRE RELIC";
	// Trophy race: prefix the entry for the location that took the trophy's place,
	// so the win is still acknowledged above whatever item (often a foreign
	// player's) the location actually sent -- same pattern as the relic/podium
	// prefixes. Short enough that it never eats into the item line's char budget.
	if (bit >= ADV_REWARD_FIRST_TROPHY && bit < ADV_REWARD_FIRST_SAPPHIRE_RELIC)
		return "TROPHY WON";
	return NULL;
}

// Copy in -> out, uppercased and restricted to the NTSC-U ceremony glyph set.
// Lowercase maps to the same glyph as uppercase, so uppercasing gives a stable
// look; anything with no glyph -- parentheses and the like -- or the reserved
// button-icon glyphs @ [ ^ * would render as silent gaps or button icons, so they
// become spaces. Truncates to cap-1 characters + terminator.
static void AP_CeremonySanitize(const char *in, char *out, int cap)
{
	int j = 0;
	if (!out || cap <= 0)
		return;
	for (int i = 0; in && in[i] && j < cap - 1; i++)
	{
		unsigned char c = (unsigned char)in[i];
		if (c >= 'a' && c <= 'z')
			c = (unsigned char)(c - ('a' - 'A'));
		int ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		         c == ' ' || c == '!' || c == '%' || c == '\'' || c == ',' ||
		         c == '-' || c == '.' || c == '/' || c == ':' || c == '<' ||
		         c == '=' || c == '>' || c == '?' || c == '_' || c == '+';
		out[j++] = ok ? (char)c : ' ';
	}
	out[j] = '\0';
}

// One centralized place for the ceremony wording (retune later).
#define AP_CEREMONY_FMT_OWN      "%s AWARDED"
#define AP_CEREMONY_FMT_FOREIGN  "%s FOR %s"
#define AP_CEREMONY_ITEM_UNKNOWN "AP ITEM"
#define AP_CEREMONY_ROW_Y        0x10 // prefix -> item line spacing

static int AP_CeremonyNameIsBlank(const char *s)
{
	// Empty, or the apclientpp "Unknown" placeholder (uppercased by the sanitizer).
	if (!s || s[0] == '\0')
		return 1;
	return (s[0] == 'U' && s[1] == 'N' && s[2] == 'K' && s[3] == 'N' &&
	        s[4] == 'O' && s[5] == 'W' && s[6] == 'N' && s[7] == '\0');
}

// Render one ledger/primary entry as a (prefix + item) block anchored at x,y.
static void AP_CeremonyDrawEntry(int x, int y, long code, int bit, int rung)
{
	char itemRaw[64], playerRaw[64];
	char item[40], player[24];
	char line[80];
	int player_slot = -1;
	long long itemId = 0;
	int sp;
	int flashOn = (sdata->gGT->timer & 1);

	if (ap_net_scout_text(code, itemRaw, (int)sizeof itemRaw, playerRaw, (int)sizeof playerRaw))
	{
		AP_CeremonySanitize(itemRaw, item, (int)sizeof item);
		AP_CeremonySanitize(playerRaw, player, (int)sizeof player);
	}
	else
	{
		item[0] = '\0';
		player[0] = '\0';
	}
	{
		char alias[40];
		if (ap_net_scout_known(code, &itemId, NULL, NULL) &&
		    AP_ItemDisplayAlias(itemId, alias, (int)sizeof alias))
			AP_CeremonySanitize(alias, item, (int)sizeof item);
	}
	if (AP_CeremonyNameIsBlank(item))
		snprintf(item, sizeof item, "%s", AP_CEREMONY_ITEM_UNKNOWN);
	if (player[0] == '\0')
		snprintf(player, sizeof player, "%s", "PLAYER");

	if (ap_net_scout_known(code, NULL, &sp, NULL))
		player_slot = sp;

	int own = (player_slot >= 0 && player_slot == ap_net_self_slot());

	// Own = orange (gentle flash like the vanilla banners); foreign = steady white
	// to echo the white foreign-item marker used on the pads.
	int itemColor = JUSTIFY_CENTER | (own ? (flashOn ? ORANGE : WHITE) : WHITE);

	const char *prefix = AP_CeremonyPrefix(bit, rung);
	int itemY = y;
	if (prefix != NULL)
	{
		DecalFont_DrawLine((char *)prefix, x, y, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
		itemY = y + AP_CEREMONY_ROW_Y;
	}

	if (own)
		snprintf(line, sizeof line, AP_CEREMONY_FMT_OWN, item);
	else
		snprintf(line, sizeof line, AP_CEREMONY_FMT_FOREIGN, item, player);

	// FONT_SMALL + word-wrap so a long foreign name folds instead of overrunning.
	DecalFont_DrawMultiLine(line, x, itemY, 0x1b0, FONT_SMALL, itemColor);
}

// Shared entry point called from every end-of-race draw function. Draws the AP
// award block (cycling if the race sent several checks) and returns 1 so the
// caller suppresses its vanilla award line. Returns 0 -- caller draws vanilla --
// when AP is inactive or nothing here is scouted (the vanilla fallback).
// primaryBit is the reward bit the ceremony celebrates, or -1 for none.
// includeLedger folds in the whole race's just-sent checks (relic tiers, podium
// rungs) so a multi-check screen cycles them; single-reward screens pass 0 so a
// shared multi-reward race (e.g. token+trophy) doesn't echo the same rungs twice.
static int AP_CeremonyDrawInternal(int x, int y, int primaryBit,
	int includeLedger, int elapsedFrames, int visibleFrames)
{
	if (!ctr_cfg_active())
		return 0;

	long codes[AP_CEREMONY_MAX + 1];
	int  bits[AP_CEREMONY_MAX + 1];
	int  rungs[AP_CEREMONY_MAX + 1];
	int  n = 0;

	long pcode = (primaryBit >= 0) ? AP_LookupLocationCode(primaryBit) : -1;
	if (pcode >= 0)
	{
		codes[n] = pcode;
		bits[n] = primaryBit;
		rungs[n] = -1;
		n++;
	}
	for (int i = 0; includeLedger && i < ap_ceremony_count && n <= AP_CEREMONY_MAX; i++)
	{
		long c = ap_ceremony_ledger[i].code;
		int dup = 0;
		for (int k = 0; k < n; k++)
			if (codes[k] == c)
			{
				dup = 1;
				break;
			}
		if (dup)
			continue;
		codes[n] = c;
		bits[n] = ap_ceremony_ledger[i].bit;
		rungs[n] = ap_ceremony_ledger[i].rung;
		n++;
	}

	if (n == 0)
		return 0; // not connected / nothing scouted -> caller draws vanilla

	int idx;
	if (elapsedFrames >= 0 && visibleFrames > 0)
		idx = AP_CeremonyCycleIndex(n, elapsedFrames, visibleFrames);
	else
		idx = (n > 1)
		          ? (int)((sdata->gGT->timer / AP_CEREMONY_CYCLE_FRAMES) % (unsigned)n)
		          : 0;
	AP_CeremonyDrawEntry(x, y, codes[idx], bits[idx], rungs[idx]);
	return 1;
}

int AP_CeremonyDraw(int x, int y, int primaryBit, int includeLedger)
{
	return AP_CeremonyDrawInternal(x, y, primaryBit, includeLedger, -1, 0);
}

int AP_CeremonyDrawTimed(int x, int y, int primaryBit, int includeLedger,
	int elapsedFrames, int visibleFrames)
{
	return AP_CeremonyDrawInternal(x, y, primaryBit, includeLedger,
	                              elapsedFrames, visibleFrames);
}

int AP_CustomTrackTrophyCeremonyDraw(int x, int y)
{
#ifdef CTR_CUSTOM_TRACKS
	if (!AP_CustomTrophyCeremonyActive(&ap_custom_trophy_ceremony))
		return 0;
	return AP_CeremonyDraw(x, y, ap_custom_ceremony_bit, 1);
#else
	(void)x;
	(void)y;
	return 0;
#endif
}

int AP_CustomTrackTrophyCeremonyProp(struct Instance *prop)
{
#ifdef CTR_CUSTOM_TRACKS
	if (!AP_CustomTrophyCeremonyActive(&ap_custom_trophy_ceremony))
		return 0;
	return AP_CeremonyRewardProp(prop, ap_custom_ceremony_bit);
#else
	(void)prop;
	return 0;
#endif
}

void AP_CustomTrackTrophyCeremonyEnd(void)
{
#ifdef CTR_CUSTOM_TRACKS
	AP_CustomTrophyCeremonyEnd(&ap_custom_trophy_ceremony);
	ap_custom_ceremony_bit = AP_CUSTOM_TROPHY_PSEUDO_BIT;
#endif
}

// Issue #235. A trial-track or Cortex Vortex podium reuses STATIC_TROPHY but has
// no ordinary retail trophy bit, so it must not be treated as the retail-Trophy
// presentation: prevLEV + ADV_REWARD_FIRST_TROPHY would resolve another track's
// location (Cortex Vortex hosts Oxide Station's LevelID 13). Detect it from the
// level we came from and the Cortex latch's previous-serving state.
int AP_PodiumSpecialTrack(void)
{
	int lev;

	if (sdata == NULL || sdata->gGT == NULL)
		return 1; // no track context -> not an ordinary retail Trophy podium

	lev = (int)sdata->gGT->prevLEV;
	if (lev < 0 || lev > 15)
		return 1; // outside the 16 retail track trophies (trial tracks are 16/17)

#ifdef CTR_CUSTOM_TRACKS
	if (lev == CTR_CFG_CORTEX_HOST_LEVEL && CustomTrack_CortexTrackPrevServed())
		return 1;
#endif
	return 0;
}

// Issue #235. game/233/CS_Podium.c bypasses the retail Trophy prize (and its
// INC_TROPHY count-up) for an AP Trophy presentation, so this draws what the
// ceremony should say instead: the AP-owned received Trophy count, then the
// #330 item sentence naming what the check actually sent. Self-gating matches
// the CS_Podium branch exactly (AP active + ordinary retail Trophy), so every
// non-AP, custom-Trophy, trial/Cortex and other-reward podium draws nothing
// here. Display-only: the check itself was already sent by the award path. The
// retail "win a trophy" string is the fallback when the scout is missing,
// exactly as #330 requires.
void AP_TrophyPodiumCeremonyDraw(int x, int y)
{
	char count[24];
	char *retail;
	char *text;

	if (sdata->gGT == NULL ||
	    !AP_PodiumIsApTrophyPresentation(ctr_cfg_active(),
	                                     (int)sdata->gGT->podiumRewardID,
	                                     AP_PodiumSpecialTrack()))
		return;

	// The vanilla counter reads the cosmetic AdvProgress mirror that the
	// count-up would have bumped; draw the received count from AP truth.
	snprintf(count, sizeof count, "TROPHIES: %d", AP_GateCount(AP_IDX_TROPHY));
	DecalFont_DrawLine(count, x, y, FONT_SMALL, JUSTIFY_CENTER | ORANGE);

	// The trophy location is the track we just came from (prevLEV, set by the
	// hub load), matching the relic podium's prevLEV + ADV_REWARD_FIRST_* rule.
	retail = sdata->lngStrings[LNG_CONGRATULATIONS_YOU_WIN_A_TROPHY];
	text = AP_RewardSubtitleForBit((int)sdata->gGT->prevLEV + ADV_REWARD_FIRST_TROPHY,
	                               retail);
	DecalFont_DrawMultiLine(text, x, y + 0x10, 0x1b0, FONT_SMALL,
	                        JUSTIFY_CENTER | WHITE);
}

// Issue #235. One-shot latch for the terminal work the missing retail prize
// thread would have performed at ceremony end. Reset when a podium scene is
// born (CS_Podium_FullScene_Init) and consumed by the exit below.
static ap_podium_exit_state ap_podium_exit;

void AP_PodiumExitReset(void)
{
	AP_PodiumExitStateReset(&ap_podium_exit);
}

// Issue #235. CS_Podium_Prize_ThTick3 is the code that raises overlayTransition
// to 2, clears VEH_FREEZE_PODIUM and plays the completion FX (0x67) when the
// ceremony ends. The AP ordinary-Trophy presentation skips that prize thread,
// so nothing performs the terminal work and the player can be left frozen after
// pressing continue. CS_Camera_ThTick_Podium calls this right after
// CS_DestroyPodium_StartDriving() to reproduce exactly that work at the
// equivalent moment. Self-gating on the same production policy means every
// podium whose prize thread still exists is untouched, and the latch means the
// work cannot run twice.
void AP_PodiumExitTerminalWork(void)
{
	ap_podium_exit_work work;

	if (sdata == NULL || sdata->gGT == NULL)
		return;

	work = AP_PodiumExitWork(ctr_cfg_active(), (int)sdata->gGT->podiumRewardID,
	                         AP_PodiumSpecialTrack(), &ap_podium_exit);

	if (work.overlayTransition != 0)
		sdata->gGT->overlayTransition = (u8)work.overlayTransition;
	if (work.releaseFreeze)
		sdata->gGT->gameMode2 &= ~VEH_FREEZE_PODIUM;
	if (work.completionSound)
		OtherFX_Play(0x67, 1);
}

int AP_CeremonyOffscreenX(int logicalWidth, int wrapWidth)
{
	return AP_CeremonyOffscreenCenterX(logicalWidth, wrapWidth);
}

// ---------------------------------------------------------------------------
// POST-GOAL CREDITS ROLL: AP SECTION (issue #117, display-only)
//
// CS_Credits_Init relocates the vanilla credits scroll (a "\r"-separated text
// blob from the credits LEV asset) to plain C pointers, then points
// credits_topString at it. This hook builds the hand-authored AP section
// (ap_credits_data.h) in front of a copy of that scroll inside the same
// MEMPACK high-mem arena the vanilla block lives in, so lifetime (cleared by
// CS_Credits_DestroyCreditGhost at credits end) and native-checkpoint pointer
// relocation behave identically to the vanilla block.
//
// Each rendered line is: leading "~NN" colour code + centering spaces + text +
// "\r". CS_Credits_DrawNames strips leading colour codes itself and resets its
// colour state at the top visible line every frame, so every non-blank line
// carries its own code. The scroller walks "\r" until the final NUL, so the
// prepended section simply extends the roll; scroll speed, fade and the
// epilogue path are untouched.
// ---------------------------------------------------------------------------
#include "ap_credits_data.h"

char *AP_Credits_PrependScroll(char *origScroll)
{
	if (!ctr_cfg_active() || origScroll == NULL)
	{
		return origScroll;
	}

	// Upper bound per line: 3-char colour code + padded field + "\r".
	u32 apMax = (u32)AP_CREDITS_NUM_LINES * (3 + AP_CREDITS_FIELD_WIDTH + 1);
	u32 origLen = strlen(origScroll);

	char *merged = MEMPACK_AllocHighMem((int)(apMax + origLen + 1));
	char *dst = merged;

	for (int i = 0; i < AP_CREDITS_NUM_LINES; i++)
	{
		const char *text = AP_CREDITS_LINES[i].text;
		int textLen = (int)strlen(text);

		if (textLen > 0)
		{
			// Colour slot 00 = ORANGE (headers), 04 = WHITE (entries).
			const char *code = AP_CREDITS_LINES[i].header ? "~00" : "~04";
			memcpy(dst, code, 3);
			dst += 3;

			// Centre in the field (spaces advance a full 14px glyph slot).
			int pad = (AP_CREDITS_FIELD_WIDTH - textLen) / 2;
			for (; pad > 0; pad--)
			{
				*dst++ = ' ';
			}

			memcpy(dst, text, textLen);
			dst += textLen;
		}

		*dst++ = '\r';
	}

	memcpy(dst, origScroll, origLen + 1);

	AP_LogLine("[AP CREDITS] prepended AP section to the credits scroll\n");

	return merged;
}

#endif // CTR_AP
