"""J11: boss opponent and kit shuffle on content-plan rooms (decisions 7, 8, 9).

A content plan with opponent or kit shuffle fills the ordinary version 2 boss
encounter configuration (ctr_boss_encounters: garage racers, composed kits,
the `final` record, kit.loss_easing). Its garage venues may be custom bytes
(a custom package or Cortex Vortex) served on the garage's home LevelID
(custom[g] 1, points[g] the certified restart point count). This compiles the
production J03 helpers of the ap_hooks unit with CTR_CUSTOM_TRACKS on and pins:

1. a composed kit runs on a custom venue (the content-plan gate in
   AP_BossEncounterKitGarage is gone), against the venue's certified count,
   with the decision 8 remap when the loaded graph differs, and loss easing
   follows kit.loss_easing there;
2. the drawn racer's model plan (garage boss pack plus one extra) on such a
   venue, and the final racer on both N. Oxide encounters, the Cortex Vortex
   Final Challenge included, with N. Oxide's composed profile remapped there;
3. cups (boss cups are refused with a drawn opponent or kit) stay untouched;
4. the wiring: the garage load seats the drawn racer as P2 on every room,
   the panel and tracker name the venue, rival and kit (a custom venue by its
   own name), and a Hit Character hit credits the racer actually seated.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
unit = ap_hooks_unit.read()


def extract(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


prod = extract(unit, 'static int AP_BossCupGarageNow(int g)\n{')
prod += extract(unit, 'static int AP_BossCupGarageHeld(int g)\n{')
prod += extract(unit, 'int AP_BossGarageOpponent(int bossIdx)\n{')
prod += extract(unit, 'int AP_BossRaceOpponentModels(int player, int *extra)\n{')
prod += extract(unit, 'static int AP_BossKitGarageNow(void)\n{')
prod += extract(unit, 'int AP_BossLossEasingNow(void)\n{')
prod += extract(unit, 'static int AP_BossShuffledGarageNow(void)\n{')
prod += 'static struct MetaDataBOSS s_apBossVenueAttack[8];\n'
prod += extract(unit, 'static int AP_BossVenueCertifiedNow(int g)\n{')
prod += extract(unit, 'static int AP_BossKitCertifiedNow(int g)\n{')
prod += extract(unit, 'static struct MetaDataBOSS *AP_BossInertSchedule(struct MetaDataBOSS *dst, int rows)\n{')
prod += extract(unit, 'static void AP_BossFillKit(')
prod += extract(unit, 'static int AP_BossFitKit(')
prod += 'static struct MetaDataBOSS s_apBossFinalAttack[AP_BOSS_FINAL_SEGMENTS + 1];\n'
prod += extract(unit, 'int AP_BossFinalComposedNow(void)\n{')
prod += extract(unit, 'static struct MetaDataBOSS *AP_BossFinalAttackSchedule(int fill)\n{')
prod += 'static struct MetaDataBOSS s_apBossGauntletAttack[AP_BOSS_VENUE_SEGMENTS + 1];\nstatic int s_apBossGauntletLeg = -1;\n'
prod += extract(unit, 'static int AP_BossGauntletLegNow(void)\n{')
prod += extract(unit, 'int AP_BossGauntletAttacker(void)\n{')
prod += extract(unit, 'static struct MetaDataBOSS *AP_BossGauntletAttackSchedule(int fill)\n{')
prod += extract(unit, 'struct MetaDataBOSS *AP_BossVenueAttackSchedule(int fill)\n{')

fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#define CTR_CUSTOM_TRACKS 1
typedef unsigned char u8; typedef unsigned short u16;
struct MetaDataBOSS { u8 trackCheckpoint, throwFlag, weaponType, pathChangeDisabled; u16 weaponCooldown, juiceFlag; };
#include "ap_boss_venue.h"
#define AP_BOSS_ADAPTER_PURE
#include "ap_boss_adapter.h"
#include "ap_oxide_weapon_schedule.h"
#define ADVENTURE_BOSS 0x80000000u
#define ADVENTURE_CUP 0x10000000u
#define ADVENTURE_MODE 0x00080000u
#define BOSS_WEAPON_NORMAL 3
#define BOSS_WEAPON_NONE 0xf
struct Level { int cnt_restart_points; };
struct Cup { int cupID; int trackIndex; };
struct GameTracker { unsigned gameMode1; short levelID; char bossID; Level *level1; Cup cup; };
struct SData { GameTracker *gGT; int bossWeaponCooldown; int bossJuiceCounter; } sdata_s, *sdata = &sdata_s;
struct Data { MetaDataBOSS *bossWeaponMetaPtr[5]; } data;
struct CustomTrackRaceContext { int retail; };
static MetaDataBOSS retail[5][8];
static int cfg_active = 1; static int ctr_cfg_active(void) { return cfg_active; }
AP_BossEncounters ctr_boss_encounters;
static int logs; static char lastLog[256];
static void AP_AppendLog(const char *m) { logs++; std::snprintf(lastLog, sizeof lastLog, "%s", m); }
/* Content-plan seams: no boss cup runs; the Cortex Vortex Final serves on Oxide Station. */
static int oxideFinalCortex;
static int AP_ContentGarageCupRunning(void) { return -1; }
static int AP_ContentGarageCupReady(int) { return -1; }
struct ctr_content_pad { int gem_cup; }; static const ctr_content_pad purplePad = {104};
static const ctr_content_pad *AP_ContentCupPad(void) { return &purplePad; }
static int AP_ContentGarageCupLegPoints(void) { return -1; }
static const CustomTrackRaceContext *CustomTrack_StandaloneContext(int) { return nullptr; }
static int CustomTrack_OxideFinalServing(int level, int boss, int adv) { return oxideFinalCortex && level == 13 && adv && (boss == 4 || boss == 5); }
''' + prod + r'''
static GameTracker gt; static Level lev;
static void race(unsigned mode, int level, int boss, int points) {
  gt.gameMode1 = mode; gt.levelID = (short)level; gt.bossID = (char)boss; lev.cnt_restart_points = points; gt.level1 = &lev;
}
static void kit(int g, int lastCp) {
  ctr_boss_encounters.composed[g] = 1;
  for (int i = 0; i < 5; i++) {
    AP_BossKitSegment &s = ctr_boss_encounters.schedule[g][i];
    s.checkpoint = (u8)(i * lastCp / 4); s.throwFlag = (u8)(i % 2 ? 2 : 3);
    s.weapon = (u8)(i % 2 ? 0x66 : 0x01); s.pathDisabled = 1; s.cooldown = (u16)(6 + i); s.juice = (u16)(i % 2 ? 2 : 0);
  }
}
int main() {
  int cases = 0, extra = 0;
  sdata->gGT = &gt;
  for (int i = 0; i < 5; i++) { retail[i][0].throwFlag = 3; retail[i][0].weaponType = 1; data.bossWeaponMetaPtr[i] = retail[i]; }
  memset(&ctr_boss_encounters, 0, sizeof ctr_boss_encounters);
  AP_BossEncounters &e = ctr_boss_encounters;
  e.state = AP_BOSS_ENC_VALID; e.version = AP_BOSS_ENCOUNTERS_VERSION_2; e.kit = 1; e.lossEasing = 0;
  e.opponentOn = 1; e.oxideInPool = 1;
  for (int g = 0; g < 4; g++) { e.track[g] = (signed char)AP_BOSS_VENUE_GARAGE[g].homeLevel; e.opponent[g] = AP_BOSS_VENUE_GARAGE[g].character; }
  /* Garage 0 (Roo) on a custom package served on Roo's Tubes, 40 certified
     restart points; garage 2 (Joe) on Cortex Vortex, 255; both composed and
     racing drawn racers (Coco 3, N. Oxide 15 with oxide_in_pool). */
  const int roo = AP_BOSS_VENUE_GARAGE[0].homeLevel, joe = AP_BOSS_VENUE_GARAGE[2].homeLevel;
  e.custom[0] = 1; e.points[0] = 40; kit(0, 39); e.opponent[0] = 3;
  e.custom[2] = 1; e.points[2] = 255; kit(2, 254); e.opponent[2] = 15;

  /* 1. The composed kit on the custom venue, exactly as recorded. */
  race(ADVENTURE_MODE | ADVENTURE_BOSS, roo, 0, 40);
  assert(AP_BossEncounterKitGarage(&e, 1, 0, 0, roo) == 0); cases++;
  MetaDataBOSS *m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr);
  for (int i = 0; i < 5; i++) {
    const AP_BossKitSegment &s = e.schedule[0][i];
    assert(m[i].trackCheckpoint == s.checkpoint && m[i].weaponType == s.weapon && m[i].weaponCooldown == s.cooldown);
  }
  assert(m[5].throwFlag == 0); cases++;
  assert(AP_BossVenueAttackSchedule(0) == m); cases++;                 /* lap wrap: the same copy */
  assert(AP_BossLossEasingNow() == 0); cases++;                        /* kit.loss_easing false */
  e.lossEasing = 1; assert(AP_BossLossEasingNow() == 1); e.lossEasing = 0; cases++;
  /* Decision 8: the loaded custom LEV has another count: remapped, one log line. */
  race(ADVENTURE_MODE | ADVENTURE_BOSS, roo, 0, 52); logs = 0;
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && logs == 1 && std::strstr(lastLog, "checkpoint fallback")); cases++;
  for (int i = 1; i < 5; i++) assert(m[i].trackCheckpoint > m[i - 1].trackCheckpoint && m[i].trackCheckpoint < 52);
  cases++;
  /* Cortex Vortex venue: its own certified count. */
  race(ADVENTURE_MODE | ADVENTURE_BOSS, joe, 2, 255); logs = 0;
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && logs == 0 && m[4].trackCheckpoint == 254 && m[0].weaponType == 0x01); cases++;
  race(ADVENTURE_MODE | ADVENTURE_BOSS, joe, 2, 120); logs = 0;
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && logs == 1 && m[4].trackCheckpoint < 120); cases++;
  /* An inherited custom venue keeps the adapter path (no composed kit). */
  e.composed[0] = 0; race(ADVENTURE_MODE | ADVENTURE_BOSS, roo, 0, 40);
  assert(AP_BossEncounterKitGarage(&e, 1, 0, 0, roo) == -1 && AP_BossLossEasingNow() == 1 &&
         AP_BossEncounterShuffledGarage(&e, 1, 0, 0, roo) == 0); cases++;
  e.composed[0] = 1;

  /* 2. The drawn racer's models on the custom venue and on Cortex Vortex. */
  race(ADVENTURE_MODE | ADVENTURE_BOSS, roo, 0, 40);
  assert(AP_BossRaceOpponentModels(0, &extra) == 10 && extra == 3); cases++;  /* Roo's pack + Coco */
  assert(AP_BossRaceOpponentModels(3, &extra) == 10 && extra == -1); cases++; /* the player is Coco */
  race(ADVENTURE_MODE | ADVENTURE_BOSS, joe, 2, 255);
  assert(AP_BossRaceOpponentModels(0, &extra) == 11 && extra == 15); cases++; /* Joe's pack + N. Oxide */
  assert(AP_BossGarageOpponent(0) == 3 && AP_BossGarageOpponent(2) == 15 && AP_BossGarageOpponent(1) == 9); cases++;
  /* Both N. Oxide encounters race the final racer with the composed profile. */
  e.finalPresent = 1; e.finalOpponent = 7; e.finalComposed = 1; e.oxideProfileAll = 1;
  for (int i = 0; i < AP_BOSS_FINAL_SEGMENTS; i++) {
    static const int starts[13] = {0, 16, 33, 47, 59, 64, 70, 86, 93, 96, 101, 102, 127};
    e.finalSchedule[i].checkpoint = (u8)starts[i]; e.finalSchedule[i].throwFlag = 3; e.finalSchedule[i].pathDisabled = 1;
    e.finalSchedule[i].weapon = 0x01; e.finalSchedule[i].juice = 0; e.finalSchedule[i].cooldown = 6;
  }
  assert(AP_BossEncounterFinalOpponent(&e) == 7); cases++;
  for (int boss = 4; boss <= 5; boss++) {
    oxideFinalCortex = 0;
    race(ADVENTURE_MODE | ADVENTURE_BOSS, 13, boss, 128);              /* Oxide Station, 0x7f scale */
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && AP_BossFinalComposedNow() && m[0].weaponType == 0x01 && m[13].throwFlag == 0); cases++;
    oxideFinalCortex = 1;                                              /* the Cortex Vortex Final Challenge */
    race(ADVENTURE_MODE | ADVENTURE_BOSS, 13, boss, 200);
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && m[12].trackCheckpoint == AP_OxideWeaponCheckpoint(127, 199)); cases++;
  }
  /* oxide_in_pool: the final racer is never N. Oxide himself. */
  e.finalOpponent = AP_BOSS_OXIDE_CHARACTER; assert(AP_BossEncounterFinalOpponent(&e) == -1); e.finalOpponent = 7; cases++;

  /* 3. A cup leg and an ordinary race on the venue's host level are untouched. */
  race(ADVENTURE_MODE | ADVENTURE_BOSS | ADVENTURE_CUP, roo, 0, 40);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossRaceOpponentModels(0, &extra) == -1 && AP_BossLossEasingNow() == 1); cases++;
  race(ADVENTURE_MODE, roo, 0, 40);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossLossEasingNow() == 1); cases++;
  /* A refused record keeps the retail boss and no kit. */
  e.state = AP_BOSS_ENC_REFUSED; race(ADVENTURE_MODE | ADVENTURE_BOSS, roo, 0, 40);
  assert(AP_BossGarageOpponent(0) == -1 && AP_BossVenueAttackSchedule(1) == nullptr); cases++;
  std::printf("PASS: %d content-plan boss shuffle cases through production code\n", cases);
  return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'cpboss.cpp'
    exe = Path(tmp) / 'cpboss'
    src.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wno-unused-function', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(root / 'ap'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


# ── 4. wiring ──────────────────────────────────────────────────────────────
def text(path):
    return (root / path).read_text()


venue = (root / 'ap' / 'ap_boss_venue.h').read_text()
kitg = venue[venue.index('static inline int AP_BossEncounterKitGarage('):]
kitg = kitg[:kitg.index('\n}\n')]
assert 'custom' not in kitg.split('{', 1)[1], 'a composed kit must run on a content-plan custom venue'
garage = text('game/232/AH_Garage.c')
load = garage[garage.index('if (hubID >= 1 && hubID <= 4)'):garage.index('RaceFlag_SetDrawOrder(1);\n\t\tMainRaceTrack_RequestLoad(levelID);')]
# The garage load seats the drawn racer on every room: no content-plan gate
# between the boss cup branch and the seat, and the final racer for hub 0.
seat = load[load.index('int venue = AP_BossGarageVenue(hubID - 1);'):]
assert 'ap_content_plan' not in seat and 'data.characterIDs[1] = (s16)opponent;' in seat
assert 'data.characterIDs[1] = (s16)finalOpponent;' in load
gates = text('ap/ap_hooks_gates.c')
adv = extract(gates, 'int AP_BossVenueAdvert(int bossIdx, char *out, int cap)\n{')
assert 'ctr_boss_encounters.custom[bossIdx]' in adv and 'pkg->display_name' in adv and '"Track: Cortex Vortex"' in adv
rival = extract(gates, 'int AP_BossRivalAdvert(int bossIdx, char *out, int cap)\n{')
assert 'ap_content_plan' not in rival and 'AP_BossEncounterOpponent(&ctr_boss_encounters, bossIdx)' in rival
assert 'ap_content_plan' not in extract(gates, 'int AP_BossKitAdvert(int bossIdx, char *out, int cap)\n{')
assert re.search(r'AP_BossRivalAdvert\(advertBoss, rivalLine.*\n.*\n\s*if \(AP_BossKitAdvert\(advertBoss, kitLine', garage)
tracker = text('ap/ap_tracker.c')
assert 'AP_BossRivalAdvert(ap_tracker.hub - 1, rival' in tracker and 'AP_BossFinalGarageOpponent()' in tracker
# Hit Character: the hit credits the engine id seated in the victim's slot.
bots = text('ap/ap_hit_bots.c')
assert 'victimEngine = (int)data.characterIDs[victim->driverID];' in bots
assert 'AP_HitEncounterOnDamage(victimEngine, damageType, flags);' in bots
# The guest unlock triggers come from the room's Hit block (its bosses table
# names the seated racers on content plans too) through the checked set.
hit = text('ap/ap_hit_encounter.c')
assert 'ap_net_location_checked(t->any_of[i])' in hit and 'ap_content_plan' not in extract(hit, 'int AP_HitEncounterGuestEligible(int guest)\n{')
print('content-plan boss shuffle wiring: ok')
