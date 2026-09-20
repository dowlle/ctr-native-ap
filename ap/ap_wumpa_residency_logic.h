#ifndef AP_WUMPA_RESIDENCY_LOGIC_H
#define AP_WUMPA_RESIDENCY_LOGIC_H

// Freestanding residency, shape and relocation logic for the retail Wumpa Fruit
// model (#222), and the one place that decides whether a Wumpa package may ask
// for model 0x02.
//
// WHY THIS EXISTS
// ---------------
// Lessons Learned §24: a display resolver handed back a model id nobody checked
// was drawable, and the pad silently kept the model it was already showing. The
// retail Wumpa Fruit model (PU_WUMPA_FRUIT = 0x02) is NOT resident on the
// adventure hub: it ships only inside loose-fruit level files, never in any MPK
// pack, so gGT->modelPtr[PU_WUMPA_FRUIT] is null wherever a reward pad is drawn
// and the existing `if (apModel >= 0 && gGT->modelPtr[apModel] != 0)` pattern
// leaves the previous slot model in place.
//
// The fix for an unverifiable premise is to stop depending on it (§29): the
// client owns a harvested copy and parks it wherever the engine left the slot
// empty. This header is the decision core of that design, kept freestanding so a
// host harness can drive the REAL code rather than a re-implementation:
//
//   * the category half -- a Wumpa package resolves to the fruit model ONLY when
//     a fruit model is actually drawable here, and otherwise to the
//     Archipelago-logo marker, a CONCRETE model, not "keep whatever is on the
//     pad";
//   * the harvest half -- the exact shape the copied 12,760-byte span must have,
//     the 21 pointers relocated during the copy, and the bounded failure that
//     rejects anything else;
//   * the animation half -- the instance fields reset when, and only when, an
//     existing instance transitions INTO the fruit model, and cleared on the way
//     out so fruit animation state cannot leak into a later placeholder.
//
// Pointer arithmetic is done on 32-bit VALUES, never on host pointers: the
// retail build is 32-bit and its fixed-up pointers are 32-bit, so the logic takes
// an explicit span/body base and works identically in the 32-bit engine build and
// in a 64-bit host harness. See ap/ap_retail_wumpa.c for the compile-time
// assertion that the production build really is 32-bit.
//
// Freestanding by design, like ap_reward_policy.h and ap_cup_box_policy.h: it
// includes only pure policy headers and links nothing from the game. The
// production unit ap/ap_retail_wumpa.c calls into it, so the harness pins the
// shipped behaviour.
//
// Compiled ONLY when CTR_AP is defined, like the rest of ap/.

#ifdef CTR_AP

#include <string.h>

#include "ap_reward_policy.h" // AP_ItemCat, AP_MODEL_WUMPA, AP_RewardTintForCat
#include "ap_marker_model.h"  // STATIC_AP
#include "ap_items.h"         // AP_CAT_WUMPA

// Harvest state, the same three states ap_retail_crystal.c keeps: a static read
// of the player's own BIGFILE is attempted once, and a failure is sticky so the
// disc is never re-read. UNKNOWN covers "not tried yet", which must resolve
// exactly like FAILED for display purposes: until the harvest succeeds, there is
// no fruit model to show, and the pad must not keep a stale one in the meantime.
typedef enum
{
	AP_WUMPA_RESIDENCY_UNKNOWN = 0,
	AP_WUMPA_RESIDENCY_READY = 1,
	AP_WUMPA_RESIDENCY_FAILED = 2
} AP_WumpaResidency;

// Sentinel a caller must never receive from AP_WumpaAssignModel. It names the
// §24 bug explicitly so a regression that reintroduces "leave the slot alone"
// fails the harness instead of silently passing.
#define AP_WUMPA_MODEL_UNCHANGED (-2)

