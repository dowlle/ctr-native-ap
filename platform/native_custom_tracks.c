#ifdef CTR_CUSTOM_TRACKS

#include <platform/native_custom_tracks.h>
#include <platform/native_sha256.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

// Custom-track loader, engine half. See native_custom_tracks.h for the model
// and native_custom_tracks_policy.h for every decision this file asks.

#define CUSTOM_TRACK_PATH_MAX 512
#define CUSTOM_TRACK_HASH_CHUNK 65536

// One source file: where it is, what it must hash to, and what verifying it
// concluded. Two of these, one per role (VRM, LEV).
struct CustomTrackSource
{
	char path[CUSTOM_TRACK_PATH_MAX];
	char expectedHash[NATIVE_SHA256_HEX_BYTES];
	u32 verifiedSize;
	int verdict; // enum CustomTrackVerdict
};

static struct CustomTrackFeatureConfig s_customTrackConfig;
static struct CustomTrackSource s_customTrackVrm;
static struct CustomTrackSource s_customTrackLev;
static int s_customTracksLoaded = 0;

// Scratch used to hand a resolved path back to the caller. The game reads track
// subfiles one at a time on a single thread, so a single buffer is fine.
static char s_customTrackResolved[CUSTOM_TRACK_PATH_MAX];

static const char *CustomTrack_VerdictText(int verdict)
{
	switch (verdict)
	{
	case CTR_CT_VERDICT_OK:
		return "ok";
	case CTR_CT_VERDICT_NO_PATH:
		return "no file configured";
	case CTR_CT_VERDICT_FILE_MISSING:
		return "file missing or empty";
	case CTR_CT_VERDICT_NO_EXPECTED:
		return "no expected sha256 configured";
	case CTR_CT_VERDICT_BAD_EXPECTED:
		return "expected sha256 is not 64 hex digits";
	case CTR_CT_VERDICT_READ_FAILED:
		return "file could not be read";
	case CTR_CT_VERDICT_HASH_MISMATCH:
		return "sha256 mismatch";
	default:
		return "unknown";
	}
}

static char *CustomTrack_Trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		end--;
	*(end + 1) = '\0';
	return s;
}

// Parse a non-negative decimal integer, or -1 if the whole string is not one.
// Deliberately strict: a typo in config must read as "not configured" rather
// than as a partially-parsed levelID.
static int CustomTrack_ParseInt(const char *s)
{
	int value = 0;
	const char *c;

	if (s == NULL || *s == '\0')
		return -1;

	for (c = s; *c != '\0'; c++)
	{
		if (!isdigit((unsigned char)*c))
			return -1;
		value = value * 10 + (*c - '0');
		if (value > 100000)
			return -1;
	}

	return value;
}

// Copy a config value into a fixed field, always NUL-terminated. Written with
// an explicit length rather than strncpy so an over-long value truncates
// deliberately instead of tripping -Wstringop-truncation on a same-sized copy.
static void CustomTrack_CopyField(char *dst, size_t dstSize, const char *src)
{
	size_t len = strlen(src);

	if (len > dstSize - 1)
		len = dstSize - 1;

	memcpy(dst, src, len);
	dst[len] = '\0';
}

