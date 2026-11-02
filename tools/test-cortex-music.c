// Cortex Vortex's bundled music. The loader keeps the .sca beside the bundled
// LEV only when a content-plan race arms the exact bundled pair and the .sca's
// sha256 is the pinned one; a bad or absent file costs the music, never the
// track. Then the race selection: which loads try the music and which later
// banks the admission check sizes (include/platform/native_cortex_music_select.h).
// Compiles the real loader (the pattern of tools/test-custom-track-load.c); run
// from the repository root.
//
//   cc -Wall -Wextra -m32 -DCTR_CUSTOM_TRACKS -DCTR_NATIVE -DBUILD=926
//      -I . -I include -o /tmp/test-cortex-music tools/test-cortex-music.c
//   /tmp/test-cortex-music
//
// What only play can prove: that the song and bank actually sound right on the
// host level. This pins the file handling and the choice that decide whether
// they are tried.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CTR_CUSTOM_TRACKS 1

#include "platform/native_custom_tracks.c"
#include "platform/native_cortex_music_select.h"

struct sData sdata_static;
struct Data data;
NativeConfig g_config;

static int failures;
#define EXPECT(cond, what) do { if (!(cond)) { printf("FAIL %s\n", what); failures++; } } while (0)

#define HOST 13
#define SCA_BYTES 401108

static char s_dir[] = "/tmp/ctr-cortex-music-XXXXXX";
static char s_lev[512], s_vrm[512], s_sca[512];

static int run(const char *cmd)
{
	return system(cmd);
}

// The bundled pins, as the content plan arms them (AP_ContentArmTrack).
static struct CustomTrackSeedDescriptor cortex_descriptor(void)
{
	struct CustomTrackSeedDescriptor d;
	memset(&d, 0, sizeof d);
	d.standalone = 1;
	d.laps = 3;
	d.hostLevelID = HOST;
	d.replacesCupLevelID = -1;
	d.flagAiNav = 1;
	d.flagSpawns = 8;
	d.flagCheckpoints = 255;
	snprintf(d.levSha256, sizeof d.levSha256, "%s", CORTEX_LEV_SHA256);
	snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", CORTEX_VRM_SHA256);
	return d;
}

static int arm(void)
{
	struct CustomTrackSeedDescriptor d = cortex_descriptor();
	if (!CustomTrack_UseContentFiles(s_lev, s_vrm, "CORTEX VORTEX"))
		return 0;
	return CustomTrack_ApplySeedDescriptor(&d);
}

// The plan's Cortex Vortex pad entry, as AP_ContentPreparePad builds it.
static struct CustomTrackRaceContext pad_race(void)
{
	struct CustomTrackRaceContext race;
	memset(&race, 0, sizeof race);
	race.hostLevelID = HOST;
	race.physicalPad = 3;
	race.returnHub = 26;
	race.cortex = 1;
	race.slot = 0;
	race.laps = 3;
	race.trophyLocation = 35016300;
	strcpy(race.entryID, "entry/cortex");
	strcpy(race.trackID, "cortex_vortex");
	strcpy(race.levSha256, CORTEX_LEV_SHA256);
	strcpy(race.vrmSha256, CORTEX_VRM_SHA256);
	return race;
}

// The race's own load is the only one that plays the music.
static void enter_and_leave(const char *what)
{
	struct CustomTrackRaceContext race = pad_race();
	char label[160];

	snprintf(label, sizeof label, "%s: staged", what);
	EXPECT(CustomTrack_StageStandaloneRace(&race) == 1, label);
	snprintf(label, sizeof label, "%s: not before its load", what);
	EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, label);
	CustomTrack_OnRaceLoadRequested(HOST);
	snprintf(label, sizeof label, "%s: the race serves the music", what);
	EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 1, label);
	snprintf(label, sizeof label, "%s: no other level", what);
	EXPECT(CustomTrack_CortexMusicServing(3, 0, 0) == 0, label);
	CustomTrack_OnRaceLoadRequested(26);
	snprintf(label, sizeof label, "%s: hub return ends it", what);
	EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, label);
	// Oxide Station's own pad afterwards: a retail load of 13, no music.
	CustomTrack_SelectRetailRace();
	CustomTrack_OnRaceLoadRequested(HOST);
	snprintf(label, sizeof label, "%s: Oxide Station's own race keeps its music", what);
	EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, label);
	CustomTrack_OnRaceLoadRequested(26);
}

