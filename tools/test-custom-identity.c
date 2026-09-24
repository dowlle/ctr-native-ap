// cc -m32 -DCTR_NATIVE -DBUILD=926 -I include -I . tools/test-custom-identity.c -o /tmp/test-custom-identity
//
// Custom-track identity (box authoring build, native_custom_identity.h): the
// custom level id sits outside every retail range the engine tests, maps only
// during a custom race and whatever the host slot is, the explicit AI tuning
// is what the header says it is (checked against the retail rows in
// game/zGlobal_DATA.c), the lap rule, and the banner name.
#include <common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/native_custom_identity.h>

struct sData sdata_static; // common.h binds sdata to it

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static int cmp_short(const void *a, const void *b)
{
	return (int)*(const short *)a - (int)*(const short *)b;
}

static short parse_value(const char *p, char **end)
{
	unsigned long long v;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (strncmp(p, "AS(", 3) == 0)
	{
		v = strtoull(p + 3, end, 16);
		if (*end && **end == ')')
			(*end)++;
	}
	else
		v = strtoull(p, end, 16);
	return (short)(int)(unsigned int)v; // 32-bit two's complement, then s16 as in struct Difficulty
}

// Read the 18 retail ArcadeDifficulty rows out of game/zGlobal_DATA.c and
// return the lower-middle value of each parameter column.
static int retail_medians(short out1[14], short out2[14])
{
	static char text[1 << 22];
	short cols[2][14][18];
	FILE *f = fopen("game/zGlobal_DATA.c", "rb");
	size_t got;
	char *p, *stop;
	int row = 0, k, n;

	if (f == NULL)
		return 0;
	got = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[got] = '\0';
	p = strstr(text, ".ArcadeDifficulty");
	stop = p ? strstr(p, ".BossDifficulty") : NULL;
	if (p == NULL || stop == NULL)
		return 0;
	while (row < 18)
	{
		for (k = 0; k < 2; k++)
		{
			char *q = strstr(p, k == 0 ? ".params1" : ".params2");
			if (q == NULL || q > stop)
				return 0;
			q = strchr(q, '{') + 1;
			for (n = 0; n < 14; n++)
			{
				cols[k][n][row] = parse_value(q, &q);
				while (*q == ',' || *q == ' ' || *q == '\n' || *q == '\r' || *q == '\t')
					q++;
			}
			p = q;
		}
		row++;
	}
	for (k = 0; k < 2; k++)
		for (n = 0; n < 14; n++)
		{
			qsort(cols[k][n], 18, sizeof(short), cmp_short);
			(k == 0 ? out1 : out2)[n] = cols[k][n][8]; // lower middle of 18
		}
	return 1;
}

int main(void)
{
	static const short params1[14] = CTR_CUSTOM_DIFFICULTY_PARAMS1;
	static const short params2[14] = CTR_CUSTOM_DIFFICULTY_PARAMS2;
	short med1[14], med2[14];
	unsigned int laps;
	char name[32];
	int host, i;

	// ── the custom level id clears every retail range the engine tests ──
	CHECK(CTR_CUSTOM_RETAIL_LEVEL_COUNT == SCRAPBOOK + 1);
	CHECK(CTR_CUSTOM_AMBIENT_LEVEL_LIMIT == GEM_STONE_VALLEY);
	CHECK(CTR_CUSTOM_REVERB_LEVEL_LIMIT == INTRO_RACE_TODAY);
	CHECK(CTR_CUSTOM_RETAIL_TRACK_COUNT == NITRO_COURT);
	CHECK(CTR_CUSTOM_LEVEL_ID >= CTR_CUSTOM_RETAIL_LEVEL_COUNT); // no retail LevelID, no metaDataLEV row
	CHECK(CTR_CUSTOM_LEVEL_ID >= CTR_CUSTOM_REVERB_LEVEL_LIMIT); // no reverbMode row: engine default
	CHECK(CTR_CUSTOM_LEVEL_ID >= CTR_CUSTOM_AMBIENT_LEVEL_LIMIT); // no hardcoded ambience, no levAmbientSound row
	CHECK(CTR_CUSTOM_LEVEL_ID >= CTR_CUSTOM_RETAIL_TRACK_COUNT); // no difficulty row, high scores, BIGFILE group

	// ── mapping: custom during a custom race, whatever the host slot ──
	for (host = 0; host < CTR_CUSTOM_RETAIL_TRACK_COUNT; host++)
	{
		CHECK(CustomIdentity_LevelID(host, 1) == CTR_CUSTOM_LEVEL_ID);
		CHECK(CustomIdentity_LevelID(host, 1) != host);
		CHECK(CustomIdentity_LevelID(host, 0) == host); // retail races keep their own id
	}
	CHECK(CustomIdentity_LevelID(ROO_TUBES, 1) != ROO_TUBES);
	CHECK(CustomIdentity_LevelID(MAIN_MENU_LEVEL, 0) == MAIN_MENU_LEVEL);

	// ── explicit AI tuning = per-parameter median of the retail rows ──
	CHECK(retail_medians(med1, med2));
	for (i = 0; i < 14; i++)
	{
		CHECK(params1[i] == med1[i]);
		CHECK(params2[i] == med2[i]);
	}
	for (i = 1; i < 8; i++) // the speed ladders stay ordered
	{
		CHECK(params1[i] >= params1[i - 1]);
		CHECK(params2[i] >= params2[i - 1]);
	}

	// ── explicit music and sound: fixed values, the same for every custom track ──
	CHECK(CTR_CUSTOM_SONG_BANK == 0x03 && CTR_CUSTOM_FX_BANK == 0x04);

	// ── laps: pinned value wins, none means 3, invalid refuses ──
	CHECK(CustomIdentity_RaceLaps(0, 0, 0, &laps) && laps == 3);
	CHECK(CustomIdentity_RaceLaps(1, 1, 7, &laps) && laps == 7);
	CHECK(CustomIdentity_RaceLaps(1, 1, 1, &laps) && laps == 1);
	CHECK(!CustomIdentity_RaceLaps(1, 0, 5, &laps) && laps == 0); // invalid sidecar never defaults
	CHECK(!CustomIdentity_RaceLaps(1, 1, 8, &laps) && laps == 0); // past GameTracker.lapTime[7]
	CHECK(!CustomIdentity_RaceLaps(1, 1, 0, &laps) && laps == 0);
	CHECK(!CustomIdentity_RaceLaps(0, 0, 0, NULL));
	CHECK(sizeof(((struct GameTracker *)0)->lapTime) / sizeof(int) == CTR_CUSTOM_MAX_LAPS);

	// ── banner name: the package title, drawable ──
	CustomIdentity_BannerName(name, sizeof name, "Baby T Park");
	CHECK(strcmp(name, "BABY T PARK") == 0);
	CustomIdentity_BannerName(name, sizeof name, "Caf\xc3\xa9 Tr\xe2\x80\x8b" "ack");
	CHECK(strcmp(name, "CAF? TR?ACK") == 0);
	CustomIdentity_BannerName(name, sizeof name, "");
	CHECK(strcmp(name, "CUSTOM TRACK") == 0);
	CustomIdentity_BannerName(name, 6, "Longer than five");
	CHECK(strcmp(name, "LONGE") == 0);

	printf("%s: %d custom identity checks\n", failures ? "FAIL" : "PASS", checks);
	return failures != 0;
}
