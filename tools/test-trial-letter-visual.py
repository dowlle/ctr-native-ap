"""Exercise the actual letter visual helper and tick, including trial receipts."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/231/RB_CtrLetter.c").read_text()
start = source.index("static void AP_CtrLetter_UpdateVisual")
helper = source[start:source.index("\n#endif", start)]
start = source.index("void RB_CtrLetter_ThTick(struct Thread *t)")
tick = source[start:source.index("\n// NOTE", start)]
fixture = r'''
#include <assert.h>
#define CTR_AP 1
#define DRAW_TRANSPARENT 1
#define GHOST_DRAW_TRANSPARENT 2
#define USE_SPECULAR_LIGHT 4
#define THREAD_FLAG_DEAD 8
typedef struct { int x,y,z; } Vec;
struct Model { int id; };
struct Thread;
struct Instance { struct Model *model; struct Thread *thread; Vec scale;
 int flags, alphaScale, colorRGBA, matrix; };
struct CtrLetter { Vec rot; };
struct Thread { struct Instance *inst; struct CtrLetter *object; int flags; };
static struct { int levelID; } game;
static struct { void *unused; typeof(game) *gGT; } data={0,&game}, *sdata=&data;
static Vec letterLightDir;
static int available;
int AP_CtrLetterIndex(int id) { return id; }
int AP_LetterAvailable(int track,int letter) { return available; }
void ConvertRotToMatrix(int *m,Vec *v) {}
void Vector_SpecLightSpin3D(struct Instance *i,Vec *v,Vec *l) { i->colorRGBA=123; }
'''
tests = r'''
int main(void) {
 for(int track=0;track<18;track++) for(int letter=0;letter<3;letter++) {
  struct Model model={letter}; struct CtrLetter object={{0,0,0}};
  struct Instance inst={.model=&model,.scale={1,1,1}};
  struct Thread thread={&inst,&object,0}; inst.thread=&thread; game.levelID=track;
  available=0; RB_CtrLetter_ThTick(&thread);
  assert(inst.flags==GHOST_DRAW_TRANSPARENT && inst.alphaScale==0xa00 && inst.colorRGBA==0);
  available=1; RB_CtrLetter_ThTick(&thread);
  assert(inst.flags==USE_SPECULAR_LIGHT && inst.alphaScale==0 && inst.colorRGBA==0xffc8000);
  inst.scale.x=0; inst.flags=0; available=0; AP_CtrLetter_UpdateVisual(&inst);
  assert(inst.flags==0 && inst.scale.x==0);
  inst.scale.x=1; thread.flags=THREAD_FLAG_DEAD; AP_CtrLetter_UpdateVisual(&inst);
  assert(inst.flags==0);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp)/"fixture.c", Path(tmp)/"fixture"
    src.write_text(fixture+helper+tick+tests)
    subprocess.run(["cc", "-std=gnu11", "-fsanitize=undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("54 production letter visual cases passed: locked, received, hidden and dead")
