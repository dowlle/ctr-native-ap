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
//     white-gem category all answer marker model -1 / keep 0 / scale 0x1000.

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

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