static void loader(void)
{
	const void *bytes;
	size_t size;
	char cmd[1024];

	// The bundled pair with its pinned music: the bytes are served while armed.
	EXPECT(arm() == 1, "cortex pair arms");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 1 && size == SCA_BYTES && bytes != NULL, "pinned music is served");
	enter_and_leave("pinned music");

	// A withdrawn seed: nothing is served.
	CustomTrack_ClearSeedDescriptor();
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0 && bytes == NULL && size == 0, "no music after the seed goes");
	EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, "no serving after the seed goes");

	// A changed file: the track still arms and races, the music is refused.
	snprintf(cmd, sizeof cmd, "printf 'x' | dd of='%s' bs=1 seek=100 conv=notrunc 2>/dev/null", s_sca);
	run(cmd);
	EXPECT(arm() == 1, "track arms with changed music");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0, "changed music is refused");
	{
		struct CustomTrackRaceContext race = pad_race();
		EXPECT(CustomTrack_StageStandaloneRace(&race) == 1, "changed music: the race still stages");
		CustomTrack_OnRaceLoadRequested(HOST);
		EXPECT(CustomTrack_ServingLoad(HOST, 0, 0) == 1, "changed music: the track is served");
		EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, "changed music: Oxide Station's music");
		CustomTrack_OnRaceLoadRequested(26);
	}

	// A missing file: same.
	unlink(s_sca);
	CustomTrack_ClearSeedDescriptor();
	EXPECT(arm() == 1, "track arms without music");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0, "missing music serves nothing");
	EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, "missing music: Oxide Station's music");
	CustomTrack_ClearSeedDescriptor();

	// An ordinary package entry (a custom slot, not the plan's Cortex Vortex
	// entry) that carries the very same files: the track races, the music does
	// not play.
	snprintf(cmd, sizeof cmd, "cp 'assets/tracks/cortex-vortex/CVortex Arcade All.sca' '%s'", s_sca);
	run(cmd);
	EXPECT(arm() == 1, "same files arm");
	{
		struct CustomTrackRaceContext race = pad_race();
		race.cortex = 0;
		race.slot = 1;
		strcpy(race.trackID, "custom/cortex-copy/1.0.0");
		EXPECT(CustomTrack_StageStandaloneRace(&race) == 1, "ordinary entry stages");
		CustomTrack_OnRaceLoadRequested(HOST);
		EXPECT(CustomTrack_ServingLoad(HOST, 0, 0) == 1, "ordinary entry: the track is served");
		EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, "ordinary entry: no Cortex music");
		CustomTrack_OnRaceLoadRequested(26);
	}
	CustomTrack_ClearSeedDescriptor();

	// A load holds the bytes from the bank to the song: a seed withdrawn in
	// between does not free them; the release does, and nothing is served after.
	EXPECT(arm() == 1, "re-armed for the hold");
	{
		const void *held;
		size_t heldSize;
		const unsigned char *b;
		EXPECT(CustomTrack_CortexMusicHold(&held, &heldSize) == 1 && heldSize == SCA_BYTES, "load holds the music");
		CustomTrack_ClearSeedDescriptor();
		EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0, "withdrawn seed serves no new music");
		b = (const unsigned char *)held;
		EXPECT(memcmp(b, "SCA", 3) == 0, "held bytes stay readable");
		EXPECT(s_cortexMusicHeld == held && s_cortexMusic == NULL, "held buffer kept, current dropped");
		CustomTrack_CortexMusicRelease();
		EXPECT(s_cortexMusicHeld == NULL, "release lets go");
		EXPECT(CustomTrack_CortexMusicHold(&held, &heldSize) == 0, "nothing to hold without a seed");
	}
	// Holding the current bytes and releasing while still armed keeps them.
	EXPECT(arm() == 1, "re-armed after the hold");
	{
		const void *held;
		size_t heldSize;
		EXPECT(CustomTrack_CortexMusicHold(&held, &heldSize) == 1, "hold again");
		CustomTrack_CortexMusicRelease();
		EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 1 && bytes == held, "armed bytes survive a release");
	}
	CustomTrack_ClearSeedDescriptor();
}

