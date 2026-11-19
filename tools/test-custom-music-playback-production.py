"""Exercise HOWL's production music routing and song start bookkeeping."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (ROOT / "game/HOWL" / path).read_text()
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <common.h>
#include <platform/native_custom_identity.h>
#include <platform/native_custom_tracks.h>
#include <platform/native_cortex_music_select.h>
#include "ap/ap_boss_music_logic.h"

struct sData sdata_static;
struct Data data;
NativeConfig g_config;
static struct GameTracker gt;
static int offline, seed_track, garage, oxide, accept_bank, start_result;
static int s_customSongReady, admitted, retail_song, start_calls, start_logs;
static int loaded[8], num_loaded;
static enum CortexMusicBanks planned;

int MainRaceTrack_OfflineCustomLoad(void) { return offline; }
int CustomTrack_ServingLoad(int level, int cup, int id) { return seed_track; }
int CustomTrack_CortexMusicServing(int level, int cup, int id) { return seed_track; }
int CustomTrack_GarageVenueServing(int level, int boss, int active) { return garage && active ? boss : -1; }
int CustomTrack_OxideFinalServing(int level, int boss, int active) { return oxide && active; }
int Bank_AssignSpuAddrs(void) { return 1; }
int Bank_Load(int id, struct Bank *bank) { loaded[num_loaded++] = id; return 1; }
int howl_SetSong(int id) { retail_song = id; return 1; }
int howl_LoadSong(void) { return 1; }
void HOWL_CustomMusic_BankDone(void) {}
static void HOWL_CustomMusic_ReleaseAudio(void) {}
int HOWL_CustomMusic_FallbackFXBank(void) { return CTR_CUSTOM_FX_BANK; }
int HOWL_CustomMusic_FallbackSongBank(void) { return CTR_CUSTOM_SONG_BANK; }
int HOWL_CustomMusic_SetSong(void) { return s_customSongReady = admitted; }
static int HOWL_CustomMusic_LoadBankFor(struct Bank *bank, enum CortexMusicBanks banks)
{
    planned = banks;
    return admitted = accept_bank;
}
static void HOWL_CustomMusic_Log(const char *what, const char *detail) { start_logs++; }
int CseqMusic_Start(u16 id, int tempo, struct SongSet *set, int bits, int loop)
{
    assert(id == CSEQ_SONG_LEVEL && loop == 1);
    start_calls++;
    return start_result;
}
void CseqMusic_ChangeTempo(u16 id, int tempo) {}
void CseqMusic_Stop(u16 id) {}
'''

for path, signature in [
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_LoadBank(struct Bank *thisBank)"),
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_ContentLoadBank(int level, struct Bank *thisBank)"),
    ("HOWL_Music.c", "u32 Music_AsyncParseBanks(void)"),
    ("HOWL_Music.c", "void Music_End(void)"),
    ("HOWL_Music.c", "void Music_Adjust(u32 songID, int newTempo, struct SongSet *set, u32 songSetActiveBits)"),
]:
    fixture += function(path, signature)

fixture += r'''
static void run_load(u32 mode, int cup, u32 mode2)
{
    gt.gameMode1 = mode;
    gt.gameMode2 = mode2;
    gt.cup.cupID = cup;
    num_loaded = admitted = s_customSongReady = 0;
    planned = CORTEX_MUSIC_OFF;
    retail_song = -1;
    ((u8 *)&sdata->audioDefaults[7])[1] = 0;
    for (int tick = 0; tick < 16; tick++)
        if (Music_AsyncParseBanks())
            return;
    assert(0 && "HOWL load did not finish");
}

static void expect_custom(enum CortexMusicBanks plan, int bank)
{
    assert(planned == plan && s_customSongReady && retail_song == -1);
    assert(num_loaded == 1 && loaded[0] == bank);
}

int main(void)
{
    sdata->gGT = &gt;
    gt.levelID = 6;
    gt.bossID = 0;
    gt.numPlyrCurrGame = 1;
    data.characterIDs[0] = 1;
    data.levBank_FX[6] = 7;
    data.levBank_Song[6] = 12;
    sdata->songBankBossID[0] = 26;
    accept_bank = seed_track = 1;
    run_load(ADVENTURE_MODE, 0, 0);
    expect_custom(CORTEX_MUSIC_BANK54, 54);
    run_load(ADVENTURE_MODE, 0, TOKEN_RACE);
    expect_custom(CORTEX_MUSIC_BANK54, 54);
    run_load(ADVENTURE_MODE | RELIC_RACE, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);
    run_load(ADVENTURE_MODE | ADVENTURE_CUP, 4, 0);
    expect_custom(CORTEX_MUSIC_BANK54, 54);

    seed_track = 0;
    offline = 1;
    run_load(TIME_TRIAL, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);
    run_load(ARCADE_MODE, 0, 0);
    expect_custom(CORTEX_MUSIC_BANK54, 54);

    offline = 0;
    garage = 1;
    g_config.bossMusic = AP_BOSS_MUSIC_BOSS_THEME;
    run_load(ADVENTURE_MODE | ADVENTURE_BOSS, 0, 0);
    assert(!admitted && retail_song == HOWL_SONG_BOSS_RACE && loaded[0] == 26);
    g_config.bossMusic = AP_BOSS_MUSIC_TRACK;
    run_load(ADVENTURE_MODE | ADVENTURE_BOSS, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);
    garage = 0;
    oxide = 1;
    run_load(ADVENTURE_MODE | ADVENTURE_BOSS, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);
    accept_bank = 0;
    run_load(ADVENTURE_MODE | ADVENTURE_BOSS, 0, 0);
    assert(!admitted && retail_song == 12 && loaded[0] == 7);
    oxide = 0;
    run_load(ADVENTURE_MODE, 0, TOKEN_RACE);
    assert(planned == CORTEX_MUSIC_OFF && !admitted && retail_song == 12);
    assert(num_loaded == 2 && loaded[0] == 7 && loaded[1] == 54);

    s_customSongReady = 1;
    Music_End();
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 1 && start_logs == 0 && !sdata->cseqBoolPlay);
    start_result = 1;
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 2 && start_logs == 1 && sdata->cseqBoolPlay);
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 2 && start_logs == 1);
    puts("PASS: custom and retail race bank selection, boss music preference, custom song start retry");
}
'''

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / "music.c"
    source.write_text(fixture)
    binary = Path(tmp) / "music"
    subprocess.run(
        ["cc", "-m32", "-DCTR_NATIVE", "-DCTR_AP", "-DCTR_CUSTOM_PACKAGES", "-DCTR_CUSTOM_TRACKS",
         "-DBUILD=926", "-I.", "-Iinclude", str(source), "-o", str(binary)],
        cwd=ROOT, check=True,
    )
    subprocess.run([str(binary)], check=True)
