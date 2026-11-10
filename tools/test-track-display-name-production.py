"""Execute production name resolvers and each changed drawing selection.

CTR_NAME_SOURCE_ROOT selects a baseline checkout for negative controls.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(os.environ.get("CTR_NAME_SOURCE_ROOT", Path(__file__).resolve().parents[1]))


def source(path):
    return (root / path).read_text()


def function(path, signature):
    text = source(path)
    start = text.index(signature + "\n{")
    return text[start:text.index("\n}\n", start) + 3]


fixture = r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <platform/native_custom_race_context.h>
#include <platform/native_custom_identity.h>
#include "ap/ap_content_plan.h"
#define CTR_AP
#define CTR_CUSTOM_TRACKS
#define CTR_CUSTOM_PACKAGES
#define ADVENTURE_BOSS 1
#define ADVENTURE_CUP 2
#define CUP_ANY_KIND 4
#define FONT_BIG 1
#define PENTA_PENGUIN 13
typedef short s16;
struct GameTracker { int levelID,bossID,gameMode1,gameMode2; struct {int cupID,trackIndex;} cup; } game;
static struct { struct GameTracker *gGT; char *lngStrings[1024];int menuReadyToPass; } state={.gGT=&game},*sdata=&state;
struct MetaDataLEV {int name_LNG,characterID_Boss;};
static struct {struct MetaDataLEV metaDataLEV[65];struct {int lngIndex_CupName;} AdvCups[5],ArcadeCups[5];struct {int name_LNG_long;} MetaDataCharacters[16];} data;
static ctr_content_pad pads[105];
static ctr_content_package packages[2];
static ctr_content_bosses bosses;
static struct CustomTrackRaceContext race;
static int haveRace,active=1,serving=1,garage=-1,oxide,legacy;
static const char *offline,*legacyCup;
int ap_content_plan_active(void) {return active;}
uint64_t ap_content_plan_epoch(void) {return 7;}
const ctr_content_pad *ap_content_plan_pad(int p) {return p>=0 && p<105 ? &pads[p] : NULL;}
const ctr_content_package *ap_content_plan_package(int p) {return p>=0 && p<2 ? &packages[p] : NULL;}
const ctr_content_bosses *ap_content_plan_bosses(void) {return &bosses;}
static const struct CustomTrackRaceContext *CustomTrack_StandaloneContext(int host) {return haveRace && race.hostLevelID==host ? &race : NULL;}
static int CustomTrack_ServingLoad(int host,int cup,int id) {(void)host;(void)cup;(void)id;return serving;}
static int CustomTrack_GarageVenueServing(int host,int boss,int on) {return on && garage>=0 && boss==garage && host==6 ? garage : -1;}
static int CustomTrack_OxideFinalServing(int host,int boss,int on) {return oxide && on && boss==5 && host==13;}
static const char *AP_CortexTrackDisplayName(void) {return legacy ? "CORTEX VORTEX" : NULL;}
static const char *MainRaceTrack_OfflineCustomTitle(void) {return offline;}
static const char *CustomTrack_CupDisplayName(int cup,int adv) {(void)cup;(void)adv;return legacyCup;}
static const char *AP_ContentCupName(void) {return haveRace && race.cup ? "Mixed Cup" : NULL;}
'''
cups = "ap/ap_hooks_content_cups.c"
for signature in ("const ctr_content_pad *AP_ContentCupPad(void)",
                  "int AP_ContentCupLeg(void)", "const char *AP_ContentCupTrackName(void)",
                  "const char *AP_ContentRaceDisplayName(void)"):
    fixture += function(cups, signature)
helper = root / "game/UI/UI_TrackName.c"
if helper.exists():
    fixture += helper.read_text().replace("#include <common.h>", "").replace(
        "#include <platform/native_custom_tracks.h>", "")
banner = source("game/UI/UI_RaceFlow.c")
start = banner.index("DecalFont_DrawLineBox(") + len("DecalFont_DrawLineBox(")
end = banner.index("gGT->pushBuffer[0].rect.x", start)
expr = banner[start:end].rstrip().removesuffix(",")
fixture += "\nstatic const char *banner_name(void) {struct GameTracker *gGT=&game;return " + expr + ";}\n"
standings = source("game/UI/UI_CupStandings.c")
start = standings.index("\tint index = 0x22E;")
end = standings.index("\t// title text", start)
fixture += "\nstatic const char *standings_name(void) {struct GameTracker *gGT=&game;" + standings[start:end]
fixture += "return ctCupName ? ctCupName : sdata->lngStrings[index];}\n"
pause = source("game/232/AH_Pause.c")
fixture += r'''
static const char *drawn;
static void DecalFont_DrawLine(const char *s,int x,int y,int f,int c) {(void)x;(void)y;(void)f;(void)c;drawn=s;}
#define TEXT_ELLIPSIS 0
static void DecalFont_DrawLineBox(const char *s,int x,int y,int w,int f,int c,int mode,int slot,void *ot) {(void)w;(void)mode;(void)slot;(void)ot;DecalFont_DrawLine(s,x,y,f,c);}
'''
for tag, row in (("pause_track", "rowY + 0x26"), ("pause_crystal", "crystalRowY + 0x26")):
    lines = pause.splitlines()
    i = next(i for i, l in enumerate(lines) if "DecalFont_DrawLine" in l and row in l)
    line = lines[i].strip()
    if not line.endswith(";"):
        line += lines[i + 1]
    fixture += f"static const char *{tag}(int id) {{int trackID=id,crystalID=id,posX=0,textX=0,iconX=320,rowY=0,crystalRowY=0;struct MetaDataLEV *mdLev=&data.metaDataLEV[id];{line}return drawn;}}\n"
warp = source("game/232/AH_WarpPad.c")
start = warp.index("\t\t\t\t\twarppadLNG =", warp.index("if (ap_content_plan_active())"))
line = warp[start:warp.index("\n", start)]
fixture += "static const char *pad_name(int levelID) {const ctr_content_pad *pad=ap_content_plan_pad(levelID);char *warppadLNG;" + line + "return warppadLNG;}\n"
fixture += r'''
static int failures,checks;
static void expect(const char *got,const char *want,const char *label) {
 checks++;if(!got || strcmp(got,want)) {failures++;printf("FAIL %s: got %s, want %s\n",label,got?got:"NULL",want);}
}
static void reset(void) {
 memset(pads,0,sizeof pads);memset(&race,0,sizeof race);memset(&game,0,sizeof game);
 memset(&bosses,0,sizeof bosses);memset(packages,0,sizeof packages);
 active=serving=1;haveRace=oxide=legacy=0;garage=-1;offline=legacyCup=NULL;state.menuReadyToPass=0;
 game.levelID=6;race.hostLevelID=6;race.physicalPad=3;race.seedEpoch=7;race.cup=0;
 strcpy(race.entryID,"entry");strcpy(race.trackID,"custom:one");
 pads[3].occupied=1;pads[3].retail_id=-1;pads[3].package_index=0;pads[3].hub=26;
 strcpy(pads[3].entry_id,"entry");strcpy(pads[3].track_id,"custom:one");
 strcpy(pads[3].display_name,"Raw Package Name");strcpy(pads[3].room_title,"Room Track Name");
 strcpy(packages[0].display_name,"Raw Package Name");
 for(int i=0;i<65;i++){data.metaDataLEV[i].name_LNG=i;state.lngStrings[i]="RETAIL";}
 state.lngStrings[6]="ROO'S TUBES";state.lngStrings[13]="OXIDE STATION";state.lngStrings[0x22e]="FINAL";
}
int main(void) {
 reset();haveRace=1;
 expect(banner_name(),"ROOM TRACK NAME","Adventure banner uses generated room title");
 expect(standings_name(),"ROOM TRACK NAME","standalone per-race standings");
 expect(pause_track(3),"Room Track Name","Adventure pause track row");
 expect(pause_crystal(3),"Room Track Name","Adventure pause arena row with reassigned entry");
 expect(pad_name(3),"Room Track Name","hub pad generated room title");
 for(int mode=0;mode<3;mode++){race.mode=mode;expect(banner_name(),"ROOM TRACK NAME","Trophy CTR Relic entry");}
 reset();haveRace=1;race.cup=1;race.leg=0;race.trophyLocation=99;race.laps=3;race.returnHub=26;
 pads[3].cup=1;pads[3].trophy_location=99;pads[3].legs[0].retail_id=-1;
 pads[3].legs[0].package_index=0;pads[3].legs[0].track_pad=-1;
 pads[3].legs[0].laps=3;strcpy(pads[3].legs[0].track_id,race.trackID);
 expect(banner_name(),"RAW PACKAGE NAME","cup-only package banner");
 expect(standings_name(),"RAW PACKAGE NAME","cup leg heading");
 pads[6]=pads[3];pads[6].cup=0;strcpy(pads[6].room_title,"Generated Cup Track");
 pads[3].legs[0].track_pad=6;
 expect(banner_name(),"GENERATED CUP TRACK","cup leg generated room title");
 expect(standings_name(),"GENERATED CUP TRACK","cup leg heading generated room title");
 for(int i=1;i<4;i++) {
  pads[3].legs[i]=pads[3].legs[0];race.leg=i;
  expect(banner_name(),"GENERATED CUP TRACK","later cup leg on same host");
  expect(standings_name(),"GENERATED CUP TRACK","later cup leg heading");
 }
 race.leg=0;
 pads[3].legs[0].cortex=1;race.cortex=1;race.hostLevelID=game.levelID=13;
 expect(banner_name(),"CORTEX VORTEX","Cortex cup banner");
 expect(standings_name(),"CORTEX VORTEX","Cortex cup heading");
 reset();game.gameMode1=ADVENTURE_BOSS;game.bossID=garage=0;bosses.venue_package[0]=0;
 expect(banner_name(),"RAW PACKAGE NAME","custom garage fly-in");
 expect(standings_name(),"RAW PACKAGE NAME","custom garage per-race heading");
 bosses.venue_cortex[0]=1;expect(banner_name(),"CORTEX VORTEX","Cortex garage fly-in");
 reset();oxide=1;game.gameMode1=ADVENTURE_BOSS;game.bossID=5;game.levelID=13;
 expect(banner_name(),"CORTEX VORTEX","Oxide final fly-in");
 reset();offline="OFFLINE TIME TRIAL TRACK";
 expect(banner_name(),offline,"offline Arcade and Time Trial banner");
 expect(standings_name(),offline,"offline per-race heading");
 reset();legacyCup="LEGACY CUSTOM";game.gameMode1=ADVENTURE_CUP;
 expect(banner_name(),legacyCup,"legacy custom event banner");
 expect(standings_name(),legacyCup,"legacy custom event heading");
 reset();expect(banner_name(),"ROO'S TUBES","retail banner unchanged");
 expect(standings_name(),"ROO'S TUBES","retail heading unchanged");
 active=0;expect(pause_track(6),"ROO'S TUBES","retail pause unchanged");
 reset();haveRace=1;race.retail=1;expect(banner_name(),"ROO'S TUBES","retail content-plan race");
 race.retail=0;race.seedEpoch=6;expect(banner_name(),"ROO'S TUBES","stale seed context rejected");
 reset();game.levelID=18;expect(banner_name(),"RETAIL","retail Crystal Challenge");
 reset();haveRace=1;strcpy(race.trackID,"other");expect(banner_name(),"ROO'S TUBES","mismatched logical track rejected");
 reset();game.gameMode1=ADVENTURE_BOSS;game.bossID=garage=0;bosses.venue_package[0]=0;
 game.levelID=5;expect(banner_name(),"RETAIL","garage title does not leak to another host");
 game.levelID=6;game.gameMode1=0;expect(banner_name(),"ROO'S TUBES","garage title does not leak to ordinary retail race");
 reset();haveRace=1;memset(pads[3].room_title,'W',128);pads[3].room_title[128]=0;
 expect(banner_name(),pads[3].room_title,"full long title retained for banner scrolling");
 printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
if os.environ.get("CTR_NAME_FIXTURE_OUT"):
    Path(os.environ["CTR_NAME_FIXTURE_OUT"]).write_text(fixture)
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "names.c"
    c.write_text(fixture)
    subprocess.run(["cc", "-std=c11", "-I", str(root), "-I", str(root / "include"),
                    str(c), "-o", str(c.with_suffix(""))], check=True)
    subprocess.run([str(c.with_suffix(""))], check=True)
