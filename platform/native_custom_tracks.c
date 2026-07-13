#ifdef CTR_CUSTOM_TRACKS

#include <platform/native_custom_tracks.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

// Custom-track loader (Phase-0 spike). See native_custom_tracks.h for the model.
//
// The override table is indexed by levelID. Arcade tracks are levelID 0..17
// (< NITRO_COURT), each occupying a contiguous group of 8 BIGFILE subfiles at
// [levelID*8, levelID*8 + 8) because BI_ARCADETRACKS is 0. A configured entry
// holds the directory whose 0.bin..7.bin replace that group.

#define CUSTOM_TRACK_MAX_LEVELS  18 // NITRO_COURT: arcade tracks are levelID 0..17
#define CUSTOM_TRACK_GROUP_SIZE  8  // subfiles per arcade-track group in the BIGFILE
#define CUSTOM_TRACK_PATH_MAX    512

// Per-levelID override directory, cwd-relative. Empty string = no override.
static char s_customTrackDirs[CUSTOM_TRACK_MAX_LEVELS][CUSTOM_TRACK_PATH_MAX];
static int s_customTracksLoaded = 0;

// Scratch used to hand a resolved file path back to the caller. The game reads
// track subfiles one at a time on a single thread, so a single buffer is fine.
static char s_customTrackResolved[CUSTOM_TRACK_PATH_MAX];

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

// Recognise "custom_track_<N>" and return N (0..CUSTOM_TRACK_MAX_LEVELS-1), or
// -1 if the key is not a custom-track key or the index is out of range.
static int CustomTrack_ParseKeyIndex(const char *key)
{
	const char *prefix = "custom_track_";
	size_t prefixLen = strlen(prefix);

	if (strncmp(key, prefix, prefixLen) != 0)
		return -1;

	const char *digits = key + prefixLen;
	if (*digits == '\0')
		return -1;

	int value = 0;
	for (const char *c = digits; *c != '\0'; c++)
	{
		if (!isdigit((unsigned char)*c))
			return -1;
		value = value * 10 + (*c - '0');
		if (value >= CUSTOM_TRACK_MAX_LEVELS)
			return -1;
	}

	return value;
}

void CustomTrack_Load(void)
{
	int i;

	// Idempotent: parse config.ini exactly once per run.
	if (s_customTracksLoaded)
		return;
	s_customTracksLoaded = 1;

	for (i = 0; i < CUSTOM_TRACK_MAX_LEVELS; i++)
		s_customTrackDirs[i][0] = '\0';

	FILE *f = fopen("config.ini", "r");
	if (!f)
	{
		// No config is a valid state: behave exactly like the retail build.
		printf("[CustomTracks] config.ini not found, no track overrides active\n");
		return;
	}

	// Same INI shape as platform/native_config.c: [section] headers, key = value
	// lines, ';' or '#' comments. The custom-track keys live in [CustomTracks].
	char line[256];
	char section[64] = "";
	int activeCount = 0;

	while (fgets(line, sizeof(line), f))
	{
		char *p = CustomTrack_Trim(line);

		if (*p == '\0' || *p == ';' || *p == '#')
			continue;

		if (*p == '[')
		{
			char *end = strchr(p + 1, ']');
			if (end)
			{
				*end = '\0';
				strncpy(section, p + 1, sizeof(section) - 1);
				section[sizeof(section) - 1] = '\0';
			}
			continue;
		}

		if (strcmp(section, "CustomTracks") != 0)
			continue;

		char *eq = strchr(p, '=');
		if (!eq)
			continue;

		*eq = '\0';
		char *key = CustomTrack_Trim(p);
		char *value = CustomTrack_Trim(eq + 1);

		int levelID = CustomTrack_ParseKeyIndex(key);
		if (levelID < 0)
			continue;

		if (*value == '\0')
			continue;

		strncpy(s_customTrackDirs[levelID], value, CUSTOM_TRACK_PATH_MAX - 1);
		s_customTrackDirs[levelID][CUSTOM_TRACK_PATH_MAX - 1] = '\0';
		activeCount++;
		printf("[CustomTracks] levelID %d -> \"%s\"\n", levelID, s_customTrackDirs[levelID]);
	}

	fclose(f);

	if (activeCount == 0)
		printf("[CustomTracks] no track overrides active\n");
}

int CustomTrack_GetOverride(int subfileIndex, const char **outPath, u32 *outSize)
{
	if (!s_customTracksLoaded)
		CustomTrack_Load();

	if (subfileIndex < 0)
		return 0;

	int levelID = subfileIndex / CUSTOM_TRACK_GROUP_SIZE;
	int fileInGroup = subfileIndex % CUSTOM_TRACK_GROUP_SIZE;

	if (levelID >= CUSTOM_TRACK_MAX_LEVELS)
		return 0; // not an arcade-track subfile

	if (s_customTrackDirs[levelID][0] == '\0')
		return 0; // no override configured for this track

	snprintf(s_customTrackResolved, sizeof(s_customTrackResolved), "%s/%d.bin",
	         s_customTrackDirs[levelID], fileInGroup);

	struct stat st;
	if (stat(s_customTrackResolved, &st) != 0 || st.st_size <= 0)
	{
		// Mapped track but this slot's file is missing or empty: warn loudly and
		// fall back to the BIGFILE so the game still loads the retail track.
		printf("[CustomTracks] WARNING: levelID %d subfile %d: \"%s\" missing, "
		       "falling back to BIGFILE\n", levelID, fileInGroup, s_customTrackResolved);
		return 0;
	}

	if (outPath)
		*outPath = s_customTrackResolved;
	if (outSize)
		*outSize = (u32)st.st_size;

	printf("[CustomTracks] serving levelID %d subfile %d from \"%s\" (%ld bytes)\n",
	       levelID, fileInGroup, s_customTrackResolved, (long)st.st_size);
	return 1;
}

int CustomTrack_ReadFile(const char *path, void *dst, u32 bufBytes, u32 fileBytes)
{
	if (path == NULL || dst == NULL)
		return 0;

	if (fileBytes > bufBytes)
		fileBytes = bufBytes; // defensive: never write past the caller's buffer

	FILE *f = fopen(path, "rb");
	if (!f)
	{
		printf("[CustomTracks] ERROR: could not reopen \"%s\"\n", path);
		return 0;
	}

	size_t got = fread(dst, 1, (size_t)fileBytes, f);
	fclose(f);

	if (got != (size_t)fileBytes)
	{
		printf("[CustomTracks] ERROR: short read on \"%s\" (%zu of %u bytes)\n",
		       path, got, fileBytes);
		return 0;
	}

	// Zero the sector-round padding tail, matching a BIGFILE sector read whose
	// trailing padding is not part of the subfile's payload.
	if (bufBytes > fileBytes)
		memset((char *)dst + fileBytes, 0, (size_t)(bufBytes - fileBytes));

	return 1;
}

#endif // CTR_CUSTOM_TRACKS
