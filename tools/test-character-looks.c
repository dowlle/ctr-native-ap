// Character looks (JL2): the pure rules and the model validator in
// ap/ap_look_logic.h, checked against the bundled Komodo Moe file.
//
// cc -std=c99 -Wall -Wextra -DCTR_AP -I ap -I include -o /tmp/test-character-looks tools/test-character-looks.c
// && /tmp/test-character-looks
//
// Run from the repository root (reads assets/looks/komodo-moe/komodo_moe_hi.bin).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_look_logic.h"
#include <platform/native_sha256.h>

static int fails;
#define EXPECT(name, cond)                                                                                             \
	do                                                                                                                 \
	{                                                                                                                  \
		if (cond)                                                                                                      \
			printf("ok   %s\n", name);                                                                                 \
		else                                                                                                           \
		{                                                                                                              \
			printf("FAIL %s\n", name);                                                                                 \
			fails++;                                                                                                   \
		}                                                                                                              \
	} while (0)

static unsigned char *read_file(const char *path, size_t *len)
{
	FILE *fp = fopen(path, "rb");
	unsigned char *buf;
	long n;
	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = (unsigned char *)malloc((size_t)n);
	if (fread(buf, 1, (size_t)n, fp) != (size_t)n)
	{
		free(buf);
		buf = NULL;
	}
	fclose(fp);
	*len = (size_t)n;
	return buf;
}

static void put_u32(unsigned char *b, size_t at, unsigned v)
{
	b[at] = (unsigned char)v;
	b[at + 1] = (unsigned char)(v >> 8);
	b[at + 2] = (unsigned char)(v >> 16);
	b[at + 3] = (unsigned char)(v >> 24);
}

static int validates(const unsigned char *orig, size_t len, size_t patchAt, unsigned value, const char **why)
{
	unsigned char *copy = (unsigned char *)malloc(len);
	int ok;
	memcpy(copy, orig, len);
	if (patchAt != (size_t)-1)
		put_u32(copy, patchAt, value);
	ok = AP_LookValidateModel(copy, len, NULL, why);
	free(copy);
	return ok;
}

