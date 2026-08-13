// Out-of-engine assertions for the shared reward-display policy (issues #212,
// #219, #221, #222). Compiles the REAL freestanding logic -- ap/ap_reward_policy.h
// plus its dependencies (ap_items.h, ap_capability.h, ap_item_flags.h) are
// self-contained by design, so this harness links nothing from the game.
//
//   cc -Wall -Wextra -DCTR_AP -o /tmp/test-reward-policy tools/test-reward-policy.c && /tmp/test-reward-policy
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// What this pins, per category, for BOTH ownership kinds where it matters:
//   - the item-id -> category decision (AP_ItemCategory), including the #219
//     crystal range and the #222 wumpa mapping;
//   - whether the category keeps its own model (AP_RewardKeepsModel) -- the ONE
//     decision every model-based reward surface consumes (pad glow, ceremony
//     prop, resolver);
//   - the category -> model mapping (AP_RewardModelForCat);
//   - the category -> display scale (AP_RewardScaleForCat);
//   - the own-crystal tint (AP_RewardTintForCat);
//   - the fallback answers: marker material, unknown items and the retired
//     white-gem category all answer marker model -1 / keep 0 / scale 0x1000;
//   - the presentation decision (AP_RewardPresentation), including the #212
//     defect it was extended to prevent: a model-keeping reward whose model is
//     NOT drawable on this surface must fall back to the marker, because the
//     alternative (keep the instance's current model, colour it as a
//     model-keeping reward) paints the untextured marker at colorRGBA 0 and it
//     renders near-black;
//   - the colour a pad slot ends up with (AP_RewardPadTint) for every class,
//     both values of ctr_options.ap_item_type_colors, and the invariant that a
//     marker NEVER resolves to 0;
//   - the #219 fallback colour (AP_MarkerFallbackTint): a progression crystal
//     the surface cannot draw wears plum rather than the item's own AP class,
//     which the apworld ships as `useful`, and no other category makes that
//     claim;
//   - the ruling end to end from real item ids (expect_item_display): EVERY CTR
//     progression family with no vanilla model of its own -- the capability
//     ladder (#219), the weapon unlocks (#145), the racers (#54/#209), the
//     letters (#148) and Gas Pedal -- presents as the purple crystal where it is
//     drawable and as the plum progression marker where it is not, while traps,
//     comfort items, the wumpa bundles and the Tizi Helper keep the ordinary
//     class-tinted marker. The crystal families do NOT share an AP
//     classification (the ladder and the racers ship `useful`, the rest ship
//     `progression`), so these rows also pin that the crystal is an enumerated
//     list of item families and not a reading of the flags.

#include <stdio.h>

#define CTR_AP 1
#include "../ap/ap_reward_policy.h"

static int g_failures = 0;

static void expect_keep(AP_ItemCat cat, int want, const char *why)
{
	int got = AP_RewardKeepsModel(cat);
	printf("%-4s AP_RewardKeepsModel(%d) = %d  [%s]\n",
	       got == want ? "ok" : "FAIL", (int)cat, got, why);
	if (got != want)
		g_failures++;
}

static void expect_model(AP_ItemCat cat, int want, const char *why)
{
	int got = AP_RewardModelForCat(cat);
	printf("%-4s AP_RewardModelForCat(%d) = 0x%x  [%s]\n",
	       got == want ? "ok" : "FAIL", (int)cat, got, why);
	if (got != want)
		g_failures++;
}

static void expect_scale(AP_ItemCat cat, int want, const char *why)
{
	int got = AP_RewardScaleForCat(cat);
	printf("%-4s AP_RewardScaleForCat(%d) = 0x%x  [%s]\n",
	       got == want ? "ok" : "FAIL", (int)cat, got, why);
	if (got != want)
		g_failures++;
}

static const char *kind_name(int kind)
{
	switch (kind)
	{
	case AP_PAD_DISP_VANILLA: return "VANILLA";
	case AP_PAD_DISP_GHOST:   return "GHOST";
	case AP_PAD_DISP_MARKER:  return "MARKER";
	case AP_PAD_DISP_NONE:    return "NONE";
	}
	return "?";
}

static void expect_kind(AP_ItemCat cat, int own, int drawable, int markerAvailable,
                        int want, const char *why)
{
	int got = AP_RewardPresentation(cat, own, drawable, markerAvailable);
	printf("%-4s AP_RewardPresentation(cat %d, own %d, drawable %d, marker %d) = %s  [%s]\n",
	       got == want ? "ok" : "FAIL", (int)cat, own, drawable, markerAvailable,
	       kind_name(got), why);
	if (got != want)
		g_failures++;
}