static void pins(void)
{
	char line[512];
	int lev = 0, vrm = 0, notices = 0;
	FILE *f = fopen("ap/ap_seedcfg.h", "r");

	// The loader's pins are the client's (ap/ap_seedcfg.h) and the notices'.
	while (f && fgets(line, sizeof line, f))
	{
		if (strstr(line, "#define CTR_CFG_CORTEX_LEV_SHA256") && strstr(line, CORTEX_LEV_SHA256))
			lev = 1;
		if (strstr(line, "#define CTR_CFG_CORTEX_VRM_SHA256") && strstr(line, CORTEX_VRM_SHA256))
			vrm = 1;
	}
	if (f)
		fclose(f);
	f = fopen("THIRD_PARTY_NOTICES.md", "r");
	while (f && fgets(line, sizeof line, f))
		if (strstr(line, "SCA SHA-256") && strstr(line, CORTEX_SCA_SHA256))
			notices = 1;
	if (f)
		fclose(f);
	EXPECT(lev && vrm, "LEV and VRM pins match ap/ap_seedcfg.h");
	EXPECT(notices, "SCA pin matches THIRD_PARTY_NOTICES.md");
}

static void selection(void)
{
	const unsigned int adv = ADVENTURE_MODE, cup = ADVENTURE_CUP;

	// The copied mask bits are the engine's.
	EXPECT(CORTEX_MUSIC_ADVENTURE_MODE == (unsigned int)ADVENTURE_MODE, "adventure bit");
	EXPECT(CORTEX_MUSIC_ARCADE_MODE == (unsigned int)ARCADE_MODE, "arcade bit");
	EXPECT(CORTEX_MUSIC_ADVENTURE_CUP == (unsigned int)ADVENTURE_CUP, "cup bit");
	EXPECT(CORTEX_MUSIC_NOT_8_DRIVERS ==
	           (ADVENTURE_BOSS | (unsigned int)ADVENTURE_ARENA | (unsigned int)CRYSTAL_CHALLENGE | (unsigned int)RELIC_RACE),
	       "stage 1 mask");

	// Not the Cortex Vortex race (Oxide Station itself, any other track):
	// nothing changes, whatever the mode.
	EXPECT(CortexMusic_Banks(0, adv, 0, 0) == CORTEX_MUSIC_OFF, "oxide station adventure race");
	EXPECT(CortexMusic_Banks(0, adv | cup, 4, 0) == CORTEX_MUSIC_OFF, "oxide station purple cup leg");
	EXPECT(CortexMusic_Banks(0, ARCADE_MODE, 0, 0) == CORTEX_MUSIC_OFF, "oxide station arcade");

	// The pad race and a cup leg take bank 54, as stage 1 loads it.
	EXPECT(CortexMusic_Banks(1, adv, 0, 1) == CORTEX_MUSIC_BANK54, "pad race");
	EXPECT(CortexMusic_Banks(1, adv | cup, 0, 1) == CORTEX_MUSIC_BANK54, "red cup leg");
	EXPECT(CortexMusic_Banks(1, adv | cup, 4, 1) == CORTEX_MUSIC_BANK54, "purple cup leg (served, so displaced)");
	EXPECT(CortexMusic_Banks(1, ARCADE_MODE, 0, 1) == CORTEX_MUSIC_BANK54, "arcade race");

	// No bank 54: every player's bank, or the undisplaced purple roster's five.
	EXPECT(CortexMusic_Banks(1, adv | RELIC_RACE, 0, 1) == CORTEX_MUSIC_PER_PLAYER, "relic race");
	EXPECT(CortexMusic_Banks(1, adv | CRYSTAL_CHALLENGE, 0, 1) == CORTEX_MUSIC_PER_PLAYER, "crystal challenge");
	EXPECT(CortexMusic_Banks(1, adv | cup, 4, 0) == CORTEX_MUSIC_ROSTER5, "undisplaced purple roster");
}

int main(void)
{
	char cmd[1024];

	if (!mkdtemp(s_dir))
		return 2;
	snprintf(cmd, sizeof cmd, "mkdir -p '%s/tracks/cortex-vortex' && cp assets/tracks/cortex-vortex/* '%s/tracks/cortex-vortex/'",
	         s_dir, s_dir);
	if (run(cmd) != 0)
		return 2;
	snprintf(s_lev, sizeof s_lev, "%s/tracks/cortex-vortex/CVortex Arcade All.lev", s_dir);
	snprintf(s_vrm, sizeof s_vrm, "%s/tracks/cortex-vortex/CVortex Arcade All.vrm", s_dir);
	snprintf(s_sca, sizeof s_sca, "%s/tracks/cortex-vortex/CVortex Arcade All.sca", s_dir);

	loader();
	pins();
	selection();

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", s_dir);
	run(cmd);
	printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
