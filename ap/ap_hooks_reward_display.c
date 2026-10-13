// ap_hooks_reward_display.c -- part of the ap_hooks unit (issue #305).
// How a location's item is shown: classified tints, warp-pad reward models,
// ghosting, scale and token/gem colours, ceremony props, and the uncollected
// reward bits a pad displays.
//
// Not a separate translation unit: game/game_unity.h includes ap_hooks.c and
// then every ap_hooks_*.c in a fixed order, so this file relies on the includes,
// macros and file-static state defined before it. Keep it in that list and
// in that order.

#ifdef CTR_AP

// ---------------------------------------------------------------------------
// MAP overlay retirement: the old debug-era per-pad map state
// (AP_BitIsUsefulUnchecked / AP_PadUsefulness / AP_BitUnchecked / AP_PadMapState)
// and its SDL_SCANCODE_M colour-override toggle (g_ap_map_overlay /
// AP_MapOverlayOn) are fully retired -- superseded by the unified AP_PadState
// (Warp-Pad State Model v2), which drives an always-on 5-state map colour and
// drops the own-progression-only GREEN criterion. The M key is now reserved for
// the deferred hub-map zoom (see AP_OnFrame).
// ---------------------------------------------------------------------------

// The generic WHITE GEM marker is RETIRED (#212, ruling 2026-08-10). It used to
// stand in for every foreign item and for own filler/unmapped items, and both
// callers now resolve to a real presentation instead: an OG CTR reward keeps its
// own model (ghosted when it belongs to another CTR player) and everything else
// renders as the Archipelago-logo marker. Nothing packs 0x0ffffff0 any more, so
// no pad glow can show a white gem that is not an actual own/peer gem-cup gem.

// ---------------------------------------------------------------------------
// #124 -- CLASSIFIED FOREIGN-ITEM DISPLAY
//
// A foreign item used to render as an undifferentiated white gem, which told
// the player nothing: pads full of white gems got skipped even when they held
// progression another player was waiting on. Items belonging to players of
// OTHER GAMES now render as the Archipelago-logo marker, tinted by the item's
// AP classification.
//
// ── REVISED BY #212 (ruling 2026-08-10, display design note amendment) ──
// The split is no longer own-vs-foreign but OG-CTR-REWARD-vs-everything-else:
//   1. an OG CTR reward (trophy / relic / token / gem / key) that is MINE
//      renders as its vanilla model, exactly as it always did;
//   2. the same reward owned by ANOTHER CTR PLAYER renders as that same vanilla
//      model, GHOSTED (the time-trial ghost translucency, AP_WarpPadRewardGhost);
//   3. EVERY other item -- traps, capability/comfort items, wumpa, anything an
//      apworld invented, mine or anyone's -- renders as the Archipelago-logo
//      marker. There is deliberately no local/foreign distinction in group 3;
//   4. the generic white gem is retired, so no glow shows one any more.
// ---------------------------------------------------------------------------

// Packed as (R << 0x14) | (G << 0xc) | (B << 0x4), matching the relic tints.
#define AP_PACK_TINT(r, g, b) (((u32)(r) << 0x14) | ((u32)(g) << 0xc) | ((u32)(b) << 0x4))

// In-game-legible approximations of the standard AP scheme, not literal hex
// values from it: these are read on a small spinning model at PSX draw distance,
// so they are pushed for separation rather than fidelity. Filler cyan and useful
// slate blue are the pair most at risk of collapsing into each other, so they are
// split hard on the green channel. Final values are a ruling.
#define AP_TINT_PROGRESSION AP_PACK_TINT(0xc0, 0x88, 0xf0) // plum
#define AP_TINT_USEFUL      AP_PACK_TINT(0x50, 0x78, 0xe0) // slate blue
#define AP_TINT_FILLER      AP_PACK_TINT(0x40, 0xe8, 0xe0) // cyan
#define AP_TINT_TRAP        AP_PACK_TINT(0xff, 0x80, 0x60) // salmon

// #212 point 5: the ONE colour every marker wears when the seed asks for AP item
// types to stay a surprise (ctr_options.ap_item_type_colors = 0). Greyish white,
// The ruling candidate: it is the only neutral that stays neutral under the
// marker's multiplicative-ish tint lerp without collapsing toward any class hue,
// and it cannot be confused with a gem-cup gem because it is never on a gem.
// Never modulate to 0 here (near-black bug) -- the value stays well clear of it.
#define AP_TINT_UNIFORM     AP_PACK_TINT(0xd0, 0xd0, 0xc8) // greyish white

