"""Execute the production BSP leaf function with absent and live instances."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'game/COLL.c').read_text()
start = source.index('void COLL_FIXED_BSPLEAF_TestInstance(')
end = source.index('// NOTE(', start)
function = source[start:end]
fixture = r'''
#include <assert.h>
#include <stddef.h>
typedef int s32;
#define BSP_HITBOX_COLLIDABLE 128
#define DRAW_COLLISION_MASK 15
struct Vec { int x,y,z; };
struct BoundingBox { struct Vec min,max; };
struct Instance { int flags; };
struct InstDef { struct Instance *ptrInstance; };
struct BSP { int flag; struct BoundingBox box; union {
 struct { struct BSP *bspHitboxArray; } leaf;
 struct { struct InstDef *instDef; } hitbox;
} data; };
struct ScratchpadStruct { int numBspHitboxesHit; struct BSP *bspHitboxesHit[4]; struct BoundingBox bbox; };
static int calls;
void COLL_FIXED_INSTANC_TestPoint(struct ScratchpadStruct *s, struct BSP *b) { (void)s; (void)b; calls++; }
'''
tests = r'''
int main(void) {
 struct Instance visible={15}, hidden={0};
 struct InstDef absent={NULL}, live={&visible}, invisible={&hidden};
 struct BSP rows[2]={0}, leaf={0};
 struct ScratchpadStruct s={0};
 leaf.data.leaf.bspHitboxArray=rows;
 rows[0].flag=128; rows[0].data.hitbox.instDef=&absent;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==0);
 rows[0].data.hitbox.instDef=&live;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==1);
 rows[0].data.hitbox.instDef=&invisible;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==1);
 rows[0].data.hitbox.instDef=NULL;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==2);
 rows[0].flag=1; rows[0].data.hitbox.instDef=&absent;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 s.numBspHitboxesHit=1; s.bspHitboxesHit[0]=rows;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 s.numBspHitboxesHit=0; rows[0].box.min.x=1;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 leaf.data.leaf.bspHitboxArray=NULL;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'fixture.c'
    exe = Path(tmp) / 'fixture'
    src.write_text(fixture + function + tests)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: 8 production BSP collision cases, UBSan enabled')
