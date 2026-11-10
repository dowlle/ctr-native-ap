"""Run the bank-load reverb call and selector with the production retail tables."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]


def table(path, name):
    source = (ROOT / path).read_text()
    return re.search(r"\." + name + r"\s*=\s*(\{[^}]*\})", source).group(1)


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include <platform/native_custom_identity.h>
struct sData sdata_static;
struct Data data;
static struct GameTracker gt;
static int selected, calls, custom;
void SetReverbMode(u16 mode) { selected = mode; calls++; }
int MainRaceTrack_IdentityLevelID(void) {
    return CustomIdentity_LevelID(gt.levelID, custom);
}
void HOWL_CustomMusic_Reset(void) {}
void Bank_DestroyAll(void) {}
void Bank_DestroyUntilIndex(int index) { (void)index; }
int Bank_Load(int id, struct Bank *bank) { (void)id; (void)bank; return 1; }
'''
fixture += function("game/HOWL/HOWL_AudioState.c",
                    "void Audio_SetReverbMode(int levelID, u32 isBossRace, int bossID)")
fixture += function("game/HOWL/HOWL_Music.c", "void Music_LoadBanks(void)")
fixture += "\nstatic const char levels[] = " + table("game/zGlobal_DATA.c", "reverbMode") + ";\n"
fixture += "static const char bosses[] = " + table("game/zGlobal_SDATA.c", "reverbModeBossID") + ";\n"
fixture += r'''
int main(void) {
    sdata->gGT = &gt;
    sdata->boolAudioEnabled = 1;
    memcpy(data.reverbMode, levels, sizeof levels);
    memcpy(sdata->reverbModeBossID, bosses, sizeof bosses);
    const int home[] = {ROO_TUBES, PAPU_PYRAMID, DRAGON_MINES,
                        HOT_AIR_SKYWAY, OXIDE_STATION, OXIDE_STATION};
    const int modes[] = {3, 1, 1, 0, 3, 3};
    // Four garages and both Oxide encounters agree with retail at home.
    for (int boss = 0; boss < 6; boss++) {
        assert(levels[home[boss]] == modes[boss] && bosses[boss] == modes[boss]);
        // Every retail venue, every garage, then custom bytes on each host.
        // No pad/ownership state is needed to select the raced track's mode.
        for (int level = 0; level < INTRO_RACE_TODAY; level++) {
            gt.levelID = level;
            gt.bossID = boss;
            gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_BOSS;
            custom = 0; selected = -1; calls = 0;
            Music_LoadBanks();
#ifdef CTR_AP
            assert(selected == levels[level]);
#else
            assert(selected == bosses[boss]);
#endif
            assert(calls == 1);
            gt.gameMode1 = ARCADE_MODE;
            Music_LoadBanks();
            assert(selected == levels[level]);
#ifdef CTR_CUSTOM_PACKAGES
            custom = 1;
            gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_BOSS;
            Music_LoadBanks();
#ifdef CTR_AP
            assert(selected == 4);
#else
            assert(selected == bosses[boss]);
#endif
            gt.gameMode1 = ARCADE_MODE;
            Music_LoadBanks();
            assert(selected == 4);
            custom = 0;
#endif
            sdata->boolAudioEnabled = 0; calls = 0;
            Music_LoadBanks();
            assert(calls == 0);
            sdata->boolAudioEnabled = 1;
        }
    }
    // Non-driveable scenes retain the default and audio-off remains a no-op.
    gt.levelID = INTRO_RACE_TODAY;
    gt.gameMode1 = 0;
    Music_LoadBanks();
    assert(selected == 4);
    puts("PASS: bank-load reverb, all retail venues, four garages, both Oxide encounters, custom defaults and audio-off");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "reverb.c").write_text(fixture)
    for ap in (False, True):
        for custom in (False, True):
            defines = (["-DCTR_AP"] if ap else []) + (["-DCTR_CUSTOM_PACKAGES"] if custom else [])
            subprocess.run(["cc", "-m32", "-DCTR_NATIVE", "-DBUILD=926", *defines,
                            "-I", str(ROOT / "include"), "-I", str(ROOT),
                            str(path / "reverb.c"), "-o", str(path / "reverb")], check=True)
            subprocess.run([str(path / "reverb")], check=True)
