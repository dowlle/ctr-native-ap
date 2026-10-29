"""Spoiler Items (#459): execute the production receipt module, the pure gates
and the real schema-18 parser on the host.

Pins every combination of each reveal item absent or received against
spoiler_items on and off, the box colour decision against the seed option, the
player's row and the scout, the Options row states and the early OFF
(2026-10-28 ruling), both pad glow layouts with bits from every glow family, the
neutral marker on a held-back pad (2026-10-28 ruling), replay reset, and absent, Boolean, non-Boolean and
content-plan wire values. Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_spoiler_items.c").read_text()
source = "\n".join(line for line in source.splitlines()
                   if not line.startswith('#include "ap_hooks.h"') and not line.startswith('#include "ap_seedcfg.h"'))
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_box_colour_logic.h"
#include "ap_glow_slots_logic.h"
static struct {int spoiler_items, seed_box_locations;} ctr_cfg;
static int active=1, logs;
static int ctr_cfg_active(void){return active;}
static void AP_LogLine(const char *m){(void)m;logs++;}
SOURCE
// Bits from every glow family the pad lists since PR #443: race tiers and
// tokens, trial pseudo-bits (0x210), Cortex Vortex (0x220), Relic Perfect
// (0x240), item boxes (0x300), letters (0x420), custom letters (0x456) and
// Wumpa (0x460). The gate does not look at them; the layouts do.
static const int families[]={0,1,2,3,4,5,8,0x210,0x211,0x220,0x240,0x300,0x305,0x40d,0x420,0x455,0x456,0x460,0x471};
static int group(int bit){return bit<3?0:bit<0x300?1:2;}
static void pads(void){
 int n=(int)(sizeof families/sizeof families[0]);
 for(int len=0;len<=n;len++)for(int layout=0;layout<2;layout++)for(int phase=0;phase<9;phase++){
  int want[3],got[3],hidden[3];
  AP_GlowSlots_Select(families,len,phase,layout,group,want);
  AP_GlowSlots_Select(families,AP_Spoiler_PadDisplayCount(1,len),phase,layout,group,got);
  AP_GlowSlots_Select(families,AP_Spoiler_PadDisplayCount(0,len),phase,layout,group,hidden);
  assert(!memcmp(want,got,sizeof want));
  assert(hidden[0]==-1&&hidden[1]==-1&&hidden[2]==-1);
 }
 assert(AP_Spoiler_PadDisplayCount(0,-1)==-1 && AP_Spoiler_PadDisplayCount(1,-1)==-1);
 assert(AP_Spoiler_PadDisplayCount(0,0)==0 && AP_Spoiler_PadDisplayCount(0,7)==0 && AP_Spoiler_PadDisplayCount(1,7)==7);
 // 2026-10-28 ruling: a held-back pad with any open location shows one neutral
 // marker; a finished pad, a shown pad, a vanilla pad or a missing marker
 // model show none. Every family length counts, one open box alone included.
 for(int len=-1;len<=n;len++)for(int shown=0;shown<2;shown++)for(int ready=0;ready<2;ready++)
  assert(AP_Spoiler_PadNeutralMarker(shown,len,ready)==(!shown&&len>0&&ready));
}
static void boxes(void){
 static const unsigned flags[]={AP_ITEM_FLAG_PROGRESSION,AP_ITEM_FLAG_USEFUL,AP_ITEM_FLAG_TRAP,0};
 for(int unlocked=0;unlocked<2;unlocked++)for(int seed=0;seed<2;seed++)for(int player=0;player<2;player++)
 for(int scouted=0;scouted<2;scouted++)for(int f=0;f<4;f++){
  int got=AP_BoxColour_Effective(unlocked,seed,player,scouted,flags[f]);
  int before=AP_BoxColour_Pick(AP_BoxColour_Enabled(seed,player),scouted,flags[f]);
  // Unlocked: exactly the pre-#459 colour. Locked: pink, whatever the rest says.
  assert(got==(unlocked?before:AP_BOX_COLOUR_PINK));
 }
 // Options row: seed off always wins; locked only while a loaded seed permits colours.
 assert(AP_BoxColour_RowStateWithItem(1,0,0)==AP_BOX_COLOUR_ROW_SEED_OFF);
 assert(AP_BoxColour_RowStateWithItem(1,0,1)==AP_BOX_COLOUR_ROW_SEED_OFF);
 assert(AP_BoxColour_RowStateWithItem(1,1,0)==AP_BOX_COLOUR_ROW_ITEM_LOCKED);
 assert(AP_BoxColour_RowStateWithItem(1,1,1)==AP_BOX_COLOUR_ROW_PLAYER);
 assert(AP_BoxColour_RowStateWithItem(0,0,0)==AP_BOX_COLOUR_ROW_PLAYER);
 assert(AP_BoxColour_RowStateWithItem(0,1,1)==AP_BOX_COLOUR_ROW_PLAYER);
 // 2026-10-28 ruling: OFF set while locked stays off after AP Box Colours arrives;
 // ON set while locked shows colours after it. Locked boxes are pink either way.
 for(int f=0;f<4;f++){
  assert(AP_BoxColour_Effective(0,1,0,1,flags[f])==AP_BOX_COLOUR_PINK);
  assert(AP_BoxColour_Effective(1,1,0,1,flags[f])==AP_BOX_COLOUR_PINK);
  assert(AP_BoxColour_Effective(1,1,1,1,flags[f])==AP_BoxColour_Pick(1,1,flags[f]));
 }
}
static void receipts(void){
 // Four combinations of each item against its toggle.
 for(int on=0;on<2;on++)for(int preview=0;preview<2;preview++)for(int box=0;box<2;box++){
  ctr_cfg.spoiler_items=on;ctr_cfg.seed_box_locations=1;active=1;AP_SpoilerItemsReset();
  if(preview)AP_SpoilerItemsReceive(AP_WARP_PAD_PREVIEW_ITEM_INDEX);
  if(box)AP_SpoilerItemsReceive(AP_BOX_COLOURS_ITEM_INDEX);
  assert(AP_SpoilerPadPreviewShown()==(!on||preview));
  assert(AP_SpoilerBoxColoursUnlocked()==(!on||box));
 }
 // Each item reveals only its own display.
 ctr_cfg.spoiler_items=1;AP_SpoilerItemsReset();AP_SpoilerItemsReceive(AP_WARP_PAD_PREVIEW_ITEM_INDEX);
 assert(AP_SpoilerPadPreviewShown()&&!AP_SpoilerBoxColoursUnlocked());
 AP_SpoilerItemsReset();AP_SpoilerItemsReceive(AP_BOX_COLOURS_ITEM_INDEX);
 assert(!AP_SpoilerPadPreviewShown()&&AP_SpoilerBoxColoursUnlocked());
 // Other items reveal nothing; repeats log once.
 AP_SpoilerItemsReset();logs=0;
 for(int i=0;i<300;i++)if(i!=AP_WARP_PAD_PREVIEW_ITEM_INDEX&&i!=AP_BOX_COLOURS_ITEM_INDEX)AP_SpoilerItemsReceive(i);
 assert(!AP_SpoilerPadPreviewShown()&&!AP_SpoilerBoxColoursUnlocked()&&logs==0);
 AP_SpoilerItemsReceive(AP_WARP_PAD_PREVIEW_ITEM_INDEX);AP_SpoilerItemsReceive(AP_WARP_PAD_PREVIEW_ITEM_INDEX);
 assert(logs==1);
 // A fresh connect clears both; the replay brings them back.
 AP_SpoilerItemsReceive(AP_BOX_COLOURS_ITEM_INDEX);AP_SpoilerItemsReset();
 assert(!AP_SpoilerPadPreviewShown()&&!AP_SpoilerBoxColoursUnlocked());
 AP_SpoilerItemsReceive(AP_BOX_COLOURS_ITEM_INDEX);AP_SpoilerItemsReceive(AP_WARP_PAD_PREVIEW_ITEM_INDEX);
 assert(AP_SpoilerPadPreviewShown()&&AP_SpoilerBoxColoursUnlocked());
 // A seed without AP boxes creates no AP Box Colours: nothing to wait for.
 ctr_cfg.spoiler_items=1;ctr_cfg.seed_box_locations=0;AP_SpoilerItemsReset();
 assert(AP_SpoilerBoxColoursUnlocked()&&!AP_SpoilerPadPreviewShown());
 ctr_cfg.seed_box_locations=1;
 assert(!AP_SpoilerBoxColoursUnlocked());
 // No active seed: nothing is held back.
 AP_SpoilerItemsReset();active=0;
 assert(AP_SpoilerPadPreviewShown()&&AP_SpoilerBoxColoursUnlocked());
}
int main(void){
 pads();boxes();receipts();
 puts("PASS: reveal receipts, both pad layouts across every glow family, box colours and the Options row");
}
'''.replace("SOURCE", source)
parser = r'''
#include <cassert>
#include <fstream>
#include "ap_seedcfg.h"
#include "ap_content_plan.h"
extern "C" void AP_LogLine(const char *) {}
using J = nlohmann::json;
static J legacy(const J &value){
 J j={{"ctr_options",{{"schema_version",18}}}};
 if(!value.is_discarded())j["ctr_options"]["spoiler_items"]=value;
 return j;
}
int main(int argc,char **argv){
 ap_seedcfg_parse_json(legacy(true));assert(ctr_cfg.spoiler_items==1&&ctr_cfg.seed_box_locations==0);
 {J b=legacy(true);b["ctr_options"]["box_locations"]=true;ap_seedcfg_parse_json(b);assert(ctr_cfg.seed_box_locations==1);
  ap_seedcfg_parse_json(legacy(true));assert(ctr_cfg.seed_box_locations==0);}
 ap_seedcfg_parse_json(legacy(false));assert(ctr_cfg.spoiler_items==0);
 ap_seedcfg_parse_json(legacy(1));assert(ctr_cfg.spoiler_items==1);
 ap_seedcfg_parse_json(legacy(0));assert(ctr_cfg.spoiler_items==0);
 ap_seedcfg_parse_json(legacy("yes"));assert(ctr_cfg.spoiler_items==0);
 ap_seedcfg_parse_json(legacy(nullptr));assert(ctr_cfg.spoiler_items==0);
 ap_seedcfg_parse_json(legacy(true));
 ap_seedcfg_parse_json(legacy(J(J::value_t::discarded)));assert(ctr_cfg.spoiler_items==0);
 ap_seedcfg_parse_json(legacy(true));
 ap_seedcfg_parse_json(J::object());assert(ctr_cfg.spoiler_items==0);
 // An apworld-emitted content-plan room with both reveal items and coloured boxes.
 assert(argc==2);
 std::ifstream input(argv[1]);J plan;input>>plan;
 assert(plan["content_plan"]["item_packs"]["warp_pad_preview"]==true && plan["content_plan"]["item_packs"]["ap_box_colours"]==true);
 ap_seedcfg_parse_json(plan);
 assert(ctr_cfg_active()&&ap_content_plan_present()&&ctr_cfg.spoiler_items==1&&ctr_cfg.color_boxes_by_item==1);
 assert(ctr_cfg.seed_box_locations==1);
 assert(ctr_cfg.reserve_top_up==(plan["ctr_options"]["reserve_top_up"]==true));
 assert(!ap_content_plan_item(35010204)&&!ap_content_plan_item(35010205));
 J p=plan;p["ctr_options"]["spoiler_items"]=false;
 ap_seedcfg_parse_json(p);assert(ctr_cfg_active()&&ctr_cfg.spoiler_items==0);
 p=plan;p["ctr_options"].erase("spoiler_items");
 ap_seedcfg_parse_json(p);assert(ctr_cfg_active()&&ctr_cfg.spoiler_items==0);
 p=plan;p["ctr_options"].erase("reserve_top_up");
 ap_seedcfg_parse_json(p);assert(ctr_cfg_active()&&ctr_cfg.spoiler_items==1&&ctr_cfg.reserve_top_up==0);
 // A non-Boolean value stays in the closed envelope and is refused.
 p=plan;p["ctr_options"]["spoiler_items"]=1;
 ap_seedcfg_parse_json(p);assert(!ctr_cfg_active()&&ctr_cfg.spoiler_items==0);
 // The pack record: AP Box Colours needs Warp Pad Preview, grants are true, keys are known.
 auto drop_row=[](J &j,long long code){
  J &rows=j["content_plan"]["items"]["rows"];
  for(size_t i=0;i<rows.size();++i)if(rows[i]["item"]==code){rows.erase(rows.begin()+i);break;}
 };
 p=plan;p["content_plan"]["item_packs"].erase("warp_pad_preview");drop_row(p,35010204);
 ap_seedcfg_parse_json(p);assert(!ctr_cfg_active());
 p=plan;p["content_plan"]["item_packs"].erase("ap_box_colours");
 ap_seedcfg_parse_json(p);assert(!ctr_cfg_active());
 // Without AP Box Colours its slot goes back to filler.
 p=plan;p["content_plan"]["item_packs"].erase("ap_box_colours");drop_row(p,35010205);
 p["content_plan"]["items"]["filler_count"]=p["content_plan"]["items"]["filler_count"].get<int>()+1;
 ap_seedcfg_parse_json(p);assert(ctr_cfg_active()&&ctr_cfg.spoiler_items==1);
 p=plan;p["content_plan"]["item_packs"]["warp_pad_preview"]=1;
 ap_seedcfg_parse_json(p);assert(!ctr_cfg_active());
 p=plan;p["content_plan"]["item_packs"]["spoiler_extra"]=true;
 ap_seedcfg_parse_json(p);assert(!ctr_cfg_active());
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.spoiler_items==1);
 ap_seedcfg_parse_json(legacy(J(J::value_t::discarded)));assert(ctr_cfg.spoiler_items==0);
 std::puts("PASS: legacy and content-plan spoiler_items parse");
}
'''
# The production call sites route through the gates above. The pad enumerator
# itself is engine code, unchanged by #459 and pinned by test-pad-glow-items.c;
# these checks pin that its count passes the gate before the layout picks slots,
# that box colour asks the unlock first, and that both receipts are dispatched
# and reset on a fresh connect.
warp = (root / "game/232/AH_WarpPad.c").read_text()
enum_at = warp.index("apUncN = AP_PadUncollectedDisplayBits(")
gate_at = warp.index("apUncN = AP_Spoiler_PadDisplayCount(AP_SpoilerPadPreviewShown(), apUncN);")
slots_at = warp.index("AP_PadGlowSlots(apUncBits, apUncN,")
assert enum_at < gate_at < slots_at, "pad glow gate must sit between enumeration and slot layout"
assert warp.count("AP_PadUncollectedDisplayBits(") == 1 and warp.count("AP_PadGlowSlots(") == 1
boxes = (root / "ap/ap_boxes.c").read_text()
colour_for = boxes[boxes.index("static int AP_BoxesColourFor("):]
colour_for = colour_for[:colour_for.index("\n}\n")]
assert "AP_SpoilerBoxColoursUnlocked()" in colour_for and "AP_BoxColour_Effective(unlocked," in colour_for
assert "AP_BoxColour_RowStateWithItem(" in boxes and boxes.count("AP_BoxesColourFor(") >= 2
net = (root / "ap/ap_hooks_net.c").read_text()
assert "AP_SpoilerItemsReset();" in net
assert "idx == AP_WARP_PAD_PREVIEW_ITEM_INDEX || idx == AP_BOX_COLOURS_ITEM_INDEX" in net
assert "AP_SpoilerItemsReceive((int)idx);" in net
assert net.index("AP_SpoilerItemsReset();") < net.index("AP_SpoilerItemsReceive((int)idx);")
# 2026-10-28 ruling: the neutral marker goes to slot 0 only, ahead of the hide, and
# the reward swap is skipped for it; the helper always wears the uniform colour.
assert "apNeutral = AP_Spoiler_PadNeutralMarker(AP_SpoilerPadPreviewShown(), apUncN," in warp
assert enum_at < warp.index("apNeutral = AP_Spoiler_PadNeutralMarker(") < gate_at
assert "if (apSlotBit[i] < 0 && !(apNeutral && i == 0 && AP_WarpPadShowNeutralMarker(apPrize)))" in warp
assert "if (apSlotBit[i] >= 0)\n\t\t\t\t{\n\t\t\t\t\tint apModel = AP_WarpPadRewardModel(apSlotBit[i]);" in warp
display = (root / "ap/ap_hooks_reward_display.c").read_text()
neutral = display[display.index("int AP_WarpPadShowNeutralMarker("):]
neutral = neutral[:neutral.index("\n}\n")]
assert "prize->colorRGBA = AP_TINT_UNIFORM;" in neutral and "modelPtr[STATIC_AP]" in neutral
assert "AP_WarpPadNeutralMarkerReady()" in neutral and "AP_WarpPadRewardTint" not in neutral
# 2026-10-28 ruling: the item lock reads ON (LOCKED) or OFF and takes input; only
# the seed lock blocks the toggle and the left/right step.
menu = (root / "game/230/MM_ConfigMenu.c").read_text()
assert "AP_BOX_COLOUR_ROW_ITEM_LOCKED" in menu and '"ON (LOCKED)"' in menu and '"LOCKED (ITEM)"' not in menu
seed_lock = menu[menu.index("static int Config_RowLockedBySeed("):]
seed_lock = seed_lock[:seed_lock.index("\n}\n")]
assert "AP_BOX_COLOUR_ROW_SEED_OFF" in seed_lock and "ITEM_LOCKED" not in seed_lock
assert "Config_RowLockedByItem(e)" not in menu[menu.index("static void MM_MenuProc_Config(struct RectMenu *menu)\n{"):]
for line in ("Pink until AP Box Colours.", "Set OFF to keep them pink."):
    assert len(line) <= 33 and line in menu

# The neutral marker helper itself, executed on the host from its production
# text: it turns any stale slot (ghost, specular, tinted relic, fruit) into the
# plain uniform marker, and leaves the slot alone when the model is not parked.
display_src = (root / "ap/ap_hooks_reward_display.c").read_text()
def grab(start):
    body = display_src[display_src.index(start):]
    return body[:body.index("\n}\n") + 3]
pack = next(l for l in display_src.splitlines() if l.startswith("#define AP_PACK_TINT("))
uniform = next(l for l in display_src.splitlines() if l.startswith("#define AP_TINT_UNIFORM"))
marker = r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned int u32;
#include "ap_wumpa_residency_logic.h"
enum {DRAW_TRANSPARENT=0x10000, USE_SPECULAR_LIGHT=0x20000, GHOST_DRAW_TRANSPARENT=0x60000, HIDE_MODEL=0x80};
struct Model{int id;};
struct Instance{struct Model *model; unsigned int flags; unsigned int colorRGBA; int alphaScale;};
struct GameTracker{struct Model *modelPtr[0xe3];};
static struct GameTracker gt; static struct {struct GameTracker *gGT;} sd={&gt}, *sdata=&sd;
static int registered;
int AP_MarkerModel_IsRegistered(void){return registered;}
@PACK@
@UNIFORM@
@READY@
@SHOW@
int main(void){
 static struct Model ap={STATIC_AP}, fruit={AP_MODEL_WUMPA}, relic={0x61};
 unsigned int stale[]={0,DRAW_TRANSPARENT|USE_SPECULAR_LIGHT,GHOST_DRAW_TRANSPARENT,
                       USE_SPECULAR_LIGHT|AP_WUMPA_ANIM_LOOP|AP_WUMPA_ANIM_STOP_AT_END};
 struct Model *from[]={&relic,&fruit,&ap,0};
 for(int f=0;f<4;f++)for(int m=0;m<4;m++){
  struct Instance in={from[m],stale[f]|HIDE_MODEL,0x20a5ff0,0xa00};
  registered=0;gt.modelPtr[STATIC_AP]=&ap;
  assert(!AP_WarpPadShowNeutralMarker(&in)&&in.model==from[m]&&in.colorRGBA==0x20a5ff0);
  registered=1;gt.modelPtr[STATIC_AP]=0;
  assert(!AP_WarpPadNeutralMarkerReady()&&!AP_WarpPadShowNeutralMarker(&in));
  gt.modelPtr[STATIC_AP]=&ap;
  assert(AP_WarpPadShowNeutralMarker(&in));
  assert(in.model==&ap&&in.colorRGBA==AP_TINT_UNIFORM&&in.alphaScale==AP_MARKER_TINT_STRENGTH);
  assert(!(in.flags&(DRAW_TRANSPARENT|USE_SPECULAR_LIGHT|GHOST_DRAW_TRANSPARENT)));
  // Out of the fruit the animation bits go; HIDE_MODEL is the glow loop's to clear.
  if(from[m]==&fruit)assert(!(in.flags&(AP_WUMPA_ANIM_LOOP|AP_WUMPA_ANIM_STOP_AT_END)));
  assert(in.flags&HIDE_MODEL);
 }
 assert(!AP_WarpPadShowNeutralMarker(0));
 puts("PASS: neutral marker helper from any stale slot, and the unparked model");
}
'''.replace("@PACK@", pack).replace("@UNIFORM@", uniform).replace(
    "@READY@", grab("int AP_WarpPadNeutralMarkerReady(void)")).replace(
    "@SHOW@", grab("int AP_WarpPadShowNeutralMarker(struct Instance *prize)"))
# A neutral slot shows: the vanilla visible branch clears HIDE_MODEL (0x80)
# after the AP block, for the marker as for every reward.
assert "instArr[WPIS_OPEN_PRIZE1 + i]->flags &= ~(0x80);" in warp
# Both input paths stop only on the seed lock, so the item-locked row toggles.
proc = menu[menu.index("static void MM_MenuProc_Config(struct RectMenu *menu)\n{"):]
assert "else if (Config_RowLockedBySeed(e))\n\t\t\t\t; // the seed has item box colours off" in proc
assert "if (e != NULL && e->type == CFG_BOOL && !Config_RowLockedBySeed(e))" in proc

with tempfile.TemporaryDirectory(prefix="ctr-spoiler-items-") as tmp:
    src, exe = Path(tmp)/"test.c", Path(tmp)/"test"
    src.write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-fsanitize=undefined", "-Wall", "-Wextra",
                    "-I", str(root/"ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    src = Path(tmp)/"marker.c"
    src.write_text(marker)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-fsanitize=undefined", "-Wall", "-Wextra",
                    "-I", str(root/"ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    src = Path(tmp)/"parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root/"ap"),
                    "-I", str(root/"ap/vendor/json/include"), str(src), str(root/"ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(root/"tools/fixtures/content-plan/spoiler-items.json")], check=True)