// The colour ONE pad slot ends up writing to colorRGBA, resolved exactly the way
// AP_WarpPadRewardTint does it: presentation first, then the tint for it.
static void expect_pad_tint(AP_ItemCat cat, int own, int drawable, unsigned flags,
                            int typeColors, int want, const char *why)
{
	int kind = AP_RewardPresentation(cat, own, drawable, 1);
	int got = AP_RewardPadTint(kind, cat, flags, typeColors);
	printf("%-4s pad tint(cat %d, own %d, drawable %d, flags %u, colours %d) = 0x%08x  [%s]\n",
	       got == want ? "ok" : "FAIL", (int)cat, own, drawable, flags, typeColors,
	       (unsigned)got, why);
	if (got != want)
		g_failures++;
}

static void expect_cat(long long id, AP_ItemCat want, const char *why)
{
	AP_ItemCat got = AP_ItemCategory(id);
	printf("%-4s AP_ItemCategory(%lld) = %d  [%s]\n",
	       got == want ? "ok" : "FAIL", id, (int)got, why);
	if (got != want)
		g_failures++;
}

// END TO END from a real item id: category, then presentation, then the model
// and the colour one pad slot writes for it. Driven from the ID rather than the
// category on purpose -- these are the assertions that would catch someone
// moving an item family out of the crystal, which a category-only test cannot
// see. `own` and `drawable` describe the surface; a peer's reward is ghosted, so
// only the OWN rows have a meaningful colour.
static void expect_item_display(long long id, int own, int drawable, unsigned flags,
                                int typeColors, int wantModel, int wantTint,
                                const char *why)
{
	AP_ItemCat cat = AP_ItemCategory(id);
	int kind = AP_RewardPresentation(cat, own, drawable, 1);
	int gotModel = (kind == AP_PAD_DISP_VANILLA || kind == AP_PAD_DISP_GHOST)
	                   ? AP_RewardModelForCat(cat)
	                   : (kind == AP_PAD_DISP_MARKER ? -2 : -1);
	int gotTint = AP_RewardPadTint(kind, cat, flags, typeColors);
	int ok = (gotModel == wantModel) && (gotTint == wantTint);

	printf("%-4s item %lld (own %d, drawable %d) -> %s model %d tint 0x%08x  [%s]\n",
	       ok ? "ok" : "FAIL", id, own, drawable, kind_name(kind), gotModel,
	       (unsigned)gotTint, why);
	if (!ok)
		g_failures++;
}

// -2 in the `wantModel` column above: the slot draws the Archipelago marker
// (STATIC_AP), which is the caller's constant, not the policy's.
#define WANT_MARKER_MODEL (-2)

