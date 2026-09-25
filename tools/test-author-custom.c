// cc -std=c99 -Wall -Wextra -Werror -I ap tools/test-author-custom.c -o /tmp/test-author-custom
//
// Custom-track box placements (box authoring build): the package-identity key,
// the one-line row format and the separate placement file, exactly as
// ap/ap_author.c uses them through ap/ap_author_custom.h.
#include <stdio.h>
#include <string.h>

#include "../ap/ap_author_custom.h"

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static const char *UUID = "2C7C7846-2EAD-5B8F-A218-BECA5792106E";
static const char *LEV = "BE161E0B11AA03505C501B7DB012D830405E69E171F84633C2E24FFE9CC3CBF8";
static const char *VRM = "1a0ff56b51562292ecc30c14e7a2e6d315f641feaa00990d4242abe46434f692";

static AP_CustomBoxRow row(const AP_CustomBoxKey *key, const char *title, short x, short y, short z, short rot)
{
	AP_CustomBoxRow r;
	memset(&r, 0, sizeof r);
	r.key = *key;
	AP_CustomBox_CopyText(r.title, sizeof r.title, title);
	AP_CustomBox_CopyText(r.version, sizeof r.version, "1.0.2");
	r.x = x;
	r.y = y;
	r.z = z;
	r.rotY = rot;
	return r;
}

