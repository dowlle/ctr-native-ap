// Out-of-engine assertions for the reward-display category decision (#219).
// Compiles the REAL freestanding policy -- ap/ap_items.h and ap/ap_reward_policy.h
// are self-contained by design -- so this harness links nothing from the game and
// nothing from the network client.
//
//   cc -Wall -Wextra -Wno-unused-function -DCTR_AP -o /tmp/test-reward-policy tools/test-reward-policy.c && /tmp/test-reward-policy
//
// -Wno-unused-function only silences the itemsanity rules that ap_items.h pulls
// in for the weapon-unlock index range and this harness never calls.
//
// Exit 0 = every assertion held; the failing case is printed otherwise.
//
// What this pins, and why a category-only test is not enough: #219 asks for ONE
// category decision that every display surface reads. So each case drives the
// whole chain from a real apworld item id -- id -> category -> keeps-a-model ->
// model id -> own tint -- rather than starting at a category. A test that only
// asked "what category is this id" could not see an item family being quietly
// moved off the crystal, or the crystal being given a different model.

#include <stdio.h>

#include "../ap/ap_items.h"
#include "../ap/ap_reward_policy.h"

static int g_failures = 0;

static void expect(int got, int want, const char *what, long long id)
{
	int ok = (got == want);

	printf("%-4s id %lld: %s = %d (want %d)\n", ok ? "ok" : "FAIL", id, what, got, want);
	if (!ok)
		g_failures++;
}

// One item id, all the way through the chain every display surface consumes.
static void expect_item(long long index, AP_ItemCat wantCat, int wantModel, int wantTint, const char *why)
{
	long long  id = AP_ITEM_BASE + index;
	AP_ItemCat cat = AP_ItemCategory(id);

	printf("-- %s\n", why);
	expect((int)cat, (int)wantCat, "category", id);
	expect(AP_RewardKeepsModel(cat), wantModel >= 0, "keeps a model", id);
	expect(AP_RewardModelForCat(cat), wantModel, "model", id);
	expect(AP_RewardTintForCat(cat), wantTint, "own tint", id);
}

#define CRYSTAL_PURPLE 0x0d22fff0

int main(void)
{
	// ── Matrix rules 1-2: the base-game rewards keep their vanilla models ──
	// Base-game PROGRESSION is emphatically NOT crystal material; the ruling says
	// so in as many words, and this is the assertion that keeps it that way.
	expect_item(AP_IDX_TROPHY, AP_CAT_TROPHY, AP_MODEL_TROPHY, 0, "Trophy -> trophy, natural colour");
	expect_item(AP_IDX_SAPPHIRE, AP_CAT_SAPPHIRE, AP_MODEL_RELIC, 0x020a5ff0, "Sapphire Relic -> relic, blue");
	expect_item(AP_IDX_GOLD, AP_CAT_GOLD, AP_MODEL_RELIC, 0x0ffc6290, "Gold Relic -> relic, gold");
	expect_item(AP_IDX_PLATINUM, AP_CAT_PLATINUM, AP_MODEL_RELIC, 0x0ebebf50, "Platinum Relic -> relic, platinum");
	expect_item(AP_IDX_TOKEN_RED, AP_CAT_TOKEN, AP_MODEL_TOKEN, 0, "CTR Token -> token, natural colour");
	expect_item(AP_IDX_GEM_RED, AP_CAT_GEM, AP_MODEL_GEM, 0, "Gem -> gem, natural colour");
	expect_item(AP_IDX_KEY, AP_CAT_KEY, AP_MODEL_KEY, 0, "Key -> key, natural colour");

	// ── Matrix rule 4: filler, traps and comfort items are marker material ──
	// A Wumpa PACKAGE is a quantity bundle the multiworld invented, not one of the
	// five rewards a vanilla pad can hold, so it is rule 4 despite being CTR's.
	expect_item(15, AP_CAT_WUMPA, -1, 0, "Wumpa Fruit -> marker material");
	expect_item(16, AP_CAT_NONE, -1, 0, "first trap -> marker material");
	expect_item(20, AP_CAT_NONE, -1, 0, "last trap -> marker material");
	expect_item(21, AP_CAT_NONE, -1, 0, "first comfort item -> marker material");
	expect_item(25, AP_CAT_NONE, -1, 0, "last comfort item -> marker material");
	expect_item(26, AP_CAT_NONE, -1, 0, "the gap below the ladder -> marker material");

	// ── Matrix rule 3: every CTR progression family is the purple crystal ──
	// Both edges of every block, so a range that silently loses or gains an item
	// fails here rather than in game.
	expect_item(27, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "Progressive Boost -> crystal");
	expect_item(28, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "Progressive Top Speed -> crystal");
	expect_item(30, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "Progressive Turning -> crystal");
	expect_item(31, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "first per-character chain -> crystal");
	expect_item(94, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "last per-character chain -> crystal");
	expect_item(95, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "first weapon unlock -> crystal");
	expect_item(105, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "last weapon unlock -> crystal");
	expect_item(123, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "first character unlock -> crystal");
	expect_item(138, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "last character unlock -> crystal");
	expect_item(139, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "first letter -> crystal");
	expect_item(186, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "last letter -> crystal");
	expect_item(187, AP_CAT_CRYSTAL, AP_MODEL_CRYSTAL, CRYSTAL_PURPLE, "Gas Pedal -> crystal");

	// The gaps between the crystal blocks must NOT be swept in. 106..122 sits
	// between the weapon unlocks and the character unlocks, and 188 is the Tizi
	// Helper -- a per-track assist, not a progression step, and nothing has ruled
	// it into the family.
	expect_item(106, AP_CAT_NONE, -1, 0, "just above the weapon unlocks -> marker material");
	expect_item(122, AP_CAT_NONE, -1, 0, "just below the character unlocks -> marker material");
	expect_item(188, AP_CAT_NONE, -1, 0, "Tizi Helper -> marker material");
	expect_item(400, AP_CAT_NONE, -1, 0, "an id no block owns -> marker material");

	// The crystal's purple is the vanilla one (UI_Instance.c:90) and can never be
	// 0: a model tinted to 0 renders near-black, which is the #212 defect family
	// this whole policy exists to keep unreachable.
	expect(AP_RewardTintForCat(AP_CAT_CRYSTAL), CRYSTAL_PURPLE, "crystal tint is the vanilla purple", 0);
	expect(AP_RewardTintForCat(AP_CAT_CRYSTAL) != 0, 1, "crystal tint is never 0", 0);

	// AP_CAT_CRYSTAL must stay OUT of the bit-pool range: it is display-only, and
	// an enumerator below AP_CAT_COUNT would let AP_CATEGORY_POOLS index it.
	expect(AP_CAT_CRYSTAL > AP_CAT_COUNT, 1, "AP_CAT_CRYSTAL sits past AP_CAT_COUNT", 0);

	if (g_failures != 0)
	{
		printf("\n%d assertion(s) FAILED\n", g_failures);
		return 1;
	}

	printf("\nall assertions held\n");
	return 0;
}