// ─────────────────────────────────────────────────────────────────────────────
// The compact span, measured from the player's own NTSC-U BIGFILE (entry 117,
// Coco Park LOD3/4, the smallest fruit-bearing LEV) and byte-identical across
// all 24 entries that carry model 0x02.
// ─────────────────────────────────────────────────────────────────────────────

#define AP_WUMPA_SPAN_BYTES 12760
#define AP_WUMPA_HEADER_COUNT 4
#define AP_WUMPA_ANIM_BYTES 11520
#define AP_WUMPA_SLOT_COUNT 21
#define AP_WUMPA_MAX_HEADERS 512 // sanity cap on the model-table walk

// The 21 pointer slots, relative to the copied span, in ascending order. The
// harvest reads them out of the fixed-up source and rewrites each to the
// corresponding destination slot.
static const unsigned int AP_WUMPA_SLOT_OFFSETS[AP_WUMPA_SLOT_COUNT] = {
    0x14,  0x38,  0x40,  0x44,  0x50,  0x78,  0x80,   0x84,   0x90,   0xb8,  0xc0,
    0xc4,  0xd0,  0xf8,  0x100, 0x104, 0x110, 0x138,  0x133c, 0x2154, 0x2b88};

// Their expected targets, also relative to the span. Shape validation rejects a
// graph whose pointers do not match this table, so a corrupt or wrong-shape
// source is refused rather than copied into a live model slot.
static const unsigned int AP_WUMPA_SLOT_TARGETS[AP_WUMPA_SLOT_COUNT] = {
    0x18,  0x13c, 0x234, 0x234, 0x138, 0x1340, 0x13e8, 0x13e8, 0x133c, 0x2158, 0x21c0,
    0x21c0, 0x2154, 0x2b8c, 0x2bac, 0x2bac, 0x2b88, 0x2a4, 0x143c, 0x21f0, 0x2bc0};

// The four headers, in order. Each must carry exactly one 32-frame `spin`.
static const char AP_WUMPA_HEADER_NAMES[AP_WUMPA_HEADER_COUNT][0x10] = {"fruit_hi", "fruit_med", "fruit_low",
                                                                       "fruit_box"};

// The model's own 0x10-byte name field, measured from the retail entry-117 span
// (and byte-identical across all 24 fruit-bearing entries): model id 0x02 is
// named `fruit`, stored as "fruit" + NUL and zero-padded to the field. Validating
// it as well as the id refuses a same-id but wrong-shape source. The 16-byte
// field must hold this name with its terminator INSIDE the field.
#define AP_WUMPA_MODEL_NAME "fruit"

// Failure reason codes for the one redacted log line per failed harvest. Stable
// strings, deliberately the only detail reported: no path, host, pointer or
// player data may reach the log.
typedef enum
{
	AP_WUMPA_FAIL_NONE = 0,
	AP_WUMPA_FAIL_ALLOCATION,
	AP_WUMPA_FAIL_READ,
	AP_WUMPA_FAIL_ENTRY,
	AP_WUMPA_FAIL_POINTER_MAP,
	AP_WUMPA_FAIL_BOUNDS,
	AP_WUMPA_FAIL_SHAPE,
	AP_WUMPA_FAIL_RELOCATION,
	AP_WUMPA_FAIL_HEADER
} AP_WumpaFailReason;

