// Box authoring build only (CTR_CUSTOM_PACKAGES), included by HOWL_Music.c.
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
// Without an .sca, or when it is refused, the race keeps the fixed default
// (Crash Cove's bank and song, native_custom_identity.h).

#include <platform/native_custom_music.h>
#include <platform/native_custom_offline.h>

void AP_LogLine(const char *msg);

// Sector numbers at and above this one never exist in KART.HWL (a few
// thousand sectors); the level bank reads land here while an .sca is served.
#define HOWL_CUSTOM_SCA_SECTOR 0x10000000
// numPlyrCurrGame never exceeds four.
#define HOWL_CUSTOM_MAX_PLAYERS 4

static struct CustomMusicSca s_customSca; // valid from LoadBank until BankDone
static int s_customBankServing;
static int s_customBankLoaded; // the level bank of this load is the .sca's
static u16 s_customSavedIDs[CTR_SCA_MAX_SAMPLES];
static u16 s_customSavedSizes[CTR_SCA_MAX_SAMPLES];
static int s_customSavedCount;
static s16 s_customLaterBanks[1 + HOWL_CUSTOM_MAX_PLAYERS][CTR_SCA_SECTOR / 2];
// Byte buffer like retail sampleBlock1, 4-aligned for the CseqHeader reads.
static unsigned char s_customSong[CTR_SCA_MAX_SONG_BYTES] __attribute__((aligned(4)));

static void HOWL_CustomMusic_Log(const char *what, const char *detail)
{
	char msg[192];
	snprintf(msg, sizeof msg, "[CUSTOM MUSIC] %s%s%s\n", what, detail && *detail ? ": " : "", detail ? detail : "");
	AP_LogLine(msg);
}

static int HOWL_CustomMusic_Acquire(struct CustomMusicSca *sca, char *error, size_t errorSize)
{
	const void *bytes;
	size_t size;

	if (!CustomOffline_RuntimeAudio(&bytes, &size))
	{
		snprintf(error, errorSize, "%s", "");
		return 0;
	}
	return CustomMusic_Parse(bytes, size, (unsigned int)sdata->ptrHowlHeader->numSpuAddrs, sca, error, errorSize);
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
}

// Read one retail bank's header sector for the admission check.
static int HOWL_CustomMusic_ReadBankHeader(int bankID, s16 *sector)
{
	if (bankID < 0 || bankID >= sdata->ptrHowlHeader->numBanks)
		return 0;
	return LOAD_HowlHeaderSectors(&sdata->KartHWL_CdFile, sector, sdata->howl_bankOffsets[bankID], 1);
}

// Music_AsyncParseBanks stage 0 of a custom race. Returns 1 when the .sca's
// bank was started with Bank_Load; 0 means use the default bank.
int HOWL_CustomMusic_LoadBank(struct Bank *thisBank)
{
	struct CustomMusicSca sca;
	const int16_t *later[1 + HOWL_CUSTOM_MAX_PLAYERS];
	unsigned int laterCount = 0, needed = 0, i;
	char error[128];
	struct GameTracker *gGT = sdata->gGT;

	HOWL_CustomMusic_Reset();
	if (sdata->boolAudioEnabled == 0)
		return 0;
	if (!HOWL_CustomMusic_Acquire(&sca, error, sizeof error))
	{
		HOWL_CustomMusic_Log(error[0] ? "track music refused, default music" : "no track music in this package, default music", error);
		return 0;
	}

	// The banks Music_AsyncParseBanks loads after this one in a custom race:
	// bank 54 (always, see the custom-race terms in stage 1), then a bank for
	// each player whose character bank 54 does not carry (stage 2).
	if (!HOWL_CustomMusic_ReadBankHeader(54, s_customLaterBanks[laterCount]))
	{
		HOWL_CustomMusic_Log("track music refused, default music", "cannot read the driver bank");
		return 0;
	}
	later[laterCount] = s_customLaterBanks[laterCount];
	laterCount++;
	for (i = 0; i < (unsigned int)gGT->numPlyrCurrGame && i < HOWL_CUSTOM_MAX_PLAYERS; i++)
	{
		if (data.characterIDs[i] < PINSTRIPE)
			continue;
		if (!HOWL_CustomMusic_ReadBankHeader(data.characterIDs[i] + 0x37, s_customLaterBanks[laterCount]))
		{
			HOWL_CustomMusic_Log("track music refused, default music", "cannot read a character bank");
			return 0;
		}
		later[laterCount] = s_customLaterBanks[laterCount];
		laterCount++;
	}

	if (!CustomMusic_Admit(&sca, (const uint16_t *)sdata->howl_spuAddrs, (unsigned int)sdata->ptrHowlHeader->numSpuAddrs,
	                       (unsigned int)sdata->audioAllocPtr, later, laterCount, &needed, error, sizeof error))
	{
		HOWL_CustomMusic_Log("track music refused, default music", error);
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
		HOWL_CustomMusic_Log("track music refused, default music", "no free bank slot");
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
		HOWL_CustomMusic_Log("playing the track's own music", detail);
	}
	return 1;
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
	return 1;
}
