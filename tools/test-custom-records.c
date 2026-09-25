// cc -Wall -Wextra -DCTR_CUSTOM_TRACKS -I include -I . tools/test-custom-records.c
// platform/native_custom_records.c -o /tmp/test-custom-records
//
// Custom-track Time Trial records (native_custom_records.h): the key, the file
// names, the exact .times text, the .ghost layout, and every refusal: another
// track, another version of its files, another lap count, a damaged file.
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <platform/native_custom_records.h>

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

#define UUID "6b2090fe-963d-5795-9058-619492a52aea"
#define LEV "8093b663855d3fd0096fd2980a85bc6efe334c0aff250e8a33eea11909e5c21c"
#define VRM "044024f265783dd285a3f9e76ee29d336cb3c34d81274d1a7ff2510d67680da8"
#define LEV2 "4d5a7397feaa242083a59279c28d8e96118ec7074de5f03eeab329207c4472d4"

static int same(const struct CustomRecordTimes *a, const struct CustomRecordTimes *b)
{
	int i;
	for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
		if (a->entry[i].time != b->entry[i].time || a->entry[i].characterID != b->entry[i].characterID ||
		    strcmp(a->entry[i].name, b->entry[i].name) != 0)
			return 0;
	return 1;
}

static struct CustomRecordTimes sample(void)
{
	struct CustomRecordTimes t;
	int i;
	memset(&t, 0, sizeof t);
	for (i = 0; i < CTR_RECORDS_ENTRIES; i++)
	{
		t.entry[i].time = 0x8c640;
		t.entry[i].characterID = (unsigned int)i;
		snprintf(t.entry[i].name, sizeof t.entry[i].name, "CRASH%d", i);
	}
	t.entry[0].time = 19900;
	t.entry[1].time = 60000;
	strcpy(t.entry[1].name, "TEST RACER");
	t.entry[2].name[0] = 0; // an empty name is allowed
	return t;
}

