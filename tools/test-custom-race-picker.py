"""Execute the production custom warp picker control block with engine seams."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/232/AH_WarpPad.c").read_text()
start = source.index("\t\tif (ctr_cfg_active() && ctr_cfg.custom_ctr_enabled &&")
end = source.index("\n#endif", start)
block = source[start:end]
capture_start = source.index("static struct {", source.index("static int apCustomRaceChoice"))
capture_end = source.index("static void AH_WarpPad_CustomRaceMenuProc", capture_start)
capture = source[capture_start:capture_end]
fixture = r'''
#include <assert.h>
#include <stddef.h>
typedef short s16;
typedef struct { s16 x,y,z; } SVec3;
#define REFLECTIVE 1
#define HIDE_MODEL 2
#define TOKEN_RACE 8
#define RELIC_RACE 128
#define DRIVER_FUNC_INIT 0
struct Instance { SVec3 scale; int flags; s16 vertSplit; } instance;
struct Driver { void (*funcPtrs[1])(void); int driverID;
 struct { int y; } posCurr; s16 turnAngleCurr; SVec3 rotCurr; struct Instance *instSelf; } driver;
struct GameTracker { int gameMode1, gameMode2; struct Driver *drivers[1];
 struct { s16 cameraMode; } cameraDC[1]; } game, *gGT=&game;
struct Warp { int framesWarping, boolEnteredWarppad; } warp, *warppadObj=&warp;
struct Menu { int rowSelected; } apCustomRaceMenu;
struct { struct { struct { int AddBitsConfig8, RemBitsConfig8, AddBitsConfig0, RemBitsConfig0; } OnBegin; } Loading; } data, *sdata=&data;
struct { int custom_ctr_enabled; struct { int replaces_cup_level_id; } custom_track; } ctr_cfg;
static int apCustomRaceChoice, shown, checked, active=1;
void VehPhysProc_Driving_Init(void) {}
int ctr_cfg_active(void) { return active; }
int AP_CustomTrackTrophyChecked(void) { return checked; }
void RECTMENU_Show(struct Menu *menu) { assert(menu==&apCustomRaceMenu); shown++; }
'''
fixture += capture + r'''
int evaluate(void) {
 int levelID=104;
'''
tests = r'''
 return 1;
WarpPad_TrophyAnimateOnly: return -2;
WarpPad_AnimateOpen: return -1;
}
int main(void) {
 gGT->drivers[0]=&driver;
 driver.instSelf=&instance;
 ctr_cfg.custom_ctr_enabled=1; ctr_cfg.custom_track.replaces_cup_level_id=104;
 for(checked=0;checked<2;checked++) {
  warp.framesWarping=61; shown=0; apCustomRaceChoice=0;
  assert(evaluate()==-2 && shown==1 && apCustomRaceChoice==-2);
  assert(apCustomRaceMenu.rowSelected==checked);
  warp.framesWarping=63; assert(evaluate()==-2 && shown==1);
 }
 for(int choice=-1;choice<=1;choice++)
 for(int live=0;live<2;live++)
 for(int add=0;add<2;add++)
 for(int rem=0;rem<2;rem++) {
  warp.framesWarping=63; warp.boolEnteredWarppad=1;
  driver.posCurr.y=123; instance.scale=(SVec3){100,200,300};
  driver.turnAngleCurr=-1234; driver.rotCurr.y=2345;
  instance.flags=16; instance.vertSplit=42; game.cameraDC[0].cameraMode=7;
  AH_WarpPad_CustomRaceCapture(gGT);
  driver.posCurr.y=999; instance.scale=(SVec3){1,2,3};
  driver.turnAngleCurr=1000; driver.rotCurr.y=-1000;
  instance.flags=32 | REFLECTIVE | HIDE_MODEL;
  instance.vertSplit=99; game.cameraDC[0].cameraMode=3;
  apCustomRaceChoice=choice; driver.funcPtrs[0]=0;
  game.gameMode2=16 | (live ? TOKEN_RACE : 0);
  game.gameMode1=256 | RELIC_RACE;
  data.Loading.OnBegin.AddBitsConfig0=512 | RELIC_RACE;
  data.Loading.OnBegin.RemBitsConfig0=1024;
  data.Loading.OnBegin.AddBitsConfig8=32 | (add ? TOKEN_RACE : 0);
  data.Loading.OnBegin.RemBitsConfig8=64 | (rem ? TOKEN_RACE : 0);
  int result=evaluate();
  if(choice<0) {
   assert(result==-1 && warp.framesWarping==0 && warp.boolEnteredWarppad==0);
   assert(driver.funcPtrs[0]==VehPhysProc_Driving_Init);
   assert(driver.posCurr.y==123);
   assert(driver.turnAngleCurr==-1234 && driver.rotCurr.y==2345);
   assert(instance.scale.x==100 && instance.scale.y==200 && instance.scale.z==300);
   assert(instance.flags==32 && instance.vertSplit==42 && game.cameraDC[0].cameraMode==7);
   assert(apCustomWarpSnapshot.driver==NULL);
   assert(game.gameMode1==(256 | RELIC_RACE));
  } else {
   assert(result==1);
   assert(game.gameMode1==256);
   assert(data.Loading.OnBegin.AddBitsConfig0==512);
   assert(data.Loading.OnBegin.RemBitsConfig0==(1024 | RELIC_RACE));
   assert(game.gameMode2==(16 | (choice==1 ? TOKEN_RACE : 0)));
   assert(data.Loading.OnBegin.AddBitsConfig8==(32 | (choice==1 ? TOKEN_RACE : 0)));
   assert(data.Loading.OnBegin.RemBitsConfig8==(64 | (choice==0 ? TOKEN_RACE : 0)));
  }
 }
 active=0; warp.framesWarping=61; shown=0;
 assert(evaluate()==1 && shown==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "picker.c", Path(tmp) / "picker"
    src.write_text(fixture + block + tests)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: production custom picker waiting, cursor, 24 mode/cancel cases, inactive bypass")