// AP item flags: bit0 = progression, bit1 = useful, bit2 = trap, 0 = filler.
// The precedence that resolves them is the freestanding AP_ItemFlagsClass
// (ap_item_flags.h), shared with the hub feed so the two presentations can
// never disagree about a class.

static int AP_ClassTint(unsigned flags)
{
	// Progression wins over useful when an item carries both, matching how AP's
	// own clients present it.
	switch (AP_ItemFlagsClass(flags))
	{
	case AP_ITEM_CLASS_PROGRESSION: return AP_TINT_PROGRESSION;
	case AP_ITEM_CLASS_USEFUL:      return AP_TINT_USEFUL;
	case AP_ITEM_CLASS_TRAP:        return AP_TINT_TRAP;
	case AP_ITEM_CLASS_FILLER:      return AP_TINT_FILLER;
	}
	return AP_TINT_FILLER;
}

// The colour ONE Archipelago-logo marker wears, honouring the seed's surprise
// toggle (#212 point 5). ctr_options.ap_item_type_colors: absent or nonzero ->
// the class tints above, i.e. exactly the display every shipped client renders;
// 0 -> the single uniform colour, so the AP item pool stays a surprise.
//
// The key is EXPECTED but not required: the apworld half of #212 lands after the
// 0.2.0 name freeze, and a seed generated before it carries no such key. That is
// why the parse defaults to 1 rather than to the new behaviour -- an old seed on
// this client must glow the way it does today, not silently switch to grey.
static int AP_MarkerTint(unsigned flags)
{
	if (!ctr_cfg.ap_item_type_colors)
		return AP_TINT_UNIFORM;
	return AP_ClassTint(flags);
}

// Font-palette colour for one hub item-feed line (issue #195), resolved from the
// same class as the marker tint above. CTR's font palette has no literal RGB
// text colouring path, so this picks fixed, always-present flat ptrColors
// entries: purple (N_GIN_PURPLE) progression, blue (CRASH_BLUE) useful, cyan
// (POLAR_CYAN) filler, red (CORTEX_RED) trap. The exact hues are a legibility
// call (a design ruling): the marker tints are packed model RGBA and
// do not map 1:1 onto the palette.
static int AP_ClassFontColor(unsigned flags)
{
	switch (AP_ItemFlagsClass(flags))
	{
	case AP_ITEM_CLASS_PROGRESSION: return N_GIN_PURPLE;
	case AP_ITEM_CLASS_USEFUL:      return CRASH_BLUE;
	case AP_ITEM_CLASS_TRAP:        return CORTEX_RED;
	case AP_ITEM_CLASS_FILLER:      return POLAR_CYAN;
	}
	return POLAR_CYAN;
}

// The three presentations one scouted pad reward can have (#212). One resolver
// feeds the model, the tint and the ghost flag, so those three can never
// disagree about what a slot is showing.
#define AP_PAD_DISP_VANILLA 0 // my own model-keeping reward -> that model, untouched
#define AP_PAD_DISP_GHOST   1 // another CTR player's -> the same model, ghosted
#define AP_PAD_DISP_MARKER  2 // everything else -> the Archipelago-logo marker
#define AP_PAD_DISP_NONE    3 // undecidable right now -> leave the placeholder alone

