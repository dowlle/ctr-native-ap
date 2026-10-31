"""#169 tier 1 boss track shuffle: the production glue around ap/ap_boss_venue.h.

1. Compile the production venue helpers out of the ap_hooks unit
   (AP_BossGarageVenue, AP_BossRetailRulesLevel, AP_BossVenueAttackSchedule)
   against a stub GameTracker and the retail data.BossWeapon* rows, and check
   that a garage boss on a drawn venue gets its own retail schedule with every
   segment mapped onto the venue, that the home track and every other race
   keep the retail path, and that a venue whose loaded checkpoint graph does
   not match the certification throws nothing.
2. Check the wiring: every retail site that keys boss behaviour on the loaded
   level asks AP_BossRetailRulesLevel or the schedule, the garage loads the
   drawn venue while keeping the physical bossID and opponent, a refused
   record shuts the garage, and the panel and tracker name the venue.

The table, parser and decision logic are covered by tools/test-boss-venue.cpp.
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
prod += extract(unit, 'int AP_BossGarageVenue(int bossIdx)\n{')
prod += extract(unit, 'static int AP_BossKitGarageNow(void)\n{')
prod += extract(unit, 'static int AP_BossShuffledGarageNow(void)\n{')
prod += extract(unit, 'int AP_BossRetailRulesLevel(int levelID)\n{')
prod += 'static struct MetaDataBOSS s_apBossVenueAttack[8];\n'
prod += extract(unit, 'static int AP_BossVenueCertifiedNow(int g)\n{')
prod += extract(unit, 'static int AP_BossKitCertifiedNow(int g)\n{')
# J03: the inert row, the decision 8 fit, the final record and the gauntlet.
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

# The retail rows, read from the data table so the stub cannot drift.
data_c = (root / 'game/zGlobal_DATA.c').read_text()
rows = {}
for name in ('BossWeaponRoo', 'BossWeaponPapu', 'BossWeaponJoe', 'BossWeaponPinstripe'):
    block = data_c[data_c.index(f'.{name} =') :]
    block = block[: block.index('},\n\n') if '},\n\n' in block else block.index('            },')]
    rows[name] = re.findall(r'\{(0x[0-9a-f]+, 0x[0-9a-f]+, 0x[0-9a-f]+, 0x[0-9a-f]+, 0x[0-9a-f]+, 0x[0-9a-f]+)\}', block)[:6]
    assert len(rows[name]) == 6, name

fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
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
static MetaDataBOSS oxide[1], roo[6] = {ROO}, papu[6] = {PAPU}, joe[6] = {JOE}, pin[6] = {PIN};
static int cfg_active = 1; static int ctr_cfg_active(void) { return cfg_active; }
AP_BossEncounters ctr_boss_encounters;
static int logs; static void AP_AppendLog(const char *) { logs++; }
''' + prod + r'''
static GameTracker gt; static Level lev;
static void race(unsigned mode, int level, int boss, int points) {
  gt.gameMode1 = mode; gt.levelID = (short)level; gt.bossID = (char)boss; lev.cnt_restart_points = points; gt.level1 = &lev;
}
int main() {
  sdata->gGT = &gt;
  MetaDataBOSS *tables[5] = {oxide, roo, papu, joe, pin};
  for (int i = 0; i < 5; i++) data.bossWeaponMetaPtr[i] = tables[i];
  ctr_boss_encounters.state = AP_BOSS_ENC_VALID;
  ctr_boss_encounters.track[0] = 3; ctr_boss_encounters.track[1] = 5;
  ctr_boss_encounters.track[2] = 13; ctr_boss_encounters.track[3] = 17;
  assert(AP_BossGarageVenue(0) == 3 && AP_BossGarageVenue(1) == 5);
  assert(AP_BossGarageVenue(4) == -1);

  // Pinstripe on Turbo Track: his own rows, checkpoints mapped 174 -> 116.
  race(ADVENTURE_BOSS, 17, 3, 117);
  assert(AP_BossRetailRulesLevel(17) == 7);
  MetaDataBOSS *m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr);
  for (int i = 0; i < 5; i++) {
    assert(m[i].throwFlag == pin[i].throwFlag && m[i].weaponType == pin[i].weaponType);
    assert(m[i].weaponCooldown == pin[i].weaponCooldown && m[i].juiceFlag == pin[i].juiceFlag);
    assert(m[i].trackCheckpoint == AP_BossVenueCheckpoint(pin[i].trackCheckpoint, 174, 116));
  }
  assert(m[5].throwFlag == 0);
  // The lap wrap returns the same copy without refilling it.
  m[0].throwFlag = 9;
  assert(AP_BossVenueAttackSchedule(0) == m && m[0].throwFlag == 9);
  assert(pin[0].throwFlag == 3); // the retail table is never written

  // Joe on Oxide Station keeps his own pool size and is not Oxide.
  race(ADVENTURE_BOSS, 13, 2, 190);
  assert(AP_BossRetailRulesLevel(13) == 1);
  assert(AP_BossVenueAttackSchedule(1)[2].trackCheckpoint == AP_BossVenueCheckpoint(0x2a, 83, 189));

  // Home track, Oxide, an ordinary race and a cup leg take the retail path.
  race(ADVENTURE_BOSS, 5, 1, 102);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossRetailRulesLevel(5) == 5);
  race(ADVENTURE_BOSS, 13, 4, 190);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossRetailRulesLevel(13) == 13);
  race(0, 3, 0, 72);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossRetailRulesLevel(3) == 3);
  race(ADVENTURE_CUP | ADVENTURE_BOSS, 3, 0, 72);
  assert(AP_BossVenueAttackSchedule(1) == nullptr);

  // Decision 8: a loaded graph the certification did not see is mapped from
  // the home track onto the count it really has, with one log line.
  race(ADVENTURE_BOSS, 3, 0, 60);
  logs = 0;
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && logs == 1);
  for (int i = 0; i < 5; i++) {
    assert(m[i].weaponType == roo[i].weaponType);
    assert(m[i].trackCheckpoint == AP_BossVenueCheckpoint(roo[i].trackCheckpoint, 77, 59));
  }
  // One that cannot keep the segments apart: logged, nothing thrown.
  race(ADVENTURE_BOSS, 3, 0, 4);
  logs = 0;
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && logs == 1);
  assert(m[0].weaponType == BOSS_WEAPON_NONE && m[1].throwFlag == 0);

  // J03 composed kit on a modified venue: remapped from the recorded count.
  {
    static const unsigned char cps[5] = {0, 16, 33, 47, 70};
    ctr_boss_encounters.version = AP_BOSS_ENCOUNTERS_VERSION_2;
    ctr_boss_encounters.composed[0] = 1;
    for (int i = 0; i < 5; i++) {
      AP_BossKitSegment g = {0, 3, 0x01, 1, 15, 0};
      g.checkpoint = (unsigned char)AP_BossVenueCheckpoint(cps[i], 77, 71);
      if (i == 2) { g.weapon = 0x06; g.throwFlag = 3; g.pathDisabled = 1; g.cooldown = 12; }
      ctr_boss_encounters.schedule[0][i] = g;
    }
    race(ADVENTURE_BOSS, 3, 0, 72);
    logs = 0;
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && logs == 0 && m[2].weaponType == 0x06 && m[5].throwFlag == 0);
    for (int i = 0; i < 5; i++) assert(m[i].trackCheckpoint == ctr_boss_encounters.schedule[0][i].checkpoint);
    race(ADVENTURE_BOSS, 3, 0, 100);
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && logs == 1 && m[2].weaponType == 0x06);
    for (int i = 0; i < 5; i++)
      assert(m[i].trackCheckpoint == (ctr_boss_encounters.schedule[0][i].checkpoint * 99) / 71);
    race(ADVENTURE_BOSS, 3, 0, 3);
    logs = 0;
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && logs == 1 && m[0].weaponType == BOSS_WEAPON_NONE);
    ctr_boss_encounters.composed[0] = 0;
  }

  // J03 N. Oxide's composed final record: both Oxide races, retail Oxide
  // segment starts, the 0x7f scale remapped on a track it does not fit.
  {
    ctr_boss_encounters.finalPresent = 1;
    ctr_boss_encounters.finalComposed = 1;
    for (int i = 0; i < AP_BOSS_FINAL_SEGMENTS; i++) {
      AP_BossKitSegment g = {AP_BOSS_OXIDE_CHECKPOINTS[i], 3, (unsigned char)(i == 4 ? 0x0d : 0x01), 1, 12, 0};
      ctr_boss_encounters.finalSchedule[i] = g;
    }
    for (int boss = 4; boss <= 5; boss++) {
      race(ADVENTURE_BOSS, 13, boss, 190);
      assert(AP_BossFinalComposedNow());
      m = AP_BossVenueAttackSchedule(1);
      assert(m != nullptr && m[4].weaponType == 0x0d && m[12].trackCheckpoint == 0x7f && m[13].throwFlag == 0);
      assert(AP_BossVenueAttackSchedule(0) == m);
    }
    race(ADVENTURE_BOSS, 13, 5, 100);
    m = AP_BossVenueAttackSchedule(1);
    assert(m[12].trackCheckpoint == 99 && m[1].trackCheckpoint == AP_OxideWeaponCheckpoint(0x10, 99));
    // Never a garage race, never without the record.
    race(ADVENTURE_BOSS, 13, 2, 190);
    assert(!AP_BossFinalComposedNow());
    ctr_boss_encounters.finalComposed = 0;
    race(ADVENTURE_BOSS, 13, 5, 190);
    assert(!AP_BossFinalComposedNow() && AP_BossVenueAttackSchedule(1) == nullptr);
    ctr_boss_encounters.finalPresent = 0;
  }

  // J03 Purple Gem Cup gauntlet: leg 2 on its recorded track only.
  {
    static const unsigned char cps[5] = {0, 15, 34, 83, 93};
    ctr_boss_encounters.gauntletOn = 1;
    ctr_boss_encounters.gauntletTrack[1] = 9; // Mystery Caves, 123 points
    ctr_boss_encounters.gauntletAttacker[1] = 13;
    ctr_boss_encounters.gauntletComposed[1] = 1;
    for (int i = 0; i < 5; i++) {
      AP_BossKitSegment g = {(unsigned char)AP_BossVenueCheckpoint(cps[i], 101, 122), 2, 0x09, 1, 12, 0};
      ctr_boss_encounters.gauntletSchedule[1][i] = g;
    }
    race(ADVENTURE_MODE | ADVENTURE_CUP, 9, 0, 123);
    gt.cup.cupID = 4; gt.cup.trackIndex = 1;
    assert(AP_BossGauntletAttacker() == -1); // not armed before race init
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && m[0].weaponType == 0x09 && m[5].throwFlag == 0);
    assert(AP_BossGauntletAttacker() == 13 && AP_BossVenueAttackSchedule(0) == m);
    // Another cup, another leg, a wrong track, or no adventure: nothing.
    gt.cup.cupID = 3;
    assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossGauntletAttacker() == -1);
    gt.cup.cupID = 4; gt.cup.trackIndex = 0;
    assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossGauntletAttacker() == -1);
    gt.cup.trackIndex = 1; gt.levelID = 8;
    assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossGauntletAttacker() == -1);
    race(ADVENTURE_CUP, 9, 0, 123);
    assert(AP_BossVenueAttackSchedule(1) == nullptr);
    // A boss cup leg (#133 PR 6b) is a boss race, never the gauntlet.
    race(ADVENTURE_MODE | ADVENTURE_CUP | ADVENTURE_BOSS, 9, 0, 123);
    assert(AP_BossGauntletAttacker() == -1);
    // A modified leg track takes the decision 8 remap, a hopeless one nothing.
    race(ADVENTURE_MODE | ADVENTURE_CUP, 9, 0, 140);
    logs = 0;
    m = AP_BossVenueAttackSchedule(1);
    assert(m != nullptr && logs == 1 && m[4].trackCheckpoint == AP_BossVenueCheckpoint(cps[4] * 122 / 101, 122, 139));
    race(ADVENTURE_MODE | ADVENTURE_CUP, 9, 0, 3);
    assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossGauntletAttacker() == -1);
    ctr_boss_encounters.gauntletOn = 0;
    race(ADVENTURE_MODE | ADVENTURE_CUP, 9, 0, 123);
    assert(AP_BossVenueAttackSchedule(1) == nullptr);
  }

  // No slot_data: retail venues; a refused record loads nothing.
  cfg_active = 0;
  assert(AP_BossGarageVenue(2) == 1);
  race(ADVENTURE_BOSS, 3, 0, 72);
  assert(AP_BossVenueAttackSchedule(1) == nullptr);
  cfg_active = 1;
  ctr_boss_encounters.state = AP_BOSS_ENC_REFUSED;
  assert(AP_BossGarageVenue(0) == -1);
  assert(AP_BossVenueAttackSchedule(1) == nullptr);
  std::puts("production venue helpers: ok");
  return 0;
}
'''
fixture = (fixture.replace('{ROO}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponRoo']) + '}')
           .replace('{PAPU}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponPapu']) + '}')
           .replace('{JOE}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponJoe']) + '}')
           .replace('{PIN}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponPinstripe']) + '}'))

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'venue.cpp'
    exe = Path(tmp) / 'venue'
    src.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-I', str(root / 'ap'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# ── 2. wiring ──────────────────────────────────────────────────────────────
def text(path):
    return (root / path).read_text()


def require(path, *needles):
    body = text(path)
    for needle in needles:
        assert needle in body, f'{path}: missing {needle!r}'


garage = text('game/232/AH_Garage.c')
load = garage[garage.index('// new levelID'):garage.index('MainRaceTrack_RequestLoad(levelID);')]
# The opponent is read from the home track BEFORE the venue replaces levelID.
assert load.index('characterID_Boss') < load.index('AP_BossGarageVenue(hubID - 1)'), 'opponent must come from the home track'
assert 'gGT->bossID = R232.bossIDs[hubID];' in garage, 'bossID stays the physical garage'
assert 'levelID = venue;' in load and 'fadeFromBlack_desiredResult = 0x1000' in load
require('game/232/AH_Garage.c', 'AP_BossVenueAdvert(advertBoss, venueLine')
require('ap/ap_hooks_gates.c',
        'if (bossIdx < AP_BOSS_VENUE_GARAGES && AP_BossGarageVenue(bossIdx) < 0)\n\t\treturn 0;',
        '"Boss track refused: %s"')
require('game/PickupBots.c', 'AP_BossVenueAttackSchedule(1)', 'AP_BossVenueAttackSchedule(0)',
        'AP_BossRetailRulesLevel(gGT->levelID) == OXIDE_STATION')
require('game/231/RB_MinePool.c', 'bossLevel = AP_BossRetailRulesLevel(bossLevel);')
require('game/231/RB_Hazard.c', 'AP_BossRetailRulesLevel(gGT->levelID) == OXIDE_STATION')
require('game/222.c', 'AP_BossRetailRulesLevel(gGT->levelID) == HOT_AIR_SKYWAY')
require('game/Vehicle/VehPhysGeneral.c', 'AP_BossRetailRulesLevel(gGT->levelID) == DRAGON_MINES')
require('game/Vehicle/VehBirth.c', 'gGT->prevLEV == AP_BossGarageVenue(3)')
require('ap/ap_tracker.c', 'AP_BossVenueAdvert(ap_tracker.hub - 1, n->pad')
# J03 boss adapter wiring: protection on both bot damage paths, the
# per-frame self-state tick, race-init reset, and the advanced families
# routed away from the projectile branch.
require('game/231/RB_Hazard.c', 'if (PickupBots_AdapterProtect(driverVictim))')
require('game/BOTS.c', 'if (PickupBots_AdapterProtect(botDriver))')
require('game/PickupBots.c', 'PickupBots_AdapterTick();', 'AP_BossAdapterReset(&s_apBossAdapter);',
        'if (AP_BossAdapterIsAdvanced(bossMeta->weaponType))', 'PickupBots_UpdateBoss(gGT->drivers[1]);',
        'PickupBots_UpdateBoss(attacker);')
require('ap/ap_seedcfg.cpp', 'ap_seedcfg_parse_boss_encounters(j, opt, schema);',
        'std::memset(&ctr_boss_encounters, 0, sizeof ctr_boss_encounters);')
# Every remaining retail boss-track test in the AP build must be one of the
# adapted sites above; a new one has to decide whether it is venue geometry
# or boss behaviour.
boss_level = re.compile(r'levelID\s*==\s*(ROO_TUBES|PAPU_PYRAMID|DRAGON_MINES|HOT_AIR_SKYWAY|OXIDE_STATION)')
allowed = {
    'game/PickupBots.c': 2,         # initial-meta Oxide hub (after the venue copy) + retail branch of the double potion
    'game/231/RB_MinePool.c': 0,
    'game/231/RB_Hazard.c': 1,      # retail (#else) branch
    'game/222.c': 1,                # retail (#else) branch
    'game/Vehicle/VehPhysGeneral.c': 1,  # retail (#else) branch
}
for path, count in allowed.items():
    found = len(boss_level.findall(text(path)))
    assert found == count, f'{path}: {found} direct boss-track level tests, expected {count}'
print('boss venue production wiring: ok')
