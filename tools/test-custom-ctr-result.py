"""Run the production custom CTR notification with controlled engine seams."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks.c").read_text()
start = source.index("void AP_NotifyCustomTrackCtr(int didWin, int collected)")
end = source.index("\nint AP_LetterAvailable(", start)
function = source[start:end]
fixture = r'''
#include <assert.h>
#include <stddef.h>
#define TOKEN_RACE 8
#define AP_CUSTOM_CTR_PSEUDO_BIT 514
static int ap_custom_ceremony_bit;
static struct { int active; } ap_custom_trophy_ceremony;
void AP_CustomTrophyCeremonyArm(void *state, int sent) {
 assert(state==&ap_custom_trophy_ceremony && sent==1);
 ap_custom_trophy_ceremony.active=1;
}
struct GameTracker { int gameMode2; };
struct Sdata { struct GameTracker *gGT; } data, *sdata = &data;
struct Config { int custom_ctr_enabled; long custom_ctr_location;
 struct { int host_level_id, slot; } custom_track;
} ctr_cfg;
static int serving, earned, sends;
static int emitted_new=1;
static int location_exists=1;
int ap_net_location_exists(long code) { assert(code==35023006); return location_exists; }
static long sent_code;
int AP_CustomLetterSlot(int track) {
 assert(track == 6); return serving;
}
int AP_LetterTokenEarned(int track, int win, int collected) {
 assert(track == 6); return win && collected == 2 && earned;
}
int AP_EmitClassCheck(long code, int mirror, int bit, int count, int toast,
                     const char *format, ...) {
 (void)format;
 assert(mirror == 0 && bit == -1 && count == -1 && toast == 1);
 sends++; sent_code = code; return emitted_new;
}
'''
tests = r'''
int main(void) {
 struct GameTracker game = {0};
 data.gGT = &game;
 ctr_cfg.custom_track.host_level_id = 6;
 ctr_cfg.custom_track.slot = 7;
 ctr_cfg.custom_ctr_location = 35023006;
 for (int token=0; token<2; token++)
 for (int mode=0; mode<2; mode++)
 for (serving=0; serving<2; serving++)
 for (earned=0; earned<2; earned++)
 for (int win=0; win<2; win++)
 for (int collected=0; collected<4; collected++) {
  game.gameMode2 = token ? TOKEN_RACE : 0;
  ctr_cfg.custom_ctr_enabled = mode;
  sends=0; sent_code=-1;
  ap_custom_trophy_ceremony.active=0; ap_custom_ceremony_bit=-1;
  AP_NotifyCustomTrackCtr(win,collected);
  int expected = token && mode && serving && earned && win && collected==2;
  assert(sends == expected);
  assert(sent_code == (expected ? 35023006 : -1));
  assert(ap_custom_trophy_ceremony.active==expected);
  assert(ap_custom_ceremony_bit==(expected ? 514 : -1));
 }
 game.gameMode2=TOKEN_RACE; serving=earned=1; ctr_cfg.custom_ctr_enabled=1;
 emitted_new=0; sends=0;
 ap_custom_trophy_ceremony.active=0; ap_custom_ceremony_bit=-1;
 AP_NotifyCustomTrackCtr(1,2);
 assert(sends==1 && ap_custom_trophy_ceremony.active==0 && ap_custom_ceremony_bit==-1);
 ap_custom_trophy_ceremony.active=1; ap_custom_ceremony_bit=512;
 AP_NotifyCustomTrackCtr(1,2);
 assert(ap_custom_trophy_ceremony.active==1 && ap_custom_ceremony_bit==512);
 location_exists=0; emitted_new=1; sends=0;
 ap_custom_trophy_ceremony.active=0; ap_custom_ceremony_bit=-1;
 AP_NotifyCustomTrackCtr(1,2);
 assert(sends==0 && ap_custom_trophy_ceremony.active==0 && ap_custom_ceremony_bit==-1);
 location_exists=1;
 data.gGT = NULL; sends=0;
 AP_NotifyCustomTrackCtr(1,2); assert(sends==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "fixture.c", Path(tmp) / "fixture"
    src.write_text(fixture + function + tests)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 128 production custom CTR result cases, absent tracker and duplicate-arm isolation, UBSan")

cup_source = (root / "game/UI/UI_CupStandings.c").read_text()
start = cup_source.index("if (data.cupPositionPerPlayer[0] == gGT->drivers[0]->driverID")
end = cup_source.index("\n\t\t\t\t\t{", start)
condition = cup_source[start:end]
cup_fixture = r'''
#include <assert.h>
#define CTR_AP
#define CTR_CUSTOM_TRACKS
#define TOKEN_RACE 8
struct Driver { int driverID; struct { int numCollected; } PickupLetterHUD; } driver;
struct GameTracker { int gameMode2; struct Driver *drivers[1]; } game, *gGT = &game;
struct { int cupPositionPerPlayer[1]; } data;
struct { struct { int host_level_id; } custom_track; } ctr_cfg;
static int active, redirect, earned;
int ctr_cfg_active(void) { return active; }
int CustomTrack_CupRaceRedirectActive(int cup, int adventure) {
 assert(cup == 4 && adventure == 1); return redirect;
}
int AP_LetterTokenEarned(int host, int win, int count) {
 assert(host == 6 && win == 1 && count == 2); return earned;
}
static int allowed(void) {
 int i=4;
'''
cup_tests = r'''
 { return 1; }
 return 0;
}
int main(void) {
 driver.driverID=0; driver.PickupLetterHUD.numCollected=2;
 game.drivers[0]=&driver; ctr_cfg.custom_track.host_level_id=6;
 for (active=0; active<2; active++)
 for (redirect=0; redirect<2; redirect++)
 for (earned=0; earned<2; earned++)
 for (int token=0; token<2; token++)
 for (int win=0; win<2; win++) {
  game.gameMode2=token ? TOKEN_RACE : 0;
  data.cupPositionPerPlayer[0]=win ? 0 : 1;
  assert(allowed() == (win && (!(active && redirect && token) || earned)));
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "cup.c", Path(tmp) / "cup"
    src.write_text(cup_fixture + condition + cup_tests)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 32 production Cup award eligibility cases, retail/custom isolation")
