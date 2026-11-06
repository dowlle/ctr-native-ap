#!/usr/bin/env python3
"""Run production menu input functions with engine and worker boundaries stubbed."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    # Ignore braces inside comments and strings without changing offsets.
    clean = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                   lambda m: ' ' * len(m[0]), source)
    depth = 1
    end = opening + 1
    while depth:
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    return source[start:end]


common = r'''
#include <assert.h>
#include <string.h>
#include <common.h>
struct sData sdata_static;
struct Data data;
static struct GameTracker gt;
int OtherFX_Play(u32 a, int b) { return 0; }
int LOAD_IsOpen_AdvHub(void) { return 1; }
'''

shop = r'''
#define AP_WSHOP_VISIBLE 9
static struct { int open, buying, hub, cursor, top, msgFrames, marqueeFrame; } ap_wshop;
static struct { int slots; } ctr_wumpa_shop = {3};
static int buys;
int AP_WumpaShopRowShown(void) { return 1; }
int AP_WumpaShopHubOfLevel(int level) { return 0; }
void AP_WShopClose(void) { ap_wshop.open = 0; }
int ap_net_wumpa_pending(void) { return 1; }
int ap_net_wumpa_bought(int slot) { return 0; }
void AP_WShopSay(const char *s) {}
int AP_WumpaShopUsable(void *p) { return 1; }
void AP_WShopBuy(void) { buys++; }
void AP_WShopDraw(void) {}
'''
shop += function('ap/ap_wumpa_shop_ui.c', 'int AP_WumpaShopMenuFrame(void)')
shop += r'''
static void shop_tests(void) {
    sdata->gGT = &gt; sdata->ptrActiveMenu = &data.menuGreenLoadSave;
    const int confirm[] = {BTN_CROSS, BTN_CIRCLE};
    for (int i = 0; i < 2; ++i) {
        ap_wshop.open = 1; ap_wshop.buying = -1;
        sdata->buttonTapPerPlayer[0] = confirm[i];
        int before = buys; assert(AP_WumpaShopMenuFrame());
        assert(buys == before + 1 && ap_wshop.open);
    }
    const int back[] = {BTN_TRIANGLE, BTN_START};
    for (int i = 0; i < 2; ++i) {
        ap_wshop.open = 1; sdata->buttonTapPerPlayer[0] = back[i];
        int before = buys; assert(AP_WumpaShopMenuFrame());
        assert(!ap_wshop.open && buys == before);
    }
    ap_wshop.open = 1; sdata->buttonTapPerPlayer[0] = BTN_SQUARE;
    assert(AP_WumpaShopMenuFrame() && ap_wshop.open);
}
'''

tracker = r'''
#include "ap/ap_pad_activity_logic.h"
#include "ap/ap_tracker_tabs.h"
#define TRACKER_W 1280
#define TRACKER_H 800
#define SDL_BUTTON_LMASK 1
typedef struct { int physical, destination, kind, x,y,cx,cy,cw,ch; } AP_TrackerNode;
static struct {
    int open,hub,tab,focus,pinned,count,acts_open,acts_cursor;
    float mouse_x,mouse_y; unsigned mouse_buttons;
    ap_padact_list acts; AP_TrackerNode nodes[2];
} ap_tracker;
static int ap_tracker_request, launches, launchEnabled = 1;
static struct { struct { int w,h; } disp; } activeDispEnv;
int ctr_cfg_active(void) { return 1; }
unsigned SDL_GetMouseState(float *x,float *y) { *x=0; *y=0; return 0; }
void SDL_ShowCursor(void) {}
void AH_Pause_Destroy(void) {}
void MainFreeze_MenuPtrDefault(struct RectMenu *menu) {}
int AP_TrackerLoadAssets(void) { return 1; }
void AP_TrackerClose(void) { ap_tracker.open=0; ap_tracker.acts_open=0; ap_tracker.pinned=0; }
void AP_TrackerShutdown(void) {}
void AP_TrackerBuildNodes(void) {}
void AP_TrackerDraw(void) {}
void AP_LogLine(const char *s) {}
void AP_TrackerActivitiesBuild(void) {
    ap_tracker.acts.count=1; ap_tracker.acts.reason[0]=AP_PADACT_OPEN;
}
int AP_TrackerLaunchEnabled(void) { return launchEnabled; }
int AP_TrackerLaunchBegin(int h,int p,int d,int a,int m) { launches++; return 1; }
int AP_TrackerRacersOn(void) { return 0; }
int NativeRenderer_DisplayRectToWindow(int x,int y,int w,int h,int *a,int *b,int *c,int *d) { return 0; }
'''
tracker += function('ap/ap_tracker.c', 'int AP_TrackerMenuFrame(void)')
tracker += r'''
static void tracker_tests(void) {
    gt.gameMode1=ADVENTURE_ARENA|PAUSE_1;
    data.menuGreenLoadSave.funcPtr=MainFreeze_MenuPtrDefault;
    for (int b=0; b<2; b++) {
        const int confirms[]={BTN_CROSS,BTN_CIRCLE};
        memset(&ap_tracker,0,sizeof ap_tracker); ap_tracker.open=1; ap_tracker.count=1;
        sdata->buttonTapPerPlayer[0]=confirms[b];
        assert(AP_TrackerMenuFrame() && ap_tracker.open && ap_tracker.acts_open);
        int before=launches;
        assert(AP_TrackerMenuFrame() && launches==before+1 && !ap_tracker.open);
        launchEnabled=0; ap_tracker.open=1;
        assert(AP_TrackerMenuFrame() && ap_tracker.pinned);
        launchEnabled=1;
    }
    const int backs[]={BTN_TRIANGLE,BTN_START};
    for (int b=0; b<2; b++) {
        ap_tracker.open=1; ap_tracker.acts_open=1; ap_tracker.count=1;
        sdata->buttonTapPerPlayer[0]=backs[b];
        sdata->AnyPlayerTap=backs[b]; /* The later retail unpause consumer reads this. */
        assert(AP_TrackerMenuFrame() && ap_tracker.open && !ap_tracker.acts_open);
        assert(!(sdata->AnyPlayerTap & BTN_START));
        sdata->AnyPlayerTap=backs[b];
        assert(AP_TrackerMenuFrame() && !ap_tracker.open);
        assert(!(sdata->AnyPlayerTap & BTN_START));
    }
    ap_tracker.open=1; sdata->buttonTapPerPlayer[0]=BTN_SQUARE;
    assert(AP_TrackerMenuFrame() && ap_tracker.open);
    ap_tracker.tab=0; sdata->buttonTapPerPlayer[0]=BTN_R1;
    assert(AP_TrackerMenuFrame() && ap_tracker.tab==1);
    sdata->buttonTapPerPlayer[0]=BTN_L1;
    assert(AP_TrackerMenuFrame() && ap_tracker.tab==0);
    ap_tracker.open=0; sdata->buttonTapPerPlayer[0]=BTN_START; sdata->AnyPlayerTap=BTN_START;
    assert(!AP_TrackerMenuFrame() && (sdata->AnyPlayerTap & BTN_START)); /* Retail keeps its Start. */
}
'''

chars = r'''
static struct GamepadSystem pads;
static struct { int characterID,next[4]; } ap_cs_tiles[2];
static int ap_cs_cursor, ap_cs_editFocus, ap_cs_panelRow, ap_cs_open, swaps;
unsigned ap_cs_panelRowsAvailable(int character) { return 15; }
int AP_StatSel_NextRow(int row,int step,unsigned available) { return row; }
int AP_StatSel_ValidRow(int row,unsigned available) { return row; }
void ap_cs_adjust(struct GameTracker *gt,int step) {}
int ap_cs_editorAvailable(void) { return 1; }
int ap_cs_isUnlocked(int character) { return 1; }
void ap_cs_setFreeze(struct GameTracker *gt,int freeze) {}
void ap_cs_requestSwap(struct GameTracker *gt,int character) { swaps++; }
'''
chars += function('ap/ap_charswap.c', 'static void ap_cs_input(struct GameTracker *gGT)')
chars += r'''
static void character_tests(void) {
    sdata->gGamepads=&pads; pads.numGamepadsConnected=1;
    ap_cs_tiles[0].characterID=1; data.characterIDs[0]=0;
    const int confirms[]={BTN_CROSS_one,BTN_CIRCLE}, backs[]={BTN_TRIANGLE,BTN_START};
    for (int i=0;i<2;i++) {
        ap_cs_open=1; ap_cs_editFocus=0; pads.gamepad[0].buttonsTapped=confirms[i];
        int before=swaps; ap_cs_input(&gt); assert(swaps==before+1);
        pads.gamepad[0].buttonsTapped=BTN_SQUARE_one; ap_cs_input(&gt);
        assert(ap_cs_editFocus);
        ap_cs_input(&gt); assert(ap_cs_editFocus); /* Square never backs out. */
        pads.gamepad[0].buttonsTapped=backs[i]; ap_cs_input(&gt);
        assert(!ap_cs_editFocus && ap_cs_open);
        ap_cs_input(&gt); assert(!ap_cs_open);
    }
}
'''

link = r'''
#include <platform/native_link_handoff.h>
static NativeLinkHandoff ap_link;
static int ap_link_prompting, ap_link_ready=1, appliedLinks;
static NativeLinkFrameInput linkInput;
int AP_LinkSafe(struct GameTracker *gt) { return 1; }
void AP_LinkPoll(void) {}
int AP_LinkSessionLive(void) { return 1; }
long long AP_LinkNowMono(void) { return 0; }
void AP_LinkApply(const NativeLinkRecord *record) { appliedLinks++; }
void AP_LinkDrawPrompt(const NativeLinkRecord *record) {}
void AP_AppendLog(const char *s) {}
NativeLinkFrameAction NativeLinkHandoff_Frame(NativeLinkHandoff *h,const NativeLinkFrameInput *in,
                                            NativeLinkRecord *taken,NativeLinkRecord *shown) {
    linkInput=*in;
    return in->acceptTapped ? NATIVE_LINK_FRAME_ACCEPT :
           in->declineTapped ? NATIVE_LINK_FRAME_DECLINED : NATIVE_LINK_FRAME_PROMPT;
}
'''
link += function('ap/ap_link.c', 'int AP_LinkMenuFrame(void)')
link += r'''
static void link_tests(void) {
    gt.gameMode1=MAIN_MENU; ap_link.haveOffer=1;
    const int confirms[]={BTN_CROSS,BTN_CIRCLE}, backs[]={BTN_TRIANGLE,BTN_START};
    for (int i=0;i<2;i++) {
        sdata->buttonTapPerPlayer[0]=confirms[i]; int before=appliedLinks;
        assert(AP_LinkMenuFrame() && appliedLinks==before+1 && linkInput.acceptTapped && !linkInput.declineTapped);
        sdata->buttonTapPerPlayer[0]=backs[i]; before=appliedLinks;
        assert(AP_LinkMenuFrame() && appliedLinks==before && !linkInput.acceptTapped && linkInput.declineTapped);
    }
}
'''

picker = r'''
static int s_choiceSelected, s_choiceCount=3, pickerOpen=1, applied;
void Controls_ClosePicker(void) { pickerOpen=0; }
void Controls_ApplyChoice(void) { applied++; }
'''
picker += function('game/230/MM_ControlsConfig.c', 'static void Controls_UpdatePicker(u32 tapped)')
picker += r'''
static void picker_tests(void) {
    const int confirms[]={BTN_CROSS,BTN_CIRCLE}, backs[]={BTN_TRIANGLE,BTN_START};
    for (int i=0;i<2;i++) {
        pickerOpen=1; int before=applied; Controls_UpdatePicker(confirms[i]);
        assert(!pickerOpen && applied==before+1);
        pickerOpen=1; before=applied; Controls_UpdatePicker(backs[i]);
        assert(!pickerOpen && applied==before);
    }
}
int main(void) { shop_tests(); tracker_tests(); picker_tests(); character_tests(); link_tests(); return 0; }
'''

with tempfile.TemporaryDirectory(prefix='ctr-menu-actions-') as tmp:
    src = Path(tmp) / 'actions.c'
    src.write_text(common + shop + tracker + chars + link + picker)
    exe = Path(tmp) / 'actions'
    subprocess.run(['cc', '-m32', '-DCTR_NATIVE', '-DBUILD=926', '-I', str(root / 'include'),
                    '-I', str(root), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('production menu actions: shop, tracker map/activity list, Controls picker, character picker, room links passed')
