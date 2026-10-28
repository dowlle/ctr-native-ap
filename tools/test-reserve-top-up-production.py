"""Execute the production top-up module and real schema parser with host seams."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_reserve_top_up.c").read_text()
source = "\n".join(line for line in source.splitlines()
                   if line not in ('#include <common.h>', '#include "ap_net.h"', '#include "ap_seedcfg.h"'))
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include "ap_fxmarker_logic.h"
#define START_OF_RACE 1
#define END_OF_RACE 2
#define MAIN_MENU 4
#define GAME_CUTSCENE 8
#define PAUSE_ALL 16
#define ACTION_BOT 32
#define AP_NET_FX_RESERVE_SPENT 2
struct Driver { short reserves; int actionsFlagSet, fireCap; };
struct GameTracker { int gameMode1, trafficLightsTimer; struct Driver *drivers[2]; };
static struct {int reserve_top_up;} ctr_cfg;
static int connected=1, active=1, onTrack=1, state=AP_FXM_STORE_ABSENT, writes;
static long long stored;
static unsigned rev;
static int ap_net_is_connected(void){return connected;}
static int ctr_cfg_active(void){return active;}
static int LOAD_IsOpen_RacingOrBattle(void){return onTrack;}
static int ap_net_fx_store(int key,long long *v,unsigned *r){
 assert(key==AP_NET_FX_RESERVE_SPENT);if(v)*v=stored;if(r)*r=rev;return state;
}
static void ap_net_fx_store_write(int key,long long value,const char *op){
 assert(key==AP_NET_FX_RESERVE_SPENT);
 assert((state==AP_FXM_STORE_INVALID && !strcmp(op,"replace")) || !strcmp(op,"max"));
 if(state!=AP_FXM_STORE_VALID || value>stored)stored=value;
 state=AP_FXM_STORE_VALID;rev++;writes++;
}
SOURCE
static struct Driver local,other;
static struct GameTracker gt;
static void reset(void){
 connected=active=onTrack=1;state=AP_FXM_STORE_ABSENT;stored=0;rev=0;writes=0;
 ctr_cfg.reserve_top_up=1;local=(struct Driver){100,0,1234};other=(struct Driver){200,0,5678};
 gt=(struct GameTracker){0,0,{&local,&other}};AP_ReserveTopUpReset();
}
static void countdown(void){gt.trafficLightsTimer=1;AP_ReserveTopUpTick(&gt);gt.trafficLightsTimer=0;}
int main(void){
 for(int r=1;r<=32767;r++){
  reset();local.reserves=r;AP_ReserveTopUpReceive();countdown();AP_ReserveTopUpTick(&gt);
  int expected=r+983;if(expected>32767)expected-=65536;
  assert(local.reserves==expected && local.fireCap==1234 && other.reserves==200);
  assert(stored==1&&writes==1);
  AP_ReserveTopUpTick(&gt);assert(stored==1&&writes==1&&local.reserves==expected);
 }
 for(int r=-32768;r<=0;r++){
  reset();local.reserves=r;AP_ReserveTopUpReceive();countdown();AP_ReserveTopUpTick(&gt);
  assert(local.reserves==r&&writes==0);
  local.reserves=1;AP_ReserveTopUpTick(&gt);assert(local.reserves==984&&stored==1);
 }
 for(int gate=0;gate<13;gate++){
  reset();AP_ReserveTopUpReceive();countdown();
  switch(gate){
   case 0:connected=0;break;case 1:active=0;break;case 2:ctr_cfg.reserve_top_up=0;break;
   case 3:onTrack=0;break;case 4:gt.trafficLightsTimer=1;break;
   case 5:gt.gameMode1=START_OF_RACE;break;case 6:gt.gameMode1=END_OF_RACE;break;
   case 7:gt.gameMode1=MAIN_MENU;break;case 8:gt.gameMode1=GAME_CUTSCENE;break;
   case 9:gt.gameMode1=PAUSE_ALL;break;case 10:gt.drivers[0]=NULL;break;
   case 11:local.actionsFlagSet=ACTION_BOT;break;case 12:AP_ReserveTopUpReset();AP_ReserveTopUpReceive();break;
  }
  AP_ReserveTopUpTick(&gt);assert(writes==0&&local.reserves==100);
 }
 reset();AP_ReserveTopUpReceive();AP_ReserveTopUpReceive();state=AP_FXM_STORE_PENDING;
 countdown();AP_ReserveTopUpTick(&gt);assert(writes==0);
 state=AP_FXM_STORE_ABSENT;AP_ReserveTopUpTick(&gt);assert(writes==1&&local.reserves==1083);
 AP_ReserveTopUpTick(&gt);assert(writes==2&&local.reserves==2066);
 // Reconnect/restart to the same room: replay does not spend applied copies.
 AP_ReserveTopUpReset();AP_ReserveTopUpReceive();AP_ReserveTopUpReceive();countdown();
 AP_ReserveTopUpTick(&gt);assert(writes==2&&local.reserves==2066);
 AP_ReserveTopUpReceive();AP_ReserveTopUpReset(); // pending copy survives process via replay
 for(int i=0;i<3;i++)AP_ReserveTopUpReceive();
 countdown();AP_ReserveTopUpTick(&gt);
 assert(writes==3&&local.reserves==3049);
 // Fresh room / slot: no inherited spent count.
 state=AP_FXM_STORE_ABSENT;stored=0;rev++;AP_ReserveTopUpReset();AP_ReserveTopUpReceive();
 countdown();AP_ReserveTopUpTick(&gt);assert(stored==1&&writes==4);
 // A newer room reply can only raise the local spent counter.
 AP_ReserveTopUpReceive();stored=2;rev++;AP_ReserveTopUpTick(&gt);assert(writes==4);
 reset();state=AP_FXM_STORE_INVALID;AP_ReserveTopUpReceive();countdown();AP_ReserveTopUpTick(&gt);
 assert(writes==1&&stored==1);
 reset();state=AP_FXM_STORE_VALID;stored=9223372036854775807LL;
 AP_ReserveTopUpReceive();countdown();AP_ReserveTopUpTick(&gt);assert(writes==0);
 puts("PASS: all signed reserve values, live gates, local isolation and room replay accounting");
}
'''.replace("SOURCE", source)
parser = r'''
#include <cassert>
#include <fstream>
#include "ap_seedcfg.h"
extern "C" void AP_LogLine(const char *) {}
int main(int argc,char **argv){
 ap_seedcfg_parse_json(nlohmann::json{{"ctr_options",{{"schema_version",18},{"reserve_top_up",true}}}});
 assert(ctr_cfg.reserve_top_up==1);
 ap_seedcfg_parse_json(nlohmann::json{{"ctr_options",{{"schema_version",18},{"reserve_top_up",false}}}});
 assert(ctr_cfg.reserve_top_up==0);
 assert(argc==2);
 std::ifstream input(argv[1]);nlohmann::json plan;input>>plan;
 plan["ctr_options"]["reserve_top_up"]=true;
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.reserve_top_up==1);
 plan["ctr_options"]["reserve_top_up"]=false;
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.reserve_top_up==0);
 plan["ctr_options"].erase("reserve_top_up");
 ap_seedcfg_parse_json(plan);assert(ctr_cfg_active()&&ctr_cfg.reserve_top_up==0);
 plan["ctr_options"]["reserve_top_up"]=true;
 plan["ctr_options"]["unrecognized_option"]=true;
 ap_seedcfg_parse_json(plan);assert(!ctr_cfg_active()&&ctr_cfg.reserve_top_up==0);
 plan["ctr_options"].erase("unrecognized_option");
 plan["content_plan"]["version"]=99;
 ap_seedcfg_parse_json(plan);assert(!ctr_cfg_active()&&ctr_cfg.reserve_top_up==0);
 ap_seedcfg_parse_json(nlohmann::json{{"ctr_options",{{"schema_version",18},{"reserve_top_up",true}}}});
 ap_seedcfg_parse_json(nlohmann::json{{"ctr_options",{{"schema_version",18}}}});
 assert(ctr_cfg.reserve_top_up==0);
}
'''
with tempfile.TemporaryDirectory(prefix="ctr-reserve-top-up-") as tmp:
    src, exe = Path(tmp)/"test.c", Path(tmp)/"test"
    src.write_text('#include <string.h>\n' + fixture)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-fsanitize=undefined", "-Wall", "-Wextra",
                    "-I", str(root/"ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    src = Path(tmp)/"parser.cpp"
    src.write_text(parser)
    subprocess.run(["g++", "-m32", "-std=c++17", "-DCTR_AP", "-I", str(root/"ap"),
                    "-I", str(root/"ap/vendor/json/include"), str(src), str(root/"ap/ap_seedcfg.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(root/"tools/fixtures/content-plan/baseline.json")], check=True)