// Resolve one scouted (item, player) pair to its presentation, and hand back the
// model-keeping category on the two model paths (AP_CAT_NONE otherwise). Since
// #219 that is the base-game rewards AND the CTR progression crystal, so "OG" no
// longer describes the set -- see ap_reward_policy.h for the ruling.
//
// The sync gate is unchanged from #124 and still matters: the glow pass runs
// every frame from connect, so resolving a FOREIGN item before scouts and slot
// info have landed would flicker a slot between presentations. Before the gate
// opens a foreign item resolves to AP_PAD_DISP_NONE and the slot keeps the
// placeholder model it was born with. An OWN item needs neither (the self slot
// is known at connect), so own rewards never flicker.
//
// AP_ItemCategory reads OUR item-id space, so it is only meaningful for a slot
// that is playing CTR. Another game's ids collide with ours numerically and must
// never be run through it -- that is the ap_net_player_is_ctr guard below, not a
// nicety.
static int AP_PadDisplayKind(long long item, int player, AP_ItemCat *outCat)
{
	AP_ItemCat cat = AP_CAT_NONE;
	int own = (player == ap_net_self_slot());

	if (!own)
	{
		if (!ap_net_scouts_ready() || !ap_net_slot_info_ready())
		{
			if (outCat)
				*outCat = AP_CAT_NONE;
			return AP_PAD_DISP_NONE;
		}
	}

	if (own || ap_net_player_is_ctr(player))
	{
		cat = AP_ItemCategory(item);

		// The one category decision, in one place (ap_reward_policy.h): base-game
		// rewards keep their vanilla model (matrix rules 1-2), CTR progression
		// keeps the crystal (rule 3), and every Wumpa package keeps the retail
		// fruit (the 2026-09-20 amendment). Traps, comfort items and anything an
		// apworld invents are marker material whoever owns them.
		if (!AP_RewardKeepsModel(cat))
			cat = AP_CAT_NONE;

		// ...and one drawability question, which is the half Lessons Learned §24
		// says a display resolver must be able to ask. The crystal is not a stock
		// per-level model: the hub's pack does not carry it, so it exists only
		// where ap_retail_crystal.c managed to harvest and park one. If that
		// failed, this reward is marker material exactly as it is today, rather
		// than a slot left showing whichever placeholder it was born with while the
		// tint is resolved for a crystal.
		//
		// The fruit is the same shape of problem: model 0x02 ships only in
		// loose-fruit level files, never in an MPK pack, so on the hub it is not
		// resident until ap_retail_wumpa.c harvests its own copy. Gate it on that
		// harvest OR a level that carries the model itself; otherwise a Wumpa
		// package resolves to the marker rather than the §24 stale-placeholder bug.
		//
		// Only these two are gated here. The base-game categories keep the
		// arrangement they already had: AH_WarpPad_ThTick reassigns the model only
		// when it is resident, and resolves the colour from the model the slot
		// ENDS UP with, so those two can never disagree.
		if (cat == AP_CAT_CRYSTAL && !AP_RetailCrystal_IsRegistered())
			cat = AP_CAT_NONE;

		// The fruit's own one-line version of the same question, kept in the
		// freestanding header so the host harness pins it.
		cat = AP_WumpaEffectiveCategory(cat, AP_RetailWumpa_IsDrawable(sdata ? sdata->gGT : 0));
	}

	if (outCat)
		*outCat = cat;

	if (cat != AP_CAT_NONE)
		return own ? AP_PAD_DISP_VANILLA : AP_PAD_DISP_GHOST;

	// Marker material. Without the marker model parked we have nothing to show it
	// with (the white gem is retired), so the slot keeps its placeholder rather
	// than inventing a stand-in that would lie about the reward.
	return AP_MarkerModel_IsRegistered() ? AP_PAD_DISP_MARKER : AP_PAD_DISP_NONE;
}

// Resolve a location's global bit straight to (kind, category, flags). Returns
// AP_PAD_DISP_NONE for anything not checkable or not yet scouted, which every
// caller reads as "leave the vanilla/placeholder presentation alone".
static int AP_PadDisplayForBit(int globalBit, AP_ItemCat *outCat, unsigned *outFlags)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	if (outCat)
		*outCat = AP_CAT_NONE;
	if (outFlags)
		*outFlags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return AP_PAD_DISP_NONE; // not a checkable location
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return AP_PAD_DISP_NONE; // not scouted yet (not connected / pre-scout)

	if (outFlags)
		*outFlags = flags;
	return AP_PadDisplayKind(item, player, outCat);
}

// ---------------------------------------------------------------------------
// REWARD GLOW -- map a location (by its AdvProgress global bit) to the model of
// the AP item placed there, so each warp pad's glow shows its real reward.
// ---------------------------------------------------------------------------

int AP_WarpPadRewardModel(int globalBit)
{
	AP_ItemCat cat = AP_CAT_NONE;
	int kind = AP_PadDisplayForBit(globalBit, &cat, NULL);

	switch (kind)
	{
	case AP_PAD_DISP_MARKER:
		// Every non-OG item, mine or anyone's (#212 point 3).
		return STATIC_AP;

	case AP_PAD_DISP_VANILLA:
	case AP_PAD_DISP_GHOST:
		// A model-keeping reward: the same model either way. The GHOST kind adds
		// translucency at the instance (AP_WarpPadRewardGhost), never a different
		// model -- a peer's Sapphire Relic stays a Sapphire Relic, and a peer's
		// progression item stays a crystal (matrix rules 2 and 3).
		return AP_RewardModelForCat(cat);

	default:
		return -1; // not checkable / not scouted / marker unavailable -> placeholder
	}
}

// 1 when this location's scouted reward belongs to ANOTHER CTR PLAYER and keeps
// its own OG model, i.e. the slot must be ghost-rendered (#212 point 2). Own
// rewards, marker items and everything unresolved answer 0.
int AP_WarpPadRewardGhost(int globalBit)
{
	return AP_PadDisplayForBit(globalBit, NULL, NULL) == AP_PAD_DISP_GHOST ? 1 : 0;
}

