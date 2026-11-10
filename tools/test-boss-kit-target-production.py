"""A4-48a2 boss-kit missile target: the production wiring.

The rule lives in ap/ap_boss_kit_target_logic.h and is covered by
tools/test-boss-kit-target.c. This checks how the engine calls it, and that the
retail tables the harness mirrors are the ones in the source.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
bots = (root / 'game/PickupBots.c').read_text()
veh = (root / 'game/Vehicle/VehPickupItem.c').read_text()
gates = (root / 'ap/ap_hooks_gates.c').read_text()
data = (root / 'game/zGlobal_DATA.c').read_text()
fails = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)


# 1. The retail tables: no missile anywhere, and the sets the harness mirrors.
def table(name):
    body = data[data.index('.BossWeapon%s =' % name):]
    body = body[:body.index('\n                },')]
    rows = re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+),', body)
    out = []
    for cp, throw, typ in rows:
        if int(throw, 16) == 0:
            break
        out.append(int(typ, 16))
    return out

expect = {
    'Oxide': [0x66, 0x64, 3, 1, 0xf, 1, 4, 0xf, 4, 0xf, 4, 0xf, 4],
    'Roo': [3] * 5, 'Papu': [0x66, 0xf, 0x66, 0xf, 0x66],
    'Joe': [0x64, 0x64, 0xf, 0x64, 0x64], 'Pinstripe': [1, 1, 0xf, 1, 1],
}
for name, want in expect.items():
    got = table(name)
    check(got == want, 'retail table %s is the one the harness mirrors (%s)' % (name, got))
    check(2 not in got, 'retail table %s has no missile' % name)

# 2. The fire path brackets the projectile shot, and nothing else sets the flag.
check(bots.count('AP_BossKitShotSet(bossMeta->weaponType);') == 1, 'PickupBots sets the shot once')
check(bots.count('AP_BossKitShotClear();') == 1, 'PickupBots clears the shot once')
upd = bots[bots.index('static void PickupBots_UpdateBoss(struct Driver *boss)\n{'):]
set_i = upd.index('AP_BossKitShotSet(')
clr_i = upd.index('AP_BossKitShotClear();')
first_shoot = upd.index('VehPickupItem_ShootNow(boss, 2, (s16)weaponFlags);')
last_shoot = upd.index('VehPickupItem_ShootNow(boss, weaponID, (s16)weaponFlags);')
adv = upd.index('PickupBots_FireAdvanced(boss, player, bossMeta);')
check(adv < set_i < first_shoot < last_shoot < clr_i, 'Set precedes every projectile shot, Clear follows them')
check(upd.index('boss->heldItemID = PICKUPBOTS_ITEM_NONE;', last_shoot) > clr_i, 'Clear runs inside the fire block')
check('AP_BossKitShot' not in bots[:bots.index('static void PickupBots_UpdateBoss(struct Driver *boss)\n{')],
      'no other PickupBots path touches the flag (bots, arcade, advanced families)')

# 3. ShootNow asks only when retail found nobody, inside the Missile branch.
check(veh.count('AP_BossKitShotTarget(d)') == 1, 'ShootNow has one call')
ask = veh.index('victim = AP_BossKitShotTarget(d);')
check(veh.index('struct Driver *victim = VehPickupItem_MissileGetTargetDriver(d);') < ask
      < veh.index('// set up missile'), 'the call sits after the retail target rules, before the weapon is born')
check('if (victim == 0)\n\t\t\tvictim = AP_BossKitShotTarget(d);' in veh, 'the call only fills a missing target')
check(veh[:ask].rindex('#ifdef CTR_AP') > veh.index('case 2:'), 'the call is AP-only')

# 4. The hook: boss races only, composed kit or final, garage table, player 0.
hook = gates[gates.index('void AP_BossKitShotSet(int weaponType)'):gates.index('struct MetaDataBOSS *AP_BossVenueAttackSchedule(int fill)\n{')]
check('(gGT->gameMode1 & ADVENTURE_BOSS) == 0' in hook, 'boss races only')
check('AP_BossKitGarageNow() >= 0 || AP_BossFinalComposedNow()' in hook, 'composed kit or composed final only')
check('data.bossWeaponMetaPtr[gGT->bossID < 4 ? gGT->bossID + 1 : 0]' in hook, "the garage's own retail table")
check('gGT->drivers[0]' in hook, 'the human player is driver 0')
check('KS_MASK_GRABBED' in hook, 'a mask-grabbed player is skipped, as in retail')

for f in fails:
    print('FAIL', f)
print(f'{checks - len(fails)} of {checks} checks passed')
raise SystemExit(1 if fails else 0)
