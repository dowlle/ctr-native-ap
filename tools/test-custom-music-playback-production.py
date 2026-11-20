"""Exercise HOWL's production music routing, fallback picks and decision logs.

Covers the A6-47 behavior: a custom race without verified music of its own
plays a random retail track's bank and song (the pick itself is supplied by
stubs here; CustomMusic_FallbackPick is pinned in tools/test-custom-music.c),
each music decision logs one line including the previously silent fallback
branches, and the first song 0 start of a race logs which song data is active.
"""
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
#include <string.h>
#include <common.h>
#include <platform/native_custom_identity.h>
#include <platform/native_custom_tracks.h>
#include <platform/native_cortex_music_select.h>
#include "ap/ap_boss_music_logic.h"

struct sData sdata_static;
struct Data data;
NativeConfig g_config;
static struct GameTracker gt;
static int offline, serving_load, cortex_serving, garage, oxide, accept_bank, start_result;
static int content_fallback = -1, offline_fallback = -1;
static int s_customSongReady, admitted, retail_song, start_calls, start_logs;
static int s_fallbackLevel = -1, s_songStartLogged;
static int loaded[8], num_loaded;
static enum CortexMusicBanks planned;
static char log_lines[16][192];

int MainRaceTrack_OfflineCustomLoad(void) { return offline; }
int CustomTrack_ServingLoad(int level, int cup, int id) { return serving_load; }
int CustomTrack_CortexMusicServing(int level, int cup, int id) { return cortex_serving; }
int CustomTrack_GarageVenueServing(int level, int boss, int active) { return garage && active ? boss : -1; }
int CustomTrack_OxideFinalServing(int level, int boss, int active) { return oxide && active; }
static int fallback_bossActive_seen;
int CustomTrack_MusicFallbackLevel(int level, int cup, int id, int boss, int bossActive) { fallback_bossActive_seen = bossActive; return content_fallback; }
int CustomOffline_MusicFallbackLevel(void) { return offline_fallback; }
int Bank_AssignSpuAddrs(void) { return 1; }
int Bank_Load(int id, struct Bank *bank) { loaded[num_loaded++] = id; return 1; }
int howl_SetSong(int id) { retail_song = id; return 1; }
int howl_LoadSong(void) { return 1; }
void HOWL_CustomMusic_BankDone(void) {}
static void HOWL_CustomMusic_ReleaseAudio(void) {}
int HOWL_CustomMusic_SetSong(void) { return s_customSongReady = admitted; }
static int HOWL_CustomMusic_LoadBankFor(struct Bank *bank, enum CortexMusicBanks banks)
{
    planned = banks;
    return admitted = accept_bank;
}
static void HOWL_CustomMusic_Log(const char *what, const char *detail)
{
    if (start_logs < 16)
        snprintf(log_lines[start_logs], sizeof log_lines[0], "%s%s%s", what,
                 detail && *detail ? ": " : "", detail ? detail : "");
    start_logs++;
}
int CseqMusic_Start(u16 id, int tempo, struct SongSet *set, int bits, int loop)
{
    assert(id <= 2 && loop == 1);
    start_calls++;
    return start_result;
}
void CseqMusic_ChangeTempo(u16 id, int tempo) {}
void CseqMusic_Stop(u16 id) {}
'''

for path, signature in [
    ("HOWL_CustomMusic.c", "static int HOWL_CustomMusic_BossVenueServing(int level)"),
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_FallbackLevel(void)"),
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_FallbackFXBank(void)"),
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_FallbackSongBank(void)"),
    ("HOWL_CustomMusic.c", "static void HOWL_CustomMusic_LogFallback(const char *what)"),
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_LoadBank(struct Bank *thisBank)"),
    ("HOWL_CustomMusic.c", "int HOWL_CustomMusic_ContentLoadBank(int level, struct Bank *thisBank)"),
    ("HOWL_Music.c", "u32 Music_AsyncParseBanks(void)"),
    ("HOWL_Music.c", "void Music_End(void)"),
    ("HOWL_Music.c", "void Music_Stop(void)"),
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
    s_fallbackLevel = -1;
    s_songStartLogged = 0;
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

static int logged(const char *text)
{
    for (int i = 0; i < start_logs && i < 16; i++)
        if (strstr(log_lines[i], text)) return 1;
    return 0;
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
    data.levBank_FX[3] = 33;
    data.levBank_Song[3] = 44;
    data.levBank_FX[5] = 55;
    data.levBank_Song[5] = 66;
    sdata->songBankBossID[0] = 26;

    // A seed race or venue with its own verified music: unchanged, no
    // fallback lines.
    serving_load = cortex_serving = accept_bank = 1;
    content_fallback = 3;
    run_load(ADVENTURE_MODE, 0, 0);
    expect_custom(CORTEX_MUSIC_BANK54, 54);
    run_load(ADVENTURE_MODE, 0, TOKEN_RACE);
    expect_custom(CORTEX_MUSIC_BANK54, 54);
    run_load(ADVENTURE_MODE | RELIC_RACE, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);
    run_load(ADVENTURE_MODE | ADVENTURE_CUP, 4, 0);
    expect_custom(CORTEX_MUSIC_BANK54, 54);
    assert(!logged("fallback"));

    // An offline custom race with its own music: unchanged.
    serving_load = cortex_serving = 0;
    offline = 1;
    run_load(TIME_TRIAL, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);
    run_load(ARCADE_MODE, 0, 0);
    expect_custom(CORTEX_MUSIC_BANK54, 54);
    offline = 0;

    // A boss venue plays the boss theme when Boss Music says so: unchanged.
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

    // A custom boss venue whose music is refused plays the picked fallback,
    // not the host slot's bank, and logs the decision at both stages.
    accept_bank = 0;
    run_load(ADVENTURE_MODE | ADVENTURE_BOSS, 0, 0);
    assert(!admitted && !s_customSongReady);
    assert(fallback_bossActive_seen == 1); // IS_BOSS_RACE on the signed game mode
    assert(num_loaded == 2 && loaded[0] == 33 && loaded[1] == 56);
    assert(retail_song == 44);
    assert(logged("track music not used, random fallback: level 3 (FX bank 33, song bank 44)"));
    oxide = 0;

    // A custom cup leg with its own music: unchanged.
    serving_load = cortex_serving = accept_bank = 1;
    run_load(ADVENTURE_MODE | ADVENTURE_BOSS | ADVENTURE_CUP, 0, 0);
    expect_custom(CORTEX_MUSIC_PER_PLAYER, 56);

    // A retail seed race keeps its own bank and song and logs nothing.
    serving_load = cortex_serving = 0;
    content_fallback = -1;
    accept_bank = 0;
    {
        int logsBefore = start_logs;
        run_load(ADVENTURE_MODE, 0, TOKEN_RACE);
        assert(planned == CORTEX_MUSIC_OFF && !admitted && retail_song == 12);
        assert(num_loaded == 2 && loaded[0] == 7 && loaded[1] == 54);
        assert(start_logs == logsBefore);
    }

    // A seed race whose package serves no music plays the picked fallback
    // (previously the silent Roo's Tubes branch), and logs both stages.
    serving_load = 1;
    content_fallback = 3;
    run_load(ADVENTURE_MODE, 0, TOKEN_RACE);
    assert(planned == CORTEX_MUSIC_OFF && !admitted && !s_customSongReady);
    assert(num_loaded == 2 && loaded[0] == 33 && loaded[1] == 54);
    assert(retail_song == 44);
    assert(logged("no track music served, random fallback: level 3 (FX bank 33, song bank 44)"));

    // An offline custom race without music plays its own pick in Arcade.
    serving_load = 0;
    offline = 1;
    offline_fallback = 5;
    run_load(ARCADE_MODE, 0, 0);
    assert(!admitted && !s_customSongReady);
    assert(num_loaded == 2 && loaded[0] == 55 && loaded[1] == 54);
    assert(retail_song == 66);
    assert(logged("no verified track music, random fallback: level 5 (FX bank 55, song bank 66)"));
    // ... and in Time Trial.
    run_load(TIME_TRIAL, 0, 0);
    assert(loaded[0] == 55 && retail_song == 66);
    offline = 0;

    // The custom song start retry, with the start line logged once per race.
    s_customSongReady = 1;
    s_fallbackLevel = -1;
    s_songStartLogged = 0;
    start_calls = start_logs = 0;
    Music_End();
    start_result = 0;
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 1 && start_logs == 0 && !sdata->cseqBoolPlay);
    start_result = 1;
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 2 && start_logs == 1 && sdata->cseqBoolPlay);
    assert(logged("started the track's own music"));
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 2 && start_logs == 1);
    // A mask song and back restarts song 0 without logging again.
    Music_Adjust(1, 0, NULL, 0);
    assert(start_calls == 3);
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 4 && start_logs == 1);

    // A fallback race logs once which retail song is active when it starts.
    s_customSongReady = 0;
    s_fallbackLevel = 3;
    s_songStartLogged = 0;
    Music_End();
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 5 && start_logs == 2);
    assert(logged("started fallback music: level 3 (FX bank 33, song bank 44)"));
    Music_Adjust(1, 0, NULL, 0);
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 7 && start_logs == 2);
    // A Retry stops the music and starts the song again: logged again.
    Music_End();
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 8 && start_logs == 3);
    // A paused Retry reaches the traffic-light stop without Music_End.
    Music_Stop();
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 9 && start_logs == 4);

    // A retail race logs nothing when its song starts.
    s_fallbackLevel = -1;
    s_songStartLogged = 0;
    Music_End();
    Music_Adjust(CSEQ_SONG_LEVEL, 0, NULL, 0);
    assert(start_calls == 10 && start_logs == 4);

    puts("PASS: custom and retail race bank selection, boss music preference, random fallback and decision logs, custom song start retry");
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
