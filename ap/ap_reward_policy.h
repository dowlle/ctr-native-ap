#ifndef AP_REWARD_POLICY_H
#define AP_REWARD_POLICY_H

// Freestanding reward-display policy: category -> presentation, and nothing
// else. Pure data, no engine and no network client, so an out-of-engine harness
// (tools/test-reward-policy.c) pins the decisions directly. The production
// resolver in ap_hooks.c consumes exactly these functions rather than keeping a
// second copy of the policy, which is what acceptance check 2 of #219 asks for:
// ONE testable category decision that every display surface reads.
//
// ── THE WARP-PAD GLOW MATRIX (ruled 2026-08-13, binding) ──
//
// The ruling of record for what a warp-pad glow slot shows. Four axes: CTR vs
// non-CTR, local vs non-local, base-game vs non-base-game, progression vs other.
//
//   1. LOCAL CTR base-game items      -> the original model, fully visible.
//   2. NON-LOCAL CTR base-game items  -> the original model, TRANSLUCENT.
//   3. CTR NON-base-game PROGRESSION  -> the purple crystal; local fully
//                                        visible, non-local translucent.
//   4. Everything else                -> the Archipelago logo, NEVER
//                                        translucent, coloured per the #48
//                                        ap_item_type_colors option or greyish
//                                        white when that option is off.
//
// Two consequences the ruling states explicitly, both encoded here:
//
//   - base-game PROGRESSION (trophies, relics, gems, keys, tokens) renders as
//     the ORIGINAL models, not as crystals. Crystals are exclusively for
//     non-base-game progression: the capability ladder, the weapon unlocks, the
//     character unlocks, the letters and Gas Pedal -- the CTR families with no
//     vanilla model of their own (see AP_ItemCategory for the id ranges);
//   - the AP logo's ONLY variation is type colour vs grey-white. It is never
//     dimmed for being non-local, which is why AP_RewardPresentation returns
//     MARKER for marker material regardless of ownership.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include "ap_items.h" // AP_ItemCat

// Model ids the policy can hand back. Mirrors of the engine's MODEL_ID enum
// (namespace_Instance.h), spelled out here so the policy stays freestanding;
// ap_hooks.c static-asserts each one against the engine constant so the mirror
// can never drift.
#define AP_MODEL_CRYSTAL 0x60 // STATIC_CRYSTAL
#define AP_MODEL_GEM     0x5f // STATIC_GEM
#define AP_MODEL_RELIC   0x61 // STATIC_RELIC
#define AP_MODEL_TROPHY  0x62 // STATIC_TROPHY
#define AP_MODEL_KEY     0x63 // STATIC_KEY
#define AP_MODEL_TOKEN   0x7d // STATIC_TOKEN

// 1 when a category KEEPS a model of its own -- matrix rules 1-3. That is the
// base-game pad rewards (trophy / relic / token / gem / key, rules 1-2) and the
// CTR non-base-game progression crystal (rule 3). These are exactly the
// categories the local/peer VANILLA/GHOST split applies to. 0 = Archipelago-logo
// material, matrix rule 4.
//
// AP_CAT_WUMPA IS RULE 4. A Wumpa Fruit package is a quantity bundle the
// multiworld invented, not one of the five rewards a vanilla warp pad can hold,
// so it is not base-game under rules 1-2; and it is filler, so it is not rule 3
// either. Rule 4 is what is left.
//
// Every enumerator is spelled out rather than folded into `default` so
// -Wswitch-enum keeps flagging these switches if the category set ever grows.
static int AP_RewardKeepsModel(AP_ItemCat cat)
{
	switch (cat)
	{
	case AP_CAT_TROPHY:
	case AP_CAT_SAPPHIRE:
	case AP_CAT_GOLD:
	case AP_CAT_PLATINUM:
	case AP_CAT_TOKEN:
	case AP_CAT_GEM:
	case AP_CAT_KEY:
	case AP_CAT_CRYSTAL:
		return 1;
	case AP_CAT_WUMPA:
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:
		return 0;
	}
}

// Model id for a model-keeping category, or -1 for marker material. Marker
// material resolves to the Archipelago-logo marker (STATIC_AP), which is a
// separate policy in ap_marker_model.h -- see AP_PadDisplayKind in ap_hooks.c.
static int AP_RewardModelForCat(AP_ItemCat cat)
{
	switch (cat)
	{
	case AP_CAT_TROPHY:   return AP_MODEL_TROPHY;
	case AP_CAT_SAPPHIRE:
	case AP_CAT_GOLD:
	case AP_CAT_PLATINUM: return AP_MODEL_RELIC;
	case AP_CAT_TOKEN:    return AP_MODEL_TOKEN;
	case AP_CAT_GEM:      return AP_MODEL_GEM;
	case AP_CAT_KEY:      return AP_MODEL_KEY;
	case AP_CAT_CRYSTAL:  return AP_MODEL_CRYSTAL;
	case AP_CAT_WUMPA:
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:              return -1;
	}
}

// Tint for an OWN model-keeping reward, 0 = keep the caller's natural per-model
// colour. A ghosted peer reward never reaches this (the ghost writer needs
// colorRGBA 0); marker tinting is a separate path in ap_hooks.c.
//
// The crystal's purple is the one this file adds, and it is derived rather than
// picked: UI_Instance.c:85-90 gives a menu crystal colorRGBA 0xd22fff0 together
// with USE_SPECULAR_LIGHT, so a pad crystal wearing the same value is the same
// crystal players already know rather than a new hue.
static int AP_RewardTintForCat(AP_ItemCat cat)
{
	switch (cat)
	{
	case AP_CAT_SAPPHIRE:
		return 0x020a5ff0; // blue (vanilla relic colour)
	case AP_CAT_GOLD:
		return 0x0ffc6290; // gold
	case AP_CAT_PLATINUM:
		return 0x0ebebf50; // platinum / pale silver
	case AP_CAT_CRYSTAL:
		return 0x0d22fff0; // purple (vanilla crystal colour, UI_Instance.c:90)
	case AP_CAT_TROPHY:
	case AP_CAT_TOKEN:
	case AP_CAT_GEM:
	case AP_CAT_KEY:
	case AP_CAT_WUMPA:
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:
		// Own gem / trophy / token / key -> the natural colour path the glow
		// switch already applies per model (gem-cup colour, untinted trophy, token
		// group colour, golden key). Matrix rule 1: a local base-game item renders
		// as its original, untouched.
		return 0;
	}
}

// Ceremony props are born at the vanilla token scale. Return a fixed-point
// multiplier (0x1000 = 1.0) matching the established warp-pad model sizes.
static int AP_RewardScaleForCat(AP_ItemCat cat)
{
	switch (cat)
	{
	case AP_CAT_TOKEN:
		return 0x1000;
	case AP_CAT_SAPPHIRE:
	case AP_CAT_GOLD:
	case AP_CAT_PLATINUM:
		return 0x0c00;
	case AP_CAT_TROPHY:
	case AP_CAT_GEM:
	case AP_CAT_KEY:
	case AP_CAT_CRYSTAL:
	case AP_CAT_WUMPA:
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:
		return 0x1400;
	}
}

#endif // CTR_AP
#endif // AP_REWARD_POLICY_H
