// ap_hooks_apply_items.c -- part of the ap_hooks unit (issue #305).
// AdvProgress reconcile from received items (AP_ApplyItems), the greeting-hint
// pre-latch, ap-config.txt reading and the item-drain log name tables.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// Reconcile AdvProgress category bits to EXACTLY the received-item counts: for
// the fungible categories set the top `count` bits of the pool (high-end) and
// CLEAR the rest; for the per-colour categories (Gem, CTR Token) mirror each
// colour's own bits from its received count. Runs on the throttled adventure
// tick in ap_onframe_body (~every 30 frames), not every frame.
//
// OPTION B NOTE: as of Phase 1 the adventure GATES no longer read these bits --
// they read the per-item-TYPE counters via AP_GateCount* (see game/232/AH_*.c).
// These bits are now COSMETIC ONLY: they still feed GAMEPROG_AdvPercent so the
// progress %, podium, and hub-map icons render, but they no longer gate anything.
// Because the gates are independent of the bits, the old item-bit / location-bit
// collision (BL-2/BL-3) is dissolved regardless of what these bits hold. Kept as
// the original high-end reconcile so the cosmetic UI behaves exactly as before.
// Consequences:
//  - BL-2 fix: a local race win sets a vanilla reward bit, but it's not backed
//    by a received item, so it's cleared on the next tick -> a win never
//    double-counts toward the gates. Progression comes only from received items.
//  - Any save works: stale bits from a non-AP save are wiped and the received
//    set re-applied; on (re)connect the server re-sends your items so the counts
//    rebuild from scratch. Load any file and your progress == your AP inventory.
// Location checks are still emitted by the grant-site events (AP_NotifyAdvReward),
// independent of these bits. Only the 7 managed location-bit pools are touched;
// story/hint/door flags are left to the game.
// Idempotent + self-healing across loads. Refreshes the live counters via
// GAMEPROG_AdvPercent so gates that read currAdvProfile see the change.
static void AP_ApplyItems(struct AdvProgress *adv)
{
	int changed = 0, granted = 0;
	int c, k;
	for (c = 0; c < AP_CAT_COUNT; c++)
	{
		const AP_CatPool *p = &AP_CATEGORY_POOLS[c];
		int want = ap_item_count[c];
		if (want > p->size)
			want = p->size;
		for (k = 0; k < p->size; k++)
		{
			int bit, wantSet;
			if (c == AP_CAT_GEM)
			{
				// Gems are per-colour items and requirements can pin a specific
				// colour, so mirror each colour's own bit truthfully from the
				// per-colour receive counts (the pause screen shows WHICH gems
				// you hold). Pooled high-end-first fill stays for the fungible
				// categories. (issue #35)
				bit = p->bits[k]; // 106 + colour, matches AP_IDX_GEM_RED + k
				wantSet = ap_recv_count[AP_IDX_GEM_RED + k] > 0;
			}
			else if (c == AP_CAT_TOKEN)
			{
				// Same argument as the gems above (issue #142): CTR Tokens are
				// per-colour items, requirements can pin a colour, and the pause
				// screen shows WHICH colours you hold -- so mirror each colour's
				// own four-bit run from the per-colour receive counts instead of
				// the colour-blind high-end fill. AP_POOL_TOKEN is COLOUR-MAJOR,
				// so k / 4 is the colour and k % 4 the slot within it. WHICH slot
				// of a colour lights is still arbitrary (the icons track received
				// items, not checked locations -- that is the 0.2.0 display
				// question), but the COLOUR is now truthful.
				//
				// This is also what finally puts the crystal-arena bits under the
				// reconcile: colour 4 is bits 111-114, which no pool covered, so a
				// locally won arena lit its pause icon permanently even though the
				// player never received that Purple CTR Token.
				int colour = k / 4;
				bit = p->bits[k];
				wantSet = (k % 4) < ap_recv_count[AP_IDX_TOKEN_RED + colour];
			}
			else
			{
				bit = p->bits[p->size - 1 - k]; // high-end first
				wantSet = k < want;
			}
			// Issue #171/#28 R1: a relic Time Trial this seed never created
			// (a below-count tier) is a local-grant slot, not a fungible pool
			// member -- its bit is the SOLE record of a native local grant
			// (AP_RelicRewardOwnedByBit reads it back directly), written once
			// by the grant site's own UNLOCK_ADV_BIT. This reconcile must
			// never touch it: setting it from the pooled high-end fill would
			// fabricate a relic that was never earned, and clearing it as
			// "not backed by a received item" (every removed slot's bit fails
			// that test by construction, since it can never receive an AP
			// item) would erase a real local win the instant it lands. Only
			// the three relic categories can ever have a removed slot
			// (trophies, tokens, gems, keys are always real 1:1 locations
			// this seed -- see the package-3 build note).
			if ((c == AP_CAT_SAPPHIRE || c == AP_CAT_GOLD || c == AP_CAT_PLATINUM) &&
			    !AP_LocationExistsByBit(bit))
				continue;
			int isSet = CHECK_ADV_BIT(adv->rewards, bit) != 0;
			if (wantSet)
			{
				if (!isSet)
				{
					UNLOCK_ADV_BIT(adv->rewards, bit);
					changed = 1;
					granted = 1;
				}
			}
			else if (isSet)
			{
				// bit not backed by a received item -> clear (local-win leak /
				// stale save bit)
				adv->rewards[bit >> 5] &= ~(1u << (bit & 31));
				changed = 1;
				// Diagnostics only (#269, 2026-08-23 bundle inspection): the
				// clear used to be silent, so a log could not show that a
				// boss win's local key bit was present at hub birth and gone
				// a tick later. Rare by construction (one line per leaked bit).
				{
					char msg[128];
					snprintf(msg, sizeof msg,
					         "[AP ITEM] cleared unbacked reward bit %d (local grant not backed by a received item) | lvl=%d\n",
					         bit, (int)sdata->gGT->levelID);
					AP_AppendLog(msg);
				}
			}
		}
	}
	if (changed)
		GAMEPROG_AdvPercent(adv);
	if (granted)
		AP_AppendLog("[AP ITEM] applied received items to AdvProgress\n");
}

