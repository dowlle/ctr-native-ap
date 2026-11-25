"""Execute production HUD, scroll bar and sky selection with rendering stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def source(path):
    return (root / path).read_text()


def function(path, signature):
    text = source(path)
    start = text.index(signature + "\n{")
    return text[start:text.index("\n}\n", start) + 3]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ap/ap_feed_lines_logic.h"
#include "include/ctr_menu_scrollbar.h"
#include "include/text_box.h"
#define CTR_AP
#include "include/platform/native_config.h"
#define CTR_NATIVE
#define CTR_CUSTOM_TRACKS
#define CTR_CUSTOM_PACKAGES
#include "include/ctr_menu_pages.h"
#define CONFIG_PAGE_WINDOW CTR_MENU_PAGE_VISIBLE_ROWS
#define CTR_SCROLLBAR_W 4
#define CTR_SCROLLBAR_MIN_THUMB 8
#define TRANS_50_DECAL 5
typedef struct { short x,y,w,h; } RECT;
typedef struct { unsigned char r,g,b,code; } Color;
#define FONT_SMALL 0
#define FONT_BIG 1
#define ORANGE 0
#define WHITE 1
#define JUSTIFY_CENTER 2
#define AP_IDX_TROPHY 0
#define INC_TROPHY 1
#define AP_FEED_X 8
#define AP_FEED_BASE_Y 200
#define AP_FEED_RACE_WIDTH 364
#define AP_FEED_LINE_H 12
#define AP_FEED_BOX_WARNING_Y 72
#define AP_FEED_CONTENT_WARNING_Y 48
#define PARKING_LOT 20
#define ADVENTURE_CUP 0x10000000
typedef short s16;
typedef uint32_t u32;
struct DB { struct { unsigned char isbg,r,g,b; } drawEnv; } db[2];
struct GameTracker {
    struct { int numTrophies; } currAdvProfile;
    int gameMode1,gameMode2,levelID;
    struct { int cupID; } cup;
    struct DB db[2];
} game;
static struct { struct GameTracker *gGT; char s_x[4],s_longInt[4]; Color menuRowHighlight_Normal; } state={&game,"x","%d",{1,2,3,0}},*sdata=&state;
static struct { char Options_VolumeSlider_Colors[0x14]; } data={{0,0,0x20,0,0x80,0x80,0xFF,0,0x80,0x80,0xFF,0,0xFF,0xFF,0xFF,0,0,0,0,0}};
static int boxes; static RECT boxRect[4]; static int boxSolid[4]; static Color boxColor[4];
static void CTR_Box_DrawClearBox(const RECT *r,const Color *c,int t,uint32_t *ot) { assert(t==TRANS_50_DECAL&&c==&sdata->menuRowHighlight_Normal); boxRect[boxes]=*r;boxSolid[boxes]=0;boxColor[boxes]=*c;boxes++; }
static void CTR_Box_DrawSolidBox(RECT *r,Color c,uint32_t *ot) { boxRect[boxes]=*r;boxSolid[boxes]=1;boxColor[boxes]=c;boxes++; }
static int active=1,received,hidden,enabled=1,draws,lastY,custom,offline;
static int drawnY[8];
static struct { int box_placement_refused; } ctr_cfg;
static AP_FeedLines ap_feed;
static char lastText[128];
static int ctr_cfg_active(void) { return active; }
static int AP_GateCount(int i) { assert(i==AP_IDX_TROPHY); return received; }
static int AP_HubFeedOn(void) { return enabled; }
static int feedMode = AP_ITEM_FEED_ALL;
static int AP_ItemFeedMode(void) { return feedMode; }
static int AP_CharSwap_HubHudHidden(void) { return hidden; }
static void OtherFX_Play(int fx,int volume) { assert(fx==0x41&&volume==1); }
static int DecalFont_GetLineWidthStrlen(char *s,int n,int font) {
    (void)font; return n < 0 ? (int)strlen(s) * 8 : n * 8;
}
int DecalFont_LineBoxPrefixLen(const char *s,int w,short font) {
    (void)font; return (int)strlen(s) * 8 <= w ? (int)strlen(s) : (w > 24 ? w / 8 - 3 : 0);
}
static void DecalFont_DrawLine(char *s,int x,int y,int font,int flags) { snprintf(lastText,sizeof lastText,"%s",s);if(draws<8)drawnY[draws]=y;draws++;lastY=y; }
static void DecalFont_DrawLineOT(char *s,int x,int y,int font,int flags,uint32_t *ot) { DecalFont_DrawLine(s,x,y,font,flags); }
void DecalFont_DrawLineBox(const char *s,int x,int y,int width,short font,int flags,int mode,int slot,unsigned int *ot) { assert(width==(mode==TEXT_ELLIPSIS?364:480));DecalFont_DrawLine((char *)s,x,y,font,flags); }
void DecalFont_ResetLineBox(int slot) { assert(slot>=TEXT_SLOT_FEED&&slot<TEXT_SLOT_COUNT); }
static int CustomTrack_ServingLoad(int level,int cup,int id) { return custom; }
static int MainRaceTrack_OfflineCustomLoad(void) { return offline; }
'''
fixture += function("game/UI/UI_DrawNum.c", "void UI_DrawNumTrophy(s16 posX, s16 posY)")
fixture += "\nstatic int missingContent;\nstatic int AP_CustomContentWarningVisible(void) {return missingContent;}\n"
fixture += function("ap/ap_hooks_feed.c", "void AP_FeedDiscardUnimportant(void)")
fixture += function("ap/ap_hooks_feed.c", "static const char *AP_FeedRaceText(AP_FeedLine *line, char text[AP_FEED_TEXT_CAP])")
fixture += function("ap/ap_hooks_feed.c", "static void AP_FeedTickAndDraw(int x, int baseY, int playableRace)")
fixture += "\nstatic int ap_feed_hub_pad_y;\n"
fixture += function("ap/ap_hooks_feed.c", "void AP_FeedHubPadTitle(int feedY)")
fixture += function("ap/ap_hooks_feed.c", "void AP_FeedDrawHub(void)")
fixture += function("game/230/MM_ConfigMenu.c", "static void CTR_MenuDrawScrollBar(int x, int y, int h, int total, int visible, int top, uint32_t *ot)")
fixture += function("game/230/MM_ConfigMenu.c", "static void Config_DrawScrollBar(int top, int pageRows, uint32_t *ot)")
text = source("game/MAIN/MainInit.c")
start = text.index("\tu32 clearColor = lev1->clearColorRGBA;")
end = text.index("\n\tif ((gGT->numPlyrCurrGame", start)
fixture += "\nstatic void sky(void) { struct GameTracker *gGT=&game; struct {u32 clearColorRGBA;} lev={0x123456},*lev1=&lev;\n" + text[start:end] + "\n}\n"
fixture += r'''
int main(void) {
    game.currAdvProfile.numTrophies=16;
    for(received=0;received<=255;received++) {
        char want[16];sprintf(want,"%d",received);
        for(game.gameMode2=0;game.gameMode2<2;game.gameMode2++) {
            UI_DrawNumTrophy(10,20);assert(!strcmp(lastText,want));
        }
    }
    active=0;game.gameMode2=0;UI_DrawNumTrophy(10,20);assert(!strcmp(lastText,"16"));
    game.gameMode2=INC_TROPHY;UI_DrawNumTrophy(10,20);assert(!strcmp(lastText,"15"));
    const int anchors[]={0,103,120};
    for(int row=0;row<3;row++)for(active=0;active<2;active++)for(hidden=0;hidden<2;hidden++)for(enabled=0;enabled<2;enabled++) {
        int anchor=anchors[row];
        AP_FeedHubPadTitle(anchor);
        AP_FeedLinesReset(&ap_feed);AP_FeedLinesEnqueue(&ap_feed,"WAITING",WHITE,0);
        draws=0;AP_FeedDrawHub();assert(draws==(active&&!hidden&&enabled));
        assert(ap_feed.qcount==(draws?0:1));
        if(draws) assert(lastY==(anchor?anchor:200));
    }
    active=enabled=1;hidden=0;
    // Leaving the pad restores the old anchor on the next frame.
    AP_FeedHubPadTitle(103);AP_FeedHubPadTitle(0);
    draws=0;AP_FeedDrawHub();assert(lastY==200);
    for(int warning=0;warning<2;warning++)for(int count=0;count<=5;count++) {
        AP_FeedLinesReset(&ap_feed);ctr_cfg.box_placement_refused=warning;
        ap_feed.vcount=count;
        for(int i=0;i<count;i++) { strcpy(ap_feed.vis[i].text,"VISIBLE");ap_feed.vis[i].framesLeft=240; }
        AP_FeedLinesEnqueue(&ap_feed,"WAITING",WHITE,0);
        draws=0;AP_FeedTickAndDraw(8,120,0);
        int limit=warning?3:5;
        assert(draws==(count<limit?count+1:limit));
        assert(ap_feed.qcount==(count<limit?0:1));
        for(int i=0;i<draws;i++) assert(drawnY[i]<=120&&drawnY[i]>=(warning?96:72));
    }
    // Warning capacity is hub-only: races still draw five lines.
    draws=0;AP_FeedTickAndDraw(8,200,1);assert(draws==5&&drawnY[0]==152);
    // A raised custom-pad anchor leaves one row below the two warning lines.
    draws=0;AP_FeedTickAndDraw(8,103,0);assert(draws==1&&drawnY[0]==103&&ap_feed.qcount==1);
    // Missing content alone leaves three rows. The lower box warning wins when both are drawn.
    missingContent=1;ctr_cfg.box_placement_refused=0;
    draws=0;AP_FeedTickAndDraw(8,103,0);assert(draws==3&&drawnY[0]==79&&drawnY[2]==103&&ap_feed.qcount==1);
    ctr_cfg.box_placement_refused=1;
    draws=0;AP_FeedTickAndDraw(8,103,0);assert(draws==1&&drawnY[0]==103&&ap_feed.qcount==1);
    AP_FeedLinesReset(&ap_feed);
    AP_FeedLinesEnqueue(&ap_feed,"FILLER",WHITE,0)->unimportant = 1;
    AP_FeedLinesPromote(&ap_feed);
    AP_FeedLinesEnqueue(&ap_feed,"STOCK",WHITE,0)->unimportant = 1;
    AP_FeedLinesEnqueue(&ap_feed,"UNLOCK",WHITE,0);
    ap_feed.stagger = 0;
    feedMode = AP_ITEM_FEED_IMPORTANT;
    draws=0;AP_FeedTickAndDraw(8,200,0);
    assert(draws == 1 && !strcmp(lastText,"UNLOCK") && !ap_feed.qcount);
    /* The menu applies this discard before the next receipt can evict
       a progression line from an old, full ALL queue. */
    AP_FeedLinesReset(&ap_feed);
    AP_FeedLinesEnqueue(&ap_feed,"PROGRESSION",WHITE,0);
    for (int i = 1; i < AP_FEED_NORMAL_LIMIT; ++i)
        AP_FeedLinesEnqueue(&ap_feed,"FILLER",WHITE,0)->unimportant = 1;
    AP_FeedDiscardUnimportant();
    AP_FeedLinesEnqueue(&ap_feed,"NEW USEFUL",WHITE,0);
    assert(ap_feed.qcount == 2 && !strcmp(ap_feed.queue[ap_feed.qhead].text,"PROGRESSION"));
    feedMode = AP_ITEM_FEED_ALL;
    // A4-36: a slim bar, no text, only when the page scrolls; track and thumb inside the compact window.
    draws=0;
    for(int count=0;count<=CONFIG_PAGE_WINDOW;count++) { boxes=0;Config_DrawScrollBar(0,count,NULL);assert(!boxes); }
    for(int rows=CONFIG_PAGE_WINDOW+1;rows<=24;rows++)for(int top=0;top<=rows-CONFIG_PAGE_WINDOW;top++) {
        boxes=0;Config_DrawScrollBar(top,rows,NULL);assert(boxes==2);
        RECT t=boxRect[0],h=boxRect[1];
        assert(!boxSolid[0]&&boxSolid[1]);
        assert(t.x==0x1E4&&t.y==CTR_MenuOptionRowY(rows,0)-2&&t.w==4&&t.h==CONFIG_PAGE_WINDOW*CTR_MenuOptionRowSpacing(rows)&&h.x==t.x&&h.w==t.w);
        assert(h.y>=t.y&&h.y+h.h<=t.y+t.h&&h.h>=8);
        assert(h.h==(t.h*CONFIG_PAGE_WINDOW/rows>8?t.h*CONFIG_PAGE_WINDOW/rows:8));
        if(top==0) assert(h.y==t.y);
        if(top==rows-CONFIG_PAGE_WINDOW) assert(h.y+h.h==t.y+t.h);
        assert(boxColor[1].r==0xFF&&boxColor[1].g==0xFF&&boxColor[1].b==0xFF);
    }
    assert(!draws); // the bar adds no text
    {   // a selection moving down never moves the thumb up; bigger lists get thinner thumbs
        int lastY=-1;
        for(int top=0;top<=3;top++) { boxes=0;Config_DrawScrollBar(top,15,NULL);assert(boxRect[1].y>lastY);lastY=boxRect[1].y; }
        boxes=0;Config_DrawScrollBar(0,15,NULL);int h15=boxRect[1].h;
        boxes=0;Config_DrawScrollBar(0,24,NULL);assert(boxRect[1].h<h15);
    }
    for(game.levelID=0;game.levelID<32;game.levelID++)for(custom=0;custom<2;custom++)for(offline=0;offline<2;offline++) {
        sky();u32 want=game.levelID==20&&!custom&&!offline?0xff3b04:0x123456;
        assert(*(u32*)&game.db[0].drawEnv.isbg==(want<<8));
        assert(*(u32*)&game.db[1].drawEnv.isbg==(want<<8));
    }
    puts("PASS: Trophy receipts 0..255 and retail animation; feed ownership; scroll bounds; retail-only sky backing");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-fno-strict-aliasing", "-I", str(root), "-I", str(root / "include"), str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

# The title flag follows drawing, including hint/picker suppression, rather
# than mere proximity or a previous frame's nearest-pad state.
logic = function("game/MAIN/MainFrame.c", "void MainFrame_GameLogic(struct GameTracker *gGT, struct GamepadSystem *gGamepads)")
assert logic.index("AP_FeedHubPadTitle(0)") < logic.index("ThTick_RunBucket")
warp = source("game/232/AH_WarpPad.c")
title = warp.index("AP_FeedHubPadTitle(apPadFeedY)")
draws = warp[warp.rindex("sdata->AkuAkuHintState == 0", 0, title):title]
assert "AP_CharSwap_HubHudHidden()" in draws
assert "DecalFont_DrawLine(warppadLNG" in draws
assert "DecalFont_DrawLineBox(warppadLNG" in draws
