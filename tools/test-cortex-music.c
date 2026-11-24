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
#define CTR_CUSTOM_PACKAGES 1

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
	// The reported alpha3 race was a cup leg. Its identity is the content
	// context, with no legacy Cortex pad latch, just like a standalone race.
	{
		struct CustomTrackRaceContext race = pad_race();
		race.cup = 1; race.leg = 0;
		EXPECT(CustomTrack_StageStandaloneRace(&race) == 1, "Cortex cup leg stages");
		CustomTrack_OnRaceLoadRequested(HOST);
		EXPECT(CustomTrack_CortexMusicServing(HOST, 1, 4) == 1, "Cortex cup leg serves its pinned music");
		CustomTrack_OnRaceLoadRequested(26);
		EXPECT(CustomTrack_CortexMusicServing(HOST, 1, 4) == 0, "leaving the cup stops its music");
	}

	// CV1: a Relic Race arms the bundled relic pair (its own pins, same
	// folder). It is still the plan's Cortex Vortex entry, so the music plays.
	{
		struct CustomTrackSeedDescriptor d = cortex_descriptor();
		struct CustomTrackRaceContext race = pad_race();
		char relicLev[sizeof s_lev], relicVrm[sizeof s_vrm];
		snprintf(relicLev, sizeof relicLev, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_LEV_FILE, s_dir);
		snprintf(relicVrm, sizeof relicVrm, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_VRM_FILE, s_dir);
		snprintf(d.levSha256, sizeof d.levSha256, "%s", CTR_CFG_CORTEX_RELIC_LEV_SHA256);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", CTR_CFG_CORTEX_RELIC_VRM_SHA256);
		d.soloRace = 1;
		EXPECT(CustomTrack_UseContentFiles(relicLev, relicVrm, "CORTEX VORTEX") && CustomTrack_ApplySeedDescriptor(&d) == 1,
		       "cortex relic pair arms");
		EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 1 && size == SCA_BYTES, "relic pair: pinned music is served");
		strcpy(race.levSha256, CTR_CFG_CORTEX_RELIC_LEV_SHA256);
		strcpy(race.vrmSha256, CTR_CFG_CORTEX_RELIC_VRM_SHA256);
		EXPECT(CustomTrack_StageStandaloneRace(&race) == 1, "relic pair: the Relic Race stages");
		CustomTrack_OnRaceLoadRequested(HOST);
		EXPECT(CustomTrack_ServingLoad(HOST, 0, 0) == 1, "relic pair: the track is served");
		EXPECT(CustomTrack_CortexMusicServing(HOST, 0, 0) == 1, "relic pair: the race serves the music");
		CustomTrack_OnRaceLoadRequested(26);
		CustomTrack_ClearSeedDescriptor();
		EXPECT(arm() == 1, "main pair arms again after the relic pair");
	}

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


// A content-plan package's own music (A2-30): any package whose shared store
// entry pins a Saphi .sca plays it on its own host slot. The pair here is a
// byte-changed copy of the bundled pair (so it is not Cortex Vortex), the .sca
// is the bundled one standing in for a Saphi file.
#define PKG_HOST 5
static char s_pkgLev[512], s_pkgVrm[512], s_pkgSca[512], s_pkgLevSha[65], s_pkgScaSha[65];

static void sha_of(const char *path, char out[65])
{
	char cmd[1200], line[200] = "";
	FILE *p;
	snprintf(cmd, sizeof cmd, "sha256sum '%s'", path);
	p = popen(cmd, "r");
	if (p && fgets(line, sizeof line, p))
		snprintf(out, 65, "%.64s", line);
	if (p)
		pclose(p);
}

static int arm_package(int withMusic, const char *musicLevSha)
{
	struct CustomTrackSeedDescriptor d = cortex_descriptor();
	char vrmSha[65];
	sha_of(s_pkgVrm, vrmSha);
	d.hostLevelID = PKG_HOST;
	snprintf(d.levSha256, sizeof d.levSha256, "%s", s_pkgLevSha);
	snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", vrmSha);
	if (!CustomTrack_UseContentFiles(s_pkgLev, s_pkgVrm, "ACID LAKE"))
		return 0;
	if (withMusic)
		CustomTrack_UseContentMusic(s_pkgSca, s_pkgScaSha, SCA_BYTES, musicLevSha);
	return CustomTrack_ApplySeedDescriptor(&d);
}

