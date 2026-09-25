#ifndef NATIVE_CUSTOM_STATE_IDENTITY_H
#define NATIVE_CUSTOM_STATE_IDENTITY_H

// Which custom track a savestate belongs to (box authoring build,
// CTR_CUSTOM_PACKAGES). Freestanding: C99 only, so
// tools/test-custom-state-identity.c checks exactly what the engine uses.
//
// A savestate holds the mempack (the package LEV), VRAM (its VRM), sdata and
// gGT, but not the offline custom-race runtime that decides whether the host
// slot is serving a package. Restoring across that boundary would put custom
// geometry under the host slot's retail identity, or retail geometry under a
// package's identity. Box placements and AI recordings would then be filed
// under the wrong track, which is issue #356 again.
//
// So every checkpoint carries the identity of the loaded package, and a
// restore is refused unless it equals the live one:
//
//   no custom track active     "none"
//   a package is active        its UUID plus the SHA-256 of its LEV and VRM
//   a package is active but    "unknown": never restorable in either direction
//   its manifest is unusable
//
// The record has a fixed size and layout; it is a checkpoint region.

#include <stddef.h>
#include <string.h>

#define CTR_CUSTOM_STATE_MAGIC 0x31445343u // "CSD1", little-endian
#define CTR_CUSTOM_STATE_UUID_CHARS 36
#define CTR_CUSTOM_STATE_SHA_CHARS 64

enum CustomStateKind
{
	CTR_CUSTOM_STATE_NONE = 0,
	CTR_CUSTOM_STATE_PACKAGE = 1,
	CTR_CUSTOM_STATE_UNKNOWN = 2,
};

struct CustomStateIdentity
{
	unsigned int magic;
	unsigned int kind; // enum CustomStateKind
	char uuid[40];      // lowercase 8-4-4-4-12, NUL-padded
	char levSha256[68]; // lowercase hex, NUL-padded
	char vrmSha256[68]; // lowercase hex, NUL-padded
};

static inline void CustomStateIdentity_Init(struct CustomStateIdentity *out, unsigned int kind)
{
	memset(out, 0, sizeof *out);
	out->magic = CTR_CUSTOM_STATE_MAGIC;
	out->kind = kind;
}

static inline void CustomStateIdentity_None(struct CustomStateIdentity *out)
{
	CustomStateIdentity_Init(out, CTR_CUSTOM_STATE_NONE);
}

static inline void CustomStateIdentity_Unknown(struct CustomStateIdentity *out)
{
	CustomStateIdentity_Init(out, CTR_CUSTOM_STATE_UNKNOWN);
}

static inline int CustomStateIdentity_Hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline char CustomStateIdentity_Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// An active package. Anything that is not a canonical UUID and two 64-digit
// digests becomes "unknown", which no restore accepts. Returns 1 for a
// package identity, 0 for unknown.
static inline int CustomStateIdentity_Package(struct CustomStateIdentity *out, const char *uuid, const char *lev,
                                              const char *vrm)
{
	int i;

	CustomStateIdentity_Unknown(out);
	if (uuid == NULL || lev == NULL || vrm == NULL || strlen(uuid) != CTR_CUSTOM_STATE_UUID_CHARS ||
	    strlen(lev) != CTR_CUSTOM_STATE_SHA_CHARS || strlen(vrm) != CTR_CUSTOM_STATE_SHA_CHARS)
		return 0;
	for (i = 0; i < CTR_CUSTOM_STATE_UUID_CHARS; i++)
	{
		int dash = (i == 8 || i == 13 || i == 18 || i == 23);
		if (dash ? uuid[i] != '-' : !CustomStateIdentity_Hex(uuid[i]))
		{
			CustomStateIdentity_Unknown(out);
			return 0;
		}
		out->uuid[i] = CustomStateIdentity_Lower(uuid[i]);
	}
	for (i = 0; i < CTR_CUSTOM_STATE_SHA_CHARS; i++)
	{
		if (!CustomStateIdentity_Hex(lev[i]) || !CustomStateIdentity_Hex(vrm[i]))
		{
			CustomStateIdentity_Unknown(out);
			return 0;
		}
		out->levSha256[i] = CustomStateIdentity_Lower(lev[i]);
		out->vrmSha256[i] = CustomStateIdentity_Lower(vrm[i]);
	}
	out->kind = CTR_CUSTOM_STATE_PACKAGE;
	return 1;
}

static inline int CustomStateIdentity_Valid(const struct CustomStateIdentity *id)
{
	return id != NULL && id->magic == CTR_CUSTOM_STATE_MAGIC &&
	       (id->kind == CTR_CUSTOM_STATE_NONE || id->kind == CTR_CUSTOM_STATE_PACKAGE ||
	        id->kind == CTR_CUSTOM_STATE_UNKNOWN);
}

// A saved state may be restored only into the same load context: both "none",
// or both the same package (UUID, LEV and VRM). "unknown" or a damaged record
// on either side refuses.
static inline int CustomStateIdentity_RestoreAllowed(const struct CustomStateIdentity *saved,
                                                     const struct CustomStateIdentity *live)
{
	if (!CustomStateIdentity_Valid(saved) || !CustomStateIdentity_Valid(live))
		return 0;
	if (saved->kind != live->kind || saved->kind == CTR_CUSTOM_STATE_UNKNOWN)
		return 0;
	if (saved->kind == CTR_CUSTOM_STATE_NONE)
		return 1;
	return memcmp(saved->uuid, live->uuid, sizeof saved->uuid) == 0 &&
	       memcmp(saved->levSha256, live->levSha256, sizeof saved->levSha256) == 0 &&
	       memcmp(saved->vrmSha256, live->vrmSha256, sizeof saved->vrmSha256) == 0;
}

// One log-friendly line: "none", "unknown", "damaged" or
// "custom <uuid> lev <12 hex> vrm <12 hex>". Reads a possibly damaged record
// safely: every field is bounded.
static inline void CustomStateIdentity_Describe(const struct CustomStateIdentity *id, char *out, size_t cap)
{
	char uuid[CTR_CUSTOM_STATE_UUID_CHARS + 1];
	char lev[13];
	char vrm[13];
	const char *text = NULL;
	size_t n;

	if (out == NULL || cap == 0)
		return;
	if (!CustomStateIdentity_Valid(id))
		text = "damaged";
	else if (id->kind == CTR_CUSTOM_STATE_NONE)
		text = "none";
	else if (id->kind == CTR_CUSTOM_STATE_UNKNOWN)
		text = "unknown";
	if (text != NULL)
	{
		n = strlen(text);
		if (n >= cap)
			n = cap - 1;
		memcpy(out, text, n);
		out[n] = '\0';
		return;
	}
	memcpy(uuid, id->uuid, CTR_CUSTOM_STATE_UUID_CHARS);
	uuid[CTR_CUSTOM_STATE_UUID_CHARS] = '\0';
	memcpy(lev, id->levSha256, 12);
	lev[12] = '\0';
	memcpy(vrm, id->vrmSha256, 12);
	vrm[12] = '\0';
	// Assembled by hand to stay freestanding (no stdio).
	{
		const char *parts[6] = {"custom ", uuid, " lev ", lev, " vrm ", vrm};
		size_t pos = 0;
		int p;
		for (p = 0; p < 6; p++)
		{
			const char *s = parts[p];
			while (*s != '\0' && pos + 1 < cap)
				out[pos++] = *s++;
		}
		out[pos] = '\0';
	}
}

#endif
