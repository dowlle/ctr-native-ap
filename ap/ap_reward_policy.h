#ifndef AP_REWARD_POLICY_H
#define AP_REWARD_POLICY_H

// Freestanding reward-display policy shared by the pad glow, the token-ceremony
// prop and the display resolver itself (issues #212, #219, #221, #222). All of
// it is pure category -> presentation data, so an out-of-engine harness
// (tools/test-reward-policy.c) pins the decisions without pulling in the game
// or the network client. The production resolver in ap_hooks.c consumes exactly
// these functions, never a second copy of the policy.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include "ap_items.h"      // AP_ItemCat, AP_ItemCategory
#include "ap_item_flags.h" // AP_ItemClass for the marker tint precedent

// Model ids the policy can hand back. Mirrors of the engine's MODEL_ID enum
// (namespace_Instance.h), spelled out here so the policy stays freestanding;
// ap_hooks.c static-asserts each one against the engine constant so the mirror
// can never drift.
#define AP_MODEL_CRYSTAL 0x60 // STATIC_CRYSTAL
#define AP_MODEL_WUMPA   0x02 // PU_WUMPA_FRUIT
#define AP_MODEL_TROPHY  0x62 // STATIC_TROPHY
#define AP_MODEL_RELIC   0x61 // STATIC_RELIC
#define AP_MODEL_TOKEN   0x7d // STATIC_TOKEN
#define AP_MODEL_GEM     0x5f // STATIC_GEM
#define AP_MODEL_KEY     0x63 // STATIC_KEY

// 1 when a category KEEPS its own model -- an OG CTR reward (trophy / relic /
// token / gem / key), a CTR progression crystal (#219), or a Wumpa Fruit
// package (#222). These are the categories the local/peer VANILLA/GHOST split
// applies to. 0 = marker material (everything else). This is THE one testable
// category decision every model-based reward surface consumes.
static inline int AP_RewardKeepsModel(AP_ItemCat cat)
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
	case AP_CAT_WUMPA:
		return 1;
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:
		return 0;
	}
}

// Model id for a model-keeping category, or -1 for marker material. Marker
// material resolves to the Archipelago-logo marker (STATIC_AP), which is a
// separate policy in ap_marker_model.h -- see AP_PadDisplayKind in ap_hooks.c.
static inline int AP_RewardModelForCat(AP_ItemCat cat)
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
	case AP_CAT_WUMPA:    return AP_MODEL_WUMPA;
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:              return -1;
	}
}

// Display-scale multiplier for a model-keeping category's ceremony prop, in
// fixed point where 0x1000 = 1.0. Values MIRROR the pad glow's vanilla
// per-model scales (AH_WarpPad_ThTick): token 0x2000, relic 0x1800, everything
// else 0x2800 -- normalized to the token reference the ceremony grows toward,
// so the ceremony prop and the pad display agree about a category's size. The
// crystal and wumpa land on the pad's trophy default until the in-game scale
// pass picks per-model values. Marker material / unscouted answer 0x1000 (the
// vanilla token's own scale); callers treat the display scale as 1.0 there.
static inline int AP_RewardScaleForCat(AP_ItemCat cat)
{
	switch (cat)
	{
	case AP_CAT_TOKEN:
		return 0x1000; // pad 0x2000
	case AP_CAT_SAPPHIRE:
	case AP_CAT_GOLD:
	case AP_CAT_PLATINUM:
		return 0xc00; // pad 0x1800
	case AP_CAT_TROPHY:
	case AP_CAT_GEM:
	case AP_CAT_KEY:
	case AP_CAT_CRYSTAL:
	case AP_CAT_WUMPA:
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:
		return 0x1400; // pad default 0x2800
	}
}

// Tint for an OWN model-keeping reward, 0 = keep the caller's natural colour.
// The crystal's OG purple (#219) is the only model-keeping category that is
// recoloured by the policy; the ghosted peer path never reaches this (the ghost
// writer needs colorRGBA 0). Marker tint is separate (AP_MarkerTint, ap_hooks.c).
static inline unsigned AP_RewardTintForCat(AP_ItemCat cat)
{
	switch (cat)
	{
	case AP_CAT_CRYSTAL:
		return 0x0d22fff0u; // purple (vanilla crystal colour, UI_Instance.c:90)
	case AP_CAT_TROPHY:
	case AP_CAT_SAPPHIRE:
	case AP_CAT_GOLD:
	case AP_CAT_PLATINUM:
	case AP_CAT_TOKEN:
	case AP_CAT_GEM:
	case AP_CAT_KEY:
	case AP_CAT_WUMPA:
	case AP_CAT_COUNT:
	case AP_CAT_NONE:
	default:
		return 0;
	}
}

#endif // CTR_AP
#endif // AP_REWARD_POLICY_H