int main(void)
{
	// ── #219: item-id -> category, including the crystal ladder ──
	expect_cat(AP_ITEM_BASE + 0,  AP_CAT_TROPHY,   "Trophy");
	expect_cat(AP_ITEM_BASE + 1,  AP_CAT_SAPPHIRE, "Sapphire Relic");
	expect_cat(AP_ITEM_BASE + 2,  AP_CAT_GOLD,     "Gold Relic");
	expect_cat(AP_ITEM_BASE + 3,  AP_CAT_PLATINUM, "Platinum Relic");
	expect_cat(AP_ITEM_BASE + 4,  AP_CAT_TOKEN,    "Red Token");
	expect_cat(AP_ITEM_BASE + 8,  AP_CAT_TOKEN,    "Purple Token");
	expect_cat(AP_ITEM_BASE + 9,  AP_CAT_GEM,      "Red Gem");
	expect_cat(AP_ITEM_BASE + 13, AP_CAT_GEM,      "Purple Gem");
	expect_cat(AP_ITEM_BASE + 14, AP_CAT_KEY,      "Key");
	expect_cat(AP_ITEM_BASE + 15, AP_CAT_WUMPA,    "#222 Wumpa Fruit");
	// The shared-global capability block (27..30) and the per-character block
	// (31..94) are the #219 crystal category.
	expect_cat(AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX, AP_CAT_CRYSTAL,
	           "#219 Progressive Boost");
	expect_cat(AP_ITEM_BASE + 30, AP_CAT_CRYSTAL, "#219 Progressive Turning");
	expect_cat(AP_ITEM_BASE + AP_CAPABILITY_PC_ITEM_FIRST_INDEX, AP_CAT_CRYSTAL,
	           "#219 per-character first");
	expect_cat(AP_ITEM_BASE + AP_CAPABILITY_PC_ITEM_FIRST_INDEX +
	                  AP_CAPABILITY_PC_ITEM_COUNT - 1,
	           AP_CAT_CRYSTAL, "#219 per-character last");
	// The character unlocks (123..138) join the SAME crystal category: a racer is
	// a CTR progression item and reads as the purple crystal, not as a generic
	// marker (ruled, #54/#209).
	expect_cat(AP_ITEM_BASE + AP_CHARACTER_ITEM_FIRST_INDEX, AP_CAT_CRYSTAL,
	           "#54 character unlock first (Crash Bandicoot)");
	expect_cat(AP_ITEM_BASE + 137, AP_CAT_CRYSTAL, "#54 Nitros Oxide is a crystal");
	expect_cat(AP_ITEM_BASE + AP_CHARACTER_ITEM_FIRST_INDEX +
	                  AP_CHARACTER_ITEM_COUNT - 1,
	           AP_CAT_CRYSTAL, "#54 character unlock last (Penta Penguin)");
	// The lettersanity letters (139..186, 16 tracks x C/T/R) are the third crystal
	// family, ruled in for the same uniform-visuals reason as the racers.
	expect_cat(AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX, AP_CAT_CRYSTAL,
	           "#148 first letter (Letter C, Crash Cove)");
	expect_cat(AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX +
	                  CTR_CFG_LETTER_TRACK_COUNT * CTR_CFG_LETTER_COUNT - 1,
	           AP_CAT_CRYSTAL, "#148 last letter (Letter R, Oxide Station)");
	// The itemsanity weapon unlocks (95..105) and Gas Pedal (187) are the last two
	// families the ruling reached, so the crystal now covers every CTR progression
	// item that has no vanilla model of its own.
	expect_cat(AP_ITEM_BASE + AP_ITEMSANITY_ITEM_FIRST_INDEX, AP_CAT_CRYSTAL,
	           "#145 first weapon unlock (Turbo)");
	expect_cat(AP_ITEM_BASE + AP_ITEMSANITY_ITEM_FIRST_INDEX +
	                  AP_ITEMSANITY_WEAPON_COUNT - 1,
	           AP_CAT_CRYSTAL, "#145 last weapon unlock (Missile x3)");
	expect_cat(AP_ITEM_BASE + 101, AP_CAT_CRYSTAL,
	           "#145 Mask, the index #223 also reads -> still a crystal");
	expect_cat(AP_ITEM_BASE + AP_GAS_PEDAL_ITEM_INDEX, AP_CAT_CRYSTAL, "Gas Pedal");
	// Traps, comfort/surface items, the wumpa bundles and the Tizi Helper stay
	// marker material: none of them is CTR progression. The trap rows are the ones
	// that matter -- a trap wearing the progression crystal would lose the salmon
	// warning colour the class tints exist to give it.
	expect_cat(AP_ITEM_BASE + 16, AP_CAT_NONE, "trap -> marker material");
	expect_cat(AP_ITEM_BASE + 20, AP_CAT_NONE, "last trap -> marker material");
	expect_cat(AP_ITEM_BASE + 21, AP_CAT_NONE, "comfort -> marker material");
	expect_cat(AP_ITEM_BASE + 26, AP_CAT_NONE, "comfort -> marker material");
	expect_cat(AP_ITEM_BASE + 106, AP_CAT_NONE,
	           "Wumpa Reset Trap, just above the weapon block -> marker material");
	expect_cat(AP_ITEM_BASE + 188, AP_CAT_NONE,
	           "#223 Tizi Helper, just above Gas Pedal -> marker material");
	// The crystal-block edge that still has a non-crystal neighbour below it, so
	// widening it has to be a deliberate edit. 122 is Progressive Starting Wumpa.
	expect_cat(AP_ITEM_BASE + AP_CHARACTER_ITEM_FIRST_INDEX - 1, AP_CAT_NONE,
	           "just below the character block -> marker material");

	// ── Model-keeping: the ONE category decision every surface consumes ──
	// #212 OG rewards keep their models; #219 crystals and #222 wumpa join them.
	expect_keep(AP_CAT_TROPHY, 1, "#212 Trophy keeps its model");
	expect_keep(AP_CAT_SAPPHIRE, 1, "#212 Sapphire keeps its model");
	expect_keep(AP_CAT_GOLD, 1, "#212 Gold keeps its model");
	expect_keep(AP_CAT_PLATINUM, 1, "#212 Platinum keeps its model");
	expect_keep(AP_CAT_TOKEN, 1, "#212 Token keeps its model");
	expect_keep(AP_CAT_GEM, 1, "#212 Gem keeps its model");
	expect_keep(AP_CAT_KEY, 1, "#212 Key keeps its model");
	expect_keep(AP_CAT_CRYSTAL, 1, "#219 crystal keeps its model");
	expect_keep(AP_CAT_WUMPA, 0, "matrix rule 4: a wumpa PACKAGE is marker material");
	expect_keep(AP_CAT_COUNT, 0, "bit-pool sentinel is marker material");
	expect_keep(AP_CAT_NONE, 0, "unmapped is marker material");

	// ── Category -> model ──
	expect_model(AP_CAT_TROPHY,   AP_MODEL_TROPHY,  "Trophy -> trophy model");
	expect_model(AP_CAT_SAPPHIRE, AP_MODEL_RELIC,   "Sapphire -> relic model");
	expect_model(AP_CAT_GOLD,     AP_MODEL_RELIC,   "Gold -> relic model");
	expect_model(AP_CAT_PLATINUM, AP_MODEL_RELIC,   "Platinum -> relic model");
	expect_model(AP_CAT_TOKEN,    AP_MODEL_TOKEN,   "Token -> token model");
	expect_model(AP_CAT_GEM,      AP_MODEL_GEM,     "Gem -> gem model");
	expect_model(AP_CAT_KEY,      AP_MODEL_KEY,     "Key -> key model");
	expect_model(AP_CAT_CRYSTAL,  AP_MODEL_CRYSTAL, "#219 crystal -> crystal model");
	expect_model(AP_CAT_WUMPA,    -1, "matrix rule 4: wumpa has no own model any more");
	expect_model(AP_CAT_COUNT,    -1, "marker material -> no own model");
	expect_model(AP_CAT_NONE,     -1, "marker material -> no own model");

	// ── Category -> display scale (mirrors the pad glow's per-model scales) ──
	expect_scale(AP_CAT_TOKEN,    0x1000, "token = pad token scale reference");
	expect_scale(AP_CAT_SAPPHIRE, 0xc00,  "relic = pad relic scale");
	expect_scale(AP_CAT_GOLD,     0xc00,  "relic = pad relic scale");
	expect_scale(AP_CAT_PLATINUM, 0xc00,  "relic = pad relic scale");
	expect_scale(AP_CAT_TROPHY,   0x1400, "trophy = pad default scale");
	expect_scale(AP_CAT_GEM,      0x1400, "gem = pad default scale");
	expect_scale(AP_CAT_KEY,      0x1400, "key = pad default scale");
	expect_scale(AP_CAT_CRYSTAL,  0x1400, "#219 crystal = pad default scale (in-game pass)");
	expect_scale(AP_CAT_WUMPA,    0x1400, "wumpa is marker material; scale only reads as the default");
	expect_scale(AP_CAT_COUNT,    0x1400, "marker material -> pad default scale");
	expect_scale(AP_CAT_NONE,     0x1400, "marker material -> pad default scale");

	// ── Own-crystal tint (#219); every other own category keeps natural colours ──
	if (AP_RewardTintForCat(AP_CAT_CRYSTAL) != 0x0d22fff0u)
	{
		printf("FAIL AP_RewardTintForCat(CRYSTAL) != purple 0x0d22fff0\n");
		g_failures++;
	}
	else
	{
		printf("ok   AP_RewardTintForCat(CRYSTAL) = 0x0d22fff0  [#219 own crystal tint]\n");
	}
	if (AP_RewardTintForCat(AP_CAT_TROPHY) != 0 ||
	    AP_RewardTintForCat(AP_CAT_WUMPA) != 0 ||
	    AP_RewardTintForCat(AP_CAT_NONE) != 0)
	{
		printf("FAIL a non-crystal category returned a policy tint\n");
		g_failures++;
	}
	else
	{
		printf("ok   non-crystal categories return 0 (keep the caller's natural colour)\n");
	}

	// ── #212: presentation, including the not-drawable fallback ──
	// A model-keeping reward presents as its own model only where that model can
	// actually be drawn. The pad glow passes drawable = 0 for a category whose
	// model is not loaded on the hub, and the answer must be the marker: keeping
	// the instance's current model while colouring it as a model-keeping reward is
	// what rendered own Wumpa Fruit as a near-black marker.
	expect_kind(AP_CAT_TROPHY, 1, 1, 1, AP_PAD_DISP_VANILLA, "own trophy, model loaded");
	expect_kind(AP_CAT_TROPHY, 0, 1, 1, AP_PAD_DISP_GHOST, "peer trophy, model loaded");
	expect_kind(AP_CAT_GEM, 0, 1, 1, AP_PAD_DISP_GHOST, "peer gem stays ghosted");
	expect_kind(AP_CAT_CRYSTAL, 1, 1, 1, AP_PAD_DISP_VANILLA, "#219 own crystal, model loaded");
	expect_kind(AP_CAT_CRYSTAL, 0, 1, 1, AP_PAD_DISP_GHOST, "#219 peer crystal, model loaded");
	// Matrix rule 4 has no local/non-local axis and no residency axis: a wumpa
	// package is the marker in all four combinations. The first two rows are the
	// ones that changed -- they used to be VANILLA and GHOST.
	expect_kind(AP_CAT_WUMPA, 1, 1, 1, AP_PAD_DISP_MARKER, "rule 4: own wumpa is the marker even where the fruit is loaded");
	expect_kind(AP_CAT_WUMPA, 0, 1, 1, AP_PAD_DISP_MARKER, "rule 4: a peer wumpa is the marker, NOT a ghost");
	expect_kind(AP_CAT_WUMPA, 1, 0, 1, AP_PAD_DISP_MARKER, "rule 4: own wumpa, fruit not loaded");
	expect_kind(AP_CAT_WUMPA, 0, 0, 1, AP_PAD_DISP_MARKER, "rule 4: peer wumpa, fruit not loaded");
	expect_kind(AP_CAT_CRYSTAL, 1, 0, 1, AP_PAD_DISP_MARKER, "#212 own crystal, model NOT loaded");
	expect_kind(AP_CAT_TROPHY, 1, 0, 1, AP_PAD_DISP_MARKER, "own trophy, model NOT loaded");
	expect_kind(AP_CAT_NONE, 1, 1, 1, AP_PAD_DISP_MARKER, "marker material is always the marker");
	expect_kind(AP_CAT_NONE, 0, 1, 1, AP_PAD_DISP_MARKER, "#212 group 3 draws no own/peer line");
	expect_kind(AP_CAT_NONE, 1, 1, 0, AP_PAD_DISP_NONE, "no marker model parked -> placeholder");
	expect_kind(AP_CAT_WUMPA, 1, 0, 0, AP_PAD_DISP_NONE, "no marker parked -> placeholder");
	// The fallback must never hand a surface a marker it cannot draw either: with
	// the marker unavailable the answer is the placeholder, never a model-keeping
	// presentation whose model is missing.
	expect_kind(AP_CAT_CRYSTAL, 1, 0, 0, AP_PAD_DISP_NONE, "crystal not loaded and no marker -> placeholder");
	expect_kind(AP_CAT_TROPHY, 0, 0, 0, AP_PAD_DISP_NONE, "peer reward, nothing drawable -> placeholder");

	// ── #212: the colour a pad slot writes, per class and per surprise toggle ──
	// Hex values are the packed (R<<0x14)|(G<<0xc)|(B<<0x4) forms of the tints, so
	// changing a colour has to be a deliberate edit here too.
	expect_pad_tint(AP_CAT_WUMPA, 1, 0, 0, 1, 0x040e8e00,
	                "#212 own wumpa on a pad -> FILLER cyan marker, never near-black");
	expect_pad_tint(AP_CAT_NONE, 1, 1, AP_ITEM_FLAG_PROGRESSION, 1, 0x0c088f00,
	                "own progression -> plum marker");
	expect_pad_tint(AP_CAT_NONE, 1, 1, AP_ITEM_FLAG_USEFUL, 1, 0x05078e00,
	                "own useful -> slate blue marker");
	expect_pad_tint(AP_CAT_NONE, 1, 1, AP_ITEM_FLAG_TRAP, 1, 0x0ff80600,
	                "own trap -> salmon marker");
	expect_pad_tint(AP_CAT_NONE, 1, 1, 0, 1, 0x040e8e00, "own filler -> cyan marker");
	expect_pad_tint(AP_CAT_NONE, 0, 1, AP_ITEM_FLAG_PROGRESSION, 1, 0x0c088f00,
	                "a peer's progression gets the same plum");
	expect_pad_tint(AP_CAT_NONE, 1, 1, AP_ITEM_FLAG_PROGRESSION, 0, 0x0d0d0c80,
	                "ap_item_type_colors = 0 -> the one greyish white");
	expect_pad_tint(AP_CAT_WUMPA, 1, 0, 0, 0, 0x0d0d0c80,
	                "uniform mode covers the fallback marker too");
	expect_pad_tint(AP_CAT_WUMPA, 1, 1, 0, 1, 0x040e8e00,
	                "rule 4: an own wumpa is cyan marker even where the fruit IS loaded");
	expect_pad_tint(AP_CAT_CRYSTAL, 1, 1, 0, 1, 0x0d22fff0,
	                "#219 own crystal keeps the OG purple");
	expect_pad_tint(AP_CAT_SAPPHIRE, 1, 1, 0, 1, 0x020a5ff0, "own sapphire relic blue");
	expect_pad_tint(AP_CAT_GOLD, 1, 1, 0, 1, 0x0ffc6290, "own gold relic");
	expect_pad_tint(AP_CAT_PLATINUM, 1, 1, 0, 1, 0x0ebebf50, "own platinum relic");
	expect_pad_tint(AP_CAT_TROPHY, 1, 1, 0, 1, 0, "own trophy keeps its natural colour");
	expect_pad_tint(AP_CAT_GEM, 0, 1, 0, 1, 0, "a ghosted peer reward takes no tint");

	// ── #219: a crystal that FELL BACK to the marker still reads as a crystal ──
	// The apworld ships every capability item as `useful` (see the note on
	// AP_MarkerFallbackTint), so taking the item's AP class here would paint the
	// #219 reward slate blue on a surface whose pack has no crystal and purple on
	// one that does -- one reward, two looks. The fallback claims plum whatever
	// the item's flags say, and only for the crystal.
	expect_pad_tint(AP_CAT_CRYSTAL, 1, 0, AP_ITEM_FLAG_USEFUL, 1, 0x0c088f00,
	                "#219 own capability crystal falls back to the PLUM marker, not slate blue");
	expect_pad_tint(AP_CAT_CRYSTAL, 0, 0, AP_ITEM_FLAG_USEFUL, 1, 0x0c088f00,
	                "a peer's fallen-back crystal gets the same plum");
	expect_pad_tint(AP_CAT_CRYSTAL, 1, 0, 0, 1, 0x0c088f00,
	                "missing flags cannot demote a fallen-back crystal to filler cyan");
	expect_pad_tint(AP_CAT_CRYSTAL, 1, 0, AP_ITEM_FLAG_USEFUL, 0, 0x0d0d0c80,
	                "the surprise toggle still wins over the crystal's own colour");
	// The claim is the CRYSTAL's alone: every other fallback keeps reading the
	// item's classification, which is the #212 behaviour this must not disturb.
	expect_pad_tint(AP_CAT_TROPHY, 1, 0, AP_ITEM_FLAG_PROGRESSION, 1, 0x0c088f00,
	                "a fallen-back trophy still takes its item's class");
	expect_pad_tint(AP_CAT_WUMPA, 1, 0, AP_ITEM_FLAG_USEFUL, 1, 0x05078e00,
	                "a wumpa marker still takes its item's class");

	// ── The ruling, end to end from real item ids ──
	// Every CTR progression item presents as the purple crystal where the crystal
	// is drawable, and as the plum progression marker where it is not. Both
	// families ship as `useful`, so every one of these rows would have come out
	// slate blue on the classification path -- that is the whole point of them.
	expect_item_display(AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX, 1, 1,
	                    AP_ITEM_FLAG_USEFUL, 1, AP_MODEL_CRYSTAL, 0x0d22fff0,
	                    "#219 own Progressive Boost -> purple crystal");
	expect_item_display(AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX, 1, 0,
	                    AP_ITEM_FLAG_USEFUL, 1, WANT_MARKER_MODEL, 0x0c088f00,
	                    "#219 own Progressive Boost, no crystal here -> plum marker");
	expect_item_display(AP_ITEM_BASE + 137, 1, 1, AP_ITEM_FLAG_USEFUL, 1,
	                    AP_MODEL_CRYSTAL, 0x0d22fff0,
	                    "#54 own Nitros Oxide -> purple crystal, never the marker");
	expect_item_display(AP_ITEM_BASE + 137, 1, 0, AP_ITEM_FLAG_USEFUL, 1,
	                    WANT_MARKER_MODEL, 0x0c088f00,
	                    "#54 own Nitros Oxide, no crystal here -> plum marker");
	expect_item_display(AP_ITEM_BASE + AP_CHARACTER_ITEM_FIRST_INDEX, 0, 1,
	                    AP_ITEM_FLAG_USEFUL, 1, AP_MODEL_CRYSTAL, 0,
	                    "a peer's character unlock is a GHOSTED crystal, untinted");
	// A letter carries the PROGRESSION flag where the other two families carry
	// `useful`, and lands on exactly the same two answers -- which is the point of
	// the ruling: one look for CTR progression, whatever the datapackage says.
	expect_item_display(AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX, 1, 1,
	                    AP_ITEM_FLAG_PROGRESSION, 1, AP_MODEL_CRYSTAL, 0x0d22fff0,
	                    "#148 own letter -> purple crystal");
	expect_item_display(AP_ITEM_BASE + CTR_LETTER_ITEM_FIRST_INDEX, 1, 0,
	                    AP_ITEM_FLAG_PROGRESSION, 1, WANT_MARKER_MODEL, 0x0c088f00,
	                    "#148 own letter, no crystal here -> plum marker");
	// The itemsanity weapon unlocks are the family a player is most likely to
	// actually meet -- they are reachable in a real seed today, where Gas Pedal has
	// no receive path and the per-character ladder cannot even generate.
	expect_item_display(AP_ITEM_BASE + AP_ITEMSANITY_ITEM_FIRST_INDEX, 1, 1,
	                    AP_ITEM_FLAG_PROGRESSION, 1, AP_MODEL_CRYSTAL, 0x0d22fff0,
	                    "#145 own weapon unlock -> purple crystal");
	expect_item_display(AP_ITEM_BASE + AP_ITEMSANITY_ITEM_FIRST_INDEX, 1, 0,
	                    AP_ITEM_FLAG_PROGRESSION, 1, WANT_MARKER_MODEL, 0x0c088f00,
	                    "#145 own weapon unlock, no crystal here -> plum marker");
	// The contrast row: an item that is NOT one of the progression families still
	// takes the ordinary marker and its own class colour, crystal or no crystal.
	// This is the row that fails first if someone widens the crystal to "every CTR
	// item", which would cost a trap its salmon warning.
	expect_item_display(AP_ITEM_BASE + 16, 1, 1, AP_ITEM_FLAG_TRAP, 1,
	                    WANT_MARKER_MODEL, 0x0ff80600,
	                    "a trap is still a salmon marker, not a crystal");

	// The two marker entry points must agree on everything that was marker
	// material to begin with, so the crystal claim above cannot leak into #212's
	// ordinary classification path.
	{
		unsigned f;
		int colors;
		int disagree = 0;

		for (colors = 0; colors <= 1; colors++)
			for (f = 0; f < 8; f++)
				if (AP_MarkerFallbackTint(AP_CAT_NONE, f, colors) !=
				    AP_MarkerTintForFlags(f, colors))
					disagree = 1;

		printf("%-4s AP_MarkerFallbackTint(AP_CAT_NONE) == AP_MarkerTintForFlags everywhere\n",
		       disagree ? "FAIL" : "ok");
		if (disagree)
			g_failures++;
	}

	// ── The invariant behind the whole defect: a MARKER never resolves to 0 ──
	// The marker is untextured and its colours are lerped toward colorRGBA, so a
	// zero here is the near-black slot Stef saw. No flag combination, in either
	// colour mode, may produce one -- and now no fallback category either, since
	// a fallen-back reward reaches the marker carrying its category.
	{
		unsigned f;
		int colors;
		int c;
		int zero = 0;

		for (c = 0; c <= (int)AP_CAT_NONE; c++)
			for (colors = 0; colors <= 1; colors++)
				for (f = 0; f < 8; f++)
					if (AP_RewardPadTint(AP_PAD_DISP_MARKER, (AP_ItemCat)c, f, colors) == 0)
						zero = 1;

		printf("%-4s no marker tint is 0 over every category, flag combination and colour mode\n",
		       zero ? "FAIL" : "ok");
		if (zero)
			g_failures++;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// THE WARP-PAD GLOW MATRIX (Stef, 2026-08-13, binding). One block that walks
	// all four rules, so a future edit that satisfies some other reading of the
	// display design fails here rather than in game. Every colour is pinned as the
	// exact packed value; the near-black invariant above covers the marker rows.
	// ─────────────────────────────────────────────────────────────────────────
	{
		// Rule 1: LOCAL CTR base-game -> original model, fully visible.
		// "Fully visible" is the VANILLA kind: it is GHOST that makes a slot
		// translucent, so asserting the kind IS asserting the opacity.
		expect_item_display(AP_ITEM_BASE + 0, 1, 1, AP_ITEM_FLAG_PROGRESSION, 1,
		                    AP_MODEL_TROPHY, 0,
		                    "rule 1: own Trophy -> original model, untinted");
		expect_item_display(AP_ITEM_BASE + 14, 1, 1, AP_ITEM_FLAG_PROGRESSION, 1,
		                    AP_MODEL_KEY, 0,
		                    "rule 1: own Key -> original model");
		expect_item_display(AP_ITEM_BASE + 9, 1, 1, AP_ITEM_FLAG_PROGRESSION, 1,
		                    AP_MODEL_GEM, 0,
		                    "rule 1: own Gem -> original model");
		expect_kind(AP_CAT_TROPHY, 1, 1, 1, AP_PAD_DISP_VANILLA,
		            "rule 1: local base-game is never translucent");

		// Stef's confirmed consequence 1: base-game PROGRESSION stays on its
		// original model. These five carry the progression flag and are the exact
		// rows that would turn into crystals under the superseded 2026-08-12
		// reading, so this is the assertion that pins the ruling that replaced it.
		expect_cat(AP_ITEM_BASE + 0,  AP_CAT_TROPHY,   "consequence 1: Trophy is NOT a crystal");
		expect_cat(AP_ITEM_BASE + 1,  AP_CAT_SAPPHIRE, "consequence 1: Relic is NOT a crystal");
		expect_cat(AP_ITEM_BASE + 4,  AP_CAT_TOKEN,    "consequence 1: Token is NOT a crystal");
		expect_cat(AP_ITEM_BASE + 9,  AP_CAT_GEM,      "consequence 1: Gem is NOT a crystal");
		expect_cat(AP_ITEM_BASE + 14, AP_CAT_KEY,      "consequence 1: Key is NOT a crystal");

		// Rule 2: NON-LOCAL CTR base-game -> original model, TRANSLUCENT. The
		// ghost path requires tint 0 (the ghost writer reads its mask colour from a
		// scratchpad word only refreshed on the colorRGBA == 0 branch), so the 0 in
		// these rows is a correctness constraint, not a look.
		expect_item_display(AP_ITEM_BASE + 0, 0, 1, AP_ITEM_FLAG_PROGRESSION, 1,
		                    AP_MODEL_TROPHY, 0,
		                    "rule 2: a peer's Trophy -> same model, ghosted");
		expect_kind(AP_CAT_KEY, 0, 1, 1, AP_PAD_DISP_GHOST,
		            "rule 2: non-local base-game IS translucent");

		// Rule 3: CTR non-base-game progression -> the purple crystal; local
		// visible, non-local translucent. The crystal is drawable on every surface
		// now (ap_crystal_model.c parks a stand-in wherever the level did not load
		// the retail one), which is why the drawable-1 rows are the real ones and
		// the drawable-0 fallback rows above are the defence in depth.
		expect_item_display(AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX, 1, 1,
		                    AP_ITEM_FLAG_USEFUL, 1, AP_MODEL_CRYSTAL, 0x0d22fff0,
		                    "rule 3: own capability -> purple crystal, fully visible");
		expect_item_display(AP_ITEM_BASE + AP_CAPABILITY_ITEM_FIRST_INDEX, 0, 1,
		                    AP_ITEM_FLAG_USEFUL, 1, AP_MODEL_CRYSTAL, 0,
		                    "rule 3: a peer's capability -> translucent crystal");
		expect_item_display(AP_ITEM_BASE + AP_CHARACTER_ITEM_FIRST_INDEX, 1, 1,
		                    AP_ITEM_FLAG_USEFUL, 1, AP_MODEL_CRYSTAL, 0x0d22fff0,
		                    "rule 3: own character unlock -> purple crystal");

		// Rule 4: everything else -> the AP logo, NEVER translucent, coloured by
		// the option or grey-white. Stef's confirmed consequence 2 is the
		// never-translucent half, and it is worth walking exhaustively because it
		// is a claim about a NEGATIVE: no category that reaches the marker may ever
		// come back GHOST, for either ownership.
		{
			int c;
			int ghosted = 0;

			for (c = 0; c <= (int)AP_CAT_NONE; c++)
			{
				if (AP_RewardKeepsModel((AP_ItemCat)c))
					continue;
				if (AP_RewardPresentation((AP_ItemCat)c, 0, 1, 1) == AP_PAD_DISP_GHOST ||
				    AP_RewardPresentation((AP_ItemCat)c, 0, 0, 1) == AP_PAD_DISP_GHOST ||
				    AP_RewardPresentation((AP_ItemCat)c, 1, 1, 1) == AP_PAD_DISP_GHOST ||
				    AP_RewardPresentation((AP_ItemCat)c, 1, 0, 1) == AP_PAD_DISP_GHOST)
					ghosted = 1;
			}

			printf("%-4s consequence 2: the AP logo is NEVER translucent, any category, either owner\n",
			       ghosted ? "FAIL" : "ok");
			if (ghosted)
				g_failures++;
		}

		// Consequence 2's other half: the logo's ONLY variation is type colour vs
		// grey-white. Resolve the whole presentation for both owners rather than
		// calling the tint helper twice -- the helper takes no ownership argument,
		// so comparing it against itself would assert nothing. The claim under test
		// is that OWNERSHIP CANNOT REACH THE COLOUR AT ALL: both owners land on the
		// marker and both markers resolve to the same value.
		{
			unsigned f;
			int colors;
			int split = 0;

			for (colors = 0; colors <= 1; colors++)
				for (f = 0; f < 8; f++)
				{
					int ownKind = AP_RewardPresentation(AP_CAT_NONE, 1, 1, 1);
					int peerKind = AP_RewardPresentation(AP_CAT_NONE, 0, 1, 1);
					int own = AP_RewardPadTint(ownKind, AP_CAT_NONE, f, colors);
					int peer = AP_RewardPadTint(peerKind, AP_CAT_NONE, f, colors);

					if (ownKind != AP_PAD_DISP_MARKER || peerKind != AP_PAD_DISP_MARKER)
						split = 1;
					if (own != peer)
						split = 1;
					// And the OFF mode really is one single colour, not a palette.
					if (!colors && own != AP_TINT_UNIFORM)
						split = 1;
					// While ON gives the class colour, so the two modes are genuinely
					// different answers and not an accidentally-constant tint.
					if (colors && own != AP_ClassTint(f))
						split = 1;
				}

			printf("%-4s consequence 2: the logo's only variation is type colour vs grey-white\n",
			       split ? "FAIL" : "ok");
			if (split)
				g_failures++;
		}

		// Rule 4, the concrete rows a player meets: a trap keeps its salmon
		// warning, a wumpa package its filler cyan, and neither becomes a crystal.
		expect_item_display(AP_ITEM_BASE + 16, 1, 1, AP_ITEM_FLAG_TRAP, 1,
		                    WANT_MARKER_MODEL, 0x0ff80600,
		                    "rule 4: own trap -> salmon AP logo, never a crystal");
		expect_item_display(AP_ITEM_BASE + 15, 1, 1, 0, 1,
		                    WANT_MARKER_MODEL, 0x040e8e00,
		                    "rule 4: own Wumpa package -> cyan AP logo, not the fruit");
		expect_item_display(AP_ITEM_BASE + 15, 0, 1, 0, 1,
		                    WANT_MARKER_MODEL, 0x040e8e00,
		                    "rule 4: a peer's Wumpa package -> the same cyan logo, undimmed");
		expect_item_display(AP_ITEM_BASE + 16, 1, 1, AP_ITEM_FLAG_TRAP, 0,
		                    WANT_MARKER_MODEL, 0x0d0d0c80,
		                    "rule 4: colours off -> the one greyish white");
	}

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
