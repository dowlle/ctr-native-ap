"""Exercise the actual hub spawn lookup with colliding custom/retail hosts."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/232/AH_WarpPad.c").read_text()
start = source.index("s16 *AH_WarpPad_GetSpawnPosRot(s16 *posData)")
end = source.index("\n}\n", start) + 3
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "platform/native_custom_race_context.h"
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#define WARPPAD 0
#define AP_CORTEX_DEST 110
typedef int16_t s16;
struct InstDef { struct { s16 x,y,z; } rot; } definitions[3];
struct Instance { char name[32]; struct { int t[3]; } matrix; struct InstDef *instDef; } instances[3];
struct WarpPad { int levelID; } pads[3];
struct Thread { struct Thread *siblingThread; void *object; struct Instance *inst; } threads[3];
struct GameTracker { int levelID,prevLEV; struct { struct Thread *thread; } threadBuckets[1]; } game;
static struct {struct GameTracker *gGT;} data={&game}, *sdata=&data;
static struct CustomTrackRaceContext previous;
static int havePrevious, cortex;
const struct CustomTrackRaceContext *CustomTrack_PreviousStandaloneContext(void) {
 return havePrevious ? &previous : NULL;
}
int CustomTrack_CortexTrackPrevServed(void) { return cortex; }
int MATH_Cos(int a) { (void)a; return 4096; }
int MATH_Sin(int a) { (void)a; return 0; }
'''
fixture += source[start:end]
fixture += r'''
static void reset(void) {
 memset(&previous,0,sizeof previous); havePrevious=1; cortex=0;
 previous.hostLevelID=6; previous.returnHub=26; previous.physicalPad=3;
 game.levelID=26; game.prevLEV=6; game.threadBuckets[0].thread=&threads[0];
 for(int i=0;i<3;i++) {
  threads[i].siblingThread=i<2 ? &threads[i+1] : NULL;
  threads[i].object=&pads[i]; threads[i].inst=&instances[i];
  instances[i].instDef=&definitions[i]; instances[i].matrix.t[1]=100+i;
 }
 strcpy(instances[0].name,"warppad#6"); pads[0].levelID=6;
 strcpy(instances[1].name,"warppad#3"); pads[1].levelID=120;
 strcpy(instances[2].name,"warppad#100"); pads[2].levelID=121;
}
int main(void) {
 s16 out[3];
 reset(); assert(AH_WarpPad_GetSpawnPosRot(out)==&definitions[1].rot.x && out[1]==101);
 previous.physicalPad=100;
 assert(AH_WarpPad_GetSpawnPosRot(out)==&definitions[2].rot.x && out[1]==102);
 previous.physicalPad=104; assert(AH_WarpPad_GetSpawnPosRot(out)==NULL);
 reset(); previous.returnHub=27;
 assert(AH_WarpPad_GetSpawnPosRot(out)==&definitions[0].rot.x);
 reset(); previous.hostLevelID=3;
 assert(AH_WarpPad_GetSpawnPosRot(out)==&definitions[0].rot.x);
 reset(); havePrevious=0;
 assert(AH_WarpPad_GetSpawnPosRot(out)==&definitions[0].rot.x);
 reset(); havePrevious=0; cortex=1; game.prevLEV=13; pads[2].levelID=110;
 assert(AH_WarpPad_GetSpawnPosRot(out)==&definitions[2].rot.x);
 puts("PASS: 7 production hub-return cases; source pad wins over borrowed host");
}
'''
with tempfile.TemporaryDirectory() as temp:
    src, exe = Path(temp) / "return.c", Path(temp) / "return"
    src.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-I", str(root / "include"),
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
