"""A4-60c: a random hub behind adventure name entry. Check the engine wiring.

tools/test-hub-name-entry.c pins the decisions (ap/ap_hub_name_entry_logic.h)
and replays the New Adventure flows; this checks the production code asks
them where the garage is special today:
- New Adventure requests the level the feature picks (hub or garage);
- the level load treats the backdrop like the garage (menu mode, one player,
  no hub threads, the garage's overlay and render flags, the racer's adventure
  pack) and gives it the hub's primitive arena, ordering table and instance
  budget, with its pointer map in the native scratch;
- CS_Garage_Init hands over to the feature, name entry Cancel leaves for the
  title, LOAD_LevelFile records the garage when the backdrop is left;
- the AP hooks that key on a hub level id alone stay quiet while it is up:
  the hub return (one-lap restore, racer lock), the goal credits, the hub save
  box close (A4-58, #667) and Discord;
- the glue is a unity member and runs from AP_OnFrame;
- A4-87: the handoff shows name entry, not only asks for it, and the two
  engine facts the C harness's menu model rests on still hold (ProcessState
  runs only while a menu is shown; only ProcessState turns the desired menu
  into the shown one), with nothing showing a menu over the hub before it;
  the handoff waits for a released Enter.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
failures = 0


def check(label, ok):
    global failures
    print(("ok    " if ok else "FAIL  ") + label)
    failures += not ok


def body(text, signature):
    start = text.find(signature)
    if start < 0:
        return ""
    end = text.find("\n}\n", start)
    return text[start:end]


def read(rel):
    p = root / rel
    return p.read_text() if p.exists() else ""


title = read("game/230/MM_Title.c")
m = re.search(r"case 0:(.*?)break;", title, re.S)
case0 = m.group(1) if m else ""
check("New Adventure requests the feature's level in AP builds and the garage otherwise",
      "MainRaceTrack_RequestLoad((s16)AP_HubNameEntry_NewAdventureLevel());" in case0
      and "#else" in case0 and "MainRaceTrack_RequestLoad(ADVENTURE_GARAGE);" in case0.split("#else", 1)[-1])

ten = read("game/LOAD/LOAD_TenStages.c")
check("the load asks once whether this level is the backdrop", "b32 apHubBackdrop = AP_HubNameEntry_IsBackdropLevel(levelID);" in ten)
m = re.search(r"else if \(apHubBackdrop\)\s*\{(.*?)\n\t\t\}", ten, re.S)
branch = m.group(1) if m else ""
hub_branch = ten.find("else if (strncmp(gGT->levelName, sdata->s_hub, 3) == 0)")
check("the backdrop takes its own mode branch before the hub branch",
      branch != "" and 0 <= ten.find("else if (apHubBackdrop)") < hub_branch)
for needle, label in [
    ("gGT->gameMode1 |= MAIN_MENU;", "the backdrop loads in menu mode (no ADVENTURE_ARENA, no LEV_SWAP)"),
    ("gGT->numPlyrCurrGame = 1;", "one player, as the garage"),
    ("sdata->mainMenuState = MAIN_MENU_ADVENTURE;", "the garage's menu state, so stage 4 calls CS_Garage_Init"),
    ("gGT->gameMode2 |= NO_LEV_INSTANCE;", "hub instances are drawn without their threads"),
]:
    check(label, needle in branch)
code = "\n".join(l for l in branch.splitlines() if not l.strip().startswith("//"))
check("no hub mode flag leaks into the backdrop branch", "ADVENTURE_ARENA" not in code and "LEV_SWAP" not in code)
check("the garage's thread overlay (233), not the main menu's",
      "(levelID != ADVENTURE_GARAGE) && !apHubBackdrop && ((gGT->gameMode1 & MAIN_MENU) != 0)" in ten
      and "(levelID == ADVENTURE_GARAGE) || apHubBackdrop)" in ten)
check("the garage's render flags at the end of the load",
      "((gGT->gameMode1 & MAIN_MENU) != 0) && (gGT->levelID != ADVENTURE_GARAGE) && !apHubBackdrop)" in ten)
ptr = ten.find("int ptrIndex = LOAD_GetBigfileIndex(gGT->levelID, sdata->levelLOD, LVI_PTR);")
seg = ten[ptr:ptr + 900]
check("the backdrop's pointer map goes to the native scratch before it is queued",
      "if (apHubBackdrop)" in seg and "sdata->PatchMem_Ptr = NativePtrScratch_Data();" in seg
      and seg.find("sdata->PatchMem_Ptr = NativePtrScratch_Data();") < seg.find("LOAD_AppendQueue"))

init = read("game/MAIN/MainInit.c")
prim = body(init, "static int MainInit_GetPrimMemSize(")
check("primitive arena: the hub's 0x1c000, decided before the menu's 0x17c00",
      0 <= prim.find("return AP_HNE_PRIM_BYTES;") < prim.find("return 0x17c00;"))
ot = body(init, "void MainInit_OTMem(")
check("ordering table: the hub's, decided before the menu's",
      0 <= ot.find("size = AP_HNE_OT_BYTES;") < ot.find("size = 0x1800;"))
jit = body(init, "void MainInit_JitPoolsNew(")
check("render bucket and instance budget: the garage's and the hub's 0x800",
      "AP_HubNameEntry_IsBackdropLevel(gGT->levelID)" in jit and "renderBucketSize = 0x800;" in jit.split("AP_HubNameEntry_IsBackdropLevel", 1)[-1][:120])

assets = read("game/LOAD/LOAD_Assets.c")
i = assets.find("(gGT->levelID == ADVENTURE_GARAGE)")
check("driver pack: the racer's adventure pack, as the garage", i >= 0 and
      "AP_HubNameEntry_IsBackdropLevel(gGT->levelID)" in assets[i:i + 400] and "BI_ADVENTUREPACK" in assets[i:i + 600])

level = body(read("game/LOAD/LOAD_Level.c"), "void LOAD_LevelFile(")
check("LOAD_LevelFile records prevLEV through the feature before levelID changes",
      0 <= level.find("gGT->prevLEV = gGT->levelID;") < level.find("AP_HubNameEntry_OnLevelFile(gGT->prevLEV, levelID)") < level.find("gGT->levelID = levelID;"))

garage = read("game/233/CS_Garage.c")
ginit = body(garage, "void CS_Garage_Init(")
check("CS_Garage_Init hands the backdrop to the feature before the garage menu",
      0 <= ginit.find("if (AP_HubNameEntry_GarageInit())") < ginit.find("sdata->ptrActiveMenu = &gGarage.menuGarage;"))
cancel = body(garage, "int CS_Garage_APNameEntryCancelToMainMenu(")
check("name entry Cancel on the backdrop leaves for the title first",
      0 <= cancel.find("AP_HubNameEntry_CancelToTitle()") < cancel.find("AP_GarageSkip_ShouldExitToMainMenu"))

frame = read("ap/ap_hooks_frame.c")
onframe = body(frame, "void AP_OnFrame(")
check("AP_OnFrame runs the feature every frame", "AP_HubNameEntry_OnFrame(gGT);" in onframe)
check("the one-lap restore on a hub return stays quiet on the backdrop",
      "!AP_HubNameEntry_Active() &&\n\t\t\t    (int)gGT->levelID >= GEM_STONE_VALLEY" in frame)
check("the racer lock restore on a hub return stays quiet on the backdrop",
      "(int)gGT->levelID <= CITADEL_CITY && !AP_HubNameEntry_Active())\n\t\t\t{\n\t\t\t\tAP_RacerLock_RestoreOnHub();" in frame)
check("the goal credits wait for a real hub", "ap_goal_presentation.creditsPending && !AP_HubNameEntry_Active()" in frame)

savebox = body(read("ap/ap_hub_savebox.c"), "void AP_HubSaveBoxCloseForLoad(")
check("the hub save box close (A4-58, #667) does not treat the backdrop as a hub",
      "!AP_HubNameEntry_Active()" in savebox.split("int a =", 1)[0])
check("Discord shows the backdrop as the garage",
      "if (AP_HubNameEntry_IsBackdropLevel(facts.levelID))\n\t\t\tfacts.levelID = ADVENTURE_GARAGE;" in read("ap/ap_discord.c"))

glue = read("ap/ap_hub_name_entry.c")
for needle, label in [
    ("AP_CharSwap_GarageRacer()", "the glue gates on the seed owning the racer"),
    ("BI_ADVENTUREHUB", "the glue checks the hub files on the disc"),
    ("NativePtrScratch_Capacity()", "the preflight knows the pointer map scratch"),
    ("SubmitName_RestoreName(0);", "the handoff seeds name entry as the garage skip does"),
    ("MainRaceTrack_RequestLoad(ADVENTURE_GARAGE);", "the fallback loads the garage"),
    ("MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);", "Cancel loads the title"),
    ("HIDE_MODEL", "the menu-mode drivers are hidden"),
    ("cameraMode = 3;", "the camera is a free camera"),
]:
    check(label, needle in glue)

# ---- A4-87: name entry has to be shown, not only asked for ------------------
handoff = glue[glue.find("if (action == AP_HNE_ACT_HANDOFF)"):]
handoff = handoff[:handoff.find("\n\t}\n")]
check("A4-87: the handoff shows name entry and leaves it desired (AP_HNE_OpenMenu)",
      "AP_HNE_OpenMenu(&sdata->ptrActiveMenu, &sdata->ptrDesiredMenu, &data.menuSubmitName);" in handoff)
logic = read("ap/ap_hub_name_entry_logic.h")
openmenu = body(logic, "static inline void AP_HNE_OpenMenu(")
check("A4-87: AP_HNE_OpenMenu sets both the shown and the desired menu",
      "*desiredMenu = menu;" in openmenu and "*activeMenu = menu;" in openmenu)
check("A4-87: the handoff waits for an idle load and a released Enter (AP_HNE_FrameReady)",
      "AP_HNE_FrameReady(sdata->Loading.stage == LOAD_IDLE, s_apHne.handedOff, AP_HNE_EnterHeld())" in glue
      and "return NikoGetEnterKey();" in body(glue, "static int AP_HNE_EnterHeld("))
hginit = body(glue, "int AP_HubNameEntry_GarageInit(")
check("A4-87: the backdrop's garage init clears the shown menu (why the handoff must show one)",
      "sdata->ptrActiveMenu = NULL;" in hginit)
render = body(read("game/MAIN/MainFrame_RenderFrame.c"), "void MainFrame_RenderFrame(")
m = re.search(r"if \(sdata->ptrActiveMenu != 0\)\s*\{\s*if \(sdata->Loading.stage == LOAD_IDLE\)\s*\{\s*RECTMENU_ProcessState\(\);", render)
check("A4-87 model fact 1: MainFrame_RenderFrame runs RECTMENU_ProcessState only while a menu is shown", m is not None)
proc = body(read("game/RECTMENU.c"), "void RECTMENU_ProcessState()")
promotes = "currMenu = sdata->ptrDesiredMenu;" in proc and "sdata->ptrActiveMenu = currMenu;" in proc
copies = []
for path in sorted((root / "game").rglob("*.c")) + sorted((root / "ap").rglob("*.c")):
    for line in path.read_text(errors="replace").splitlines():
        if re.search(r"ptrActiveMenu\s*=[^=][^;]*ptrDesiredMenu", line):
            copies.append("%s: %s" % (path.relative_to(root), line.strip()))
check("A4-87 model fact 2: only RECTMENU_ProcessState turns the desired menu into the shown one",
      promotes and not copies)
for c in copies:
    print("      other copy: " + c)

unity = read("game/game_unity.h")
check("the glue is a unity member after the save box unit, its header among the AP headers",
      0 <= unity.find('#include "../ap/ap_hub_savebox.c"') < unity.find('#include "../ap/ap_hub_name_entry.c"')
      and '#include "../ap/ap_hub_name_entry.h"' in unity)

print("%s (%d failure%s)" % ("FAILED" if failures else "PASSED", failures, "" if failures == 1 else "s"))
sys.exit(1 if failures else 0)
