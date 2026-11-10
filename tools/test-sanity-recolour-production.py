"""Exercise production texture copies and GPU metadata with reused vertices."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
gpu = (root / 'platform/native_gpu.c').read_text()
bucket = (root / 'game/RenderBucket/RenderBucket_QueueExecute.c').read_text()

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "ap_sanity_colour.h"
#define CTR_AP
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int16_t s16;
typedef struct { s16 x,y,page,clut; u8 u,v,bright,dither,r,g,b,a; signed char tcx,tcy,_p0,_p1; } GrVertex;
#define AP_TPAGE_SIDELOAD_MASK 0xc000
static int g_cfg_bilinearFiltering;
static short GetTPageBase(int page) { return (page & 15) + ((page & 16) ? 16 : 0); }
struct TextureLayout { u8 u0,v0; u16 clut; u8 u1,v1; u16 tpage; u8 u2,v2,u3,v3; };
struct InstDrawPerPlayer { struct TextureLayout **ptrTexLayout; };
struct RenderBucketDrawContext { struct InstDrawPerPlayer *idpp; struct TextureLayout sanityTexture; unsigned sanityTag; };
'''
production = function(bucket, 'static struct TextureLayout *RenderBucket_GetCommandTexture(')
production += '\n' + function(bucket, 'static u32 RenderBucket_SanityColour(')
for name in ['Quad', 'Triangle', 'Rect', 'LineZero', 'TriangleZero', 'QuadZero']:
    production += '\n' + function(gpu, 'void MakeTexcoord' + name + '(')
tests = r'''
int main(void) {
    struct TextureLayout original = {1,2,0x123,3,4,0x15f,5,6,7,8}, saved = original;
    struct TextureLayout *textures[] = {&original, 0};
    struct InstDrawPerPlayer idpp = {textures};
    struct RenderBucketDrawContext ctx = {&idpp, {0}, 0};
    GrVertex vertices[4]; u8 uv[] = {20,30}; int valid;
    assert(RenderBucket_GetCommandTexture(&ctx,1,&valid) == &original && valid);
    for (int tag=1; tag<=9; ++tag) {
        ctx.sanityTag = tag << 10;
        assert(RenderBucket_SanityColour(&ctx, 0x32abcdef) == AP_SanityRecolourRGB(0x32abcdef, tag));
        struct TextureLayout *copy = RenderBucket_GetCommandTexture(&ctx,1,&valid);
        assert(copy != &original && valid && copy->tpage == (original.tpage | ctx.sanityTag));
        assert(!memcmp(&original,&saved,sizeof original));
        assert(copy->clut == original.clut && copy->u0 == original.u0 && copy->v3 == original.v3);
        memset(vertices,0x7f,sizeof vertices);
        MakeTexcoordTriangle(vertices,uv,uv,uv,copy->tpage,copy->clut,1);
        for (int i=0;i<3;++i) assert(vertices[i]._p0==tag && vertices[i].page==GetTPageBase(original.tpage) && vertices[i].clut==original.clut);
        MakeTexcoordQuad(vertices,uv,uv,uv,uv,copy->tpage,copy->clut,1);
        for (int i=0;i<4;++i) assert(vertices[i]._p0==tag);
        MakeTexcoordRect(vertices,uv,copy->tpage,copy->clut,8,8);
        for (int i=0;i<4;++i) assert(vertices[i]._p0==tag);
        MakeTexcoordQuad(vertices,uv,uv,uv,uv,original.tpage,copy->clut,1);
        for (int i=0;i<4;++i) assert(!vertices[i]._p0);
        memset(vertices,0x7f,sizeof vertices); MakeTexcoordTriangleZero(vertices,0);
        for (int i=0;i<3;++i) assert(!vertices[i]._p0);
        memset(vertices,0x7f,sizeof vertices); MakeTexcoordQuadZero(vertices,0);
        for (int i=0;i<4;++i) assert(!vertices[i]._p0);
        memset(vertices,0x7f,sizeof vertices); MakeTexcoordLineZero(vertices,0);
        for (int i=0;i<4;++i) assert(!vertices[i]._p0);
    }
    assert(!RenderBucket_GetCommandTexture(&ctx,0,&valid) && valid);
    assert(!RenderBucket_GetCommandTexture(&ctx,2,&valid) && valid);
    idpp.ptrTexLayout=0;
    assert(!RenderBucket_GetCommandTexture(&ctx,1,&valid) && !valid);
    puts("production recolour: private texture copy, nine tags, all vertex paths and reused slots pass");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / 'recolour.c', Path(tmp) / 'recolour'
    src.write_text(fixture + production + tests)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-I', str(root / 'ap'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
