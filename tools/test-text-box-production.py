"""Run the production font and box renderer against a packet-recording GPU."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def source(p): return (root / p).read_text()
def function(p, signature):
    s = source(p)
    start = s.index(signature + "\n{")
    return s[start:s.index("\n}\n", start) + 3]
def table(name):
    body = re.search(r"\." + name + r"\s*=\s*\{(.*?)\}", source("game/zGlobal_DATA.c"), re.S).group(1)
    return ",".join(re.findall(r"0x[0-9A-Fa-f]+|-?\d+", re.sub(r"//[^\n]*", "", body)))

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <text_box.h>
#include <ctr_menu_ux.h>
#include "ap/ap_feed_lines_logic.h"
typedef short s16; typedef unsigned char u8; typedef unsigned int u32;
#define UsaRetail 926
#define EurRetail 1020
#define JpnRetail 1111
#define JpnTrial 1006
#define BUILD UsaRetail
#define FONT_BIG 1
#define FONT_SMALL 2
#define JUSTIFY_CENTER 0x1000
#define JUSTIFY_RIGHT 0x2000
#define WHITE 0
static struct { short font_charPixWidth[4],font_buttonPixWidth[4],font_puncPixWidth[4]; u32 *ptrColor[1]; } data={%s};
typedef struct { struct {short x,y,w,h;} clip; short ofs[2]; int isbg; } DRAWENV;
struct DB { DRAWENV drawEnv; struct { void *cursor,*guardEnd; } primMem; } db;
struct PushBuffer { struct {int x,y,w,h;} rect; uint32_t *ptrOT; };
struct GameTracker { struct DB *backBuffer; struct PushBuffer pushBuffer_UI; unsigned timer,frameTimer_VsyncCallback;int elapsedTimeMS,numPlyrCurrGame; } game;
static struct { struct GameTracker *gGT; unsigned frameCounter; } state={&game,0},*sdata=&state;
static unsigned char arena[8192];
static char events[256]; static int ne,drawWidth,drawX,drawFlags;
static DRAWENV environments[2];static int envCount;
static u32 CTR_ReadU32LE(const void *p) { u32 v;memcpy(&v,p,4);return v; }
static void CTR_WriteU32LE(void *p,u32 v) { memcpy(p,&v,4); }
static void SetDrawEnv(void *p,DRAWENV *env) { memcpy(p,env,sizeof *env);environments[envCount++]=*env; }
static void AddPrim(uint32_t *ot,void *p) { assert(ot==game.pushBuffer_UI.ptrOT);events[ne++]=(p==arena?'R':'C'); }
''' % ("{" + "},{".join(table(n) for n in ("font_charPixWidth","font_buttonPixWidth","font_puncPixWidth")) + "}")
fixture += function("game/DecalFont.c", "int DecalFont_GetLineWidthStrlen(char *character, int len, int fontType)")
gpu = source("include/psx/libgpu.h")
fixture += "\ntypedef short VERTTYPE;\n#define DECLARE_P_ADDR uint32_t tag;\n"
for primitive in ("POLY_GT4", "POLY_F4", "LINE_F2"):
    end = gpu.index("} " + primitive + ";") + len("} " + primitive + ";")
    start = gpu.rfind("typedef struct", 0, end)
    fixture += gpu[start:end] + "\n"
fixture += r'''
static void DecalFont_DrawLineStrlen(char *s,s16 n,int x,s16 y,s16 f,int flags) {
    (void)y;drawWidth+=DecalFont_GetLineWidthStrlen(s,n,f);drawX=x;drawFlags=flags;events[ne++]='G';
    size_t bytes=(size_t)n*sizeof(POLY_GT4);
    assert((uintptr_t)db.primMem.cursor+bytes<=(uintptr_t)db.primMem.guardEnd);
    memset(db.primMem.cursor,0xA5,bytes);db.primMem.cursor=(unsigned char *)db.primMem.cursor+bytes;
}
static void DecalFont_DrawLineOT(char *s,int x,int y,s16 f,int flags,uint32_t *ot) { assert(ot==game.pushBuffer_UI.ptrOT);DecalFont_DrawLineStrlen(s,strlen(s),x,y,f,flags); }
'''
fixture += source("game/DecalFontBox.c").replace("#include <common.h>", "")
fixture += r'''
static void CtrGpu_WriteColorCode(void *p,u32 value) { memcpy(p,&value,4); }
static void setPolyF4(POLY_F4 *p) {p->code=0x28;}
static void addLineF2(u32 *ot,LINE_F2 *p) {AddPrim(ot,p);}
'''
fixture += function("game/230/MM_ConfigMenu.c", "static void Conn_DrawSlotCaseMarks(const char *slot, int valueX, int y, int shift, int visibleWidth, uint32_t *ot)")
fixture += "\n#define BTN_UP 1\n#define BTN_DOWN 2\nstatic void OtherFX_Play(int a,int b) {(void)a;(void)b;}\n"
fixture += function("game/230/MM_CustomTracks.c", "static int CT_Step(int value, int count, int tapped)")
fixture += function("game/230/MM_CustomTrackSelect.c", "static void MM_CustomTrackSelect_MarqueePin(const char *pin)")
fixture += "\n#define AP_FEED_LINE_H 12\nstatic AP_FeedLines ap_feed;\nstatic struct {int box_placement_refused;} ctr_cfg;\n"
fixture += function("ap/ap_hooks_feed.c", "static void AP_FeedTickAndDraw(int x, int baseY, int playableRace)")
fixture += r'''
static void reset(void) { db.primMem.cursor=arena;ne=envCount=drawWidth=0; }
int main(void) {
    uint32_t ot;
    game.backBuffer=&db;game.pushBuffer_UI.rect.w=512;game.pushBuffer_UI.rect.h=216;game.pushBuffer_UI.ptrOT=&ot;
    game.numPlyrCurrGame=1;db.primMem.guardEnd=arena+sizeof arena-64;
    for(int w=1;w<=480;w++) {
        reset();DecalFont_DrawLineBox("Long title with @ [ ^ * and punctuation: ...",16,80,w,FONT_SMALL,0,TEXT_ELLIPSIS,0,&ot);
        assert(drawWidth<=w);assert(envCount==0);
        int n=DecalFont_LineBoxPrefixLen("Long title with @ [ ^ * and punctuation: ...",w,FONT_SMALL);
        assert(DecalFont_GetLineWidthStrlen("Long title with @ [ ^ * and punctuation: ...",n,FONT_SMALL)<=w);
    }
    reset();DecalFont_DrawLineBox("St Patricks Day Hub World",136,80,240,FONT_BIG,0,TEXT_MARQUEE,0,&ot);
    assert(ne==3&&!memcmp(events,"RGC",3));assert(envCount==2);
    assert(environments[0].clip.w==512&&environments[1].clip.x==136&&environments[1].clip.w==240);
    assert(game.pushBuffer_UI.ptrOT==&ot);
    /* Pause freezes the gameplay timer, while UI frames and VBlanks continue. */
    for(unsigned frame=1;frame<=180;frame++) {
        state.frameCounter=frame;game.frameTimer_VsyncCallback=frame;
        reset();DecalFont_DrawLineBox("St Patricks Day Hub World",136,80,240,FONT_BIG,0,TEXT_MARQUEE,0,&ot);
    }
    assert(game.timer==0&&DecalFont_LineBoxOffset("St Patricks Day Hub World",240,FONT_BIG,0)==60);
    /* Adjacent revisions share a title: actual menu navigation must restart it. */
    reset();DecalFont_DrawLineBox("St Patricks Day Hub World",32,68,244,FONT_SMALL,0,TEXT_MARQUEE,TEXT_SLOT_LIBRARY,&ot);
    for(unsigned f=181;f<=360;f++) {
        state.frameCounter=f;game.frameTimer_VsyncCallback=f;
        reset();DecalFont_DrawLineBox("St Patricks Day Hub World",32,68,244,FONT_SMALL,0,TEXT_MARQUEE,TEXT_SLOT_LIBRARY,&ot);
    }
    assert(DecalFont_LineBoxOffset("St Patricks Day Hub World",244,FONT_SMALL,TEXT_SLOT_LIBRARY)>0);
    assert(CT_Step(0,2,BTN_DOWN)==1);
    state.frameCounter++;game.frameTimer_VsyncCallback++;
    reset();DecalFont_DrawLineBox("St Patricks Day Hub World",32,83,244,FONT_SMALL,0,TEXT_MARQUEE,TEXT_SLOT_LIBRARY,&ot);
    assert(DecalFont_LineBoxOffset("St Patricks Day Hub World",244,FONT_SMALL,TEXT_SLOT_LIBRARY)==0);
    MM_CustomTrackSelect_MarqueePin("old-revision");
    for(unsigned f=362;f<=542;f++) {
        state.frameCounter=f;game.frameTimer_VsyncCallback=f;
        reset();DecalFont_DrawLineBox("St Patricks Day Hub World",40,80,240,FONT_BIG,0,TEXT_MARQUEE,TEXT_SLOT_ARCADE,&ot);
    }
    assert(DecalFont_LineBoxOffset("St Patricks Day Hub World",240,FONT_BIG,TEXT_SLOT_ARCADE)>0);
    MM_CustomTrackSelect_MarqueePin("new-revision");
    state.frameCounter++;game.frameTimer_VsyncCallback++;
    reset();DecalFont_DrawLineBox("St Patricks Day Hub World",40,80,240,FONT_BIG,0,TEXT_MARQUEE,TEXT_SLOT_ARCADE,&ot);
    assert(DecalFont_LineBoxOffset("St Patricks Day Hub World",240,FONT_BIG,TEXT_SLOT_ARCADE)==0);

    const char *notice="A VERY LONG IDENTICAL ITEM NOTICE FROM A PLAYER WITH A LONG NAME";
    AP_FeedLinesReset(&ap_feed);AP_FeedLinesEnqueue(&ap_feed,notice,0,0);
    for(int f=0;f<180;f++) {
        if(f==60) {AP_FeedLinesEnqueue(&ap_feed,notice,0,0);ap_feed.stagger=0;}
        for(int i=0;i<ap_feed.vcount;i++) ap_feed.vis[i].framesLeft=999;
        state.frameCounter++;game.frameTimer_VsyncCallback++;
        reset();AP_FeedTickAndDraw(16,200,0);
    }
    assert(ap_feed.vcount==2);
    assert(DecalFont_LineBoxOffset(notice,480,FONT_SMALL,TEXT_SLOT_FEED)>0);
    ap_feed.vis[0].framesLeft=1;ap_feed.vis[1].framesLeft=999;
    state.frameCounter++;game.frameTimer_VsyncCallback++;
    reset();AP_FeedTickAndDraw(16,200,0);
    assert(ap_feed.vcount==1&&DecalFont_LineBoxOffset(notice,480,FONT_SMALL,TEXT_SLOT_FEED)==0);
    reset();DecalFont_DrawLineBox("St Patricks Day Hub World",-240,80,240,FONT_BIG,0,TEXT_MARQUEE,1,&ot);
    assert(ne==0&&envCount==0); /* empty PSX scissor endpoints would wrap */
    reset();DecalFont_DrawLineBox("St Patricks Day Hub World",136,-30,240,FONT_BIG,0,TEXT_MARQUEE,1,&ot);
    assert(ne==0&&envCount==0);
    reset();DecalFont_DrawLineBox("Acid Lake",256,80,240,FONT_BIG,JUSTIFY_CENTER,TEXT_MARQUEE,0,&ot);
    assert(envCount==0&&ne==1&&drawFlags==JUSTIFY_CENTER);
    /* Real GT4-sized glyph writes, across every near-capacity boundary. */
    for(int available=0;available<=1600;available++) {
        reset();db.primMem.guardEnd=arena+available;
        DecalFont_DrawLineBox("St Patricks Day Hub World",136,80,240,FONT_BIG,0,TEXT_MARQUEE,0,&ot);
        assert((uintptr_t)db.primMem.cursor<=(uintptr_t)db.primMem.guardEnd);
        assert((ne==0&&envCount==0)||(ne==3&&envCount==2));
        reset();DecalFont_DrawLineBox("A long line of text",16,80,100,FONT_SMALL,0,TEXT_ELLIPSIS,0,&ot);
        assert((uintptr_t)db.primMem.cursor<=(uintptr_t)db.primMem.guardEnd);
        reset();DecalFont_DrawLineBox("Short",16,80,240,FONT_SMALL,0,TEXT_MARQUEE,0,&ot);
        assert((uintptr_t)db.primMem.cursor<=(uintptr_t)db.primMem.guardEnd);
    }
    static u32 white=0xFFFFFF;data.ptrColor[WHITE]=&white;
    const char *caps="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int markBytes=23*(sizeof(LINE_F2)+sizeof(POLY_F4));
    for(int available=0;available<=1600;available++) {
        reset();db.primMem.guardEnd=arena+available;
        Conn_DrawSlotCaseMarks(caps,0,80,0,296,&ot);
        assert((uintptr_t)db.primMem.cursor<=(uintptr_t)db.primMem.guardEnd);
        assert(ne==(available<markBytes?0:46));
    }
    reset();db.primMem.guardEnd=arena+500;
    DecalFont_DrawLineBox(caps,0,80,296,FONT_SMALL,0,TEXT_MARQUEE,TEXT_SLOT_CONNECTION,&ot);
    Conn_DrawSlotCaseMarks(caps,0,80,0,296,&ot);
    assert(ne==0&&db.primMem.cursor==arena);
    reset();db.primMem.guardEnd=arena;DecalFont_DrawLineBox("St Patricks Day Hub World",136,80,240,FONT_BIG,0,TEXT_MARQUEE,0,&ot);
    assert(ne==0&&envCount==0); /* allocation cannot emit half a clip pair */
    puts("PASS: production width, ellipses at every box width, packet order, clip and restore, fitting alignment, arena exhaustion");
}
'''

# Authored warnings must fit with equal 16 px margins using real font metrics.
for path, pattern in (("ap/ap_hooks_feed.c", r'static char warn[12]\[\] = "([^"]+)"'),
                      ("ap/ap_hooks_custom_content.c", r'"([^"\n]+)"')):
    text = source(path)
    if "custom_content" in path: text = text[text.index("void AP_DrawCustomContentWarning"):]
    for line in re.findall(pattern, text):
        fixture = fixture.replace('    puts("PASS: production width',
            '    assert(DecalFont_GetLineWidthStrlen(' + json.dumps(line) + ',-1,FONT_SMALL)<=480);\n    puts("PASS: production width', 1)

with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-I", str(root / "include"), "-I", str(root), str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