static void serve_package_race(int expectMusic, const char *what)
{
	struct CustomTrackRaceContext race = pad_race();
	char label[160];
	race.hostLevelID = PKG_HOST;
	race.cortex = 0;
	race.slot = 1;
	strcpy(race.trackID, "custom/acid-lake/1.0.0");
	strcpy(race.levSha256, s_pkgLevSha);
	sha_of(s_pkgVrm, race.vrmSha256);
	snprintf(label, sizeof label, "%s: stages", what);
	EXPECT(CustomTrack_StageStandaloneRace(&race) == 1, label);
	CustomTrack_OnRaceLoadRequested(PKG_HOST);
	snprintf(label, sizeof label, "%s: the track is served", what);
	EXPECT(CustomTrack_ServingLoad(PKG_HOST, 0, 0) == 1, label);
	snprintf(label, sizeof label, "%s: music %s", what, expectMusic ? "plays on its host slot" : "falls back");
	EXPECT(CustomTrack_CortexMusicServing(PKG_HOST, 0, 0) == expectMusic, label);
	snprintf(label, sizeof label, "%s: not on another level", what);
	EXPECT(CustomTrack_CortexMusicServing(3, 0, 0) == 0 && CustomTrack_CortexMusicServing(HOST, 0, 0) == 0, label);
	CustomTrack_OnRaceLoadRequested(26);
	snprintf(label, sizeof label, "%s: hub return ends it", what);
	EXPECT(CustomTrack_CortexMusicServing(PKG_HOST, 0, 0) == 0, label);
}