// Per-category tint for the reward glow (see header): the relic tiers and, since
// #219, the crystal's purple. colorRGBA is packed as (R<<0x14)|(G<<0xc)|(B<<0x4);
// 0x020a5ff0 is the vanilla relic blue. Returns 0 to leave the caller's default
// colour untouched.
int AP_WarpPadRewardTint(int globalBit)
{
	AP_ItemCat cat = AP_CAT_NONE;
	unsigned flags = 0;
	int kind = AP_PadDisplayForBit(globalBit, &cat, &flags);

	// The Archipelago-logo marker carries the classification (or the one uniform
	// surprise colour) in its tint; the marker's own colours are neutral greys so
	// this lerps cleanly. Same answer for my own non-OG items as for anyone
	// else's -- #212 draws no local/foreign line here.
	if (kind == AP_PAD_DISP_MARKER)
		return AP_MarkerTint(flags);

	// A GHOSTED peer reward takes NO tint: the ghost writer needs colorRGBA 0 (see
	// the ghost block in AH_WarpPad_ThTick), and the caller forces it to 0 anyway.
	if (kind != AP_PAD_DISP_VANILLA)
		return 0;

	// Own relic tiers and the crystal's purple; 0 for everything whose natural
	// per-model colour the glow switch already applies (gem-cup colour, untinted
	// trophy, token group colour, golden key). Own filler and traps do not reach
	// here at all: they are marker items, handled above.
	return AP_RewardTintForCat(cat);
}

int AP_WarpPadRewardScale(int globalBit)
{
	AP_ItemCat cat = AP_CAT_NONE;
	int kind = AP_PadDisplayForBit(globalBit, &cat, NULL);

	if (kind != AP_PAD_DISP_VANILLA && kind != AP_PAD_DISP_GHOST)
		return 0x1000;
	return AP_RewardScaleForCat(cat);
}

