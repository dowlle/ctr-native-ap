// Builds with packages only (CTR_CUSTOM_PACKAGES, which brings the 1 MiB sound
// memory of include/platform/native_spu_memory.h), included by HOWL_Music.c.
//
// A custom-page race plays its package's own music when the package pins a
// Saphi .sca (include/platform/native_custom_music.h). The .sca carries a
// retail-format level sound bank, the spuSize of each of its sample slots and
// a retail CSEQ pack, so the engine's own bank loader and sequencer play it:
//
//   level bank  Bank_Load as usual, then its sector reads are served from the
//               .sca (LOAD_HowlSectorChainStart, below a sentinel sector)
//   slot sizes  howl_spuAddrs[id].spuSize is set from SIZE before the bank
//               loads and restored before the next level loads any bank
//   song        the CSEQ pack is copied to a buffer of its own and parsed in
//               place of howl_SetSong/howl_LoadSong
//
// Without an .sca, or when it is refused, the race plays a random retail
// track's music: a deterministic per-track pick from the 18 retail race
// levels (CustomMusic_FallbackPick, native_custom_music.h), seeded by the
// room identity and the track's content-plan key for a seed race, or by the
// package pin offline. The pick is stable across Retry and across sessions of
// one room, differs between tracks, and never lands on a boss, menu or
// cutscene song. Bank and song always come from the same level.
//
// The Cortex Vortex race of a content plan (a pad or a cup leg, on host level 13)
// uses the same path with the .sca that ships with the project
// (CustomTrack_CortexMusic). Whenever the bundled file is missing,
// changed or refused, or the race is one this file does not size (below), the
// picked fallback plays instead of the host level's (Oxide Station's) bank and
// song, with no error.
//
// Any other content-plan package whose shared store entry pins a Saphi .sca
// takes the same path on its own host slot (CustomTrack_UseContentMusic, set
// when the race is armed). Without one, or when it is refused, the picked
// fallback plays instead of the host slot's own bank and song.
// Custom boss venues use the same path with Boss Music set to TRACK.

#include <platform/native_custom_music.h>
#include <platform/native_custom_offline.h>
#include <platform/native_custom_tracks.h>
#include <platform/native_cortex_music_select.h>

void AP_LogLine(const char *msg);

// Sector numbers at and above this one never exist in KART.HWL (a few
// thousand sectors); the level bank reads land here while an .sca is served.
#define HOWL_CUSTOM_SCA_SECTOR 0x10000000
// numPlyrCurrGame never exceeds four; the undisplaced Purple Gem Cup loads five
// character banks. Bank 54 plus four players is the other most.
#define HOWL_CUSTOM_MAX_PLAYERS 4
#define HOWL_CUSTOM_ROSTER_BANKS 5
#define HOWL_CUSTOM_LATER_BANKS 5

static struct CustomMusicSca s_customSca; // valid from LoadBank until BankDone
// Cortex Vortex's bundled .sca, held from LoadBank until SetSong or Reset.
static const void *s_cortexBytes;
static size_t s_cortexSize;
static unsigned char *s_bossMusicBytes;
static size_t s_bossMusicSize;
static int s_customBankServing;
static int s_customBankLoaded; // the level bank of this load is the .sca's
static int s_customSongReady;
// The retail race level this load's fallback music comes from, picked per
// track at the stage-0 decision; -1 when this load is not a custom race on
// fallback music. Bank and song both come from it.
static int s_fallbackLevel = -1;
// One "which song data is active" line per race load.
static int s_songStartLogged;
static u16 s_customSavedIDs[CTR_SCA_MAX_SAMPLES];
static u16 s_customSavedSizes[CTR_SCA_MAX_SAMPLES];
static int s_customSavedCount;
static s16 s_customLaterBanks[HOWL_CUSTOM_LATER_BANKS][CTR_SCA_SECTOR / 2];
// Byte buffer like retail sampleBlock1, 4-aligned for the CseqHeader reads.
static unsigned char s_customSong[CTR_SCA_MAX_SONG_BYTES] __attribute__((aligned(4)));

static void HOWL_CustomMusic_Log(const char *what, const char *detail)
{
	char msg[192];
	snprintf(msg, sizeof msg, "[CUSTOM MUSIC] %s%s%s\n", what, detail && *detail ? ": " : "", detail ? detail : "");
	AP_LogLine(msg);
}

static int HOWL_CustomMusic_BossVenueServing(int level)
{
	const struct GameTracker *gGT = sdata->gGT;
	return IS_BOSS_RACE(gGT->gameMode1) &&
	    (CustomTrack_GarageVenueServing(level, gGT->bossID, 1) >= 0 ||
	     CustomTrack_OxideFinalServing(level, gGT->bossID, 1));
}