// Hash a whole file and compare against its configured digest. Sets
// source->verdict and, on OK, source->verifiedSize.
static void CustomTrack_VerifySource(struct CustomTrackSource *source, const char *roleName)
{
	struct NativeSha256Ctx ctx;
	unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
	char actualHex[NATIVE_SHA256_HEX_BYTES];
	unsigned char chunk[CUSTOM_TRACK_HASH_CHUNK];
	struct stat st;
	FILE *f;
	u32 total = 0;
	size_t got;

	source->verifiedSize = 0;

	if (source->path[0] == '\0')
	{
		source->verdict = CTR_CT_VERDICT_NO_PATH;
		return;
	}

	if (source->expectedHash[0] == '\0')
	{
		source->verdict = CTR_CT_VERDICT_NO_EXPECTED;
		printf("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
		       CustomTrack_VerdictText(source->verdict));
		return;
	}

	if (strlen(source->expectedHash) != NATIVE_SHA256_DIGEST_BYTES * 2)
	{
		source->verdict = CTR_CT_VERDICT_BAD_EXPECTED;
		printf("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
		       CustomTrack_VerdictText(source->verdict));
		return;
	}

	if (stat(source->path, &st) != 0 || st.st_size <= 0)
	{
		source->verdict = CTR_CT_VERDICT_FILE_MISSING;
		printf("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
		       CustomTrack_VerdictText(source->verdict));
		return;
	}

	f = fopen(source->path, "rb");
	if (f == NULL)
	{
		source->verdict = CTR_CT_VERDICT_READ_FAILED;
		printf("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
		       CustomTrack_VerdictText(source->verdict));
		return;
	}

	NativeSha256_Init(&ctx);
	while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
	{
		NativeSha256_Update(&ctx, chunk, got);
		total += (u32)got;
	}

	if (ferror(f))
	{
		fclose(f);
		source->verdict = CTR_CT_VERDICT_READ_FAILED;
		printf("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
		       CustomTrack_VerdictText(source->verdict));
		return;
	}
	fclose(f);

	NativeSha256_Final(&ctx, digest);
	NativeSha256_ToHex(digest, actualHex);

	if (!NativeSha256_HexEquals(source->expectedHash, actualHex))
	{
		source->verdict = CTR_CT_VERDICT_HASH_MISMATCH;
		printf("[CustomTracks] REFUSED %s \"%s\": %s\n", roleName, source->path,
		       CustomTrack_VerdictText(source->verdict));
		printf("[CustomTracks]   expected %s\n", source->expectedHash);
		printf("[CustomTracks]   actual   %s\n", actualHex);
		return;
	}

	source->verdict = CTR_CT_VERDICT_OK;
	source->verifiedSize = total;
	printf("[CustomTracks] verified %s \"%s\" (%u bytes, sha256 %s)\n", roleName, source->path,
	       (unsigned)total, actualHex);
}

