"""Execute the real profile-load statements, including their AP racer override."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/SelectProfile.c").read_text()

def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]

fixture = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define CTR_AP
#define MAIN_MENU_LEVEL 39
typedef short s16;
struct Adv { s16 characterID; char name[16]; };
struct GameTracker { int levelID, currLEV; char prevNameEntered[16]; };
struct RectMenu { int unused; };
struct MemcardProfile { int gameProgress; struct Adv advProgress[3]; } card;
static struct GameTracker gt;
static struct { struct GameTracker *gGT; int gameProgress; struct Adv advProgress; } state, *sdata = &state;
static struct { s16 characterIDs[8]; } data;
static int authoritative = -1, loads, seen;
static int AP_CharSwap_GarageRacer(void) { return authoritative; }
static struct MemcardProfile *SelectProfile_MemcardProfile(void) { return &card; }
static void GAMEPROG_SyncGameAndCard(int *a, int *b) { (void)a; (void)b; }
static void MainRaceTrack_RequestLoad(int level) {
    assert(level == 26); loads++; seen = data.characterIDs[0];
    assert(data.characterIDs[0] == sdata->advProgress.characterID);
}
static void RECTMENU_Hide(struct RectMenu *m) { (void)m; }
'''
fixture += function("static void SelectProfile_ApplyApRacer(void)")
fixture += function("void SelectProfile_QueueLoadHub_MenuProc(struct RectMenu *menu)")
fixture += function("static void SelectProfile_LoadAdvProfile(int slot)")
fixture += r'''
int main(void) {
    sdata->gGT = &gt; gt.currLEV = 26;
    card.advProgress[1].characterID = 2;
    strcpy(card.advProgress[1].name, "saved profile");
    for (int racer = -1; racer < 16; ++racer) {
        authoritative = racer;
        SelectProfile_LoadAdvProfile(1);
        int expected = racer < 0 ? 2 : racer;
        assert(data.characterIDs[0] == expected);
        assert(sdata->advProgress.characterID == expected);
        assert(!strcmp(gt.prevNameEntered, "saved profile"));
        assert(card.advProgress[1].characterID == 2); /* No card write. */
        /* The queued load must defend against a later legacy field write. */
        sdata->advProgress.characterID = 2;
        SelectProfile_QueueLoadHub_MenuProc(NULL);
        assert(seen == expected);
    }
    assert(loads == 17);
    puts("PASS: both profile-load paths apply the AP racer before model load; offline save is preserved");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "profile.c").write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(path / "profile.c"), "-o", str(path / "profile")], check=True)
    subprocess.run([str(path / "profile")], check=True)
