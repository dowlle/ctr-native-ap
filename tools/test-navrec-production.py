"""A3-19 recorded AI laps: pin the production wiring of the pool pick, the
lane-limited fallback, the seam blend, the crate bit and the one-name-per-lane
labels in ap/ap_navrec.c.

The rules themselves are asserted in tools/test-navrec.c against the
freestanding headers. This file checks that the loader actually runs them,
in the right order, on the right data.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = (root / "ap" / "ap_navrec.c").read_text()
fmt = (root / "ap" / "ap_navrec_format.h").read_text()

failures = []


def check(cond, why):
    if not cond:
        failures.append(why)
        print(f"FAIL {why}")
    else:
        print(f"ok   {why}")


def body(name):
    m = re.search(r"\n(?:static )?[a-z][^\n]*\b" + re.escape(name) + r"\([^;{]*\)\s*\{", src)
    if not m:
        return ""
    i = m.end()
    depth = 1
    while depth and i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
        i += 1
    return src[m.start():i]


load = body("AP_NavRec_LoadForLevel")
pool = body("AP_NavRec_BuildPool")
commit = body("AP_NavRec_CommitLane")
keep = body("AP_NavRec_KeepLevelLane")
capture = body("AP_NavRec_CaptureLevelLanes")
fetch = body("AP_NavRec_FetchCandidate")
point = body("AP_NavRec_PointEngineAtLanes")
init = body("AP_NavRec_AfterBotsInit")
restore = body("AP_NavRec_AfterCheckpointRestore")
draw = body("AP_NavRec_DrawBotNames")

for name, b in [("LoadForLevel", load), ("BuildPool", pool), ("CommitLane", commit), ("KeepLevelLane", keep),
                ("CaptureLevelLanes", capture), ("FetchCandidate", fetch), ("PointEngineAtLanes", point),
                ("AfterBotsInit", init), ("AfterCheckpointRestore", restore), ("DrawBotNames", draw)]:
    check(b != "", f"{name} is found")

# Step 1: pool by header, pick at random, logged with the seed.
check('#include "ap_navrec_playback_logic.h"' in src, "the loader includes the playback logic")
check("AP_NavRecFormat_Peek(" in pool and "AP_NavRecPool_EntryCompatible(" in pool and "AP_NavRecPool_Key(" in pool,
      "the pool is built from header peeks, filtered by identity, keyed by contributor")
check("AP_NavRecFormat_Read(" not in pool, "building the pool never decodes a whole file")
check("AP_NavRecPool_Pick(" in load and "AP_NAVREC_MAX_LOAD_ATTEMPTS" in load, "the loader picks through the pool with the read budget")
check("AP_NavRecLane_Select" not in src and "AP_NavRecLane_Plan" not in src, "the newest-first selector is gone")
check(re.search(r"seed %08x", load) is not None, "the pool log line carries the seed")
check(re.search(r'lane %d: \\"%s\\" driver', load) is not None, "each recorded lane logs its file and driver")
check("AP_NavRec_NewSeed()" in init and "s_navrecSeedLevel = levelID" in init, "every race start draws a new seed for its level")
check("s_navrecSeedLevel != levelID" in restore and "AP_NavRec_LoadForLevel(levelID, s_navrecSeed)" in restore,
      "a restore reuses the race's seed when it has one")

check(restore.count("AP_NavRec_RebindBots(1)") >= 2, "both lane-publishing restore branches rebind every bot")
check("s_navrecLaneHeader[0]" in restore and "if (!ours && !s_navrecLanesLive)" in restore,
      "a restore detects this module's lanes from the restored slots, not only from the session flag")
rebind = body("AP_NavRec_RebindBots")
check("LIST_RemoveMember(&sdata->navBotList[from]" in rebind and "d->botData.botPath = (s16)to" in rebind,
      "a bot whose lane has no line any more is moved to a lane that has one")
check("botData.botNavFrame = &frames[best]" in rebind, "a rebound bot lands on a node of the lane it drives")
check("AP_NavRecPool_EntryCompatible(" in pool and "pool: skipping" in pool, "an identity mismatch is logged per file")

# Eligibility: closed and seam within bound, in both passes.
check("AP_NavRecPlayback_EligibleLaps(" in fetch and "AP_NavRecPlayback_EligibleLaps(" in load,
      "the pick and the placement use the same eligible-lap mapping")
check("AP_NavRecFormat_ClosedLaps(" not in fetch and "AP_NavRecFormat_ClosedLaps(" not in load,
      "no pass uses the closed-only mapping on its own")

# Step 2: lane-limited fallback, no synthetic lanes.
check("SynthLane" not in src and "AP_NavRecFormat_SynthLane" not in fmt, "synthesised offset lanes are gone")
check("AP_NavRec_KeepLevelLane(" in load, "unrecorded lanes keep the track's own line")
check("AP_NavRecLane_GuardOpcodes(" in keep, "a kept level lane is guarded against jumps onto recorded lanes")
check("BOTS_InitNavPath(" in keep, "a lane the level has no line for is handed back to the engine")
check("LevNavTable" in capture and "NavPath_ptr" not in capture, "level lanes are read from the LEV, never from sdata")
check("s_navrecLaneOwned[lane]" in point and "continue;" in point, "only lanes this module owns are pointed at")

# Step 3 and 4: on the playback copy, seam first, then the crate bit.
seam_at = commit.find("AP_NavRecSeam_Process(")
crate_at = commit.find("AP_NavRecCrate_Annotate(")
publish_at = commit.find("AP_NavRec_PublishLane(")
check(0 <= seam_at < crate_at < publish_at, "a recorded lane is blended, then annotated, then published")
check("s_navrecLane[lane].wire" in commit[seam_at:crate_at + 80], "the blend runs on the lane copy, not the decode buffer")
check("levelLane" in commit, "the crate bit is borrowed from the captured level lanes")

# Labels: one bot per recorded lane.
check("AP_NavRecLabel_LaneFor(" in draw, "labels go through the one-owner rule")
lane_for = draw.find("AP_NavRecLabel_LaneFor(")
check(0 <= lane_for < draw.find("invisibleTimer"), "ownership is settled before any visibility test")
check("s_navrecLaneOwner[lane] = -1" in point, "owners reset whenever lanes are published")

if failures:
    print(f"\n{len(failures)} failure(s)")
    raise SystemExit(1)
print("\n0 failure(s)")
