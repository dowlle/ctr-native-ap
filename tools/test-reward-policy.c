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
//     marker NEVER resolves to 0.

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
	// Traps (16..20), comfort (21..26) and unknown ids stay marker material.
	expect_cat(AP_ITEM_BASE + 16, AP_CAT_NONE, "trap -> marker material");
	expect_cat(AP_ITEM_BASE + 21, AP_CAT_NONE, "comfort -> marker material");
	expect_cat(AP_ITEM_BASE + 26, AP_CAT_NONE, "comfort -> marker material");
	expect_cat(AP_ITEM_BASE + 95, AP_CAT_NONE, "past the ladder -> marker material");
	expect_cat(AP_ITEM_BASE + 120, AP_CAT_NONE, "unknown id -> marker material");

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
	expect_keep(AP_CAT_WUMPA, 1, "#222 wumpa keeps its model");
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
	expect_model(AP_CAT_WUMPA,    AP_MODEL_WUMPA,   "#222 wumpa -> wumpa model");
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
	expect_scale(AP_CAT_WUMPA,    0x1400, "#222 wumpa = pad default scale (in-game pass)");
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
	expect_kind(AP_CAT_WUMPA, 1, 1, 1, AP_PAD_DISP_VANILLA, "#222 own wumpa, model loaded");
	expect_kind(AP_CAT_WUMPA, 0, 1, 1, AP_PAD_DISP_GHOST, "#222 peer wumpa, model loaded");
	expect_kind(AP_CAT_WUMPA, 1, 0, 1, AP_PAD_DISP_MARKER, "#212 own wumpa, model NOT loaded");
	expect_kind(AP_CAT_WUMPA, 0, 0, 1, AP_PAD_DISP_MARKER, "#212 peer wumpa, model NOT loaded");
	expect_kind(AP_CAT_CRYSTAL, 1, 0, 1, AP_PAD_DISP_MARKER, "#212 own crystal, model NOT loaded");
	expect_kind(AP_CAT_TROPHY, 1, 0, 1, AP_PAD_DISP_MARKER, "own trophy, model NOT loaded");
	expect_kind(AP_CAT_NONE, 1, 1, 1, AP_PAD_DISP_MARKER, "marker material is always the marker");
	expect_kind(AP_CAT_NONE, 0, 1, 1, AP_PAD_DISP_MARKER, "#212 group 3 draws no own/peer line");
	expect_kind(AP_CAT_NONE, 1, 1, 0, AP_PAD_DISP_NONE, "no marker model parked -> placeholder");
	expect_kind(AP_CAT_WUMPA, 1, 0, 0, AP_PAD_DISP_NONE, "no model, no marker -> placeholder");

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
	expect_pad_tint(AP_CAT_WUMPA, 1, 1, 0, 1, 0,
	                "#222 own wumpa as a fruit keeps the model's own colours");
	expect_pad_tint(AP_CAT_CRYSTAL, 1, 1, 0, 1, 0x0d22fff0,
	                "#219 own crystal keeps the OG purple");
	expect_pad_tint(AP_CAT_SAPPHIRE, 1, 1, 0, 1, 0x020a5ff0, "own sapphire relic blue");
	expect_pad_tint(AP_CAT_GOLD, 1, 1, 0, 1, 0x0ffc6290, "own gold relic");
	expect_pad_tint(AP_CAT_PLATINUM, 1, 1, 0, 1, 0x0ebebf50, "own platinum relic");
	expect_pad_tint(AP_CAT_TROPHY, 1, 1, 0, 1, 0, "own trophy keeps its natural colour");
	expect_pad_tint(AP_CAT_GEM, 0, 1, 0, 1, 0, "a ghosted peer reward takes no tint");

	// ── The invariant behind the whole defect: a MARKER never resolves to 0 ──
	// The marker is untextured and its colours are lerped toward colorRGBA, so a
	// zero here is the near-black slot Stef saw. No flag combination, in either
	// colour mode, may produce one.
	{
		unsigned f;
		int colors;
		int zero = 0;

		for (colors = 0; colors <= 1; colors++)
			for (f = 0; f < 8; f++)
				if (AP_RewardPadTint(AP_PAD_DISP_MARKER, AP_CAT_NONE, f, colors) == 0)
					zero = 1;

		printf("%-4s no marker tint is 0 over every flag combination and both colour modes\n",
		       zero ? "FAIL" : "ok");
		if (zero)
			g_failures++;
	}

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
