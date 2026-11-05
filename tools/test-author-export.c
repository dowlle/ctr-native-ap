// cc -Wall -Wextra -DCTR_AP -I ap -I . -I include -o /tmp/test-author-export tools/test-author-export.c
//
// Box submissions (ap/ap_author_export.h): one file per track, its name, its
// exact text, and which retail tracks count as changed. A custom track is
// named by its package identity (UUID plus LEV and VRM digests) and never by
// a host levelID; two revisions of one package get two files.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_author_export.h"

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static const char *k_uuid = "0f8e2c1a-1234-4abc-9def-00112233aabb";
static const char *k_levA = "aaaaaaaaaaaa1111111111111111111111111111111111111111111111111111";
static const char *k_levB = "bbbbbbbbbbbb1111111111111111111111111111111111111111111111111111";
static const char *k_vrm = "cccccccccccc2222222222222222222222222222222222222222222222222222";

// Everything written to f, as a string.
static char *contents(FILE *f)
{
	static char buf[16384];
	size_t n;

	fflush(f);
	rewind(f);
	n = fread(buf, 1, sizeof buf - 1, f);
	buf[n] = '\0';
	return buf;
}

static AP_PlacementRow s_rows[AP_EMBEDDED_PLACEMENT_COUNT + 4];

static int copy_embedded(void)
{
	int i;
	for (i = 0; i < AP_EMBEDDED_PLACEMENT_COUNT; i++)
	{
		s_rows[i].level = AP_EMBEDDED_PLACEMENTS[i].level;
		s_rows[i].x = AP_EMBEDDED_PLACEMENTS[i].x;
		s_rows[i].y = AP_EMBEDDED_PLACEMENTS[i].y;
		s_rows[i].z = AP_EMBEDDED_PLACEMENTS[i].z;
		s_rows[i].rotY = AP_EMBEDDED_PLACEMENTS[i].rotY;
	}
	return AP_EMBEDDED_PLACEMENT_COUNT;
}

static int changed_levels(int n, int *only)
{
	int level, c = 0;
	for (level = 0; level < 25; level++)
	{
		if (AP_BoxExport_RetailChanged(s_rows, n, level))
		{
			c++;
			*only = level;
		}
	}
	return c;
}