static void package_music(void)
{
	const void *bytes;
	size_t size;
	char cmd[2400];

	snprintf(s_pkgLev, sizeof s_pkgLev, "%s/pkg.lev", s_dir);
	snprintf(s_pkgVrm, sizeof s_pkgVrm, "%s/pkg.vrm", s_dir);
	snprintf(s_pkgSca, sizeof s_pkgSca, "%s/pkg.sca", s_dir);
	snprintf(cmd, sizeof cmd, "cp 'assets/tracks/cortex-vortex/CVortex Arcade All.lev' '%s' && printf 'Z' >> '%s' && "
	         "cp 'assets/tracks/cortex-vortex/CVortex Arcade All.vrm' '%s' && cp 'assets/tracks/cortex-vortex/CVortex Arcade All.sca' '%s'",
	         s_pkgLev, s_pkgLev, s_pkgVrm, s_pkgSca);
	EXPECT(run(cmd) == 0, "package fixture");
	sha_of(s_pkgLev, s_pkgLevSha);
	sha_of(s_pkgSca, s_pkgScaSha);

	// Pinned music of an ordinary package: verified at arm, served on its slot.
	EXPECT(arm_package(1, s_pkgLevSha) == 1, "package arms with music");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 1 && size == SCA_BYTES && s_musicIsPackage == 1, "package music is held");
	serve_package_race(1, "package music");
	{
		const void *held;
		size_t heldSize;
		EXPECT(CustomTrack_CortexMusicHold(&held, &heldSize) == 1 && heldSize == SCA_BYTES, "package music can be held");
		CustomTrack_CortexMusicRelease();
	}
	// The Cortex Vortex entry never takes it, and a package race on host 13 is
	// not the Cortex race either.
	CustomTrack_ClearSeedDescriptor();
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0 && s_musicIsPackage == 0, "withdrawn seed drops the package music");

	// CV1: a downloaded package whose bytes equal the bundled relic pair keeps
	// its own pinned music, served on its own slot.
	{
		struct CustomTrackSeedDescriptor d = cortex_descriptor();
		char relicLev[sizeof s_lev], relicVrm[sizeof s_vrm];
		snprintf(relicLev, sizeof relicLev, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_LEV_FILE, s_dir);
		snprintf(relicVrm, sizeof relicVrm, "%s/tracks/cortex-vortex/" CTR_CFG_CORTEX_RELIC_VRM_FILE, s_dir);
		d.hostLevelID = PKG_HOST;
		d.soloRace = 1;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", CTR_CFG_CORTEX_RELIC_LEV_SHA256);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", CTR_CFG_CORTEX_RELIC_VRM_SHA256);
		EXPECT(CustomTrack_UseContentFiles(relicLev, relicVrm, "CORTEX VORTEX") == 1 &&
		       CustomTrack_UseContentMusic(s_pkgSca, s_pkgScaSha, SCA_BYTES, CTR_CFG_CORTEX_RELIC_LEV_SHA256) == 1 &&
		       CustomTrack_ApplySeedDescriptor(&d) == 1, "downloaded copy of the relic pair arms with its pin");
		EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 1 && size == SCA_BYTES && s_musicIsPackage == 1,
		       "downloaded copy of the relic pair: its own music, not the bundled one");
		CustomTrack_ClearSeedDescriptor();
	}

	// No pin: the host slot's music plays.
	EXPECT(arm_package(0, NULL) == 1, "package arms without a pin");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0, "no pin, no music");
	serve_package_race(0, "no pin");
	CustomTrack_ClearSeedDescriptor();

	// A pin made for another LEV is never read for this track.
	EXPECT(arm_package(1, s_pkgScaSha) == 1, "package arms with a pin for another LEV");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0, "a pin bound to another LEV is refused");
	CustomTrack_ClearSeedDescriptor();

	// The pin is forgotten by the next package.
	EXPECT(arm_package(1, s_pkgLevSha) == 1 && CustomTrack_CortexMusic(&bytes, &size) == 1, "armed with music again");
	CustomTrack_ClearSeedDescriptor();
	{
		struct CustomTrackSeedDescriptor d = cortex_descriptor();
		char vrmSha[65];
		sha_of(s_pkgVrm, vrmSha);
		d.hostLevelID = PKG_HOST;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", s_pkgLevSha);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", vrmSha);
		EXPECT(CustomTrack_UseContentMusic(s_pkgSca, s_pkgScaSha, SCA_BYTES, s_pkgLevSha) == 1, "pin handed over");
		EXPECT(CustomTrack_UseContentFiles(s_pkgLev, s_pkgVrm, "ACID LAKE") == 1, "next package selected");
		EXPECT(CustomTrack_ApplySeedDescriptor(&d) == 1 && CustomTrack_CortexMusic(&bytes, &size) == 0,
		       "a later Use call forgets the pin");
	}
	CustomTrack_ClearSeedDescriptor();

	// A changed or missing file costs the music only.
	snprintf(cmd, sizeof cmd, "printf 'x' | dd of='%s' bs=1 seek=100 conv=notrunc 2>/dev/null", s_pkgSca);
	run(cmd);
	EXPECT(arm_package(1, s_pkgLevSha) == 1, "package arms with changed music");
	EXPECT(CustomTrack_CortexMusic(&bytes, &size) == 0, "changed package music is refused");
	serve_package_race(0, "changed music");
	CustomTrack_ClearSeedDescriptor();
	unlink(s_pkgSca);
	EXPECT(arm_package(1, s_pkgLevSha) == 1 && CustomTrack_CortexMusic(&bytes, &size) == 0, "missing package music: no music");
	CustomTrack_ClearSeedDescriptor();

	// A managed-package handoff forgets the pin of the package before it.
	{
		struct CustomTrackManagerPackage managed;
		struct CustomTrackManagerStatus status;
		struct CustomTrackSeedDescriptor d = cortex_descriptor();
		char vrmSha[65];
		memset(&managed, 0, sizeof managed);
		memset(&status, 0, sizeof status);
		managed.title = "MANAGED";
		managed.navigationUuid = "0f8fad5b-d9cb-469f-a165-70867728950e";
		managed.navigationRevision = 1;
		status.state = CTR_CT_MANAGER_READY;
		snprintf(status.levPath, sizeof status.levPath, "%s", s_pkgLev);
		snprintf(status.vrmPath, sizeof status.vrmPath, "%s", s_pkgVrm);
		sha_of(s_pkgVrm, vrmSha);
		d.hostLevelID = PKG_HOST;
		snprintf(d.levSha256, sizeof d.levSha256, "%s", s_pkgLevSha);
		snprintf(d.vrmSha256, sizeof d.vrmSha256, "%s", vrmSha);
		// Restore the file the changed-music case removed.
		snprintf(cmd, sizeof cmd, "cp 'assets/tracks/cortex-vortex/CVortex Arcade All.sca' '%s'", s_pkgSca);
		run(cmd);
		EXPECT(CustomTrack_UseContentMusic(s_pkgSca, s_pkgScaSha, SCA_BYTES, s_pkgLevSha) == 1, "pin handed over for a shared package");
		EXPECT(CustomTrack_UseManagedPackage(&managed, &status) == 1, "managed package selected next");
		EXPECT(CustomTrack_ApplySeedDescriptor(&d) == 1 && CustomTrack_CortexMusic(&bytes, &size) == 0,
		       "the managed package does not play the earlier package's music");
		CustomTrack_ClearSeedDescriptor();
	}

	// Refused pins.
	EXPECT(CustomTrack_UseContentMusic(NULL, s_pkgScaSha, 10, s_pkgLevSha) == 0, "no path");
	EXPECT(CustomTrack_UseContentMusic(s_pkgSca, "short", 10, s_pkgLevSha) == 0, "short digest");
	EXPECT(CustomTrack_UseContentMusic(s_pkgSca, s_pkgScaSha, 0, s_pkgLevSha) == 0, "zero size");
}