// Issue #17 (crash on area change): pre-latch the once-per-save "you opened a
// new area" greeting hint so it can never arm during AP play. On a fresh save
// the FIRST door to ever open triggers hint 7 (ADV_MASK_HINT_ID_NEW_WORLD_GREETING)
// right at the doorway (game/232/AH_Door.c:154-174). If the player drives through
// while Aku is mid-hint, LOAD_Hub_Main tears down the level memory the hint
// machinery is still holding and the game crashes (see the mask-hint vs hub-swap
// analysis). The door only requests the hint when its reward bit is clear
// (AH_Door.c:163-174, CHECK_ADV_BIT == 0), so setting ADV_REWARD_HINT_NEW_WORLD_GREETING
// (bit 125) here makes chkRewards read 1 and the request never fires.
//
// This is the standalone randomizer's mitigation (icebound777 wrote "has seen
// this hint" to the save on creation, which eliminated the crash there). Scope
// is deliberately surgical: bit 125 sits OUTSIDE the AP-managed pools (6..110)
// so AP_ApplyItems never touches it, and only this one greeting hint is affected.
// Every other mask hint (podium, warp-pad, garage, etc.) keeps its own bit and
// its normal behaviour. AP players lose one flavour hint; vanilla builds are
// unchanged (this runs only inside CTR_AP, adventure mode). Idempotent + self-
// healing across loads, same as AP_ApplyItems. A deeper engine guard on the hub
// swap (candidate 2, LOAD_Hub_Main on AkuAkuHintState) is a later upstreamable
// root fix and is intentionally NOT built here.
static void AP_PrelatchGreetingHint(struct AdvProgress *adv)
{
	if (CHECK_ADV_BIT(adv->rewards, ADV_REWARD_HINT_NEW_WORLD_GREETING) == 0)
	{
		UNLOCK_ADV_BIT(adv->rewards, ADV_REWARD_HINT_NEW_WORLD_GREETING);
		AP_AppendLog("[AP HINT] pre-latched new-area greeting hint (issue #17)\n");
	}
}

