"""#169 tiers 2 and 3 (boss opponent and kit shuffle): the production glue.

1. Compile the production helpers out of the ap_hooks unit
   (AP_BossVenueAttackSchedule, AP_BossLossEasingNow,
   AP_BossRaceOpponentModels, AP_BossGarageOpponent) against a stub
   GameTracker and the retail data.BossWeapon* rows, and check that a garage
   with a composed kit gets exactly its seed schedule on the home track and on
   a drawn venue, that a loaded graph the record did not see throws nothing,
   that loss easing is dropped only for a composed kit with the option off,
   and that the opponent model plan keeps the garage boss's pack and adds at
   most one extra for garage boss races only.
2. Check the wiring: the garage load writes the drawn racer to P2 after the
   venue, the loader queues the extra into slot 1 and stage 6 refuses a
   missing one, both loss-easing sites ask the helper, and the panel and
   tracker name the rival.

The table, parser and decision logic are covered by tools/test-boss-encounters.cpp.
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


prod = extract(unit, 'int AP_BossGarageOpponent(int bossIdx)\n{')
prod += extract(unit, 'int AP_BossRaceOpponentModels(int player, int *extra)\n{')
prod += extract(unit, 'static int AP_BossKitGarageNow(void)\n{')
prod += extract(unit, 'int AP_BossLossEasingNow(void)\n{')
prod += extract(unit, 'static int AP_BossShuffledGarageNow(void)\n{')
prod += 'static struct MetaDataBOSS s_apBossVenueAttack[8];\n'
prod += extract(unit, 'static int AP_BossVenueCertifiedNow(int g)\n{')
# J03: the helpers the attack schedule now calls (the final record, the
# gauntlet and the decision 8 fit; test-boss-venue-production.py covers them).
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
static void kit(int g, int track, int lastCp) {
  ctr_boss_encounters.track[g] = (signed char)track;
  ctr_boss_encounters.composed[g] = 1;
  for (int i = 0; i < 5; i++) {
    AP_BossKitSegment &s = ctr_boss_encounters.schedule[g][i];
    s.checkpoint = (u8)(i * lastCp / 4); s.throwFlag = (u8)(i % 2 ? 2 : 3);
    s.weapon = (u8)(i % 2 ? 0x66 : 0x01); s.pathDisabled = 1; s.cooldown = (u16)(6 + i); s.juice = (u16)(i % 2 ? 2 : 0);
  }
}
int main() {
  sdata->gGT = &gt;
  MetaDataBOSS *tables[5] = {oxide, roo, papu, joe, pin};
  for (int i = 0; i < 5; i++) data.bossWeaponMetaPtr[i] = tables[i];
  memset(&ctr_boss_encounters, 0, sizeof ctr_boss_encounters);
  ctr_boss_encounters.state = AP_BOSS_ENC_VALID;
  ctr_boss_encounters.version = AP_BOSS_ENCOUNTERS_VERSION_2;
  ctr_boss_encounters.kit = 1;
  ctr_boss_encounters.lossEasing = 0;
  for (int g = 0; g < 4; g++) {
    ctr_boss_encounters.track[g] = (signed char)AP_BOSS_VENUE_GARAGE[g].homeLevel;
    ctr_boss_encounters.opponent[g] = AP_BOSS_VENUE_GARAGE[g].character;
  }
  // Roo on his home track (77 checkpoints) with a composed kit; Papu on
  // Crash Cove (72); Joe and Pinstripe inherit.
  kit(0, 6, 77);
  kit(1, 3, 71);
  ctr_boss_encounters.opponent[1] = 3; // Coco races Papu's garage

  race(ADVENTURE_BOSS, 6, 0, 78);
  MetaDataBOSS *m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr);
  for (int i = 0; i < 5; i++) {
    const AP_BossKitSegment &s = ctr_boss_encounters.schedule[0][i];
    assert(m[i].trackCheckpoint == s.checkpoint && m[i].throwFlag == s.throwFlag);
    assert(m[i].weaponType == s.weapon && m[i].pathChangeDisabled == s.pathDisabled);
    assert(m[i].weaponCooldown == s.cooldown && m[i].juiceFlag == s.juice);
  }
  assert(m[5].throwFlag == 0 && m[5].weaponType == 0); // terminator
  m[1].throwFlag = 9;
  assert(AP_BossVenueAttackSchedule(0) == m && m[1].throwFlag == 9); // lap wrap: same copy
  assert(roo[0].throwFlag == 3);                                      // retail rows untouched
  assert(AP_BossLossEasingNow() == 0);

  // Papu's garage on Crash Cove: the composed kit, not his mapped retail one.
  race(ADVENTURE_BOSS, 3, 1, 72);
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && m[4].trackCheckpoint == 71 && m[0].weaponType == 0x01);
  int extra = 0;
  assert(AP_BossRaceOpponentModels(0, &extra) == 9 && extra == 3);   // Papu pack + Coco
  assert(AP_BossRaceOpponentModels(3, &extra) == 9 && extra == -1);  // player is Coco
  assert(AP_BossGarageOpponent(1) == 3 && AP_BossGarageOpponent(0) == 10);

  // Decision 8: a loaded graph the record did not see keeps the composed
  // kit, remapped onto the count it has, with one log line.
  race(ADVENTURE_BOSS, 3, 1, 60);
  logs = 0;
  m = AP_BossVenueAttackSchedule(1);
  assert(m != nullptr && logs == 1 && m[0].weaponType == ctr_boss_encounters.schedule[1][0].weapon);
  for (int i = 1; i < 5; i++) assert(m[i].trackCheckpoint > m[i - 1].trackCheckpoint && m[i].trackCheckpoint < 60);
  assert(m[5].throwFlag == 0);

  // Joe inherits on his home track: retail path, retail easing.
  race(ADVENTURE_BOSS, 1, 2, 84);
  assert(AP_BossVenueAttackSchedule(1) == nullptr);
  assert(AP_BossLossEasingNow() == 1);
  assert(AP_BossRaceOpponentModels(0, &extra) == 11 && extra == -1);

  // Oxide, a cup leg and an ordinary race on Roo's Tubes are never touched.
  race(ADVENTURE_BOSS, 13, 4, 190);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossLossEasingNow() == 1);
  race(ADVENTURE_BOSS | ADVENTURE_CUP, 6, 0, 78);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossRaceOpponentModels(0, &extra) == -1);
  race(0, 6, 0, 78);
  assert(AP_BossVenueAttackSchedule(1) == nullptr && AP_BossLossEasingNow() == 1);
  assert(AP_BossRaceOpponentModels(0, &extra) == -1);

  // Loss easing on: retail easing even with the composed kit.
  ctr_boss_encounters.lossEasing = 1;
  race(ADVENTURE_BOSS, 6, 0, 78);
  assert(AP_BossLossEasingNow() == 1);

  // A tier 1 record or no slot_data: no opponent, no kit.
  ctr_boss_encounters.version = AP_BOSS_ENCOUNTERS_VERSION;
  assert(AP_BossGarageOpponent(1) == -1 && AP_BossVenueAttackSchedule(1) == nullptr);
  ctr_boss_encounters.version = AP_BOSS_ENCOUNTERS_VERSION_2;
  cfg_active = 0;
  assert(AP_BossGarageOpponent(1) == -1 && AP_BossRaceOpponentModels(0, &extra) == -1);
  assert(AP_BossVenueAttackSchedule(1) == nullptr);
  cfg_active = 1;
  ctr_boss_encounters.state = AP_BOSS_ENC_REFUSED;
  assert(AP_BossGarageOpponent(1) == -1 && AP_BossVenueAttackSchedule(1) == nullptr);
  std::puts("production opponent and kit helpers: ok");
  return 0;
}
'''
fixture = (fixture.replace('{ROO}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponRoo']) + '}')
           .replace('{PAPU}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponPapu']) + '}')
           .replace('{JOE}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponJoe']) + '}')
           .replace('{PIN}', '{' + ', '.join('{' + r + '}' for r in rows['BossWeaponPinstripe']) + '}'))

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'encounters.cpp'
    exe = Path(tmp) / 'encounters'
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
# The drawn racer replaces the home track's boss only after the venue check,
# and bossID stays the physical garage.
assert load.index('characterID_Boss') < load.index('AP_BossGarageVenue(hubID - 1)') \
    < load.index('AP_BossGarageOpponent(hubID - 1)') < load.index('data.characterIDs[1] = (s16)opponent;')
assert 'gGT->bossID = R232.bossIDs[hubID];' in garage
require('game/232/AH_Garage.c', 'AP_BossRivalAdvert(advertBoss, rivalLine', 'AP_BossKitAdvert(advertBoss, kitLine',
        'char advert[192];')

assets = text('game/LOAD/LOAD_Assets.c')
boss = assets[assets.index('LoadHighAndPack:'):assets.index('QueueLastPack:')]
assert 'AP_BossRaceOpponentModels((int)data.characterIDs[0], &bossExtra)' in boss
assert 'lastFileIndexMPK = BI_TIMETRIALPACK + bossPack;' in boss
assert '&data.driverModelExtras[1].fileBase' in boss, 'the opponent extra goes to slot 1; slot 0 is the player'
assert boss.index('BI_RACERMODELHI + data.characterIDs[0]') < boss.index('BI_RACERMODELHI + bossExtra')
require('game/LOAD/LOAD_Assets.c', 's_bossExtra = -1;\n}', 'data.driverModelExtras[1].model == NULL')
stages = text('game/LOAD/LOAD_TenStages.c')
assert stages.index('data.driverModelExtras[i].model = (struct Model *)') < stages.index('LOAD_BossOpponentValidateExtra();')

require('game/PickupBots.c', 'AP_BossLossEasingNow() ? ((s8)sdata->advProgress.timesLostBossRace[gGT->bossID] * 4) : 0')
require('game/BOTS.c', 'AP_BossLossEasingNow() ? sdata->advProgress.timesLostBossRace[bossID] : 0',
        'BOTS_Adv_NumTimesLostEvent(bossLosses) - 0xe1', 'BOTS_Adv_NumTimesLostEvent(bossLosses) - 0x141')
# The driving table stays the physical garage's boss-grade row.
require('game/BOTS.c', 'data.BossDifficulty[gGT->bossID].params1')
require('ap/ap_tracker.c', 'AP_BossRivalAdvert(ap_tracker.hub - 1, rival')
# A drawn racer's icon may be missing from the garage boss's pack: the one
# icon draw without a NULL guard has one now.
icon = text('game/UI/UI_Icon.c')
body = icon[icon.index('void UI_DrawDriverIcon('):]
assert body.index('if (icon == NULL)') < body.index('icon->texLayout'), 'UI_DrawDriverIcon must guard a NULL icon'
require('ap/ap_seedcfg.cpp', '"boss_opponent_shuffle", "boss_kit_shuffle"')
print('boss opponent and kit production wiring: ok')
