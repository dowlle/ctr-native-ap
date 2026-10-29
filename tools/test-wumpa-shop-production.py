"""#249: the Wumpa Shop and Wumpa goal are wired where the design puts them.

The decisions themselves are covered by tools/test-wumpa-bank.c,
tools/test-wumpa-shop-seedcfg.cpp and tools/test-wumpa-shop-ledger.cpp. This
checks that production calls them at the right sites:

1. banking: fruit past 10 in RB_Player_ModifyWumpa (local human player only,
   after the unlimited-fruit cheat has returned), AP fruit past 10 in
   AP_WumpaReceive instead of discarding it, the held fruit on the player's
   race-finished edge, only in an Adventure race (decision 22), armed by a
   newly seen countdown,
   never for a forced DeathLink loss, nothing under the cheat, and AP fruit
   banked only after the effect marker that covers it is written;
2. the wallet keys: connected, read, merged and flushed with "max", purchases
   written with "update", reserved against original_value, undone with "pop",
   and resent on connect;
3. the goal arm and the Oxide companion arm read the lifetime total;
4. the save screen: the WUMPA SHOP row, the menu procedure's row mapping,
   the shop page owning input in RECTMENU_ProcessState, the row label, and
   the slot_data parse;
5. the numbers on screen (decisions 20 and 21): the race HUD keeps the
   retail count only, both hub HUD passes draw the balance and banked
   total, and the tracker's hub tab draws the shop line.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
failures = 0


def check(label, ok):
    global failures
    print(('ok    ' if ok else 'FAIL  ') + label)
    failures += 0 if ok else 1


def text(path):
    return (root / path).read_text()


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


rb = body(text('game/231/RB_Player.c'), 'void RB_Player_ModifyWumpa(')
cheat = rb.index('CHEAT_WUMPA')
bank = rb.find('AP_WumpaBankKart(driver, numWumpaOriginal, wumpaDelta)')
check('ModifyWumpa banks the overflow after the cheat early return', bank > cheat)
check('ModifyWumpa banks inside the human-driver branch', bank > rb.index('ACTION_BOT) == 0)'))
check('ModifyWumpa banks before the count is capped', bank < rb.index('driver->numWumpas > 10'))

wumpa = text('ap/ap_wumpa.c')
recv = body(wumpa, 'void AP_WumpaReceive(int count)')
check('AP_WumpaReceive holds AP fruit past 10 for the bank when banking is on',
      re.search(r'discarded > 0 && AP_WumpaBanking\(\)\)\s*\{\s*g_wumpa_ap_credit \+= discarded;', recv) is not None)
fxwrite = body(text('ap/ap_hooks_received.c'), 'static void AP_FxWrite(')
check('held AP credits are banked only after the effect marker write',
      fxwrite.index('ap_net_fx_store_write(') < fxwrite.index('AP_WumpaCommitReceived();'))
banking = body(wumpa, 'static int AP_WumpaBanking(void)')
check('the unlimited-fruit cheat banks nothing', 'CHEAT_WUMPA' in banking)
kart = body(wumpa, 'void AP_WumpaBankKart(')
check('kart banking is the local human player',
      'driver != gGT->drivers[0]' in kart and 'ACTION_BOT' in kart and 'AP_WumpaBanking()' in kart)
fin = body(wumpa, 'void AP_WumpaOnRaceFinish(')
check('finish banking takes the latch with the forced-loss predicate',
      'AP_WumpaFinishTake(&g_finish_latch, held, AP_RaceAttemptIsForcedLoss())' in fin)
adv = body(wumpa, 'static int AP_WumpaAdventureRace(')
check('banking takes the Adventure race mode test', 'AP_WumpaBankRaceMode(gGT->gameMode1)' in adv)
check('kart and finish banking bank only in an Adventure race',
      '!AP_WumpaAdventureRace(gGT)' in fin and '!AP_WumpaAdventureRace(gGT)' in kart and
      'BATTLE_MODE) != 0' not in fin and 'BATTLE_MODE) != 0' not in kart)
tick = body(wumpa, 'void AP_WumpaTick(')
check('a newly seen countdown arms the finish payout',
      re.search(r'if \(!g_countdown_seen\)\s*AP_WumpaFinishArm\(&g_finish_latch\);\s*g_countdown_seen = 1;', tick) is not None)
check('the Wumpa tick banks on the rising finish edge with the previous frame\'s human state',
      re.search(r'if \(finished && !finishedPrev && !loading\)\s*AP_WumpaOnRaceFinish\(p, humanPrev,', tick) is not None and
      'AP_WumpaOnRaceFinish' not in text('ap/ap_hooks_race_checks.c'))

net = text('ap/ap_net.cpp')
check('wallet keys connected with the boss-won flags',
      re.search(r'g_wumpa\.connect\(g_room_endpoint, connectedSeed', net) is not None and
      'g_ap->Get({g_wumpa.bankKey, g_wumpa.shopKey});' in net)
check('every disconnect site clears the wallet barrier',
      net.count('g_boss_won.disconnected();') == net.count('g_wumpa.disconnected();'))
flush = body(net, 'static void ap_wumpa_flush()\n{')
check('banking flushes with max', 'op.operation = "max"; op.value = g_wumpa.lifetime;' in flush)
check('a refused purchase is undone with pop', 'op.operation = "pop"' in flush)
buy = body(net, 'extern "C" int ap_net_wumpa_buy(')
check('a purchase writes update and waits for the reply',
      'op.operation = "update";' in buy and 'g_ap->Set(g_wumpa.shopKey, nlohmann::json::object(), true, {op})' in buy and
      'ap_net_try_send_location' not in buy)
check('the set reply passes original_value to the reservation',
      'g_wumpa.shopReply(value, original)' in net and 'const nlohmann::json &original) {' in net)
check('only a confirmed purchase sends its check',
      re.search(r'REPLY_CONFIRMED\) \{[^}]*ap_net_try_send_location\(\(int64_t\)code\);', net) is not None)
resend = body(net, 'static void ap_wumpa_resend_paid()\n{')
check('an overspent wallet resends nothing', resend.index('if (g_wumpa.overspent())') < resend.index('g_wumpa.unsentPaid(done)'))
check('paid but unchecked purchases are resent after the wallet read',
      re.search(r'g_wumpa\.shopRetrieved\(wShop->second\);[\s\S]{0,400}ap_wumpa_resend_paid\(\);', net) is not None)

goal = body(text('ap/ap_hooks_goal.c'), 'void AP_EvaluateGoal(void)')
check('the goal ANDs the Wumpa arm on the lifetime total',
      'done = done && AP_WumpaGoalMet(ctr_wumpa_shop.goal, ap_net_wumpa_lifetime());' in goal)
gates = text('ap/ap_hooks_gates.c')
check('the Oxide companion arm reads the lifetime total',
      'in.wumpaShort = !AP_WumpaGoalMet(ctr_wumpa_shop.goal, ap_net_wumpa_lifetime());' in gates)
check('the verifier sweep treats the Wumpa arm as met', 'in.wumpaShort = 0;' in text('ap/ap_verify.c'))

save = text('game/232/AH_SaveObj.c')
check('the save screen prepares the WUMPA SHOP row before showing the menu',
      re.search(r'AP_WumpaShopPrepareGreenMenu\(&data\.menuGreenLoadSave\);\s*#endif\s*RECTMENU_Show\(&data\.menuGreenLoadSave\);', save) is not None)
proc = body(text('game/SelectProfile.c'), 'void SelectProfile_AdvPickMode_MenuProc(')
check('the green menu procedure maps the shop and EXIT rows first',
      proc.index('AP_WumpaShopGreenMenuRow(menu, row)') < proc.index('if ((row >= 0) && (row < 3))'))
rect = text('game/RECTMENU.c')
state = body(rect, 'void RECTMENU_ProcessState()')
check('the shop page owns input like the tracker', 'if (AP_WumpaShopMenuFrame()) return;' in state)
check('the row label resolves', 'index == AP_LNG_WUMPA_SHOP' in rect)
check('the race HUD draws no banked number', 'AP_Wumpa' not in text('game/UI/UI_DrawNum.c'))
for path, fn in (('game/UI/UI_RenderFrame.c', 'void UI_RenderFrame_AdvHub(void)'), ('game/232/AH_Map.c', None)):
    src = text(path) if fn is None else body(text(path), fn)
    check(f'{path}: the hub counters draw the Wumpa balance',
          re.search(r'UI_DrawNumTrophy\([^;]*\);\s*#ifdef CTR_AP\s*//[^\n]*\n\s*AP_WumpaDrawHub\(\);\s*#endif\s*\}', src) is not None)
hub = body(wumpa, 'void AP_WumpaDrawHub(void)')
check('the hub counter shows balance and lifetime banked total',
      'AP_WumpaHubLine(line, (int)sizeof line, ap_net_wumpa_balance(), ap_net_wumpa_lifetime())' in hub and
      'numPlyrCurrGame != 1' in hub)
tracker = body(text('ap/ap_tracker.c'), 'static void AP_TrackerDraw(void)')
check('the tracker hub tab draws the shop line',
      'AP_WumpaShopHubTrackerLine(GEM_STONE_VALLEY+ap_tracker.hub,line,(int)sizeof line)' in tracker)
tline = body(text('ap/ap_wumpa_shop_ui.c'), 'int AP_WumpaShopHubTrackerLine(')
check('the shop line tallies the shop page\'s own slot states',
      'states[k] = AP_WShopSlotState(hub, k);' in tline and 'AP_WumpaShopTallyStates(states, ctr_wumpa_shop.slots)' in tline)
cfg = text('ap/ap_seedcfg.cpp')
check('slot_data parses the block after the Cup sweeps and before activation',
      cfg.index('ap_wumpa_shop_parse(j, opt, schema, &shop)') > cfg.index('if (!parse_cup_sweeps(j,opt,schema)) return;') and
      cfg.index('ap_wumpa_shop_parse(j, opt, schema, &shop)') < cfg.index('ctr_cfg.schema_version = schema;'))
check('the shop page is a unity member after ap_wumpa.c',
      text('game/game_unity.h').index('ap_wumpa_shop_ui.c') > text('game/game_unity.h').index('ap_wumpa.c"'))
check('the shared wire fixture is present', (root / 'tools/fixtures/wumpa_shop.json').is_file())

print('FAILED' if failures else 'all passed')
sys.exit(1 if failures else 0)
