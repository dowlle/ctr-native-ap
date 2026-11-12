"""A4-58: the hub save box never outlives its hub. Check the engine wiring.

tools/test-hub-save-box.c pins the decisions (ap/ap_hub_savebox_logic.h) and
replays both player reports; this checks the production code calls them where
the bug lives:
- AH_SaveObj_ThTick asks AP_HubSaveBoxMayOpenNow before the approach starts
  and again before RECTMENU_Show opens the green menu;
- MainRaceTrack_StartLoad closes the box before LOAD_LevelFile rebuilds the
  pools;
- the glue covers both save box menus, the icon thread and the shop page, and
  is a unity member.
"""
from pathlib import Path
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


save = (root / "game/232/AH_SaveObj.c").read_text()
tick = body(save, "void AH_SaveObj_ThTick(")
trigger = tick.find("(0x8ffff < dist)")
gate1 = tick.find("AP_HubSaveBoxMayOpenNow()", trigger)
first_hint = tick.find("MainFrame_RequestMaskHint", trigger)
check("the save screen's wake test refuses while a load or hub move is pending",
      trigger >= 0 and 0 <= gate1 < first_hint)
show = tick.find("RECTMENU_Show(&data.menuGreenLoadSave)")
mark = tick.rfind("save->flags |= 2;", 0, show)
between = tick[mark:show] if mark >= 0 else ""
check("the green menu is not shown once a load is pending (the skip sits between flags |= 2 and the show)",
      show >= 0 and mark > gate1 and "if (!AP_HubSaveBoxMayOpenNow())" in between
      and "goto LAB_800af72c;" in between.split("if (!AP_HubSaveBoxMayOpenNow())", 1)[-1].split(";", 1)[0] + ";")

race = (root / "game/MAIN/MainRaceTrack.c").read_text()
start_load = body(race, "void MainRaceTrack_StartLoad(")
close = start_load.find("AP_HubSaveBoxCloseForLoad();")
guard = start_load.rfind("#ifdef CTR_AP", 0, close)
check("every level load closes the hub save box before the level file loads, unconditionally in AP builds",
      0 <= close < start_load.find("LOAD_LevelFile(levelID);") and guard >= 0
      and "if (" not in start_load[guard:close] and "#endif" not in start_load[guard:close])

glue_path = root / "ap/ap_hub_savebox.c"
glue = glue_path.read_text() if glue_path.exists() else ""
for needle, label in [
    ("&data.menuGreenLoadSave", "the glue knows the green load/save menu"),
    ("&data.menuFourAdvProfiles", "the glue knows the profile page"),
    ("sdata->ptrActiveMenu = NULL", "the glue clears the active menu"),
    ("sdata->ptrDesiredMenu = NULL", "the glue clears a queued menu"),
    ("SelectProfile_Destroy()", "the glue destroys the icon thread while it is valid"),
    ("AP_WumpaShopClosePage()", "the glue closes the Wumpa Shop page"),
    ("AP_FastTravelPending()", "the glue asks for a pending TRAVEL trip"),
    ("AP_TrackerLaunchPending()", "the glue asks for a pending tracker start"),
    ("AP_CharSwap_PickerOpen()", "the glue asks for the character picker"),
    ("LOAD_IDLE", "the glue asks for a pending load"),
]:
    check(label, needle in glue)

unity = (root / "game/game_unity.h").read_text()
m_travel = unity.find('#include "../ap/ap_fasttravel.c"')
m_glue = unity.find('#include "../ap/ap_hub_savebox.c"')
check("the glue is a unity member after the travel unit", 0 <= m_travel < m_glue)

print("%s (%d failure%s)" % ("FAILED" if failures else "PASSED", failures, "" if failures == 1 else "s"))
sys.exit(1 if failures else 0)
