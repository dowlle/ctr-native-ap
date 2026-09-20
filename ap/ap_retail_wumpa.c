// Harvest the retail Wumpa Fruit model (PU_WUMPA_FRUIT, 0x02) out of the
// player's own game data so #222 can draw it on surfaces where the engine never
// loads it. See ap_retail_wumpa.h for the design contract.
//
// WHY THE COMPACT COPY
// --------------------
// The fruit is not in any MPK pack; it lives only in loose-fruit level files
// (24 BIGFILE entries, all LEVs). The whole-level retention the crystal harvest
// uses would cost a 413,696-byte buffer for the smallest fruit-bearing LEV
// (entry 117, Coco Park LOD3/4). Every one of those 24 entries instead holds a
// BYTE-IDENTICAL 12,760-byte self-contained block: the model struct, four
// headers (fruit_hi / fruit_med / fruit_low / fruit_box), their command lists
// and colour tables, the animation pointer arrays, four 32-frame `spin`
// animations, and 11,520 bytes of frame data. All 21 DRAM pointer slots inside
// that block point inside it and nowhere else, so the copy can be relocated once
// and then kept for the process lifetime. That is a ~32x saving and the reason
// this loader reads a level file but retains only the model.
//
// The fruit is UNTEXTURED (every command list's texture index is zero, measured
// from the same data), so no VRAM page, CLUT, atlas or sideload work belongs
// here. Nothing is rewritten: the copied graph is drawn by the ordinary retail
// fruit path.
//
// A DIGRESSION ON POINTERS. The native build is 32-bit (asserted below) and the
// DRAM fixup stores 32-bit pointers, so all of the pointer arithmetic is done on
// 32-bit values in ap_wumpa_residency_logic.h. The production-specific work here
// is only: read the bytes, find the model, hand the fixup'd body to the
// harness-pinned logic, and keep the result.

#ifdef CTR_AP

#include <common.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_retail_wumpa.h"
#include "ap_retail_asset.h"          // AP_RetailAsset_ReadSubfile (shared with #256)
#include "ap_hooks.h"                 // AP_LogLine
#include "ap_wumpa_residency_logic.h" // the harness-pinned decisions

// The native build is explicitly 32-bit (CMakeLists.txt:17-21) and the retail
// pointer graph is 32-bit. Making this loader work in a 64-bit process is not a
// goal; failing the compile here is better than silently mis-relocating.
CTR_STATIC_ASSERT(sizeof(void *) == 4);

// Retail BIGFILE entry 117, the smallest verified fruit-bearing LEV (Coco Park
// LOD3/4, 413,068 bytes, sector-rounded to 413,696). Its model table carries
// PU_WUMPA_FRUIT (0x02).
#define AP_WUMPA_SRC_ENTRY 117
#define AP_WUMPA_TEMP_BYTES 413696

// Sanity cap on the level model table walk. A LEV stores numModels at Level+0x14
// and its model-pointer array at Level+0x18.
#define AP_WUMPA_MAX_MODELS 4096

// Four-byte-aligned storage for the compact copy. 12,760 is a multiple of four,
// so an unsigned-int array gives the alignment the relocated pointers need
// without a compiler-specific attribute. Kept for the life of the process on
// purpose: after relocation the model's internals point into it, and it is a
// static buffer rather than the engine's bump allocator, which has no free.
static unsigned int s_wumpaSpanStorage[AP_WUMPA_SPAN_BYTES / 4];
#define s_wumpaSpan ((unsigned char *)s_wumpaSpanStorage)

static int             s_wumpaState; // AP_WumpaResidency
static unsigned int    s_wumpaModelOffset; // the model struct's offset in the span
static struct Model   *s_wumpaModel;

// ── allocation seam ─────────────────────────────────────────────────────────
// The temporary read buffer is allocated through these pointers so a host
// harness that includes this translation unit can inject allocation failure and
// count every alloc/free on every path (tools/test-wumpa-residency.c).
// Production leaves them at the C allocator.
static void *AP_WumpaAllocDefault(unsigned long n)
{
	return malloc((size_t)n);
}

static void AP_WumpaFreeDefault(void *p)
{
	free(p);
}

static void *(*s_wumpaAlloc)(unsigned long) = AP_WumpaAllocDefault;
static void (*s_wumpaFree)(void *) = AP_WumpaFreeDefault;

// One redacted failure line for the whole attempt, then sticky failure. The
// reason is a stable code only: no path, host, pointer or player data.
static void AP_WumpaFail(AP_WumpaFailReason reason)
{
	char msg[96];

	s_wumpaState = AP_WUMPA_RESIDENCY_FAILED;
	snprintf(msg, sizeof msg, "[AP WUMPA] harvest failed (%s); packages keep the marker\n",
	         AP_WumpaFailReasonName(reason));
	AP_LogLine(msg);
}

