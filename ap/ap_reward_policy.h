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
// token / gem / key), a CTR progression crystal (every CTR progression family
// with no vanilla model of its own -- see AP_ItemCategory for the list), or a
// Wumpa Fruit
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
// writer needs colorRGBA 0). Marker tint is AP_MarkerTintForFlags below.
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

// ---------------------------------------------------------------------------
// MARKER TINT (#124, revised by #212). Moved here from ap_hooks.c so the
// harness pins the colours a marker can actually wear: the near-black failure
// mode below is invisible to a policy test that only sees category -> model.
// ---------------------------------------------------------------------------

// Packed as (R << 0x14) | (G << 0xc) | (B << 0x4), matching the relic tints.
#define AP_PACK_TINT(r, g, b) \
	(int)(((unsigned)(r) << 0x14) | ((unsigned)(g) << 0xc) | ((unsigned)(b) << 0x4))

// In-game-legible approximations of the standard AP scheme, not literal hex
// values from it: these are read on a small spinning model at PSX draw distance,
// so they are pushed for separation rather than fidelity. Filler cyan and useful
// slate blue are the pair most at risk of collapsing into each other, so they are
// split hard on the green channel. Final values are Stef's in-game call.
#define AP_TINT_PROGRESSION AP_PACK_TINT(0xc0, 0x88, 0xf0) // plum
#define AP_TINT_USEFUL      AP_PACK_TINT(0x50, 0x78, 0xe0) // slate blue
#define AP_TINT_FILLER      AP_PACK_TINT(0x40, 0xe8, 0xe0) // cyan
#define AP_TINT_TRAP        AP_PACK_TINT(0xff, 0x80, 0x60) // salmon

// #212 point 5: the ONE colour every marker wears when the seed asks for AP item
// types to stay a surprise (ctr_options.ap_item_type_colors = 0). Greyish white,
// Stef's stated candidate: it is the only neutral that stays neutral under the
// marker's multiplicative-ish tint lerp without collapsing toward any class hue,
// and it cannot be confused with a gem-cup gem because it is never on a gem.
// Never modulate to 0 here (near-black bug) -- the value stays well clear of it.
#define AP_TINT_UNIFORM     AP_PACK_TINT(0xd0, 0xd0, 0xc8) // greyish white

// AP item flags: bit0 = progression, bit1 = useful, bit2 = trap, 0 = filler.
// The precedence that resolves them is the freestanding AP_ItemFlagsClass
// (ap_item_flags.h), shared with the hub feed so the two presentations can
// never disagree about a class.
static inline int AP_ClassTint(unsigned flags)
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
// toggle (#212 point 5). typeColorsEnabled is ctr_options.ap_item_type_colors as
// parsed: absent or nonzero -> the class tints above, i.e. exactly the display
// every shipped client renders; 0 -> the single uniform colour, so the AP item
// pool stays a surprise.
//
// The key is EXPECTED but not required: the apworld half of #212 lands after the
// 0.2.0 name freeze, and a seed generated before it carries no such key. That is
// why the parse defaults to 1 rather than to the new behaviour -- an old seed on
// this client must glow the way it does today, not silently switch to grey.
//
// Every answer this can give is a real colour; none of them is 0. A marker drawn
// at colorRGBA 0 renders near-black (it is untextured and its own colours are
// lerped toward colorRGBA), which is the #212 defect this policy exists to make
// unreachable, so the harness pins that too.
static inline int AP_MarkerTintForFlags(unsigned flags, int typeColorsEnabled)
{
	if (!typeColorsEnabled)
		return AP_TINT_UNIFORM;
	return AP_ClassTint(flags);
}

// The colour a marker wears for a reward that FELL BACK to it. `keptCat` is the
// model-keeping category the surface could not draw, AP_CAT_NONE for ordinary
// marker material.
//
// Every CTR progression reward with no vanilla model of its own is a PURPLE
// crystal (AP_ItemCategory lists the families). Where the crystal model is not
// resident the reward becomes a marker, and that marker must still read as the
// same reward.
//
// Reading the item's AP class instead would split one category into three,
// because the datapackage does not classify these families alike:
// worlds/ctr/data/items.json ships the 68 ladder items and the 16 racers as
// `useful` (worlds/ctr/progressive_capability.py says so in as many words --
// "none of these items are `progression`; they ride as `useful`
// filler-replacement content") while the 48 letters, the 11 weapon unlocks and
// Gas Pedal are `progression`. On the class path the racers would come out slate
// blue and the letters plum, and the ladder would change colour depending only
// on which model pack the surface happens to have loaded. One reward family,
// three looks.
//
// Plum for all of them: it is the marker's own progression colour, so the
// fallback stays inside the palette #212 already defined rather than inventing a
// hue, and it is the right colour on the merits, since the ruling that put these
// items on the crystal is precisely that they ARE CTR progression.
//
// Only the crystal is claimed here. A WUMPA package that falls back is filler,
// and the class tint already answers cyan for it -- the #212 case this policy
// was written for -- so it keeps taking the class answer.
//
// The seed's surprise toggle still wins: a per-category colour IS an item-type
// leak, which is exactly what ap_item_type_colors = 0 exists to turn off.
static inline int AP_MarkerFallbackTint(AP_ItemCat keptCat, unsigned flags,
                                        int typeColorsEnabled)
{
	if (!typeColorsEnabled)
		return AP_TINT_UNIFORM;
	if (keptCat == AP_CAT_CRYSTAL)
		return AP_TINT_PROGRESSION;
	return AP_ClassTint(flags);
}