static int HOWL_CustomMusic_Acquire(struct CustomMusicSca *sca, char *error, size_t errorSize)
{
	const void *bytes;
	size_t size;

	if (MainRaceTrack_OfflineCustomLoad())
	{
		if (!CustomOffline_RuntimeAudio(&bytes, &size))
		{
			snprintf(error, errorSize, "%s", "");
			return 0;
		}
	}
	else if (s_bossMusicBytes ||
	         (s_cortexBytes == NULL && HOWL_CustomMusic_BossVenueServing(sdata->gGT->levelID)))
	{
		if (!s_bossMusicBytes)
		{
			const char *reason;
			s_bossMusicBytes = CustomTrack_ReadBossMusic(sdata->gGT->levelID, sdata->gGT->bossID,
			                                           &s_bossMusicSize, &reason);
			if (!s_bossMusicBytes)
			{
				snprintf(error, errorSize, "%s", reason ? reason : "");
				return 0;
			}
		}
		bytes = s_bossMusicBytes;
		size = s_bossMusicSize;
	}
	else
	{
		// The bank and the song must come from the same bytes, whatever the
		// seed does in between.
		if (s_cortexBytes == NULL && !CustomTrack_CortexMusicHold(&s_cortexBytes, &s_cortexSize))
		{
			s_cortexBytes = NULL;
			snprintf(error, errorSize, "%s", "");
			return 0;
		}
		bytes = s_cortexBytes;
		size = s_cortexSize;
	}
	return CustomMusic_Parse(bytes, size, (unsigned int)sdata->ptrHowlHeader->numSpuAddrs, sca, error, errorSize);
}

static void HOWL_CustomMusic_ReleaseAudio(void)
{
	if (s_cortexBytes != NULL)
	{
		s_cortexBytes = NULL;
		s_cortexSize = 0;
		CustomTrack_CortexMusicRelease();
	}
	free(s_bossMusicBytes);
	s_bossMusicBytes = NULL;
	s_bossMusicSize = 0;
}

// Undo the slot sizes and forget the served bank. Music_LoadBanks calls this
// before any bank of the next load, so a retail bank never sees .sca sizes.
void HOWL_CustomMusic_Reset(void)
{
	while (s_customSavedCount > 0)
	{
		s_customSavedCount--;
		sdata->howl_spuAddrs[s_customSavedIDs[s_customSavedCount]].spuSize = s_customSavedSizes[s_customSavedCount];
	}
	memset(&s_customSca, 0, sizeof s_customSca);
	s_customBankServing = 0;
	s_customBankLoaded = 0;
	s_customSongReady = 0;
	s_fallbackLevel = -1;
	s_songStartLogged = 0;
	HOWL_CustomMusic_ReleaseAudio();
}

// The retail level this load's fallback music comes from, or -1 when this
// load is not a custom race on fallback music (a retail race keeps its own
// bank and song). Set by the stage-0 decision, cleared by Reset.
int HOWL_CustomMusic_FallbackLevel(void)
{
	return s_fallbackLevel;
}

// The music of a custom race that plays no .sca: the bank and song of the
// picked retail level (s_fallbackLevel). Bank and song always come from the
// same level. The fixed defaults (native_custom_identity.h) cover a custom
// load with no pick, which the offline runtime never produces.
int HOWL_CustomMusic_FallbackFXBank(void)
{
	return s_fallbackLevel >= 0 ? data.levBank_FX[s_fallbackLevel] : CTR_CUSTOM_FX_BANK;
}

int HOWL_CustomMusic_FallbackSongBank(void)
{
	return s_fallbackLevel >= 0 ? data.levBank_Song[s_fallbackLevel] : CTR_CUSTOM_SONG_BANK;
}

// One line naming the fallback level and its bank pair. Call only with a pick
// made (s_fallbackLevel >= 0).
static void HOWL_CustomMusic_LogFallback(const char *what)
{
	char detail[96];
	snprintf(detail, sizeof detail, "level %d (FX bank %d, song bank %d)", s_fallbackLevel,
	         data.levBank_FX[s_fallbackLevel], data.levBank_Song[s_fallbackLevel]);
	HOWL_CustomMusic_Log(what, detail);
}

// Read one retail bank's header sector for the admission check.
static int HOWL_CustomMusic_ReadBankHeader(int bankID, s16 *sector)
{
	if (bankID < 0 || bankID >= sdata->ptrHowlHeader->numBanks)
		return 0;
	return LOAD_HowlHeaderSectors(&sdata->KartHWL_CdFile, sector, sdata->howl_bankOffsets[bankID], 1);
}