int main(void)
{
	AP_CustomBoxKey key, same, other, untouched;
	AP_CustomBoxRow rows[8], back[8], parsed;
	char line[AP_CUSTOM_BOX_LINE_MAX];
	char hud[64];
	char big[200];
	int n, rejected, overflow;
	FILE *f;

	// ── the key ──
	CHECK(AP_CustomBoxKey_Make(&key, UUID, LEV, VRM));
	CHECK(strcmp(key.uuid, "2c7c7846-2ead-5b8f-a218-beca5792106e") == 0); // normalised to lowercase
	CHECK(strcmp(key.levSha256, "be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8") == 0);
	CHECK(AP_CustomBoxKey_Make(&same, "2c7c7846-2ead-5b8f-a218-beca5792106e", LEV, VRM));
	CHECK(AP_CustomBoxKey_Equal(&key, &same)); // case never splits a track
	// A new revision with other geometry is another key, even with the same UUID.
	CHECK(AP_CustomBoxKey_Make(&other, UUID, VRM, LEV));
	CHECK(!AP_CustomBoxKey_Equal(&key, &other));
	memset(&untouched, 0x5a, sizeof untouched);
	other = untouched;
	CHECK(!AP_CustomBoxKey_Make(&other, "2c7c7846-2ead-5b8f-a218", LEV, VRM));      // short UUID
	CHECK(!AP_CustomBoxKey_Make(&other, "2c7c7846x2ead-5b8f-a218-beca5792106e", LEV, VRM)); // bad dash
	CHECK(!AP_CustomBoxKey_Make(&other, UUID, "abc", VRM));                            // short digest
	CHECK(!AP_CustomBoxKey_Make(&other, UUID, LEV, "g1a0ff56b51562292ecc30c14e7a2e6d315f641feaa00990d4242abe46434f69")); // not hex
	CHECK(!AP_CustomBoxKey_Make(&other, NULL, LEV, VRM));
	CHECK(memcmp(&other, &untouched, sizeof other) == 0); // refusals leave the output alone

	// ── one row ──
	rows[0] = row(&key, "Baby T \"Park\" \\ Caf\xc3\xa9", -120, 5, 32767, 0x800);
	CHECK(AP_CustomBoxRow_Format(line, sizeof line, &rows[0]));
	CHECK(strstr(line, "\"level_id\"") == NULL); // never keyed by the host levelID
	CHECK(strstr(line, "\\\"Park\\\"") != NULL && strstr(line, "\\\\") != NULL); // escaped
	CHECK(strncmp(line, "{\"package_uuid\": \"2c7c7846-", 26) == 0);
	CHECK(AP_CustomBoxRow_Parse(line, &parsed));
	CHECK(AP_CustomBoxKey_Equal(&parsed.key, &key));
	CHECK(parsed.x == -120 && parsed.y == 5 && parsed.z == 32767 && parsed.rotY == 0x800);
	CHECK(strcmp(parsed.title, rows[0].title) == 0 && strcmp(parsed.version, "1.0.2") == 0);

	// Hand edits: reordered fields, extra spaces and a trailing comma are fine.
	CHECK(AP_CustomBoxRow_Parse("  { \"pos\" : [1,-2, 3] , \"vrm_sha256\":\"1A0FF56B51562292ECC30C14E7A2E6D315F641FEAA00990D4242ABE46434F692\","
	                            "\"lev_sha256\": \"be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8\","
	                            "\"package_uuid\": \"2c7c7846-2ead-5b8f-a218-beca5792106e\"},\n", &parsed));
	CHECK(AP_CustomBoxKey_Equal(&parsed.key, &key) && parsed.x == 1 && parsed.y == -2 && parsed.z == 3 &&
	      parsed.rotY == 0 && parsed.title[0] == '\0');
	// Refused: a missing key part, a duplicate field, an unknown field, a value
	// outside 16 bits, trailing garbage, and a retail row.
	CHECK(!AP_CustomBoxRow_Parse("{\"package_uuid\": \"2c7c7846-2ead-5b8f-a218-beca5792106e\", \"pos\": [1, 2, 3]}", &parsed));
	CHECK(!AP_CustomBoxRow_Parse("{\"package_uuid\": \"2c7c7846-2ead-5b8f-a218-beca5792106e\", "
	                             "\"lev_sha256\": \"be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8\", "
	                             "\"vrm_sha256\": \"1a0ff56b51562292ecc30c14e7a2e6d315f641feaa00990d4242abe46434f692\", "
	                             "\"pos\": [1, 2, 3], \"pos\": [4, 5, 6]}", &parsed));
	CHECK(!AP_CustomBoxRow_Parse("{\"package_uuid\": \"2c7c7846-2ead-5b8f-a218-beca5792106e\", "
	                             "\"lev_sha256\": \"be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8\", "
	                             "\"vrm_sha256\": \"1a0ff56b51562292ecc30c14e7a2e6d315f641feaa00990d4242abe46434f692\", "
	                             "\"pos\": [1, 2, 3], \"level_id\": 6}", &parsed));
	CHECK(!AP_CustomBoxRow_Parse("{\"package_uuid\": \"2c7c7846-2ead-5b8f-a218-beca5792106e\", "
	                             "\"lev_sha256\": \"be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8\", "
	                             "\"vrm_sha256\": \"1a0ff56b51562292ecc30c14e7a2e6d315f641feaa00990d4242abe46434f692\", "
	                             "\"pos\": [1, 40000, 3]}", &parsed));
	CHECK(!AP_CustomBoxRow_Parse("{\"package_uuid\": \"2c7c7846-2ead-5b8f-a218-beca5792106e\", "
	                             "\"lev_sha256\": \"be161e0b11aa03505c501b7db012d830405e69e171f84633c2e24ffe9cc3cbf8\", "
	                             "\"vrm_sha256\": \"1a0ff56b51562292ecc30c14e7a2e6d315f641feaa00990d4242abe46434f692\", "
	                             "\"pos\": [1, 2, 3]} x", &parsed));
	CHECK(!AP_CustomBoxRow_Parse("{\"level_id\": 6, \"level\": \"ROO_TUBES\", \"pos\": [1, 2, 3], \"rot_y\": 0}", &parsed));

	// ── the file: several tracks round-trip in order ──
	rows[1] = row(&key, "Baby T Park", 10, 20, 30, 40);
	CHECK(AP_CustomBoxKey_Make(&other, UUID, VRM, LEV));
	rows[2] = row(&other, "Other revision", 7, 8, 9, 0);
	rows[3] = row(&key, "Baby T Park", -1, -2, -3, -4);
	f = tmpfile();
	CHECK(f != NULL);
	if (f == NULL)
		return 1;
	CHECK(AP_CustomBoxFile_Write(f, rows, 4, "0.2.1-test"));
	rewind(f);
	{
		char text[8192];
		size_t got = fread(text, 1, sizeof text - 1, f);
		text[got] = '\0';
		CHECK(strstr(text, "\"format\": \"" AP_CUSTOM_BOX_FORMAT "\"") != NULL);
		CHECK(strstr(text, "\"client\": \"0.2.1-test\"") != NULL);
		CHECK(strstr(text, "level_id") == NULL);
	}
	rewind(f);
	n = AP_CustomBoxFile_Read(f, back, 8, &rejected, &overflow);
	CHECK(n == 4 && rejected == 0 && overflow == 0);
	CHECK(AP_CustomBoxKey_Equal(&back[2].key, &other) && back[2].x == 7);
	CHECK(back[3].x == -1 && back[3].rotY == -4);
	rewind(f);
	n = AP_CustomBoxFile_Read(f, back, 2, &rejected, &overflow);
	CHECK(n == 2 && overflow == 1);
	fclose(f);

	// A damaged line is counted and skipped; the rest still load.
	f = tmpfile();
	if (f == NULL)
		return 1;
	CHECK(AP_CustomBoxRow_Format(line, sizeof line, &rows[1]));
	fprintf(f, "{\n  \"placements\": [\n    %s,\n    {\"package_uuid\": \"broken\"},\n    %s\n  ]\n}\n", line, line);
	rewind(f);
	n = AP_CustomBoxFile_Read(f, back, 8, &rejected, &overflow);
	CHECK(n == 2 && rejected == 1);
	fclose(f);

	// ── per-track helpers ──
	CHECK(AP_CustomBox_CountFor(rows, 4, &key) == 3);
	CHECK(AP_CustomBox_CountFor(rows, 4, &other) == 1);
	CHECK(AP_CustomBox_LastIndexFor(rows, 4, &key) == 3);
	n = AP_CustomBox_Remove(rows, 4, 3);
	CHECK(n == 3 && AP_CustomBox_LastIndexFor(rows, n, &key) == 1);
	CHECK(AP_CustomBoxKey_Equal(&rows[2].key, &other)); // other tracks keep their rows
	CHECK(AP_CustomBox_Remove(rows, n, 9) == n);

	// ── HUD name and stored text ──
	AP_CustomBox_HudName(hud, sizeof hud, "Caf\xc3\xa9 Tr\xe2\x80\x8b" "ack");
	CHECK(strcmp(hud, "Caf? Tr?ack") == 0);
	AP_CustomBox_HudName(hud, sizeof hud, "");
	CHECK(strcmp(hud, "CUSTOM TRACK") == 0);
	memset(big, 'a', sizeof big);
	big[AP_CUSTOM_BOX_TEXT_MAX - 2] = '\xc3';
	big[AP_CUSTOM_BOX_TEXT_MAX - 1] = '\xa9';
	big[sizeof big - 1] = '\0';
	AP_CustomBox_CopyText(parsed.title, sizeof parsed.title, big);
	CHECK(strlen(parsed.title) == AP_CUSTOM_BOX_TEXT_MAX - 2); // cut before the split sequence

	printf("%s: %d custom-track placement checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