void CustomTrack_Load(void)
{
	FILE *f;
	char line[1024];
	char section[64] = "";

	// Idempotent: parse and verify exactly once per run.
	if (s_customTracksLoaded)
		return;
	s_customTracksLoaded = 1;

	memset(&s_customTrackConfig, 0, sizeof(s_customTrackConfig));
	memset(&s_customTrackVrm, 0, sizeof(s_customTrackVrm));
	memset(&s_customTrackLev, 0, sizeof(s_customTrackLev));

	// Disarmed defaults. raceCupID 4 is the Purple Gem Cup and raceLaps 7 is the
	// ruled lap count, so a config that only turns the feature on and names the
	// files gets the ruled behaviour without restating it.
	s_customTrackConfig.mappedLevelID = -1;
	s_customTrackConfig.raceCupID = 4;
	s_customTrackConfig.raceLaps = 7;
	s_customTrackConfig.raceBoxes = 1;
	s_customTrackVrm.verdict = CTR_CT_VERDICT_NO_PATH;
	s_customTrackLev.verdict = CTR_CT_VERDICT_NO_PATH;

	f = fopen("config.ini", "r");
	if (f == NULL)
	{
		// No config is a valid state: behave exactly like the retail build.
		printf("[CustomTracks] config.ini not found, loader disarmed\n");
		return;
	}

	// Same INI shape as platform/native_config.c: [section] headers, key = value
	// lines, ';' or '#' comments. The loader's keys live in [CustomTracks].
	while (fgets(line, sizeof(line), f))
	{
		char *p = CustomTrack_Trim(line);
		char *eq;
		char *key;
		char *value;

		if (*p == '\0' || *p == ';' || *p == '#')
			continue;

		if (*p == '[')
		{
			char *end = strchr(p + 1, ']');
			if (end)
			{
				*end = '\0';
				CustomTrack_CopyField(section, sizeof(section), p + 1);
			}
			continue;
		}

		if (strcmp(section, "CustomTracks") != 0)
			continue;

		eq = strchr(p, '=');
		if (eq == NULL)
			continue;

		*eq = '\0';
		key = CustomTrack_Trim(p);
		value = CustomTrack_Trim(eq + 1);

		if (*value == '\0')
			continue;

		if (strcmp(key, "custom_track_level") == 0)
			s_customTrackConfig.mappedLevelID = CustomTrack_ParseInt(value);
		else if (strcmp(key, "custom_track_vrm") == 0)
			CustomTrack_CopyField(s_customTrackVrm.path, sizeof(s_customTrackVrm.path), value);
		else if (strcmp(key, "custom_track_lev") == 0)
			CustomTrack_CopyField(s_customTrackLev.path, sizeof(s_customTrackLev.path), value);
		else if (strcmp(key, "custom_track_vrm_sha256") == 0)
			CustomTrack_CopyField(s_customTrackVrm.expectedHash, sizeof(s_customTrackVrm.expectedHash), value);
		else if (strcmp(key, "custom_track_lev_sha256") == 0)
			CustomTrack_CopyField(s_customTrackLev.expectedHash, sizeof(s_customTrackLev.expectedHash), value);
		else if (strcmp(key, "custom_track_race") == 0)
			s_customTrackConfig.raceEnabled = (CustomTrack_ParseInt(value) > 0) ? 1 : 0;
		else if (strcmp(key, "custom_track_race_cup") == 0)
			s_customTrackConfig.raceCupID = CustomTrack_ParseInt(value);
		else if (strcmp(key, "custom_track_race_laps") == 0)
			s_customTrackConfig.raceLaps = CustomTrack_ParseInt(value);
		else if (strcmp(key, "custom_track_race_boxes") == 0)
			s_customTrackConfig.raceBoxes = (CustomTrack_ParseInt(value) > 0) ? 1 : 0;
	}

	fclose(f);

	if (s_customTrackVrm.path[0] == '\0' && s_customTrackLev.path[0] == '\0')
	{
		printf("[CustomTracks] no track configured, loader disarmed\n");
		return;
	}

	if (!CustomTrackPolicy_LevelIDIsMappable(s_customTrackConfig.mappedLevelID))
	{
		// Out-of-range slots are refused rather than clamped: data.ArcadeDifficulty
		// is [18] and data.metaDataLEV is [0x41], both indexed by gGT->levelID
		// with no range check.
		printf("[CustomTracks] REFUSED: custom_track_level must be an arcade slot 0..%d (got %d)\n",
		       CTR_CT_MAX_LEVELS - 1, s_customTrackConfig.mappedLevelID);
		s_customTrackConfig.mappedLevelID = -1;
		return;
	}

	CustomTrack_VerifySource(&s_customTrackVrm, "vrm");
	CustomTrack_VerifySource(&s_customTrackLev, "lev");

	if (s_customTrackVrm.verdict == CTR_CT_VERDICT_OK && s_customTrackLev.verdict == CTR_CT_VERDICT_OK)
	{
		s_customTrackConfig.contentVerified = 1;
		printf("[CustomTracks] content verified: levelID %d group is subfiles %d..%d\n",
		       s_customTrackConfig.mappedLevelID,
		       s_customTrackConfig.mappedLevelID * CTR_CT_GROUP_SIZE,
		       s_customTrackConfig.mappedLevelID * CTR_CT_GROUP_SIZE + CTR_CT_GROUP_SIZE - 1);
	}
	else
	{
		// Loud and total: the retail track loads AND the event destination keeps
		// its vanilla legs. Never a silent wrong-content race.
		printf("[CustomTracks] DISARMED: track content did not verify (vrm: %s, lev: %s)\n",
		       CustomTrack_VerdictText(s_customTrackVrm.verdict),
		       CustomTrack_VerdictText(s_customTrackLev.verdict));
		printf("[CustomTracks] retail track bytes will be served and the event destination stays vanilla\n");
		return;
	}

	if (!s_customTrackConfig.raceEnabled)
	{
		// Serving is conditional on the event race being the load in flight, so
		// with the race off there is no load that qualifies and the track is
		// never served. Verification still ran, which makes this a useful
		// config/hash check, but nothing in the game changes.
		printf("[CustomTracks] event destination off: nothing will be served\n");
	}

	if (s_customTrackConfig.raceEnabled)
	{
		if (CustomTrackPolicy_RaceLaps(&s_customTrackConfig, s_customTrackConfig.raceCupID, 1) == 0)
		{
			printf("[CustomTracks] REFUSED: custom_track_race_laps must be 1..7 (got %d); event destination stays vanilla\n",
			       s_customTrackConfig.raceLaps);
			s_customTrackConfig.raceEnabled = 0;
		}
		else
		{
			printf("[CustomTracks] event destination: cup %d becomes a single %d-lap race on levelID %d\n",
			       s_customTrackConfig.raceCupID, s_customTrackConfig.raceLaps,
			       s_customTrackConfig.mappedLevelID);
			printf("[CustomTracks] AP boxes on the event race: %s\n",
			       s_customTrackConfig.raceBoxes ? "allowed" : "denied");
			printf("[CustomTracks] levelID %d serves custom bytes ONLY for that race; "
			       "its retail race pad still loads retail bytes\n",
			       s_customTrackConfig.mappedLevelID);
		}
	}
}

const struct CustomTrackFeatureConfig *CustomTrack_Config(void)
{
	if (!s_customTracksLoaded)
		CustomTrack_Load();

	return &s_customTrackConfig;
}