// Music_AsyncParseBanks stage 0 of a custom race. Returns 1 when the .sca's
// bank was started with Bank_Load; 0 means use the default bank. laterBanks is
// what stages 1 and 2 load after it (native_cortex_music_select.h).
static int HOWL_CustomMusic_LoadBankFor(struct Bank *thisBank, enum CortexMusicBanks laterBanks)
{
	struct CustomMusicSca sca;
	const int16_t *later[HOWL_CUSTOM_LATER_BANKS];
	unsigned int laterCount = 0, needed = 0, i, count;
	char error[128];
	struct GameTracker *gGT = sdata->gGT;

	HOWL_CustomMusic_Reset();
	if (sdata->boolAudioEnabled == 0)
		return 0;
	if (!HOWL_CustomMusic_Acquire(&sca, error, sizeof error))
	{
		HOWL_CustomMusic_Log(error[0] ? "track music refused, fallback music" : "no verified track music, fallback music", error);
		return 0;
	}

	// The banks Music_AsyncParseBanks loads after this one: bank 54 when stage 1
	// loads it, then a bank for each player whose character bank 54 does not
	// carry, for every player without bank 54, or for the five roster slots of
	// the undisplaced Purple Gem Cup (stage 2).
	if (laterBanks == CORTEX_MUSIC_BANK54)
	{
		if (!HOWL_CustomMusic_ReadBankHeader(54, s_customLaterBanks[laterCount]))
		{
			HOWL_CustomMusic_Log("track music refused, fallback music", "cannot read the driver bank");
			return 0;
		}
		later[laterCount] = s_customLaterBanks[laterCount];
		laterCount++;
	}
	count = laterBanks == CORTEX_MUSIC_ROSTER5 ? HOWL_CUSTOM_ROSTER_BANKS : (unsigned int)gGT->numPlyrCurrGame;
	for (i = 0; i < count && laterCount < HOWL_CUSTOM_LATER_BANKS; i++)
	{
		if (laterBanks == CORTEX_MUSIC_BANK54 && data.characterIDs[i] < PINSTRIPE)
			continue;
		if (!HOWL_CustomMusic_ReadBankHeader(data.characterIDs[i] + 0x37, s_customLaterBanks[laterCount]))
		{
			HOWL_CustomMusic_Log("track music refused, fallback music", "cannot read a character bank");
			return 0;
		}
		later[laterCount] = s_customLaterBanks[laterCount];
		laterCount++;
	}

	if (!CustomMusic_Admit(&sca, (const uint16_t *)sdata->howl_spuAddrs, (unsigned int)sdata->ptrHowlHeader->numSpuAddrs,
	                       (unsigned int)sdata->audioAllocPtr << (CTR_SPU_ADDR_SHIFT - 3), later, laterCount, &needed, error, sizeof error))
	{
		HOWL_CustomMusic_Log("track music refused, fallback music", error);
		return 0;
	}

	// Give the .sca's slots their sizes, remembering the retail ones.
	for (i = 0; i < sca.numSamples; i++)
	{
		unsigned int id = CustomMusic_SampleID(&sca, i);
		s_customSavedIDs[s_customSavedCount] = (u16)id;
		s_customSavedSizes[s_customSavedCount] = sdata->howl_spuAddrs[id].spuSize;
		s_customSavedCount++;
		sdata->howl_spuAddrs[id].spuSize = (u16)CustomMusic_SampleSize(&sca, i);
	}

	// Bank_Load records the nominal default id; the sector reads are ours.
	if (Bank_Load(CTR_CUSTOM_FX_BANK, thisBank) == 0)
	{
		HOWL_CustomMusic_Reset();
		HOWL_CustomMusic_Log("track music refused, fallback music", "no free bank slot");
		return 0;
	}
	s_customSca = sca;
	sdata->bankSectorOffset = HOWL_CUSTOM_SCA_SECTOR;
	s_customBankServing = 1;
	s_customBankLoaded = 1;
	{
		char detail[96];
		snprintf(detail, sizeof detail, "%u samples, song %u bytes, SPU end 0x%x of 0x%x", sca.numSamples,
		         (unsigned int)sca.cseqSize, needed * 8, CTR_SCA_SPU_LIMIT);
		HOWL_CustomMusic_Log("track music bank admitted", detail);
	}
	return 1;
}

