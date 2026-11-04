"""Execute production plan-menu refusal and its kart/camera snapshot restore."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/232/AH_WarpPad.c").read_text()
capture_start = source.index("static struct {")
capture = source[capture_start:source.index("\n#endif", capture_start)]
refuse_start = source.index("WarpPad_RefuseEntry:\n")
refuse = source[refuse_start:source.index("\n#endif", refuse_start)]
plan = source[source.index("int host, mode = AP_CONTENT_MODE_TROPHY;"):]
assert "if (mode < 0)\n\t\t\t{\n\t\t\t\tapRaceTypeChoice = -2;\n\t\t\t\tgoto WarpPad_RefuseEntry;" in plan
fixture = r'''
#include <assert.h>
#include <stddef.h>
typedef short s16;
typedef struct { s16 x,y,z; } SVec3;
#define REFLECTIVE 1
#define HIDE_MODEL 2
#define DRIVER_FUNC_INIT 0
#define AP_TL_DROP_REFUSED 7
struct Instance { SVec3 scale; int flags; s16 vertSplit; } instance;
struct Driver { void (*funcPtrs[1])(void); int driverID;
 struct { int y; } posCurr; s16 turnAngleCurr; SVec3 rotCurr; struct Instance *instSelf; } driver;
struct GameTracker { struct Driver *drivers[1]; struct { s16 cameraMode; } cameraDC[1]; } game, *gGT=&game;
struct Warp { int framesWarping, boolEnteredWarppad, levelID; } warp, *warppadObj=&warp;
struct Menu { int unused; };
struct { struct Menu menuTokenRelic; } D232;
struct { int boolOpenTokenRelicMenu; } store, *sdata=&store;
static int apEntryRefusedDest=-1, dropped, hidden, logged;
void VehPhysProc_Driving_Init(void) {}
void AP_TrackerLaunchDrop(int reason) { assert(reason==AP_TL_DROP_REFUSED); dropped++; }
void RECTMENU_Hide(struct Menu *menu) { assert(menu==&D232.menuTokenRelic); hidden++; }
int ctr_cfg_warp_phys(int p) { return p; }
void AP_PadLogEntryRefused(int p,int d) { assert(p==d&&p==warp.levelID); logged++; }
'''
fixture += capture + '\nint evaluate(void) {\n goto WarpPad_RefuseEntry;\n' + refuse + r'''
WarpPad_AnimateOpen: return 1;
}
int main(void) {
 gGT->drivers[0]=&driver;driver.instSelf=&instance;
 for(int physical=0;physical<128;physical++)
 for(int ownedFlags=0;ownedFlags<4;ownedFlags++) {
  warp.levelID=physical;warp.framesWarping=63;warp.boolEnteredWarppad=1;
  sdata->boolOpenTokenRelicMenu=1;apEntryRefusedDest=-1;dropped=hidden=logged=0;
  driver.posCurr.y=123;instance.scale=(SVec3){100,200,300};
  driver.turnAngleCurr=-1234;driver.rotCurr.y=2345;
  instance.flags=16|ownedFlags;instance.vertSplit=42;game.cameraDC[0].cameraMode=7;
  AH_WarpPad_WarpCapture(gGT);
  driver.posCurr.y=999;instance.scale=(SVec3){1,2,3};
  driver.turnAngleCurr=1000;driver.rotCurr.y=-1000;
  instance.flags=32|REFLECTIVE|HIDE_MODEL;instance.vertSplit=99;game.cameraDC[0].cameraMode=3;
  driver.funcPtrs[0]=0;
  assert(evaluate()==1&&warp.framesWarping==0&&warp.boolEnteredWarppad==0);
  assert(driver.funcPtrs[0]==VehPhysProc_Driving_Init);
  assert(driver.posCurr.y==123&&driver.turnAngleCurr==-1234&&driver.rotCurr.y==2345);
  assert(instance.scale.x==100&&instance.scale.y==200&&instance.scale.z==300);
  assert(instance.flags==(32|ownedFlags)&&instance.vertSplit==42&&game.cameraDC[0].cameraMode==7);
  assert(apWarpSnapshot.driver==NULL&&sdata->boolOpenTokenRelicMenu==0);
  assert(apEntryRefusedDest==physical&&dropped==1&&hidden==1&&logged==1);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "cancel.c", Path(tmp) / "cancel"
    src.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: production plan menu cancellation, 512 kart/camera restore and latch cases")