int main(void)
{
	struct CustomRecordKey key, other;
	struct CustomRecordTimes t = sample(), back;
	char stem[CTR_RECORDS_STEM_MAX], text[1024];
	size_t n;

	// Key: canonical lowercase identity and a lap count the engine can store.
	CHECK(CustomRecords_MakeKey(&key, UUID, LEV, VRM, 3));
	CHECK(!CustomRecords_MakeKey(&other, "6B2090FE-963D-5795-9058-619492A52AEA", LEV, VRM, 3));
	CHECK(!CustomRecords_MakeKey(&other, UUID, "abc", VRM, 3));
	CHECK(!CustomRecords_MakeKey(&other, UUID, LEV, VRM, 0));
	CHECK(!CustomRecords_MakeKey(&other, UUID, LEV, VRM, 8));
	CHECK(!CustomRecords_MakeKey(&other, UUID, LEV, NULL, 3));
	CHECK(!CustomRecords_MakeKey(&other, "6b2090fe+963d-5795-9058-619492a52aea", LEV, VRM, 3));

	// File name stem.
	CHECK(CustomRecords_Stem(&key, stem, sizeof stem));
	CHECK(strcmp(stem, UUID "-8093b663855d3fd0-044024f265783dd2-3l") == 0);
	CHECK(!CustomRecords_Stem(&key, stem, 20) && stem[0] == 0);

	// The .times text, exactly.
	n = CustomRecords_FormatTimes(&key, &t, text, sizeof text);
	CHECK(n == strlen(text));
	CHECK(strcmp(text,
	             "CTR custom track records 1\n"
	             "uuid " UUID "\n"
	             "lev " LEV "\n"
	             "vrm " VRM "\n"
	             "laps 3\n"
	             "lap 19900 0 CRASH0\n"
	             "race1 60000 1 TEST RACER\n"
	             "race2 575040 2 \n"
	             "race3 575040 3 CRASH3\n"
	             "race4 575040 4 CRASH4\n"
	             "race5 575040 5 CRASH5\n") == 0);
	CHECK(CustomRecords_ParseTimes(&key, text, n, &back));
	CHECK(same(&back, &t));
	CHECK(CustomRecords_FormatTimes(&key, &t, text, 100) == 0);

	// Refusals: other track, other files, other laps, damage, truncation.
	n = CustomRecords_FormatTimes(&key, &t, text, sizeof text);
	CHECK(CustomRecords_MakeKey(&other, "6b2090fe-963d-5795-9058-619492a52aeb", LEV, VRM, 3));
	CHECK(!CustomRecords_ParseTimes(&other, text, n, &back));
	CHECK(CustomRecords_MakeKey(&other, UUID, LEV2, VRM, 3));
	CHECK(!CustomRecords_ParseTimes(&other, text, n, &back));
	CHECK(CustomRecords_MakeKey(&other, UUID, LEV, VRM, 5));
	CHECK(!CustomRecords_ParseTimes(&other, text, n, &back));
	CHECK(!CustomRecords_ParseTimes(&key, text, n - 1, &back)); // last newline missing
	CHECK(!CustomRecords_ParseTimes(&key, text, n - 8, &back));
	{
		char bad[2048];
		memcpy(bad, text, n + 1);
		bad[strstr(bad, "race1 ") - bad + 6] = 'x';
		CHECK(!CustomRecords_ParseTimes(&key, bad, n, &back));
		memcpy(bad, text, n + 1);
		memcpy(strstr(bad, "race3"), "race4", 5); // out of order
		CHECK(!CustomRecords_ParseTimes(&key, bad, n, &back));
		memcpy(bad, text, n + 1);
		bad[0] = 'X';
		CHECK(!CustomRecords_ParseTimes(&key, bad, n, &back));
		snprintf(bad, sizeof bad, "%sextra\n", text);
		CHECK(!CustomRecords_ParseTimes(&key, bad, strlen(bad), &back));
		memcpy(bad, text, n + 1);
		memcpy(strstr(bad, " 1 TEST"), " 70000", 6); // character id past 0xffff
		CHECK(!CustomRecords_ParseTimes(&key, bad, n, &back));
	}
	{
		struct CustomRecordTimes odd = t;
		strcpy(odd.entry[3].name, "A\tB\nC");
		n = CustomRecords_FormatTimes(&key, &odd, text, sizeof text);
		CHECK(n && strstr(text, "race3 575040 3 A?B?C\n"));
		CHECK(CustomRecords_ParseTimes(&key, text, n, &back) && strcmp(back.entry[3].name, "A?B?C") == 0);
	}

	// The .ghost layout: magic, identity, laps, size, SHA-256, then the bytes.
	{
		unsigned char ghost[0x300], file[0x4000], out[0x3e00];
		size_t got = 0, fileBytes;
		unsigned int i;
		for (i = 0; i < sizeof ghost; i++)
			ghost[i] = (unsigned char)(i * 7);
		fileBytes = CustomRecords_FormatGhost(&key, ghost, sizeof ghost, file, sizeof file);
		CHECK(fileBytes == 8 + 36 + 64 + 64 + 4 + 4 + 64 + sizeof ghost);
		CHECK(memcmp(file, "CTRCGHO1", 8) == 0 && memcmp(file + 8, UUID, 36) == 0);
		CHECK(memcmp(file + 44, LEV, 64) == 0 && memcmp(file + 108, VRM, 64) == 0);
		CHECK(file[172] == 3 && file[176] == 0x00 && file[177] == 0x03);
		CHECK(CustomRecords_ParseGhost(&key, file, fileBytes, out, sizeof out, &got) && got == sizeof ghost);
		CHECK(memcmp(out, ghost, sizeof ghost) == 0);
		CHECK(!CustomRecords_ParseGhost(&other, file, fileBytes, out, sizeof out, &got) && got == 0);
		CHECK(!CustomRecords_ParseGhost(&key, file, fileBytes - 1, out, sizeof out, &got));
		CHECK(!CustomRecords_ParseGhost(&key, file, fileBytes, out, 0x100, &got)); // too big for the buffer
		file[fileBytes - 1] ^= 1; // one flipped bit in the moves
		CHECK(!CustomRecords_ParseGhost(&key, file, fileBytes, out, sizeof out, &got));
		file[fileBytes - 1] ^= 1;
		file[180] ^= 1; // the digest itself
		CHECK(!CustomRecords_ParseGhost(&key, file, fileBytes, out, sizeof out, &got));
		CHECK(CustomRecords_FormatGhost(&key, ghost, 0, file, sizeof file) == 0);
		CHECK(CustomRecords_FormatGhost(&key, file, CTR_RECORDS_GHOST_MAX + 1, file, sizeof file) == 0);
	}

	// Files: written under the directory, read back, refused for other keys.
	{
		char dir[] = "/tmp/ctr-records-XXXXXX", sub[64], path[256];
		unsigned char ghost[0x40], out[0x3e00];
		size_t got;
		FILE *f;
		CHECK(mkdtemp(dir) != NULL);
		snprintf(sub, sizeof sub, "%s/custom-records", dir);
		CHECK(!CustomRecords_LoadTimes(sub, &key, &back)); // nothing yet
		CHECK(CustomRecords_SaveTimes(sub, &key, &t));      // creates the folder
		CHECK(CustomRecords_LoadTimes(sub, &key, &back) && same(&back, &t));
		CHECK(!CustomRecords_LoadTimes(sub, &other, &back));
		memset(ghost, 0x42, sizeof ghost);
		CHECK(CustomRecords_SaveGhost(sub, &key, ghost, sizeof ghost));
		CHECK(CustomRecords_LoadGhost(sub, &key, out, sizeof out, &got) && got == sizeof ghost);
		t.entry[1].time = 55000;
		CHECK(CustomRecords_SaveTimes(sub, &key, &t)); // replaces the old file
		CHECK(CustomRecords_LoadTimes(sub, &key, &back) && back.entry[1].time == 55000);
		CHECK(CustomRecords_Stem(&key, stem, sizeof stem));
		snprintf(path, sizeof path, "%s/%s.times.tmp", sub, stem);
		CHECK(access(path, F_OK) != 0); // no temporary file left behind
		snprintf(path, sizeof path, "%s/%s.times", sub, stem);
		f = fopen(path, "ab");
		CHECK(f != NULL);
		if (f)
		{
			fputs("junk\n", f);
			fclose(f);
		}
		CHECK(!CustomRecords_LoadTimes(sub, &key, &back)); // damaged: the caller uses its defaults
		{
			char cmd[128];
			snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
			CHECK(system(cmd) == 0);
		}
	}

	printf("custom records: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