int main(void)
{
	const AP_LookDef *moe = &AP_LOOKS[0];
	const int joe = AP_LOOK_RACER_JOE;
	const unsigned bit = 1u;
	const char *why = "";
	AP_LookModelInfo info;
	size_t len = 0;
	unsigned char *file;

	// --- the table ---------------------------------------------------------
	EXPECT("one look so far", AP_LOOK_COUNT == 1);
	EXPECT("Joe Mama is item index 215 (35010215)", moe->itemIndex == 215 && strcmp(moe->itemName, "Joe Mama") == 0);
	EXPECT("Komodo Moe is a look for Komodo Joe (id 11)", moe->racer == 11);
	EXPECT("owned bits fit the table", AP_LOOK_COUNT <= 32);
	EXPECT("ORIGINAL never collides with a look value", AP_LookForValue(AP_LOOK_ORIGINAL) == -1);
	EXPECT("item lookup", AP_LookForItem(215) == 0 && AP_LookForItem(214) == -1 && AP_LookForItem(203) == -1);
	for (int i = 0; i < AP_LOOK_COUNT; i++)
	{
		char name[96];
		snprintf(name, sizeof name, "look %d label fits the value column", i);
		EXPECT(name, strlen(AP_LOOKS[i].label) <= 12);
		for (int k = i + 1; k < AP_LOOK_COUNT; k++)
			EXPECT("item indexes are unique", AP_LOOKS[i].itemIndex != AP_LOOKS[k].itemIndex);
	}

	// --- the newest-wins choice read through the owned bits ---------------------
	EXPECT("nothing owned: original even with Moe saved", AP_LookEffective(0, joe, 215) == -1);
	EXPECT("owned and saved: Moe", AP_LookEffective(bit, joe, 215) == 0);
	EXPECT("owned, ORIGINAL saved: original", AP_LookEffective(bit, joe, AP_LOOK_ORIGINAL) == -1);
	EXPECT("a look never applies to another racer", AP_LookEffective(bit, 0, 215) == -1);
	EXPECT("an unknown saved value drives as original", AP_LookEffective(bit, joe, 999) == -1 &&
	                                                        AP_LookEffective(bit, joe, -5) == -1);
	EXPECT("row shows ORIGINAL when this seed has not delivered the saved look",
	       strcmp(AP_LookRowLabel(0, joe, 215), "ORIGINAL") == 0 && AP_LookRowValue(0, joe, 215) == 0);
	EXPECT("row shows the look when owned", strcmp(AP_LookRowLabel(bit, joe, 215), "KOMODO MOE") == 0);

	// --- the OPTIONS row ladder ------------------------------------------------
	EXPECT("nothing owned: right stays ORIGINAL", AP_LookRowStep(0, joe, 0, +1) == 0);
	EXPECT("nothing owned: left stays ORIGINAL", AP_LookRowStep(0, joe, 0, -1) == 0);
	EXPECT("owned: right goes to Moe", AP_LookRowStep(bit, joe, 0, +1) == 215);
	EXPECT("owned: right clamps at the last look", AP_LookRowStep(bit, joe, 215, +1) == 215);
	EXPECT("owned: left goes back to ORIGINAL", AP_LookRowStep(bit, joe, 215, -1) == 0);
	EXPECT("unowned saved look steps from ORIGINAL", AP_LookRowStep(0, joe, 215, -1) == 0);
	EXPECT("other racers have no looks to step", AP_LookRowStep(bit, 0, 0, +1) == 0);
	EXPECT("has-choice follows the owned bits", AP_LookRacerHasOwned(bit, joe) && !AP_LookRacerHasOwned(0, joe) &&
	                                                !AP_LookRacerHasOwned(bit, 0));

	// --- the bundled file ------------------------------------------------------
	{
		char path[256];
		snprintf(path, sizeof path, "assets/%s", moe->assetRel);
		file = read_file(path, &len);
	}
	EXPECT("bundled Komodo Moe file is present", file != NULL);
	if (file == NULL)
		return 1;
	{
		struct NativeSha256Ctx sha;
		unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
		char hex[NATIVE_SHA256_HEX_BYTES];
		NativeSha256_Init(&sha);
		NativeSha256_Update(&sha, file, len);
		NativeSha256_Final(&sha, digest);
		NativeSha256_ToHex(digest, hex);
		EXPECT("bundled file matches its size and pin", len == moe->size && NativeSha256_HexEquals(moe->sha256, hex));
	}
	EXPECT("bundled file validates", AP_LookValidateModel(file, len, &info, &why));
	printf("     validator says: %s\n", why);
	EXPECT("717 untextured triangles", info.triangles == 717 && info.triangles == moe->triangles);
	EXPECT("five pointers (headers, commands, frame, texture table, colours)", info.numPtrs == 5);
	EXPECT("one header right after the model", info.headerOff == 0x18);
	EXPECT("static frame after the command list", info.frameOff > info.commandOff && info.colorOff > info.frameOff);
	EXPECT("header has no LOD limit (0xFFFF)", AP_LookS16(file + 4, info.headerOff + 0x14) == -1);

	// --- refusals ---------------------------------------------------------------
	{
		const size_t data = 4, hdr = data + info.headerOff;
		const size_t cmd = data + info.commandOff;
		EXPECT("truncated file refused", !AP_LookValidateModel(file, len - 4, NULL, &why));
		EXPECT("tiny file refused", !AP_LookValidateModel(file, 16, NULL, &why));
		EXPECT("NULL refused", !AP_LookValidateModel(NULL, 0, NULL, &why));
		EXPECT("pointer map offset past the end refused", !validates(file, len, 0, (unsigned)len, &why));
		EXPECT("animation count refused", !validates(file, len, hdr + 0x34, 1, &why) && strcmp(why, "model is animated") == 0);
		EXPECT("animation list refused", !validates(file, len, hdr + 0x38, 0x20, &why));
		EXPECT("two headers refused", !validates(file, len, data + 0x10, 0x00020000u | 0xFFFFu, &why));
		EXPECT("textured command refused", !validates(file, len, cmd + 8, 0x10580001u, &why) &&
		                                       strcmp(why, "model is textured") == 0);
		EXPECT("missing colour table refused", !validates(file, len, hdr + 0x2C, 0, &why));
		EXPECT("command list running into the map refused", !validates(file, len, hdr + 0x20, info.frameOff, &why));
		EXPECT("pointer slot outside the data refused",
		       !validates(file, len, data + info.dataLen + 4, info.dataLen + 8, &why));
		EXPECT("pointer value outside the data refused", !validates(file, len, hdr + 0x2C, info.dataLen + 16, &why));
	}
	free(file);

	if (fails)
	{
		printf("%d FAILED\n", fails);
		return 1;
	}
	printf("PASS: character looks table, newest-wins state, OPTIONS ladder and the bundled model\n");
	return 0;
}
