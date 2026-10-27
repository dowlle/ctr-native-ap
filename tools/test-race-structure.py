"""Adventure structure ctr_first and the race type selector (#208): execute the
pure rules, the race-loss win set and the real schema-18 parser on the host,
and pin the production call sites.

Pins: which rows each structure admits, which rows are productive (including
"a plain Trophy Race never earns the Trophy under ctr_first"), the cursor, the
always / productive decision, grey replay rows, the CTR -> Trophy award pairs,
the CTR win's win set carrying its paired Trophy, the parser's classic,
ctr_first, refused and absent cases, and that the pad, result and menu code
call these gates. Run from the repository root.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

logic = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ap_race_structure_logic.h"
#include "ap_win_logic.h"

enum { T = AP_RS_TROPHY, C = AP_RS_CTR, R = AP_RS_RELIC };

static void admit(void){
 int a[3];
 for(int s=0;s<2;s++)for(int tc=0;tc<2;tc++)for(int s2=0;s2<2;s2++){
  AP_RaceStructureAdmit(s,tc,s2,a);
  assert(a[T]==1);
  assert(a[R]==(tc&&s2));
  assert(a[C]==(s==AP_RS_STRUCTURE_CTR_FIRST?1:(tc&&s2)));
 }
}
static void productive(void){
 int p[3];
 for(int s=0;s<2;s++)for(int tu=0;tu<2;tu++)for(int pl=0;pl<2;pl++)for(int cs=0;cs<2;cs++)for(int rs=0;rs<2;rs++){
  AP_RaceStructureProductive(s,tu,pl,cs,rs,p);
  if(s==AP_RS_STRUCTURE_CTR_FIRST){
   // The Trophy location is the CTR row's to earn; the Trophy row keeps only plain-race checks.
   assert(p[T]==(pl>0));
   assert(p[C]==(cs||tu));
  }else{
   assert(p[T]==(tu||pl>0));
   assert(p[C]==cs);
  }
  assert(p[R]==rs);
 }
}
static ap_rs_rows build(int s,int tc,int s2,int tu,int pl,int cs,int rs,int ctrPresent){
 ap_rs_facts f; int a[3]; ap_rs_rows r;
 f.present[T]=1;f.present[C]=ctrPresent;f.present[R]=1;
 AP_RaceStructureAdmit(s,tc,s2,a);
 AP_RaceStructureProductive(s,tu,pl,cs,rs,f.productive);
 AP_RaceSelectorBuild(&f,a,&r);
 return r;
}
static void rows(void){
 // Classic first visit: one Trophy row, productive.
 ap_rs_rows r=build(0,0,1,1,0,1,1,1);
 assert(r.count==1&&r.mode[0]==T&&r.productive[0]&&r.cursor==0);
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_ALWAYS)==AP_RS_OPEN_MENU);
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_PRODUCTIVE)==T);
 // ctr_first first visit: grey Trophy row, CTR productive, no Relic; cursor on CTR.
 r=build(1,0,1,1,0,1,1,1);
 assert(r.count==2&&r.mode[0]==T&&r.mode[1]==C&&!r.productive[0]&&r.productive[1]&&r.cursor==1);
 assert(AP_RaceSelectorReplayMask(&r)==1u);
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_PRODUCTIVE)==C);
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_ALWAYS)==AP_RS_OPEN_MENU);
 // ctr_first with boxes standing: Trophy row productive too, cursor on Trophy.
 r=build(1,0,1,1,1,1,1,1);
 assert(r.productive[0]&&r.productive[1]&&r.cursor==0);
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_PRODUCTIVE)==AP_RS_OPEN_MENU);
 // ctr_first, Trophy earned, stage 2 unmet: no Relic row.
 r=build(1,1,0,0,0,0,1,1);
 assert(r.count==2&&r.mode[1]==C&&!r.productive[0]&&!r.productive[1]);
 // Classic tier 2 with both sides open: three rows, cursor on CTR.
 r=build(0,1,1,0,0,1,1,1);
 assert(r.count==3&&r.cursor==1&&r.productiveCount==2&&AP_RaceSelectorReplayMask(&r)==1u);
 // Done pad: every row grey, all selectable, menu in both modes, cursor 0.
 r=build(0,1,1,0,0,0,0,1);
 assert(r.count==3&&r.productiveCount==0&&r.cursor==0&&AP_RaceSelectorReplayMask(&r)==7u);
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_PRODUCTIVE)==AP_RS_OPEN_MENU);
 // A trial track without a servable CTR Challenge offers no CTR row.
 r=build(0,1,1,0,0,1,1,0);
 assert(r.count==2&&r.mode[0]==T&&r.mode[1]==R);
 // No rows (refused structure): no route.
 r.count=0;r.productiveCount=0;
 assert(AP_RaceSelectorDecide(&r,AP_RS_SELECTOR_ALWAYS)==AP_RS_NO_ROUTE);
}
static void awards(void){
 ap_rs_awards a; a.count=2;
 a.ctr[0]=35000076;a.trophy[0]=35000006;a.ctr[1]=35026004;a.trophy[1]=35026000;
 assert(!AP_RaceStructureAwardsProblem(&a));
 assert(AP_RaceStructurePairedTrophy(&a,35000076)==35000006);
 assert(AP_RaceStructurePairedTrophy(&a,35026004)==35026000);
 assert(AP_RaceStructurePairedTrophy(&a,35000006)==-1);
 assert(AP_RaceStructurePairedTrophy(0,35000076)==-1);
 assert(AP_RaceStructureTrophyIsStage1Ctr(&a,35000006)&&!AP_RaceStructureTrophyIsStage1Ctr(&a,35000076));
 a.trophy[1]=35000006;assert(AP_RaceStructureAwardsProblem(&a));
 a.trophy[1]=35000076;assert(AP_RaceStructureAwardsProblem(&a));
 a.trophy[1]=0;assert(AP_RaceStructureAwardsProblem(&a));
 a.count=0;assert(AP_RaceStructureAwardsProblem(&a));
 a.count=AP_RS_AWARD_MAX+1;assert(AP_RaceStructureAwardsProblem(&a));
}
static long code(int bit){return 35000000+bit;}
static void winset(void){
 ap_rs_awards a; AP_WinRaceFacts f; long out[AP_WS_WINSET_MAX]; int mode, n;
 a.count=1;a.ctr[0]=code(AP_WS_BIT_CTR_TOKEN+3);a.trophy[0]=code(AP_WS_BIT_TROPHY+3);
 memset(&f,0,sizeof f);f.adventure=1;f.levelID=3;f.trialTrophy=f.trialCtr=-1;
 for(int i=0;i<5;i++)f.cortexCodes[i]=-1;f.customTrophy=f.customCtr=-1;
 // Classic CTR race: the CTR check alone.
 f.token=1;n=AP_WinSetResolve(&f,code,out,&mode);
 assert(n==1&&out[0]==code(AP_WS_BIT_CTR_TOKEN+3)&&mode==AP_WS_MODE_CTR);
 // ctr_first CTR race: CTR and its Trophy.
 f.ctrFirstAwards=&a;n=AP_WinSetResolve(&f,code,out,&mode);
 assert(n==2&&out[1]==code(AP_WS_BIT_TROPHY+3));
 // A Trophy race under ctr_first keeps its own set (classic stakes).
 f.token=0;n=AP_WinSetResolve(&f,code,out,&mode);
 assert(n==1&&out[0]==code(AP_WS_BIT_TROPHY+3)&&mode==AP_WS_MODE_TROPHY);
 // Trial and Cortex Vortex CTR races pair through their direct codes.
 a.count=2;a.ctr[1]=35026004;a.trophy[1]=35026000;
 f.token=1;f.cortex=1;f.cortexCodes[0]=35026000;f.cortexCodes[4]=35026004;
 n=AP_WinSetResolve(&f,code,out,&mode);assert(n==2&&out[0]==35026004&&out[1]==35026000);
 // A track not in the pairs: CTR alone.
 f.cortex=0;f.levelID=4;n=AP_WinSetResolve(&f,code,out,&mode);assert(n==1);
 // Relic races never gain a Trophy.
 f.levelID=3;f.relic=1;n=AP_WinSetResolve(&f,code,out,&mode);assert(n==3&&mode==AP_WS_MODE_RELIC);
}
int main(void){
 admit();productive();rows();awards();winset();
 puts("PASS: race structure rows, decisions, award pairs and win sets");
 return 0;
}
'''

parser = r'''
#include <cassert>
#include <cstdio>
#include "ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
using J = nlohmann::json;
static J room(const char *structure, const char *selector){
 J j={{"ctr_options",{{"schema_version",18}}}};
 if(structure)j["ctr_options"]["adventure_structure"]=structure;
 if(selector)j["ctr_options"]["race_type_selector"]=selector;
 return j;
}
static J pairs(){return J::array({J::array({35000076,35000006}),J::array({35000077,35000007})});}
int main(){
 // Absent keys: classic and the 0.3.0 always menu.
 ap_seedcfg_parse_json(room(nullptr,nullptr));
 assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_CLASSIC&&ctr_cfg.race_type_selector==AP_RS_SELECTOR_ALWAYS);
 ap_seedcfg_parse_json(room("classic","productive"));
 assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_CLASSIC&&ctr_cfg.race_type_selector==AP_RS_SELECTOR_PRODUCTIVE);
 ap_seedcfg_parse_json(room("classic","always"));assert(ctr_cfg.race_type_selector==AP_RS_SELECTOR_ALWAYS);
 ap_seedcfg_parse_json(room("classic","sometimes"));assert(ctr_cfg.race_type_selector==AP_RS_SELECTOR_ALWAYS);
 // ctr_first with a valid block.
 J j=room("ctr_first","always");j["race_structure"]={{"version",1},{"stage1_awards",pairs()}};
 ap_seedcfg_parse_json(j);
 assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_CTR_FIRST&&ctr_cfg.stage1_awards.count==2);
 assert(AP_RaceStructurePairedTrophy(&ctr_cfg.stage1_awards,35000077)==35000007);
 // A reparse of a classic room forgets the pairs.
 ap_seedcfg_parse_json(room("classic",nullptr));
 assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_CLASSIC&&ctr_cfg.stage1_awards.count==0);
 // Refusals: missing block, bad version, bad rows, repeats, unknown value, block on classic.
 ap_seedcfg_parse_json(room("ctr_first",nullptr));assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 j=room("ctr_first",nullptr);j["race_structure"]={{"version",2},{"stage1_awards",pairs()}};
 ap_seedcfg_parse_json(j);assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 j["race_structure"]={{"version",1},{"stage1_awards",J::array({J::array({1})})}};
 ap_seedcfg_parse_json(j);assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 j["race_structure"]={{"version",1},{"stage1_awards",J::array({J::array({5,6}),J::array({7,6})})}};
 ap_seedcfg_parse_json(j);assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 j["race_structure"]={{"version",1},{"stage1_awards",J::array()}};
 ap_seedcfg_parse_json(j);assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 ap_seedcfg_parse_json(room("trophy_free",nullptr));assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 j=room("classic",nullptr);j["race_structure"]={{"version",1},{"stage1_awards",pairs()}};
 ap_seedcfg_parse_json(j);assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_REFUSED);
 // A refused room followed by a classic one is classic again.
 ap_seedcfg_parse_json(room(nullptr,nullptr));assert(ctr_cfg.adventure_structure==AP_RS_STRUCTURE_CLASSIC);
 std::puts("PASS: adventure_structure, race_type_selector and race_structure parse");
 return 0;
}
'''

# Production call sites.
warp = (root / "game/232/AH_WarpPad.c").read_text()
selector_at = warp.index("if (AP_PadUsesSelector(levelID))")
cortex_at = warp.index("if (levelID == AP_CORTEX_DEST)\n\t{\n\t\tint cvTokenLeft")
racer_at = warp.index("if (!ctr_cfg_racer_lock_met(physLevelID))\n\t\tgoto WarpPad_AnimateOpen;")
assert racer_at < selector_at < cortex_at, "the selector runs after the racer lock and before the older routes"
block = warp[selector_at:cortex_at]
for needle in ("AP_PadSelectorRows(physLevelID, levelID, &apRsRows);",
               "AP_RaceSelectorDecide(&apRsRows, ctr_cfg.race_type_selector);",
               "AP_CortexTrackEntryReady(", "goto WarpPad_RefuseEntry;",
               "goto WarpPad_RequestLoad;", "RECTMENU_APSetReplayRows(0, 0);"):
    assert needle in block, needle
assert block.index("AP_CortexTrackEntryReady(") < block.index("AH_WarpPad_WarpCapture(gGT);")
assert "RECTMENU_APSetReplayRows(&apRaceTypeMenu, AP_RaceSelectorReplayMask(rows));" in warp
assert warp.count("!AP_PadReplayable(") == 2, "Done look and Done lock both honour replay"
menu = (root / "game/RECTMENU.c").read_text()
draw = menu[menu.index("textFlags = uVar8;"):]
assert draw.index("RECTMENU_APReplayRow(menu, sVar6)") < draw.index("DecalFont_DrawLine(titleString")
activate = menu[menu.index("// unlocked row"):]
assert "RECTMENU_APReplayRow" not in activate[:400], "replay rows stay selectable"
result = (root / "game/222.c").read_text()
trophy_at = result.index("rewardBit = gGT->levelID + ADV_REWARD_FIRST_TROPHY;")
assert "AP_RaceStructureSuppressTrophyBit(rewardBit)" in result[trophy_at:trophy_at + 2500]
assert "AP_RaceStructureTrophyRaceWon(gGT->levelID);" in result
race = (root / "ap/ap_hooks_race.c").read_text()
trial = race[race.index("void AP_NotifyTrialTrackRace("):]
trial = trial[:trial.index("\n}\n")]
assert "AP_RaceStructureSuppressTrophy(code)" in trial and "AP_RaceStructureCtrWon(code, podium, -1);" in trial
cortex = race[race.index("void AP_NotifyCortexTrackRace("):]
cortex = cortex[:cortex.index("\n}\n")]
assert "AP_RaceStructureSuppressTrophy(code)" in cortex and "AP_RaceStructureCtrWon(" in cortex
pads = (root / "ap/ap_hooks_pads.c").read_text()
state = pads[pads.index("int AP_PadState(int physLevelID, int destLevelID)"):]
state = state[:state.index("\n}\n")]
assert "AP_DestCtrSideLeft(destLevelID)" in state
wl = (root / "ap/ap_win_logic.c").read_text()
assert "f->ctrFirstAwards = AP_RaceStructureCtrFirst() ? &ctr_cfg.stage1_awards : 0;" in wl

with tempfile.TemporaryDirectory(prefix="ctr-race-structure-") as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    src.write_text(logic)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-fsanitize=undefined", "-Wall", "-Wextra",
                    "-I", str(root / "ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    src = Path(tmp) / "parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root / "ap"),
                    "-I", str(root / "ap/vendor/json/include"), str(src), str(root / "ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: race structure production call sites")