// Read ws uri / slot / password from "ap-config.txt" in the working dir if it
// exists. Lines: "uri=...", "slot=...", "password=...". Missing file or keys
// keep the defaults.
static void AP_ReadConfig(char *uri, int uriN, char *slot, int slotN,
                          char *pass, int passN)
{
	FILE *f = fopen("ap-config.txt", "r");
	if (!f)
		return;
	char line[256];
	while (fgets(line, sizeof line, f))
	{
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (!strncmp(line, "uri=", 4))
			snprintf(uri, uriN, "%s", line + 4);
		else if (!strncmp(line, "slot=", 5))
			snprintf(slot, slotN, "%s", line + 5);
		else if (!strncmp(line, "password=", 9))
			snprintf(pass, passN, "%s", line + 9);
		else if (!strncmp(line, "skip_hints=", 11))
			ap_skip_hints = (line[11] == '1'); // QoL: suppress Aku Aku mask hints
		else if (AP_TrapConfigLine(line))
			; // debug_trap=... -> prime a trap for testing (see ap_traps.c)
		else if (AP_DemoCamConfigLine(line))
			; // debug_democam=1 -> arm the Demo Camera prototype (see ap_democam.c)
		else if (AP_ShortcutConfigLine(line))
			; // shortcutless=... / shortcut_capture=... (see ap_shortcut.c)
		else if (!strncmp(line, "dev_keys=", 9))
			ap_dev_keys = (line[9] == '1'); // numpad dev hotkeys (traps + Shortcutless)
		else if (!strncmp(line, "map_flash=", 10))
			ap_map_flash = (line[10] != '0'); // 0 = static GREEN (no Raceable flicker)
		else if (!strncmp(line, "hud_reserves_fx=", 16))
			ap_hud_reserves_fx = (line[16] == '1'); // keep power-slide fire when holding reserves

		else if (!strncmp(line, "hub_feed=", 9))
			ap_hub_feed_on = (line[9] != '0'); // 0 = hide the AP item feed everywhere
	}
	fclose(f);
}

// Display names for the per-item-drain log below (AP_NetTick), matching the
// existing AP_CAP_CHAIN_* order (ap_capability.h) and AP_SurfaceItem enum
// (ap_surface.h) 1:1. Kept as its own small table here rather than reused from
// ap_capability.c / ap_surface.c: those files export no name table today, and
// this is a log-formatting concern, not capability or surface logic.
static const char *const AP_LOG_CAP_CHAIN_NAME[AP_CAP_CHAIN_COUNT] = {
	"boost", "top speed", "acceleration", "turning",
};
static const char *const AP_LOG_SURFACE_ITEM_NAME[AP_SURFACE_ITEM_COUNT] = {
	"grass", "dirt", "snow", "water", "ice",
};
// Held-item-ID order (0..4 direct, 6..11 offset by one -- 5/Spring is excluded,
// rewritten to Turbo before it can be gated), matching
// AP_ItemsanityWeaponIndex (ap_itemsanity_logic.h). Sourced from the case
// labels' own comments in game/Vehicle/VehPickupItem.c
// (VehPickupItem_ShootNow's held-item switch), and cross-checked against the
// two indices this file already pins independently:
// AP_TURBOGRANT_TURBO_ITEM_INDEX (index 0 must be Turbo) and
// AP_TIZI_MASK_ITEM_INDEX (index 6 must be Mask) both land correctly.
static const char *const AP_LOG_ITEMSANITY_WEAPON_NAME[AP_ITEMSANITY_WEAPON_COUNT] = {
	"Turbo", "Bomb", "Missile", "TNT/Nitro", "Beaker",
	"Shield Bubble", "Mask", "Clock", "Warpball", "Bomb x3", "Missile x3",
};

#endif // CTR_AP