// Walk the level model table for model 0x02, returning its offset in the body.
// `bodyBase` is the 32-bit address the DRAM fixup added, so stored pointers are
// absolute and are converted back to offsets with the same base.
static AP_WumpaFailReason AP_WumpaFindFruit(const unsigned char *body, unsigned int bodySize, unsigned int bodyBase,
                                            unsigned int *outFruitOffset)
{
	unsigned int numModels, arrayOffset, i;

	if (bodySize < 0x1cu)
		return AP_WUMPA_FAIL_BOUNDS;

	numModels = AP_WumpaReadU32(body + 0x14);
	if (numModels == 0 || numModels > AP_WUMPA_MAX_MODELS)
		return AP_WUMPA_FAIL_ENTRY;

	if (!AP_WumpaPtrToOffset(AP_WumpaReadU32(body + 0x18), bodyBase, bodySize, &arrayOffset))
		return AP_WUMPA_FAIL_BOUNDS;
	if (arrayOffset > bodySize || numModels > (bodySize - arrayOffset) / 4u)
		return AP_WUMPA_FAIL_BOUNDS;

	for (i = 0; i < numModels; i++)
	{
		unsigned int modelPtr = AP_WumpaReadU32(body + arrayOffset + i * 4u);
		unsigned int modelOffset;

		if (modelPtr == 0)
			break; // end of the list

		if (!AP_WumpaPtrToOffset(modelPtr, bodyBase, bodySize, &modelOffset))
			return AP_WUMPA_FAIL_BOUNDS;
		if (modelOffset > bodySize || bodySize - modelOffset < 0x18u)
			return AP_WUMPA_FAIL_BOUNDS;

		if (AP_WumpaReadS16(body + modelOffset + 0x10) == (int)AP_MODEL_WUMPA)
		{
			*outFruitOffset = modelOffset;
			return AP_WUMPA_FAIL_NONE;
		}
	}

	return AP_WUMPA_FAIL_ENTRY;
}

// The whole harvest: allocate the temporary read buffer, read entry 117, find the
// fruit, validate its shape, and relocate the span. Frees the temporary buffer on
// EVERY path. On success the static span is published and one informative line is
// logged; on failure the reason is logged once and the state is sticky.
static void AP_RetailWumpaHarvest(void)
{
	unsigned char    *temp;
	unsigned char    *body;
	int               size = 0;
	unsigned int      bodyBase;
	unsigned int      fruitOffset = 0, lo = 0, hi = 0;
	AP_WumpaFailReason reason;
	char              msg[160];

	temp = (unsigned char *)s_wumpaAlloc(AP_WUMPA_TEMP_BYTES);
	if (temp == 0)
	{
		AP_WumpaFail(AP_WUMPA_FAIL_ALLOCATION);
		return;
	}

	body = AP_RetailAsset_ReadSubfile(AP_WUMPA_SRC_ENTRY, 1, temp, AP_WUMPA_TEMP_BYTES, &size);
	if (body == 0 || size <= 0)
	{
		s_wumpaFree(temp);
		AP_WumpaFail(AP_WUMPA_FAIL_READ);
		return;
	}

	bodyBase = (unsigned int)(uintptr_t)body;
	reason = AP_WumpaFindFruit(body, (unsigned int)size, bodyBase, &fruitOffset);
	if (reason == AP_WUMPA_FAIL_NONE)
	{
		reason = AP_WumpaValidateShape(body, (unsigned int)size, bodyBase, fruitOffset, &lo, &hi);
	}
	if (reason == AP_WUMPA_FAIL_NONE)
	{
		reason = AP_WumpaRelocate(body + lo, bodyBase + lo, hi - lo, s_wumpaSpan,
		                          (unsigned int)(uintptr_t)s_wumpaSpan);
	}

	// The temporary buffer is freed on every path, success included: once the
	// span has been copied and relocated it is not referenced again.
	s_wumpaFree(temp);

	if (reason != AP_WUMPA_FAIL_NONE)
	{
		AP_WumpaFail(reason);
		return;
	}

	s_wumpaModelOffset = fruitOffset - lo;
	s_wumpaModel = (struct Model *)(s_wumpaSpan + s_wumpaModelOffset);
	s_wumpaState = AP_WUMPA_RESIDENCY_READY;

	snprintf(msg, sizeof msg, "[AP WUMPA] harvested retail '%-.16s': entry %d, %d bytes, %d header(s)\n",
	         s_wumpaModel->name, AP_WUMPA_SRC_ENTRY, AP_WUMPA_SPAN_BYTES, (int)s_wumpaModel->numHeaders);
	AP_LogLine(msg);
}

void AP_RetailWumpa_Register(struct GameTracker *gGT)
{
	if (gGT == 0 || s_wumpaState == AP_WUMPA_RESIDENCY_FAILED)
		return;

	if (s_wumpaState == AP_WUMPA_RESIDENCY_UNKNOWN)
	{
		// The read blocks on disc IO, so it waits for a frame with no load in
		// flight rather than stealing time from one. Same gate the crystal
		// harvest uses, plus the bigfile check the review fixed.
		if (sdata == 0 || sdata->ptrBigfile1 == 0)
			return;
		if (sdata->Loading.stage != LOAD_IDLE)
			return;

		AP_RetailWumpaHarvest();
		if (s_wumpaState != AP_WUMPA_RESIDENCY_READY)
			return;
	}

	// Slot 0x02 is a normal per-level slot and LibraryOfModels_Clear wipes it on
	// every transition, which is why this reasserts every frame. Reassert ONLY
	// while the slot is absent, so a loose-fruit track keeps its own model, whose
	// pointers belong to the level's own data.
	if (gGT->modelPtr[AP_MODEL_WUMPA] == 0)
		gGT->modelPtr[AP_MODEL_WUMPA] = s_wumpaModel;
}

int AP_RetailWumpa_IsReady(void)
{
	return s_wumpaState == AP_WUMPA_RESIDENCY_READY;
}

int AP_RetailWumpa_IsDrawable(struct GameTracker *gGT)
{
	int levelPresent = (gGT != 0 && gGT->modelPtr[AP_MODEL_WUMPA] != 0) ? 1 : 0;

	return AP_WumpaModelDrawable(s_wumpaState, levelPresent);
}

#endif // CTR_AP