int main(void)
{
	char name[192];
	AP_CustomBoxKey keyA, keyB;
	AP_CustomBoxRow crow[3];
	AP_PlacementRow rrow[3];
	FILE *f;
	char *text;
	int n, only = -1, first, i;

	// ── names ───────────────────────────────────────────────────────────────
	CHECK(AP_BoxExport_RetailName(name, sizeof name, "CRASH_COVE"));
	CHECK(strcmp(name, "box-submissions/retail-CRASH_COVE.json") == 0);
	CHECK(AP_CustomBoxKey_Make(&keyA, k_uuid, k_levA, k_vrm));
	CHECK(AP_CustomBoxKey_Make(&keyB, k_uuid, k_levB, k_vrm));
	CHECK(AP_BoxExport_CustomName(name, sizeof name, &keyA));
	CHECK(strcmp(name, "box-submissions/custom-0f8e2c1a-1234-4abc-9def-00112233aabb-aaaaaaaaaaaa.json") == 0);
	{
		char other[192];
		CHECK(AP_BoxExport_CustomName(other, sizeof other, &keyB));
		CHECK(strcmp(name, other) != 0); // a new revision never overwrites the old file
	}
	CHECK(!AP_BoxExport_RetailName(name, 10, "CRASH_COVE")); // does not fit: refused, not cut

	// ── retail file text ────────────────────────────────────────────────────
	rrow[0].level = 3; rrow[0].x = 10; rrow[0].y = -20; rrow[0].z = 30; rrow[0].rotY = 1024;
	rrow[1].level = 4; rrow[1].x = 1; rrow[1].y = 2; rrow[1].z = 3; rrow[1].rotY = 0;
	rrow[2].level = 3; rrow[2].x = -5; rrow[2].y = 6; rrow[2].z = -7; rrow[2].rotY = 2048;
	f = tmpfile();
	CHECK(f != NULL && AP_BoxExport_WriteRetail(f, rrow, 3, 3, "CRASH_COVE", "v0.3.0-alpha2"));
	text = contents(f);
	CHECK(strcmp(text,
	             "{\n"
	             "  \"format\": \"ctr-ap-box-submission\",\n"
	             "  \"version\": 1,\n"
	             "  \"client\": \"v0.3.0-alpha2\",\n"
	             "  \"track\": \"CRASH_COVE\",\n"
	             "  \"kind\": \"retail\",\n"
	             "  \"level_id\": 3,\n"
	             "  \"level\": \"CRASH_COVE\",\n"
	             "  \"units\": \"" AP_BOX_EXPORT_UNITS "\",\n"
	             "  \"boxes\": [\n"
	             "    {\"pos\": [10, -20, 30], \"rot_y\": 1024},\n"
	             "    {\"pos\": [-5, 6, -7], \"rot_y\": 2048}\n"
	             "  ]\n"
	             "}\n") == 0);
	fclose(f);

	// A track with no boxes left is still a valid file (an empty array).
	f = tmpfile();
	CHECK(AP_BoxExport_WriteRetail(f, rrow, 3, 7, "PAPU_PYRAMID", "v"));
	CHECK(strstr(contents(f), "  \"boxes\": [\n  ]\n}\n") != NULL);
	fclose(f);

	// ── custom file text ────────────────────────────────────────────────────
	memset(crow, 0, sizeof crow);
	crow[0].key = keyA; strcpy(crow[0].title, "Old \"Name\""); strcpy(crow[0].version, "1.0.0");
	crow[0].x = 100; crow[0].y = 200; crow[0].z = 300; crow[0].rotY = 0;
	crow[1].key = keyB; strcpy(crow[1].title, "Other revision"); strcpy(crow[1].version, "1.1.0");
	crow[1].x = 9; crow[1].y = 9; crow[1].z = 9; crow[1].rotY = 9;
	crow[2].key = keyA; strcpy(crow[2].title, "North Pole"); strcpy(crow[2].version, "1.0.1");
	crow[2].x = -1; crow[2].y = -2; crow[2].z = -3; crow[2].rotY = 4095;
	f = tmpfile();
	CHECK(AP_BoxExport_WriteCustom(f, crow, 3, &keyA, "v0.3.0-alpha2"));
	text = contents(f);
	CHECK(strcmp(text,
	             "{\n"
	             "  \"format\": \"ctr-ap-box-submission\",\n"
	             "  \"version\": 1,\n"
	             "  \"client\": \"v0.3.0-alpha2\",\n"
	             "  \"track\": \"North Pole\",\n"
	             "  \"kind\": \"custom\",\n"
	             "  \"package_uuid\": \"0f8e2c1a-1234-4abc-9def-00112233aabb\",\n"
	             "  \"package_version\": \"1.0.1\",\n"
	             "  \"lev_sha256\": \"aaaaaaaaaaaa1111111111111111111111111111111111111111111111111111\",\n"
	             "  \"vrm_sha256\": \"cccccccccccc2222222222222222222222222222222222222222222222222222\",\n"
	             "  \"units\": \"" AP_BOX_EXPORT_UNITS "\",\n"
	             "  \"boxes\": [\n"
	             "    {\"pos\": [100, 200, 300], \"rot_y\": 0},\n"
	             "    {\"pos\": [-1, -2, -3], \"rot_y\": 4095}\n"
	             "  ]\n"
	             "}\n") == 0);
	// Package identity only: no host slot, no levelID.
	CHECK(strstr(text, "level") == NULL);
	CHECK(strstr(text, "9, 9, 9") == NULL); // the other revision's box is not in this file
	fclose(f);

	// Names with quotes are escaped.
	f = tmpfile();
	crow[2].key = keyB; // now keyA's newest row is crow[0]
	CHECK(AP_BoxExport_WriteCustom(f, crow, 3, &keyA, "v"));
	CHECK(strstr(contents(f), "\"track\": \"Old \\\"Name\\\"\"") != NULL);
	fclose(f);
	crow[2].key = keyA;

	// A key with no rows writes nothing.
	{
		AP_CustomBoxKey none;
		CHECK(AP_CustomBoxKey_Make(&none, "11111111-2222-4333-8444-555555555555", k_levA, k_vrm));
		f = tmpfile();
		CHECK(!AP_BoxExport_WriteCustom(f, crow, 3, &none, "v"));
		CHECK(contents(f)[0] == '\0');
		fclose(f);
	}

	// Each custom key is exported once.
	first = 0;
	for (i = 0; i < 3; i++)
		first += AP_BoxExport_FirstOfKey(crow, i);
	CHECK(first == 2);
	CHECK(AP_BoxExport_FirstOfKey(crow, 0) && AP_BoxExport_FirstOfKey(crow, 1) && !AP_BoxExport_FirstOfKey(crow, 2));

	// ── which retail tracks changed ─────────────────────────────────────────
	n = copy_embedded();
	CHECK(changed_levels(n, &only) == 0); // the built-in set exports nothing
	s_rows[5].x += 16;                    // one box nudged
	CHECK(changed_levels(n, &only) == 1 && only == s_rows[5].level);
	n = copy_embedded();
	s_rows[n].level = 0; s_rows[n].x = 1; s_rows[n].y = 2; s_rows[n].z = 3; s_rows[n].rotY = 0;
	CHECK(changed_levels(n + 1, &only) == 1 && only == 0); // one box added on Dingo Canyon
	n = copy_embedded();
	{
		int lvl = s_rows[0].level;
		memmove(&s_rows[0], &s_rows[1], sizeof s_rows[0] * (size_t)(n - 1)); // one box deleted
		CHECK(changed_levels(n - 1, &only) == 1 && only == lvl);
	}
	n = copy_embedded();
	{
		AP_PlacementRow t = s_rows[0];
		CHECK(s_rows[1].level == t.level);
		s_rows[0] = s_rows[1]; // two boxes swapped: slot order changed
		s_rows[1] = t;
		CHECK(changed_levels(n, &only) == 1 && only == t.level);
	}
	n = copy_embedded();
	s_rows[10].rotY ^= 0x400; // a turn alone counts
	CHECK(changed_levels(n, &only) == 1);
	CHECK(AP_BoxExport_RetailChanged(s_rows, 0, 0)); // everything deleted on a track with boxes
	CHECK(!AP_BoxExport_RetailChanged(s_rows, 0, 24)); // an arena with none, still none

	printf("author export: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