int CustomTrack_GetOverride(int subfileIndex, const struct CustomTrackLoadContext *ctx, const char **outPath, u32 *outSize)
{
	const struct CustomTrackSource *source;
	struct stat st;
	int role;

	if (!s_customTracksLoaded)
		CustomTrack_Load();

	// Cheapest terms first: this runs on EVERY BIGFILE read in the game, and for
	// all but the event race's own two subfiles it must fall out immediately.
	if (!s_customTrackConfig.contentVerified)
		return 0;

	role = CustomTrackPolicy_SubfileRole(subfileIndex, s_customTrackConfig.mappedLevelID);
	if (role == CTR_CT_ROLE_NONE)
		return 0;

	// The index is in the mapped group, but that alone does not make this read
	// the event race's -- the host slot's own retail race reads the same eight
	// indices. Only the load context can tell them apart, and it is what keeps
	// that retail race retail.
	if (!CustomTrackPolicy_ShouldServe(&s_customTrackConfig, ctx))
		return 0;

	source = (role == CTR_CT_ROLE_LEV) ? &s_customTrackLev : &s_customTrackVrm;

	// The content was hashed at startup. Re-hashing on every subfile read would
	// cost a multi-MiB digest inside the load path, so the serve-time check is
	// the cheap half: the file must still be exactly the size that was verified.
	// That catches the realistic accident -- a file replaced or truncated while
	// the game is running -- without turning every level load into a hash.
	if (stat(source->path, &st) != 0 || (u32)st.st_size != source->verifiedSize)
	{
		printf("[CustomTracks] REFUSED subfile %d: \"%s\" changed since it was verified, "
		       "falling back to BIGFILE\n",
		       subfileIndex, source->path);
		return 0;
	}

	CustomTrack_CopyField(s_customTrackResolved, sizeof(s_customTrackResolved), source->path);

	if (outPath)
		*outPath = s_customTrackResolved;
	if (outSize)
		*outSize = source->verifiedSize;

	printf("[CustomTracks] serving subfile %d (%s) from \"%s\" (%u bytes)\n", subfileIndex,
	       (role == CTR_CT_ROLE_LEV) ? "lev" : "vrm", s_customTrackResolved,
	       (unsigned)source->verifiedSize);
	return 1;
}

int CustomTrack_ReadFile(const char *path, void *dst, u32 bufBytes, u32 fileBytes)
{
	FILE *f;
	size_t got;

	if (path == NULL || dst == NULL)
		return 0;

	if (fileBytes > bufBytes)
		fileBytes = bufBytes; // defensive: never write past the caller's buffer

	f = fopen(path, "rb");
	if (f == NULL)
	{
		printf("[CustomTracks] ERROR: could not reopen \"%s\"\n", path);
		return 0;
	}

	got = fread(dst, 1, (size_t)fileBytes, f);
	fclose(f);

	if (got != (size_t)fileBytes)
	{
		printf("[CustomTracks] ERROR: short read on \"%s\" (%u of %u bytes)\n", path, (unsigned)got,
		       (unsigned)fileBytes);
		return 0;
	}

	// Zero the sector-round padding tail, matching a BIGFILE sector read whose
	// trailing padding is not part of the subfile's payload.
	if (bufBytes > fileBytes)
		memset((char *)dst + fileBytes, 0, (size_t)(bufBytes - fileBytes));

	return 1;
}

int CustomTrack_CupRaceRedirectActive(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_ShouldRedirectCup(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_CupRaceLevelID(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_RaceLevelID(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_CupRaceLaps(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_RaceLaps(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_CupIsComplete(int cupID, int isAdventureCup, int trackIndexAfterIncrement)
{
	return CustomTrackPolicy_CupIsComplete(CustomTrack_Config(), cupID, isAdventureCup, trackIndexAfterIncrement);
}

int CustomTrack_CupLegCount(int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_CupLegCount(CustomTrack_Config(), cupID, isAdventureCup);
}

int CustomTrack_BoxVerdict(int levelID, int adventureCupActive, int cupID)
{
	struct CustomTrackLoadContext ctx;

	ctx.levelID = levelID;
	ctx.adventureCupActive = adventureCupActive;
	ctx.cupID = cupID;

	return CustomTrackPolicy_BoxVerdict(CustomTrack_Config(), &ctx);
}

int CustomTrack_RaceFeatureEnabled(void)
{
	const struct CustomTrackFeatureConfig *cfg = CustomTrack_Config();

	return (cfg->raceEnabled && cfg->contentVerified) ? 1 : 0;
}

#endif // CTR_CUSTOM_TRACKS
