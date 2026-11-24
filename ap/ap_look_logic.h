#ifndef AP_LOOK_LOGIC_H
#define AP_LOOK_LOGIC_H

// Character looks (Joe looks design, rulings 2026-11-23 22:45 and 22:46 CET).
//
// A look is an alternative driver model for one racer, delivered as a filler
// item. Komodo Moe for Komodo Joe ("Joe Mama") is the first. The rules:
//   * every look is its own repeatable filler item;
//   * a newly arrived copy makes that look the racer's chosen look (the newest
//     wins); a reconnect replay never changes the choice;
//   * an OPTIONS row per racer steps through ORIGINAL and the looks this seed
//     has delivered; a saved look this seed has not delivered shows and drives
//     as ORIGINAL;
//   * the YAML option `character_looks` lists the looks allowed in the pool.
//
// The table below is the one place a look is declared. Adding a look is one
// row here, its files under assets/looks/<folder>/, one minted apworld item and
// one key in the apworld option. A look for a racer that has no OPTIONS row yet
// also needs that row (see AP_LookConfigValue in ap/ap_looks.c). The table is
// shaped so a later custom character (its own model, name, icon, voice) can
// reuse the asset folder, the pin and the loader.
//
// Everything in this header is freestanding (no engine types), so the host
// harness tools/test-character-looks.c checks it directly against the bundled
// file.

#include <stddef.h>

#define AP_LOOK_RACER_JOE 11 // engine character id of Komodo Joe

// The value an OPTIONS look row stores when the racer wears its own model.
#define AP_LOOK_ORIGINAL 0

typedef struct
{
	const char *itemName;   // the apworld item, for logs
	const char *label;      // OPTIONS row value, upper case, FONT_SMALL
	int racer;              // engine character id the look is for
	int itemIndex;          // native item index (AP id - 35010000), also the
	                        // stable value the OPTIONS row saves
	const char *assetRel;   // file under the asset dir
	const char *sha256;     // pin of the exact bundled bytes
	unsigned size;          // exact byte size
	int triangles;          // expected triangle count, a plausibility check
} AP_LookDef;

// Komodo Moe: Lockheart's model, cut unchanged from BIGFILE entry 256 of his
// Komodo_Moe.xdelta patch (JL1, 2026-11-23). Credited in THIRD_PARTY_NOTICES.md
// and the credits roll.
#define AP_LOOK_KOMODO_MOE_ITEM_INDEX 215
#define AP_LOOK_KOMODO_MOE_SHA256 "45634686aaa0ede40a2fe7025911ba7cf89199212057d689bc2289795fb20724"
#define AP_LOOK_KOMODO_MOE_SIZE 15724u
#define AP_LOOK_KOMODO_MOE_REL "looks/komodo-moe/komodo_moe_hi.bin"

static const AP_LookDef AP_LOOKS[] = {
	{"Joe Mama", "KOMODO MOE", AP_LOOK_RACER_JOE, AP_LOOK_KOMODO_MOE_ITEM_INDEX, AP_LOOK_KOMODO_MOE_REL,
	 AP_LOOK_KOMODO_MOE_SHA256, AP_LOOK_KOMODO_MOE_SIZE, 717},
};
#define AP_LOOK_COUNT ((int)(sizeof(AP_LOOKS) / sizeof(AP_LOOKS[0])))

// Owned looks are one bit per table row.
typedef unsigned AP_LookOwned;

static inline int AP_LookForItem(long long itemIndex)
{
	for (int i = 0; i < AP_LOOK_COUNT; i++)
		if (AP_LOOKS[i].itemIndex == itemIndex)
			return i;
	return -1;
}

static inline int AP_LookForValue(int value)
{
	return value == AP_LOOK_ORIGINAL ? -1 : AP_LookForItem(value);
}

// The look a racer wears: the saved value when it names an owned look for that
// racer, otherwise -1 (the racer's own model).
static inline int AP_LookEffective(AP_LookOwned owned, int racer, int savedValue)
{
	int look = AP_LookForValue(savedValue);
	if (look < 0 || AP_LOOKS[look].racer != racer || !(owned & (1u << look)))
		return -1;
	return look;
}

// The value the OPTIONS row shows for a saved value: the saved look when it is
// in effect, else ORIGINAL.
static inline int AP_LookRowValue(AP_LookOwned owned, int racer, int savedValue)
{
	int look = AP_LookEffective(owned, racer, savedValue);
	return look < 0 ? AP_LOOK_ORIGINAL : AP_LOOKS[look].itemIndex;
}

static inline const char *AP_LookRowLabel(AP_LookOwned owned, int racer, int savedValue)
{
	int look = AP_LookEffective(owned, racer, savedValue);
	return look < 0 ? "ORIGINAL" : AP_LOOKS[look].label;
}

// One step left (dir < 0) or right (dir > 0) over ORIGINAL followed by the
// racer's owned looks in table order. Clamps at both ends like every other
// ladder in the menu. Returns the new value to save.
static inline int AP_LookRowStep(AP_LookOwned owned, int racer, int savedValue, int dir)
{
	int ladder[1 + AP_LOOK_COUNT];
	int n = 0, at = 0, current = AP_LookRowValue(owned, racer, savedValue);

	ladder[n++] = AP_LOOK_ORIGINAL;
	for (int i = 0; i < AP_LOOK_COUNT; i++)
		if (AP_LOOKS[i].racer == racer && (owned & (1u << i)))
			ladder[n++] = AP_LOOKS[i].itemIndex;
	for (int i = 0; i < n; i++)
		if (ladder[i] == current)
			at = i;
	at += dir < 0 ? -1 : (dir > 0 ? 1 : 0);
	if (at < 0)
		at = 0;
	if (at > n - 1)
		at = n - 1;
	return ladder[at];
}

