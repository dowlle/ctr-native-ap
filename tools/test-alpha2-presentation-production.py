"""Execute actual box spawning and cup/boss title selection."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def source(path):
    return (root / path).read_text()

def function(text, signature):
    start = text.index(signature + "\n{")
    return text[start:text.index("\n}\n", start) + 3]

boxes = source("ap/ap_boxes.c")
start = boxes.index("struct ApBoxLive\n{")
record = boxes[start:boxes.index("};", start) + 2]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ap/ap_content_plan.h"
#include "ap/ap_boss_venue.h"
#define CTR_NATIVE
#define CTR_CUSTOM_TRACKS
#define CTR_AP
#define PENTA_PENGUIN 13
#define CUP_ANY_KIND 1
#define AP_SPAWN_INVALID -1
#define AP_SPAWN_LIFE_LEVEL 1
typedef int AP_SpawnHandle;
typedef struct { int x,y,z; } Vec3;
typedef struct { short x,y,z; } SVec3;
struct Model { int unused; } model;
struct Instance;
struct GameTracker { int levelID, gameMode2; struct { int cupID,trackIndex; } cup; } tracker;
static int offset=54, spawns;
static Vec3 spawnedPos;
static SVec3 spawnedRot;
static int s_spawnFull,s_modelWarned,s_level,s_liveCount;
static char s_boxName[] = "apbox";
static struct Model *AP_BoxModel_GetOwned(struct GameTracker *g) { (void)g; return &model; }
static int AP_BoxModel_BaseOffsetY(struct Model *m) { assert(m == &model); return offset; }
static void AP_LogLine(const char *s) { (void)s; }
static int AP_BoxesColourFor(long c) { (void)c; return 0; }
static struct Model *AP_BoxModel_ForColour(struct Model *m, int c) { (void)c; return m; }
static int AP_Spawn_AddModel(struct Model *m, Vec3 *p, SVec3 *r, int life, char *name) {
    assert(m == &model && life == AP_SPAWN_LIFE_LEVEL && !strcmp(name,"apbox"));
    spawnedPos = *p; spawnedRot = *r; return ++spawns;
}
'''
fixture += record + "\nstatic struct ApBoxLive s_live[15];\n"
fixture += function(source("ap/ap_box_spawn_pos.c"), "void AP_BoxModel_SpawnPos(struct Model *model, int x, int y, int z, Vec3 *out)")
fixture += function(boxes, "static void AP_BoxesSpawnOne(struct GameTracker *gGT, int i)")
fixture += r'''
static ctr_content_pad pad;
static ctr_content_package package;
static int havePad=1, leg, active=1, garageRunning=-1;
static int AP_ContentGarageCupRunning(void) { return garageRunning; }
static const ctr_content_pad *AP_ContentCupPad(void) { return havePad ? &pad : NULL; }
static int AP_ContentCupLeg(void) { return leg; }
const ctr_content_package *ap_content_plan_package(int i) { return i == 2 ? &package : NULL; }
static const char *AP_CortexTrackDisplayName(void) { return "Cortex Vortex"; }
static struct {
    struct { int name_LNG, characterID_Boss; } metaDataLEV[18];
    struct { int lngIndex_CupName; } AdvCups[5],ArcadeCups[5];
    struct { int name_LNG_short,name_LNG_long; } MetaDataCharacters[16];
} data;
static struct { int menuReadyToPass; const char *lngStrings[1024]; } state, *sdata=&state;
static AP_BossEncounters ctr_boss_encounters;
static int ctr_cfg_active(void) { return active; }
static const char *CustomTrack_CupDisplayName(int cup, int adv) { (void)cup; (void)adv; return NULL; }
'''
cups = source("ap/ap_hooks_content_cups.c")
fixture += function(source("ap/ap_hooks_gates.c"), "int AP_BossGarageOpponent(int bossIdx)")
fixture += function(cups, "const char *AP_ContentCupName(void)")
fixture += function(cups, "const char *AP_ContentCupTrackName(void)")
ui = source("game/UI/UI_CupStandings.c")
start = ui.index("\tint index = 0x22E;")
fixture += "\nstatic const char *heading(void) { struct GameTracker *gGT=&tracker;\n"
fixture += ui[start:ui.index("\t// title text", start)]
fixture += "return ctCupName ? ctCupName : sdata->lngStrings[index]; }\n"
gates = source("ap/ap_hooks_gates.c")
fixture += function(gates, "int AP_BossRivalAdvert(int bossIdx, char *out, int cap)")
fixture += function(gates, "int AP_BossChallengeTitle(int bossIdx, char *out, int cap)")
fixture += r'''
int main(void) {
    for (int seated=0; seated<2; seated++) {
        s_live[0].spawn=AP_SPAWN_INVALID; s_live[0].seated=seated;
        s_live[0].x=12; s_live[0].y=-90; s_live[0].z=34; s_live[0].rotY=123;
        AP_BoxesSpawnOne(&tracker,0);
        assert(spawnedPos.x==12 && spawnedPos.y==-36 && spawnedPos.z==34);
        assert(spawnedRot.x==0 && spawnedRot.y==123 && spawnedRot.z==0);
        assert(s_live[0].labelY==46);
    }
    s_live[0].spawn=AP_SPAWN_INVALID; offset=0;
    AP_BoxesSpawnOne(&tracker,0); assert(spawnedPos.y==-90);

    strcpy(pad.display_name,"Mixed Cup"); strcpy(package.display_name,"North Pole");
    pad.legs[0].retail_id=-1; pad.legs[0].package_index=2;
    tracker.levelID=6; data.metaDataLEV[6].name_LNG=1;
    state.lngStrings[1]="Roo's Tubes"; state.lngStrings[2]="Retail Cup";
    state.lngStrings[0x22e]="FINAL"; data.AdvCups[0].lngIndex_CupName=2;
    assert(!strcmp(heading(),"North Pole"));
    state.menuReadyToPass=4; tracker.cup.trackIndex=1;
    assert(!strcmp(heading(),"Mixed Cup"));
    tracker.cup.trackIndex=3; assert(!strcmp(heading(),"FINAL"));
    state.menuReadyToPass=0; pad.legs[0].retail_id=6;
    assert(!strcmp(heading(),"Roo's Tubes"));
    pad.legs[0].retail_id=-1; pad.legs[0].cortex=1;
    assert(!strcmp(heading(),"Cortex Vortex"));
    havePad=0; assert(!AP_ContentCupTrackName()); havePad=1;
    leg=-1; assert(!AP_ContentCupTrackName()); leg=4; assert(!AP_ContentCupTrackName()); leg=0;

    // A boss cup is named for its boss (A2-47): the drawn racer the garage
    // names, else the garage's own boss; any other cup keeps its plan name.
    for(int c=0;c<16;c++) data.MetaDataCharacters[c].name_LNG_long=5;
    state.lngStrings[5]="RACER"; state.lngStrings[6]="DINGODILE"; state.lngStrings[7]="RIPPER ROO";
    data.MetaDataCharacters[3].name_LNG_long=6; data.metaDataLEV[6].characterID_Boss=2;
    data.MetaDataCharacters[2].name_LNG_long=7;
    ctr_boss_encounters.state=AP_BOSS_ENC_VALID;
    ctr_boss_encounters.version=AP_BOSS_ENCOUNTERS_VERSION_2;
    ctr_boss_encounters.opponent[0]=-1;
    assert(!strcmp(AP_ContentCupName(),"Mixed Cup"));
    garageRunning=0; assert(!strcmp(AP_ContentCupName(),"RIPPER ROO's Cup"));
    ctr_boss_encounters.opponent[0]=3; assert(!strcmp(AP_ContentCupName(),"DINGODILE's Cup"));
    ctr_boss_encounters.opponent[0]=13; assert(!strcmp(AP_ContentCupName(),"Penta's Cup"));
    active=0; ctr_boss_encounters.opponent[0]=3; assert(!strcmp(AP_ContentCupName(),"RIPPER ROO's Cup")); active=1;
    garageRunning=-1; assert(!strcmp(AP_ContentCupName(),"Mixed Cup"));
    // A content-plan Gem Cup (A3-5) is named by its plan name, not the engine cup's retail name
    // (data.AdvCups[0] is "Retail Cup"); the banner takes this same call.
    strcpy(pad.display_name,"Purple Gem Cup"); assert(!strcmp(AP_ContentCupName(),"Purple Gem Cup"));
    strcpy(pad.display_name,"Mixed Cup");
    havePad=0; assert(!AP_ContentCupName()); havePad=1;
    // The standings title takes the boss cup name through the same call.
    garageRunning=0; ctr_boss_encounters.opponent[0]=3; state.menuReadyToPass=4; tracker.cup.trackIndex=1;
    assert(!strcmp(heading(),"DINGODILE's Cup"));
    state.menuReadyToPass=0; garageRunning=-1; tracker.cup.trackIndex=0;

    ctr_boss_encounters.state=AP_BOSS_ENC_VALID;
    ctr_boss_encounters.version=AP_BOSS_ENCOUNTERS_VERSION_2;
    for(int c=0;c<16;c++) {
        data.MetaDataCharacters[c].name_LNG_short=3;
        data.MetaDataCharacters[c].name_LNG_long=4;
        state.lngStrings[3]="PENGUIN"; state.lngStrings[4]="PENTA PENGUIN";
        ctr_boss_encounters.opponent[0]=c;
        char rival[64],title[96];
        assert(AP_BossRivalAdvert(0,rival,sizeof rival) && !strcmp(rival,"Rival: PENTA PENGUIN"));
        assert(AP_BossChallengeTitle(0,title,sizeof title));
        assert(!strcmp(title,c==13?"Penta's Challenge":"PENTA PENGUIN's Challenge"));
    }
    char title[96]; active=0; assert(!AP_BossChallengeTitle(0,title,sizeof title));
    active=1; assert(!AP_BossChallengeTitle(-1,title,sizeof title));
    // N. Oxide's two races (bossID 4 and 5) keep their own banner (A2-44).
    assert(!AP_BossChallengeTitle(4,title,sizeof title) && !AP_BossChallengeTitle(5,title,sizeof title));
    assert(!AP_BossChallengeTitle(0,NULL,1) && !AP_BossRivalAdvert(0,title,0));
    puts("PASS: seated boxes receive model lift; custom leg and cup/final titles stay distinct; rivals use full names and Penta's Challenge");
}
'''
# Both garage title and the independent Oxide rival path must use these fixes.
garage = source("game/232/AH_Garage.c")
assert "AP_BossChallengeTitle(hubID - 1" in garage
assert "data.MetaDataCharacters[c].name_LNG_short" not in garage
# The race-start banner of a garage boss race names the same racer as the
# garage title (A2-44): only the single-race banner, after its retail string.
flow = source("game/UI/UI_RaceFlow.c")
banner = flow[flow.index("pcVar6 = sdata->lngStrings[textID];"):flow.index("sVar7 = gGT->pushBuffer[0].rect.y - (windowHeight + -7);")]
assert "AP_BossChallengeTitle(gGT->bossID, apBossTitle" in banner and "ADVENTURE_BOSS" in banner
assert flow.count("AP_BossChallengeTitle(") == 1
# The cup banner of a content-plan cup (a boss cup: its boss) names itself through the one cup-name call the
# standings use (A2-47), only on the adventure-cup branch.
cupBanner = flow[flow.index("ctCupName = CustomTrack_CupDisplayName(gGT->cup.cupID, 1);"):flow.index("textID = txtArray[iVar2];")]
# Every content-plan cup, Gem Cups included (A3-5), takes the plan name; the banner no longer
# gates on a running garage cup.
assert "ctCupName = AP_ContentCupName();" in cupBanner and "AP_ContentGarageCupRunning" not in cupBanner
assert "if (AP_ContentCupName() != NULL)" in cupBanner
assert flow.count("AP_ContentCupName()") == 2
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "presentation.c").write_text(fixture)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    str(path / "presentation.c"), "-o", str(path / "presentation")], check=True)
    subprocess.run([str(path / "presentation")], check=True)