static inline const char *AP_WumpaFailReasonName(AP_WumpaFailReason r)
{
	switch (r)
	{
	case AP_WUMPA_FAIL_ALLOCATION:  return "allocation";
	case AP_WUMPA_FAIL_READ:        return "read";
	case AP_WUMPA_FAIL_ENTRY:       return "entry";
	case AP_WUMPA_FAIL_POINTER_MAP: return "pointer-map";
	case AP_WUMPA_FAIL_BOUNDS:      return "bounds";
	case AP_WUMPA_FAIL_SHAPE:       return "shape";
	case AP_WUMPA_FAIL_RELOCATION:  return "relocation";
	case AP_WUMPA_FAIL_HEADER:      return "header";
	case AP_WUMPA_FAIL_NONE:
	default:                        return "none";
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Category decision.
// ─────────────────────────────────────────────────────────────────────────────

// A fruit model is drawable here when the client harvested its own copy, OR the
// current level already carries the real one at gGT->modelPtr[0x02] (a loose
// fruit track). In the second case the client must step aside and let the level
// keep its own, exactly as the crystal harvest does.
static inline int AP_WumpaModelDrawable(int residency, int levelRetailPresent)
{
	return (residency == AP_WUMPA_RESIDENCY_READY) || (levelRetailPresent != 0);
}

// The category a display surface should actually resolve. A Wumpa package whose
// model is not drawable degrades to AP_CAT_NONE, so every downstream consumer
// (model, tint, ghost, scale) reads the ONE marker decision instead of a
// half-resolved Wumpa one. Every other category passes through untouched.
static inline AP_ItemCat AP_WumpaEffectiveCategory(AP_ItemCat cat, int drawable)
{
	if (cat == AP_CAT_WUMPA && !drawable)
		return AP_CAT_NONE;
	return cat;
}

// The slot-assignment contract, written so a stale model cannot survive.
//
// Returns 1 and writes *outModel when this category must reassign the slot. A
// Wumpa package ALWAYS returns 1: drawable -> model 0x02, otherwise the marker
// (STATIC_AP). It never returns 0 for AP_CAT_WUMPA, so there is no path that
// leaves the pad's previous model in place. For every other category it returns
// 0 and outModel is unchanged, so the caller's own per-category policy applies.
// A null outModel makes the contract a hard 0 rather than a wild write.
static inline int AP_WumpaAssignModel(AP_ItemCat cat, int drawable, int *outModel)
{
	if (cat != AP_CAT_WUMPA)
		return 0;
	if (outModel == 0)
		return 0;

	*outModel = drawable ? AP_MODEL_WUMPA : STATIC_AP;
	return 1;
}

// Tint for the own (non-ghost) presentation. A drawable fruit keeps its natural
// retail colours (0), matching the existing default reward path. An undrawable
// Wumpa package has already degraded to AP_CAT_NONE, whose marker tint is owned
// by the classification path -- but the invariant this gate must preserve is
// that a degraded Wumpa never resolves to colour 0, because an untextured model
// tinted to 0 renders near-black (the §24 / #212 defect family). `markerTint` is
// the classification tint the caller would give the marker; when the caller has
// none yet, this returns a non-zero placeholder rather than 0.
static inline int AP_WumpaResolvedTint(AP_ItemCat cat, int drawable, int markerTint)
{
	if (cat != AP_CAT_WUMPA)
		return AP_RewardTintForCat(cat);
	if (drawable)
		return 0;
	return (markerTint != 0) ? markerTint : 0x00ffffff;
}

// ─────────────────────────────────────────────────────────────────────────────
// Endian helpers and 32-bit pointer bounds.
// ─────────────────────────────────────────────────────────────────────────────

static inline unsigned int AP_WumpaReadU32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static inline int AP_WumpaReadS16(const unsigned char *p)
{
	return (int)(short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

static inline unsigned int AP_WumpaReadU16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

// 1 when a 32-bit value is a four-byte-aligned pointer that lands inside
// [base, base + size).
static inline int AP_WumpaPtrInRange(unsigned int value, unsigned int base, unsigned int size)
{
	if ((value & 3u) != 0)
		return 0;
	if (value < base)
		return 0;
	return (value - base) < size;
}

// Convert an absolute 32-bit pointer to an offset inside [base, base + size).
// Returns 1 and writes *outOffset on success, 0 otherwise.
static inline int AP_WumpaPtrToOffset(unsigned int value, unsigned int base, unsigned int size, unsigned int *outOffset)
{
	if (!AP_WumpaPtrInRange(value, base, size))
		return 0;
	if (outOffset)
		*outOffset = value - base;
	return 1;
}

// 1 when a 0x10-byte name field holds exactly `want` and is NUL-terminated
// inside the field. A field that matches but fills all 0x10 bytes with no
// terminator is refused, as is a field whose terminator comes later than the
// wanted name (for example "wumpa" for the measured "fruit"), so a corrupt or
// wrong-shape source cannot masquerade as the retail name.
static inline int AP_WumpaNameMatches(const unsigned char *field, const char *want)
{
	unsigned int i;

	if (field == 0 || want == 0)
		return 0;

	for (i = 0; i < 0x10u; i++)
	{
		char c = (char)field[i];
		char w = want[i];

		if (c != w)
			return 0;
		if (w == '\0')
			return 1;
	}

	return 0; // the wanted name filled the field without a terminator
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape validation.
//
// `body` is the fixed-up DRAM body (past the four-byte DRAM descriptor word),
// `bodySize` its readable size, `bodyBase` the 32-bit address the fixup added
// (the address `body` itself lives at), and `fruitOffset` the offset of the
// `struct Model` inside the body. On success writes the span bounds (lo/hi
// relative to body) to *outLo/*outHi and returns AP_WUMPA_FAIL_NONE.
//
// The rules are exactly the ones the review fixed: model name `fruit`, model id
// 0x02, four headers named fruit_hi/med/low/box, one 32-frame `spin` animation
// each, 11,520 total animation bytes, no texture reference above zero, a
// 12,760-byte span, and every pointer inside the body.
static inline AP_WumpaFailReason AP_WumpaValidateShape(const unsigned char *body, unsigned int bodySize, unsigned int bodyBase,
                                                unsigned int fruitOffset, unsigned int *outLo, unsigned int *outHi)
{
	unsigned int headerOffset, lo, hi, totalAnimBytes = 0;
	int numHeaders, h, i;

	if (body == 0 || bodySize < 0x18u)
		return AP_WUMPA_FAIL_BOUNDS;
	if (fruitOffset > bodySize || bodySize - fruitOffset < 0x18u)
		return AP_WUMPA_FAIL_BOUNDS;

	// struct Model: name[0x10], s16 id, s16 numHeaders, struct ModelHeader *headers
	// The name is checked as well as the id: a file with the right id but the
	// wrong name is not the measured fruit model and must not be copied.
	if (!AP_WumpaNameMatches(body + fruitOffset, AP_WUMPA_MODEL_NAME))
		return AP_WUMPA_FAIL_SHAPE;

	if (AP_WumpaReadS16(body + fruitOffset + 0x10) != (int)AP_MODEL_WUMPA)
		return AP_WUMPA_FAIL_SHAPE;

	numHeaders = AP_WumpaReadS16(body + fruitOffset + 0x12);
	if (numHeaders != AP_WUMPA_HEADER_COUNT)
		return AP_WUMPA_FAIL_HEADER;

	if (!AP_WumpaPtrToOffset(AP_WumpaReadU32(body + fruitOffset + 0x14), bodyBase, bodySize, &headerOffset))
		return AP_WUMPA_FAIL_BOUNDS;
	if (headerOffset + (unsigned int)numHeaders * 0x40u > bodySize)
		return AP_WUMPA_FAIL_BOUNDS;

	// The span begins at the model and reaches at least through the header array.
	lo = fruitOffset;
	hi = fruitOffset + 0x18u + (unsigned int)numHeaders * 0x40u;

	for (h = 0; h < numHeaders; h++)
	{
		unsigned int ho = headerOffset + (unsigned int)h * 0x40u;
		unsigned int cmdOff = 0, colOff = 0, animArrayOff = 0;
		unsigned int numAnim, cmdCount = 0, cmdEnd = 0, colEnd = 0, animEnd = 0;
		int hasCmd, hasCol;

		// Header name must be the expected one, NUL-terminated inside 0x10.
		{
			int mismatch = 0;

			for (i = 0; i < 0x10; i++)
			{
				char c = (char)body[ho + (unsigned int)i];
				char want = AP_WUMPA_HEADER_NAMES[h][i];

				if (c != want)
				{
					mismatch = 1;
					break;
				}
				if (want == '\0')
					break;
			}
			if (mismatch)
				return AP_WUMPA_FAIL_HEADER;
		}

		hasCmd = (AP_WumpaReadU32(body + ho + 0x20) != 0);
		hasCol = (AP_WumpaReadU32(body + ho + 0x2c) != 0);
		numAnim = AP_WumpaReadU32(body + ho + 0x34);

		if (hasCmd && !AP_WumpaPtrToOffset(AP_WumpaReadU32(body + ho + 0x20), bodyBase, bodySize, &cmdOff))
			return AP_WUMPA_FAIL_BOUNDS;
		if (hasCol && !AP_WumpaPtrToOffset(AP_WumpaReadU32(body + ho + 0x2c), bodyBase, bodySize, &colOff))
			return AP_WUMPA_FAIL_BOUNDS;
		if (!AP_WumpaPtrToOffset(AP_WumpaReadU32(body + ho + 0x38), bodyBase, bodySize, &animArrayOff))
			return AP_WUMPA_FAIL_BOUNDS;

		// One animation per header, whose every frame contributes bytes.
		if (numAnim != 1u)
			return AP_WUMPA_FAIL_SHAPE;
		{
			unsigned int animOff;
			unsigned int ao;
			unsigned int numFrames;
			int frameSize;

			if (!AP_WumpaPtrToOffset(AP_WumpaReadU32(body + animArrayOff), bodyBase, bodySize, &animOff))
				return AP_WUMPA_FAIL_BOUNDS;
			ao = animOff;
			if (ao > bodySize || bodySize - ao < 0x18u)
				return AP_WUMPA_FAIL_BOUNDS;

			// The verified animation is the 32-frame `spin`; any other name is a
			// wrong-shape source.
			{
				int mismatch = 0;

				for (i = 0; i < 0x10; i++)
				{
					char c = (char)body[ao + (unsigned int)i];

					if (c != "spin"[i])
					{
						mismatch = 1;
						break;
					}
					if ("spin"[i] == '\0')
						break;
				}
				if (mismatch)
					return AP_WUMPA_FAIL_SHAPE;
			}

			numFrames = AP_WumpaReadU16(body + ao + 0x10);
			frameSize = AP_WumpaReadS16(body + ao + 0x12);

			if (numFrames != 32u)
				return AP_WUMPA_FAIL_SHAPE;
			if (frameSize <= 0)
				return AP_WUMPA_FAIL_SHAPE;
			if (ao + 0x18u > bodySize || (unsigned int)frameSize > (bodySize - ao - 0x18u) / numFrames)
				return AP_WUMPA_FAIL_BOUNDS;
			totalAnimBytes += numFrames * (unsigned int)frameSize;
			animEnd = ao + 0x18u + numFrames * (unsigned int)frameSize;
			if (animEnd > bodySize)
				return AP_WUMPA_FAIL_BOUNDS;
			if (ao < lo)
				lo = ao;
		}

		// Command list and colour table must be inside the body, and the command
		// list must not name a texture: the fruit is untextured and needs no VRAM.
		// The colour table is sized by the SAME command count, exactly as the
		// retail layout records it.
		if (hasCmd)
		{
			unsigned int p;

			if (cmdOff > bodySize || bodySize - cmdOff < 8u)
				return AP_WUMPA_FAIL_BOUNDS;
			cmdCount = AP_WumpaReadU32(body + cmdOff);
			if (cmdCount > (bodySize - cmdOff) / 4u)
				return AP_WUMPA_FAIL_BOUNDS;
			cmdEnd = cmdOff + (cmdCount + 2u) * 4u;
			if (cmdEnd > bodySize)
				return AP_WUMPA_FAIL_BOUNDS;

			for (p = cmdOff + 4u; p + 4u <= bodySize; p += 4u)
			{
				unsigned int word = AP_WumpaReadU32(body + p);

				if (word == 0xffffffffu)
					break;
				if ((word >> 16) == 0u)
					continue; // not a textured primitive
				if ((word & 0x1ffu) != 0u)
					return AP_WUMPA_FAIL_SHAPE; // texture index above zero
			}
		}
		if (hasCol)
		{
			if (colOff > bodySize || bodySize - colOff < 4u)
				return AP_WUMPA_FAIL_BOUNDS;
			if (cmdCount > (bodySize - colOff) / 4u)
				return AP_WUMPA_FAIL_BOUNDS;
			colEnd = colOff + cmdCount * 4u;
			if (colEnd > bodySize)
				return AP_WUMPA_FAIL_BOUNDS;
		}

		if (ho < lo)
			lo = ho;
		if (hasCmd && cmdOff < lo)
			lo = cmdOff;
		if (hasCol && colOff < lo)
			lo = colOff;
		if (animArrayOff < lo)
			lo = animArrayOff;
		if (cmdEnd > hi)
			hi = cmdEnd;
		if (colEnd > hi)
			hi = colEnd;
		if (animEnd > hi)
			hi = animEnd;
	}

	// The span is fixed-width in every entry that carries the fruit, and the
	// four headers between them carry 11,520 bytes of animation. A different
	// total is a wrong-shape source and is refused.
	if (hi - lo != AP_WUMPA_SPAN_BYTES)
		return AP_WUMPA_FAIL_SHAPE;
	if (totalAnimBytes != AP_WUMPA_ANIM_BYTES)
		return AP_WUMPA_FAIL_SHAPE;

	if (outLo)
		*outLo = lo;
	if (outHi)
		*outHi = hi;
	return AP_WUMPA_FAIL_NONE;
}

// Validate the pointer slots inside the span and relocate them into a
// destination copy. This is the two-phase contract the review requires: validate
// ALL pointers first, then copy and relocate, so a destination is never
// partially published.
//
//   src, srcBase  - base of the fixed-up source span, and the 32-bit address the
//                   fixup added (so stored slot values are srcBase + relative)
//   spanBytes     - AP_WUMPA_SPAN_BYTES
//   dst, dstBase  - base of the destination span copy, and the 32-bit address
//                   the relocated pointers must use
//
// Returns AP_WUMPA_FAIL_NONE on success. On any failure dst is left unwritten.
static inline AP_WumpaFailReason AP_WumpaRelocate(const unsigned char *src, unsigned int srcBase, unsigned int spanBytes,
                                           unsigned char *dst, unsigned int dstBase)
{
	unsigned int i;

	if (src == 0 || dst == 0)
		return AP_WUMPA_FAIL_RELOCATION;
	if (spanBytes != AP_WUMPA_SPAN_BYTES)
		return AP_WUMPA_FAIL_BOUNDS;

	// Phase 1: validate every slot and its target.
	for (i = 0; i < AP_WUMPA_SLOT_COUNT; i++)
	{
		unsigned int slotOff = AP_WUMPA_SLOT_OFFSETS[i];
		unsigned int value, rel;

		if (slotOff > spanBytes || spanBytes - slotOff < 4u)
			return AP_WUMPA_FAIL_BOUNDS;

		value = AP_WumpaReadU32(src + slotOff);
		if (!AP_WumpaPtrInRange(value, srcBase, spanBytes))
			return AP_WUMPA_FAIL_BOUNDS;

		rel = value - srcBase;
		if (rel >= spanBytes)
			return AP_WUMPA_FAIL_RELOCATION;

		// The fixed-up graph must match the measured target table. A different
		// shape would still relocate inside the span but would not be the fruit
		// model this loader validated, so it is refused here.
		if (rel != AP_WUMPA_SLOT_TARGETS[i])
			return AP_WUMPA_FAIL_RELOCATION;

		if (AP_WUMPA_SLOT_TARGETS[i] > spanBytes || spanBytes - AP_WUMPA_SLOT_TARGETS[i] < 4u)
			return AP_WUMPA_FAIL_BOUNDS;
	}

	// Phase 2: copy the whole span, then rewrite each pointer slot to the
	// destination base plus the same relative target.
	memcpy(dst, src, spanBytes);
	for (i = 0; i < AP_WUMPA_SLOT_COUNT; i++)
	{
		unsigned int newPtr = dstBase + AP_WUMPA_SLOT_TARGETS[i];
		unsigned int off = AP_WUMPA_SLOT_OFFSETS[i];

		dst[off + 0u] = (unsigned char)(newPtr & 0xffu);
		dst[off + 1u] = (unsigned char)((newPtr >> 8) & 0xffu);
		dst[off + 2u] = (unsigned char)((newPtr >> 16) & 0xffu);
		dst[off + 3u] = (unsigned char)((newPtr >> 24) & 0xffu);
	}

	return AP_WUMPA_FAIL_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Animation transition.
//
// When an existing instance is swapped onto the fruit model its animation state
// is whatever the placeholder it was born as carried. The fruit ships a 32-frame
// `spin` per header and retail loose fruit drives it with animIndex 0 and
// ANIM_LOOP; without the loop flag the spin is frozen on frame 0, and leaving
// ANIM_STOP_AT_END set alongside ANIM_LOOP selects the ping-pong branch instead
// of ordinary looping. So on entry reset the four fields, and on exit clear both
// animation bits so fruit state cannot leak into the next placeholder. This runs
// on the model TRANSITION only; a per-tick reset would freeze the fruit on frame
// zero.
// ─────────────────────────────────────────────────────────────────────────────

#define AP_WUMPA_ANIM_LOOP 0x10u
#define AP_WUMPA_ANIM_STOP_AT_END 0x20u

static inline int AP_WumpaIsFruitModel(int modelID)
{
	return modelID == (int)AP_MODEL_WUMPA;
}

// 1 when a swap into `newModelID` is a transition INTO the fruit from something
// that was not already the fruit. Fruit -> fruit leaves the animation running.
static inline int AP_WumpaShouldResetAnim(int oldModelID, int newModelID)
{
	return AP_WumpaIsFruitModel(newModelID) && !AP_WumpaIsFruitModel(oldModelID);
}

// 1 when a swap away from the fruit must clear the fruit animation bits so the
// destination placeholder does not inherit a half-driven spin.
static inline int AP_WumpaShouldClearAnim(int oldModelID, int newModelID)
{
	return AP_WumpaIsFruitModel(oldModelID) && !AP_WumpaIsFruitModel(newModelID);
}

// Apply the fruit entry reset to an instance's animation state. `flags` is passed
// by pointer so the two anim bits are edited in place.
static inline void AP_WumpaApplyFruitAnim(unsigned char *animIndex, short *animFrame, short *vertSplit, unsigned int *flags)
{
	if (animIndex)
		*animIndex = 0;
	if (animFrame)
		*animFrame = 0;
	if (vertSplit)
		*vertSplit = 0;
	if (flags)
	{
		*flags &= ~AP_WUMPA_ANIM_STOP_AT_END;
		*flags |= AP_WUMPA_ANIM_LOOP;
	}
}

// Clear both animation bits on the way OUT of the fruit.
static inline void AP_WumpaClearFruitAnim(unsigned int *flags)
{
	if (flags)
		*flags &= ~(AP_WUMPA_ANIM_LOOP | AP_WUMPA_ANIM_STOP_AT_END);
}

#endif // CTR_AP
#endif // AP_WUMPA_RESIDENCY_LOGIC_H