int HOWL_CustomMusic_LoadBank(struct Bank *thisBank)
{
	const struct GameTracker *gGT = sdata->gGT;
	// Pick the fallback before the bank decision (LoadBankFor resets it), so
	// stages 0 and 3 and the offline Arcade and Time Trial of one package all
	// agree on the same retail level.
	int fallback = CustomOffline_MusicFallbackLevel();
	// A custom-page Time Trial uses the character bank. Arcade uses bank 54.
	if (HOWL_CustomMusic_LoadBankFor(thisBank, CortexMusic_Banks(1, gGT->gameMode1, gGT->cup.cupID, 1)))
	{
		s_fallbackLevel = -1;
		return 1;
	}
	s_fallbackLevel = fallback;
	if (s_fallbackLevel >= 0)
		HOWL_CustomMusic_LogFallback("no verified track music, random fallback");
	return 0;
}

// Stage 0 of a seed race or a custom boss venue with TRACK selected. A race
// without verified music of its own plays the picked fallback: one log line
// per decision, including the no-music branch that used to be silent.
int HOWL_CustomMusic_ContentLoadBank(int level, struct Bank *thisBank)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 mode = gGT->gameMode1;
	int adventureCup = (mode & ADVENTURE_CUP) != 0;
	int servesMusic = HOWL_CustomMusic_BossVenueServing(level) ||
	    CustomTrack_CortexMusicServing(level, adventureCup, gGT->cup.cupID);
	enum CortexMusicBanks laterBanks =
	    CortexMusic_Banks(servesMusic, mode, gGT->cup.cupID,
	                      CustomTrack_ServingLoad(level, adventureCup, gGT->cup.cupID));

	if (laterBanks == CORTEX_MUSIC_OFF)
	{
		// No music route: a retail race (fallback -1, silent and unchanged)
		// or a custom race whose package serves no music.
		s_fallbackLevel = CustomTrack_MusicFallbackLevel(level, adventureCup, gGT->cup.cupID,
		                                                 gGT->bossID, IS_BOSS_RACE(mode));
		if (s_fallbackLevel >= 0)
			HOWL_CustomMusic_LogFallback("no track music served, random fallback");
		return 0;
	}
	if (HOWL_CustomMusic_LoadBankFor(thisBank, laterBanks))
	{
		s_fallbackLevel = -1;
		return 1;
	}
	// Release the refused asset before the fallback bank loads.
	HOWL_CustomMusic_ReleaseAudio();
	s_fallbackLevel = CustomTrack_MusicFallbackLevel(level, adventureCup, gGT->cup.cupID,
	                                                 gGT->bossID, IS_BOSS_RACE(mode));
	if (s_fallbackLevel >= 0)
		HOWL_CustomMusic_LogFallback("track music not used, random fallback");
	return 0;
}

// Stage 1 entry: the level bank is on the SPU; its sectors are no longer read.
void HOWL_CustomMusic_BankDone(void)
{
	s_customBankServing = 0;
	memset(&s_customSca, 0, sizeof s_customSca);
}

// LOAD_HowlSectorChainStart: serve a level-bank read from the .sca.
int HOWL_CustomMusic_ServeSectors(void *destination, int firstSector, int numSector)
{
	if (!s_customBankServing || firstSector < HOWL_CUSTOM_SCA_SECTOR || numSector <= 0)
		return 0;
	if (!CustomMusic_ReadBankSectors(&s_customSca, (unsigned int)(firstSector - HOWL_CUSTOM_SCA_SECTOR),
	                                 (unsigned int)numSector, destination))
		memset(destination, 0, (size_t)numSector * CTR_SCA_SECTOR);
	return 1;
}

// Stage 3 of a custom race. Returns 1 when the .sca's song is parsed and the
// song load is finished; 0 means use the default song.
int HOWL_CustomMusic_SetSong(void)
{
	struct CustomMusicSca sca;
	char error[128];

	if (!s_customBankLoaded || sdata->boolAudioEnabled == 0)
		return 0;
	if (!HOWL_CustomMusic_Acquire(&sca, error, sizeof error))
	{
		// The bank was the .sca's, so the default song would play the wrong
		// instruments; still the lesser fault than no music.
		HOWL_CustomMusic_Log("track song unavailable, default song", error);
		return 0;
	}

	// Same order as howl_SetSong + howl_LoadSong: forget the old song first,
	// then fill the song buffer and parse it.
	howl_ErasePtrCseqHeader();
	memcpy(s_customSong, sca.cseq, sca.cseqSize);
	howl_ParseCseqHeader((struct CseqHeader *)s_customSong);
	sdata->songLoadStage = 3;
	s_customSongReady = 1;
	// The song is in its own buffer and the bank on the SPU: done with the .sca.
	HOWL_CustomMusic_ReleaseAudio();
	return 1;
}