// ---------------------------------------------------------------------------
// PRESENTATION -- which of the three treatments one scouted reward gets (#212).
// One resolver feeds the model, the tint and the ghost flag, so those three can
// never disagree about what a slot is showing.
// ---------------------------------------------------------------------------

#define AP_PAD_DISP_VANILLA 0 // my own OG CTR reward -> its vanilla model, untouched
#define AP_PAD_DISP_GHOST   1 // another CTR player's OG reward -> same model, ghosted
#define AP_PAD_DISP_MARKER  2 // everything else -> the Archipelago-logo marker
#define AP_PAD_DISP_NONE    3 // undecidable right now -> leave the placeholder alone

// Presentation for one scouted reward.
//
//   cat             its category, already resolved from the item id;
//   own             1 when the reward belongs to this slot's player;
//   modelDrawable   1 when the category's own model can be drawn on THIS surface
//                   right now (see AP_RewardModelForCat + the caller's residency
//                   check). gGT->modelPtr[] is refilled per level, so a category
//                   model that exists in the engine is not necessarily loaded
//                   here: no model pack carries PU_WUMPA_FRUIT (#222), and the
//                   adventure pack behind the arenas / cutscenes / garage carries
//                   no crystal (#219) -- see the residency note in ap_hooks.c for
//                   which pack each surface runs on;
//   markerAvailable 1 when the Archipelago-logo marker model is parked.
//
// A model-keeping category whose model is NOT drawable falls back to the marker
// rather than to "keep whatever model the instance already had". That fallback
// is the #212 near-black defect: the pad slot kept the marker model it was
// showing a moment earlier while the tint was resolved as a model-keeping
// reward, i.e. no tint at all, and an untextured marker at colorRGBA 0 renders
// near-black. Falling back to the marker keeps model and tint on the same
// answer, and the marker is the presentation #212 group 3 already prescribes
// for anything the pad cannot show as itself.
static inline int AP_RewardPresentation(AP_ItemCat cat, int own, int modelDrawable,
                                        int markerAvailable)
{
	if (AP_RewardKeepsModel(cat) && modelDrawable)
		return own ? AP_PAD_DISP_VANILLA : AP_PAD_DISP_GHOST;

	// Marker material. Without the marker model parked we have nothing to show it
	// with (the white gem is retired), so the slot keeps its placeholder rather
	// than inventing a stand-in that would lie about the reward.
	return markerAvailable ? AP_PAD_DISP_MARKER : AP_PAD_DISP_NONE;
}

// Final colorRGBA for a pad-glow slot, 0 = keep the caller's natural per-model
// colour. `cat` is only read on the VANILLA path; `flags` and typeColorsEnabled
// only on the MARKER path.
static inline int AP_RewardPadTint(int kind, AP_ItemCat cat, unsigned flags,
                                   int typeColorsEnabled)
{
	// The Archipelago-logo marker carries the classification (or the one uniform
	// surprise colour) in its tint; the marker's own colours are neutral greys so
	// this lerps cleanly. Same answer for my own non-OG items as for anyone
	// else's -- #212 draws no local/foreign line here. A reward that fell back to
	// the marker from a model-keeping category keeps that category's identity
	// instead (AP_MarkerFallbackTint); `cat` is AP_CAT_NONE for everything that
	// was marker material to begin with, which is the classification path above.
	if (kind == AP_PAD_DISP_MARKER)
		return AP_MarkerFallbackTint(cat, flags, typeColorsEnabled);

	// A GHOSTED peer reward takes NO tint: the ghost writer needs colorRGBA 0 (see
	// the ghost block in AH_WarpPad_ThTick), and the caller forces it to 0 anyway.
	if (kind != AP_PAD_DISP_VANILLA)
		return 0;

	switch (cat)
	{
	case AP_CAT_SAPPHIRE:
		return 0x020a5ff0; // blue (vanilla relic colour)
	case AP_CAT_GOLD:
		return 0x0ffc6290; // gold
	case AP_CAT_PLATINUM:
		return 0x0ebebf50; // platinum / pale silver
	// #219: an OWN crystal keeps the OG purple crystal colour (UI_Instance.c:90
	// sets the same 0xd22fff0 at birth for a menu crystal). A PEER crystal never
	// reaches this case -- the ghosted kind returns 0 above, and the ghost writer
	// needs colorRGBA 0 anyway.
	case AP_CAT_CRYSTAL:
		return (int)AP_RewardTintForCat(AP_CAT_CRYSTAL);
	case AP_CAT_TROPHY:
	case AP_CAT_TOKEN:
	case AP_CAT_GEM:
	case AP_CAT_KEY:
	case AP_CAT_COUNT:
	case AP_CAT_WUMPA:
	case AP_CAT_NONE:
	default:
		// Own gem / trophy / token / key / wumpa -> keep the natural colour path
		// the glow switch already applies per model (gem-cup colour, untinted
		// trophy, token group colour, golden key, the fruit's own colours). Own
		// filler and traps no longer reach here at all: they are marker items now,
		// handled above.
		return 0;
	}
}

#endif // CTR_AP
#endif // AP_REWARD_POLICY_H