// Does the row have anything to choose from?
static inline int AP_LookRacerHasOwned(AP_LookOwned owned, int racer)
{
	for (int i = 0; i < AP_LOOK_COUNT; i++)
		if (AP_LOOKS[i].racer == racer && (owned & (1u << i)))
			return 1;
	return 0;
}

// ---------------------------------------------------------------------------
// Model file validation, on the raw bytes before relocation.
//
// A driver model file is the DRAM layout LOAD_DramFileCallback reads: a u32
// pointer-map offset, the data, then the pointer map (u32 byte count, then that
// many bytes of u32 data offsets). The data starts with a struct Model (name
// 0x10, id s16, numHeaders s16, headers ptr) and its ModelHeaders (0x40 each).
// A look must be one static, untextured header: no animation list (the
// renderer then draws ptrFrameData), every command with texture index 0 (so
// ptrTexLayout is never read) and a command list that ends before the file
// does. Offsets are 32-bit, the same as the native build's pointers.
// ---------------------------------------------------------------------------

#define AP_LOOK_MODEL_SIZE 0x18
#define AP_LOOK_HEADER_SIZE 0x40

typedef struct
{
	unsigned dataLen;      // bytes of model data (the pointer map starts here)
	unsigned numPtrs;      // pointer-map entries
	unsigned headerOff;    // data offset of the one ModelHeader
	unsigned commandOff;   // data offset of the command list
	unsigned frameOff;     // data offset of the static frame
	unsigned colorOff;     // data offset of the colour table
	int triangles;         // drawn triangles (commands after the first word / 3)
} AP_LookModelInfo;

static inline unsigned AP_LookU32(const unsigned char *b, size_t at)
{
	return (unsigned)b[at] | ((unsigned)b[at + 1] << 8) | ((unsigned)b[at + 2] << 16) | ((unsigned)b[at + 3] << 24);
}

static inline int AP_LookS16(const unsigned char *b, size_t at)
{
	int v = (int)b[at] | ((int)b[at + 1] << 8);
	return v >= 0x8000 ? v - 0x10000 : v;
}

// Returns 1 and fills `info` when `file` is an acceptable look model, else 0
// with a short reason in `why`.
static inline int AP_LookValidateModel(const unsigned char *file, size_t len, AP_LookModelInfo *info, const char **why)
{
	const unsigned char *d;
	unsigned mapOff, mapBytes, hdr, cmd, frame, colors, k, words;
	size_t dataLen;

	*why = "file too small";
	if (file == NULL || len < 4 + AP_LOOK_MODEL_SIZE + AP_LOOK_HEADER_SIZE + 4)
		return 0;
	d = file + 4;
	dataLen = len - 4;
	mapOff = AP_LookU32(file, 0);
	*why = "pointer map outside the file";
	if ((mapOff & 3) || mapOff < AP_LOOK_MODEL_SIZE + AP_LOOK_HEADER_SIZE || (size_t)mapOff + 4 > dataLen)
		return 0;
	mapBytes = AP_LookU32(d, mapOff);
	if ((mapBytes & 3) || (size_t)mapOff + 4 + mapBytes != dataLen)
		return 0;
	*why = "pointer map entry outside the data";
	for (k = 0; k < mapBytes / 4; k++)
	{
		unsigned off = AP_LookU32(d, mapOff + 4 + 4 * k) & ~3u;
		if ((size_t)off + 4 > mapOff || AP_LookU32(d, off) >= mapOff)
			return 0;
	}
	*why = "not one model header";
	if (AP_LookS16(d, 0x12) != 1)
		return 0;
	hdr = AP_LookU32(d, 0x14);
	if ((hdr & 3) || (size_t)hdr + AP_LOOK_HEADER_SIZE > mapOff)
		return 0;
	*why = "model is animated";
	if (AP_LookU32(d, hdr + 0x34) != 0 || AP_LookU32(d, hdr + 0x38) != 0 || AP_LookU32(d, hdr + 0x3C) != 0)
		return 0;
	cmd = AP_LookU32(d, hdr + 0x20);
	frame = AP_LookU32(d, hdr + 0x24);
	colors = AP_LookU32(d, hdr + 0x2C);
	*why = "missing command list, frame or colours";
	if (cmd == 0 || frame == 0 || colors == 0 || (cmd & 3) || (frame & 3) || (colors & 3) || cmd >= mapOff ||
	    (size_t)frame + 8 > mapOff || colors >= mapOff)
		return 0;
	// The first word is the list's header; commands follow until 0xFFFFFFFF.
	*why = "command list does not end";
	for (words = 1;; words++)
	{
		size_t at = (size_t)cmd + 4u * words;
		unsigned c;
		if (at + 4 > mapOff)
			return 0;
		c = AP_LookU32(d, at);
		if (c == 0xFFFFFFFFu)
			break;
		if (c & 0x1FF)
		{
			*why = "model is textured";
			return 0;
		}
	}
	if (info != NULL)
	{
		info->dataLen = mapOff;
		info->numPtrs = mapBytes / 4;
		info->headerOff = hdr;
		info->commandOff = cmd;
		info->frameOff = frame;
		info->colorOff = colors;
		info->triangles = (int)((words - 1) / 3);
	}
	*why = "ok";
	return 1;
}

#endif