static unsigned long bossMusicBytes;
static char bossMusicHash[65];
static unsigned int bossMusicLookups;

const char *NativeAssets_GetAssetDir(void) { return s_dir; }

int CustomPackage_FindPinnedMusicWithStatus(const char *assets, const char *revision, const char *uuid, int exactUuid,
	const char *levSha256, unsigned long levBytes, const char *vrmSha256, unsigned long vrmBytes,
	char *path, size_t pathSize, char sha256[65], unsigned long *bytes, const char **refusal)
{
	bossMusicLookups++;
	*refusal = NULL;
	EXPECT(!strcmp(assets, s_dir) && !strcmp(revision, "1.0.0") &&
	       !strcmp(uuid, "0f8fad5b-d9cb-469f-a165-70867728950e") && !exactUuid &&
	       !strcmp(levSha256, s_pkgLevSha) && levBytes > 0 &&
	       strlen(vrmSha256) == 64 && vrmBytes > 0, "resolve music for the pinned race package");
	if (!bossMusicBytes)
		return 0;
	snprintf(path, pathSize, "%s", s_sca);
	memcpy(sha256, bossMusicHash, 65);
	*bytes = bossMusicBytes;
	return 1;
}

static void boss_music(void)
{
	struct GarageVenueDescriptor venue = {.enabled = 1, .hostLevelID = PKG_HOST};
	struct CustomTrackLoadContext ctx = {.levelID = PKG_HOST, .bossID = 0, .adventureBossActive = 1};
	const char *reason, *path;
	char displaced[1024];
	struct stat st;
	size_t size;
	u32 trackSize;
	unsigned char *bytes;
	const int group = PKG_HOST * CTR_CT_GROUP_SIZE;
	snprintf(venue.levPath, sizeof venue.levPath, "%s", s_pkgLev);
	snprintf(venue.vrmPath, sizeof venue.vrmPath, "%s", s_pkgVrm);
	snprintf(venue.levSha256, sizeof venue.levSha256, "%s", s_pkgLevSha);
	sha_of(s_pkgVrm, venue.vrmSha256);
	snprintf(venue.musicPackageRevision, sizeof venue.musicPackageRevision, "1.0.0");
	snprintf(venue.packageUuid, sizeof venue.packageUuid, "0f8fad5b-d9cb-469f-a165-70867728950e");
	snprintf(venue.navPackageUuid, sizeof venue.navPackageUuid, "%s", venue.packageUuid);
	if (stat(s_pkgLev, &st) != 0) { EXPECT(0, "LEV fixture exists"); return; }
	venue.levBytes = st.st_size;
	if (stat(s_pkgVrm, &st) != 0) { EXPECT(0, "VRM fixture exists"); return; }
	venue.vrmBytes = st.st_size;
	sha_of(s_sca, bossMusicHash);
	EXPECT(CustomTrack_ApplyGarageVenueDescriptor(0, &venue), "assign a custom boss venue");
	EXPECT(CustomTrack_PrepareGarageVenueEntry(PKG_HOST, 0), "verify the exact pair before entering the garage race");
	CustomTrack_OnRaceLoadRequested(PKG_HOST);
	EXPECT(CustomTrack_GetOverride(group + 1, &ctx, &path, &trackSize) &&
	       !strcmp(path, CUSTOM_TRACK_BOSS_LEV), "the default Boss Theme retains verified geometry without any music lookup");
	FILE *levFile = fopen(s_pkgLev, "r+b");
	if (levFile != NULL)
	{
		int first = fgetc(levFile);
		rewind(levFile);
		fputc(first ^ 1, levFile);
		fflush(levFile);
		CustomTrack_OnRaceLoadRequested(PKG_HOST);
		EXPECT(CustomTrack_GetOverride(group + 1, &ctx, &path, &trackSize) &&
		       !strcmp(path, CUSTOM_TRACK_BOSS_LEV), "same-level retry keeps the pair verified at garage entry");
		CustomTrack_OnRaceLoadRequested(26);
		EXPECT(!CustomTrack_PrepareGarageVenueEntry(PKG_HOST, 0),
		       "a same-size LEV replacement is refused before the garage requests the load");
		EXPECT(!CustomTrack_GetOverride(group + 1, &ctx, &path, &trackSize) &&
		       !CustomTrack_GetOverride(group, &ctx, &path, &trackSize),
		       "a same-size LEV replacement refuses both custom files with Boss Theme");
		rewind(levFile);
		fputc(first, levFile);
		fclose(levFile);
	}
	else
		EXPECT(0, "open the fixture to replace the LEV");
	CustomTrack_ResetBossVenueLoad();
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason) && !reason, "an absent SCA is optional");
	EXPECT(CustomTrack_GetOverride(group + 1, &ctx, &path, &trackSize) &&
	       !strcmp(path, CUSTOM_TRACK_BOSS_LEV), "reserve the pair even when optional audio is absent");
	CustomTrack_ClearGarageVenueDescriptors();
	CustomTrack_ClearResidentSource();
	CustomTrack_NoteLoadedAsset(&ctx, CTR_CT_ROLE_LEV, 1);
	CustomTrack_NoteLoadedAsset(&ctx, CTR_CT_ROLE_VRM, 1);
	unsigned char uuid[CTR_CT_NAV_UUID_BYTES], expectedUuid[CTR_CT_NAV_UUID_BYTES];
	unsigned int revision;
	EXPECT(CustomTrackPolicy_ParseNavUuid(venue.navPackageUuid, expectedUuid) &&
	       CustomTrack_ResidentGarageNavIdentity(PKG_HOST, uuid, &revision) &&
	       memcmp(uuid, expectedUuid, sizeof uuid) == 0 &&
	       revision == CustomTrackPolicy_NavRevision(venue.levSha256),
	       "the exact retained pair keeps its identity when preflight withdraws the descriptor");
	CustomTrack_ClearResidentSource();
	CustomTrack_ResetBossVenueLoad();
	EXPECT(CustomTrack_ApplyGarageVenueDescriptor(0, &venue), "re-arm the fixture for a new load");
	bossMusicBytes = SCA_BYTES;
	bytes = CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason);
	EXPECT(bytes && size == SCA_BYTES && !reason, "a repair becomes available without re-arming the venue");
	free(bytes);
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 1, &size, &reason) && size == 0, "another boss has no access to the pin");
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST + 1, 0, &size, &reason), "another host has no access to the pin");

	snprintf(displaced, sizeof displaced, "%s.displaced", s_pkgLev);
	if (rename(s_pkgLev, displaced) != 0) { EXPECT(0, "can move the LEV fixture"); return; }
	EXPECT(CustomTrack_GetOverride(group + 1, &ctx, &path, &trackSize) &&
	       !strcmp(path, CUSTOM_TRACK_BOSS_LEV) && trackSize == venue.levBytes,
	       "the current load retains the exact LEV after its file is moved");
	bytes = malloc(venue.levBytes);
	EXPECT(bytes && CustomTrack_ReadFile(CUSTOM_TRACK_BOSS_LEV, bytes, venue.levBytes, venue.levBytes),
	       "the geometry loader reads the bytes retained by music admission");
	free(bytes);
	CustomTrack_ResetBossVenueLoad();
	unsigned int lookups = bossMusicLookups;
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason) && reason && !size,
	       "a new load with missing geometry cannot play custom music");
	EXPECT(bossMusicLookups == lookups, "geometry refusal precedes the production lookup boundary");
	EXPECT(CustomTrack_GarageVenueReady(0), "load failure does not close the garage");
	EXPECT(!CustomTrack_GetOverride(group, &ctx, &path, &trackSize) &&
	       !CustomTrack_GetOverride(group + 1, &ctx, &path, &trackSize),
	       "reject both custom geometry files for that load");
	if (rename(displaced, s_pkgLev) != 0) { EXPECT(0, "can restore the LEV fixture"); return; }
	CustomTrack_ResetBossVenueLoad();
	bytes = CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason);
	EXPECT(bytes && !reason, "a restored file can be used at the next load without arming again");
	free(bytes);
	CustomTrack_ResetBossVenueLoad();
	bossMusicBytes--;
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason) && reason && strstr(reason, "size"),
	       "size verification failure has a diagnostic");
	CustomTrack_ResetBossVenueLoad();
	bossMusicBytes = SCA_BYTES;
	memset(bossMusicHash, '0', sizeof bossMusicHash - 1);
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason) && reason && strstr(reason, "SHA-256"),
	       "digest verification failure has a diagnostic");
	CustomTrack_ResetBossVenueLoad();

	snprintf(venue.levPath, sizeof venue.levPath, "%s", s_lev);
	snprintf(venue.vrmPath, sizeof venue.vrmPath, "%s", s_vrm);
	snprintf(venue.levSha256, sizeof venue.levSha256, "%s", CORTEX_LEV_SHA256);
	snprintf(venue.vrmSha256, sizeof venue.vrmSha256, "%s", CORTEX_VRM_SHA256);
	venue.musicPackageRevision[0] = 0;
	EXPECT(CustomTrack_ApplyGarageVenueDescriptor(0, &venue), "assign an ordinary venue using the Cortex files");
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason), "matching geometry alone does not select bundled music");
	CustomTrack_ResetBossVenueLoad();
	venue.cortex = 1;
	EXPECT(CustomTrack_ApplyGarageVenueDescriptor(0, &venue), "assign the bundled boss venue");
	bytes = CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason);
	EXPECT(bytes && size == SCA_BYTES, "the bundled boss venue reads its bundled music");
	free(bytes);
	CustomTrack_ResetBossVenueLoad();
	CustomTrack_ClearGarageVenueDescriptors();
	EXPECT(!CustomTrack_ReadBossMusic(PKG_HOST, 0, &size, &reason), "withdrawing the venue withdraws its music");
}

static void pins(void)
{
	char line[512];
	int lev = 0, vrm = 0, notices = 0;
	FILE *f = fopen("include/platform/native_cortex_asset_pins.h", "r");

	// The loader uses the client's shared geometry pins and the notices' SCA pin.
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
	package_music();
	boss_music();
	pins();
	selection();

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", s_dir);
	run(cmd);
	printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