int AP_CeremonyRewardProp(struct Instance *prop, int globalBit)
{
	struct GameTracker *gGT;
	int model;
	int ghost;
	int tint;
	int oldModelID;

	if (prop == 0 || !ctr_cfg_active())
		return 0;

	model = AP_WarpPadRewardModel(globalBit);
	if (model < 0)
		return 0;
	gGT = sdata->gGT;
	if (gGT == 0 || gGT->modelPtr[model] == 0)
		return 0;

	ghost = AP_WarpPadRewardGhost(globalBit);
	tint = AP_WarpPadRewardTint(globalBit);
	// Captured before the swap so the animation transition below can tell an
	// entry into the fruit (reset) from a swap away from it (clear).
	oldModelID = (prop->model != 0) ? prop->model->id : -1;
	prop->model = gGT->modelPtr[model];
	prop->flags &= ~(DRAW_TRANSPARENT | USE_SPECULAR_LIGHT | GHOST_DRAW_TRANSPARENT);
	prop->alphaScale = 0;

	switch (prop->model->id)
	{
	case STATIC_RELIC:
		prop->colorRGBA = tint ? tint : 0x020a5ff0;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	case STATIC_CRYSTAL:
		prop->colorRGBA = tint ? tint : 0x0d22fff0;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	case STATIC_TOKEN:
	{
		int tg = AP_WarpPadRewardTokenColour(globalBit);
		if (tg < 0)
			tg = data.metaDataLEV[gGT->levelID].ctrTokenGroupID;
		prop->colorRGBA = ((u32)data.AdvCups[tg].color[0] << 0x14) |
		                  ((u32)data.AdvCups[tg].color[1] << 0xc) |
		                  ((u32)data.AdvCups[tg].color[2] << 0x4);
		prop->flags |= (DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
		break;
	}
	case STATIC_GEM:
	{
		int gc = AP_WarpPadRewardGemColour(globalBit);
		if (gc >= 0)
			prop->colorRGBA = ((u32)data.AdvCups[gc].color[0] << 0x14) |
			                  ((u32)data.AdvCups[gc].color[1] << 0xc) |
			                  ((u32)data.AdvCups[gc].color[2] << 0x4);
		else
			prop->colorRGBA = tint;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	}
	case STATIC_AP:
		prop->colorRGBA = tint;
		prop->alphaScale = AP_MARKER_TINT_STRENGTH;
		break;
	case STATIC_KEY:
		prop->colorRGBA = 0x0dca6000;
		prop->flags |= USE_SPECULAR_LIGHT;
		break;
	default:
		prop->colorRGBA = 0;
		break;
	}

	if (ghost)
	{
		prop->colorRGBA = 0;
		prop->alphaScale = AP_PAD_GHOST_ALPHA;
		prop->flags &= ~(DRAW_TRANSPARENT | USE_SPECULAR_LIGHT);
		prop->flags |= GHOST_DRAW_TRANSPARENT;
	}

	// #222: reset the fruit animation only on the transition INTO the fruit, and
	// clear the fruit animation bits on the way out so a later placeholder does
	// not inherit a half-driven spin. A per-tick reset would freeze the fruit on
	// frame zero.
	if (AP_WumpaShouldResetAnim(oldModelID, prop->model->id))
		AP_WumpaApplyFruitAnim(&prop->animIndex, &prop->animFrame, &prop->vertSplit, &prop->flags);
	else if (AP_WumpaShouldClearAnim(oldModelID, prop->model->id))
		AP_WumpaClearFruitAnim(&prop->flags);

	return 1;
}

// Colour index of the OWN CTR Token scouted here (see header). The token item id
// encodes its colour: ids AP_ITEM_BASE+4..8 = R,G,B,Y,P (AP_IDX_TOKEN_RED = 4),
// which lines up with data.AdvCups[0..4]. Foreign / unscouted / non-token -> -1.
int AP_WarpPadRewardTokenColour(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return -1;
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return -1;
	if (player != ap_net_self_slot())
		return -1; // foreign multiworld item is not an own coloured token
	if (AP_ItemCategory(item) != AP_CAT_TOKEN)
		return -1;
	return (int)((item - AP_ITEM_BASE) - AP_IDX_TOKEN_RED); // 0..4 = R,G,B,Y,P
}

// Colour index of the OWN Gem scouted here (see header). The gem item id
// encodes its colour: ids AP_ITEM_BASE+9..13 = R,G,B,Y,P (AP_IDX_GEM_RED = 9),
// which lines up with data.AdvCups[0..4]. Foreign / unscouted / non-gem -> -1.
int AP_WarpPadRewardGemColour(int globalBit)
{
	long code;
	long long item = 0;
	int player = -1;
	unsigned flags = 0;

	code = AP_LookupLocationCode(globalBit);
	if (code < 0)
		return -1;
	if (!ap_net_scout_known(code, &item, &player, &flags))
		return -1;
	if (player != ap_net_self_slot())
		return -1; // foreign multiworld item is not an own coloured gem
	if (AP_ItemCategory(item) != AP_CAT_GEM)
		return -1;
	return (int)((item - AP_ITEM_BASE) - AP_IDX_GEM_RED); // 0..4 = R,G,B,Y,P
}

// Enumerate the still-UNCOLLECTED (unchecked) AP reward locations of race track
// `destLevelID` (0..15), writing their global AdvProgress bits into `outBits`
// (caller array, capacity `cap`) in fixed tier order -- Trophy +0x06, Sapphire
// +0x16, Gold +0x28, Platinum +0x3a, CTR Token +0x4c -- and returning the count
// written (0..5). A location counts as collected purely by AP CHECKED-STATE
// (AP_LocationCheckedByBit / ap_net_location_checked), NEVER by CHECK_ADV_BIT or
// modelIndex: AP_ApplyItems clears local-win bits on every reconcile tick, so only
// the server's checked set is authoritative. Returns 0 for a non-race destination or
// when every tier is checked (caller then shows nothing). This is the sole input
// to the warp-pad glow's "which rewards to advertise + cycle" decision.
int AP_WarpPadUncollectedBits(int destLevelID, int *outBits, int cap)
{
	static const int kTierBit[5] = {
	    ADV_REWARD_FIRST_TROPHY,
	    ADV_REWARD_FIRST_SAPPHIRE_RELIC,
	    ADV_REWARD_FIRST_GOLD_RELIC,
	    ADV_REWARD_FIRST_PLATINUM_RELIC,
	    ADV_REWARD_FIRST_CTR_TOKEN};
	int count = 0;
	int i;

	if (outBits == 0 || cap <= 0)
		return 0;
	if (destLevelID < 0 || destLevelID >= 16)
		return 0; // only race tracks 0..15 carry this 5-location pool

	for (i = 0; i < 5 && count < cap; i++)
	{
		int bit = destLevelID + kTierBit[i];
		// Not a real location THIS SEED (e.g. a tier the apworld didn't place,
		// or -- issue #171/#28 R1 -- a below-count relic Time Trial that was
		// never created at all) is "not advertisable" -> skip, so the glow
		// never shows an AP check for a location that doesn't exist.
		if (!AP_LocationExistsByBit(bit))
			continue;
		if (!AP_LocationCheckedByBit(bit))
			outBits[count++] = bit;
	}

	return count;
}

#endif // CTR_AP
