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
bank = rb.find('AP_WumpaRaceModify(driver, wumpaDelta)')
check('ModifyWumpa changes the full race count after cheat and mask protection', bank > cheat and bank > rb.index('ACTION_MASK_WEAPON'))
check('the full race count handles losses as well as gains', bank > rb.index('driver->numTimesWumpa += wumpaDelta;'))
check('the full race count is applied before the retail cap', bank < rb.index('driver->numWumpas > 10'))

wumpa = text('ap/ap_wumpa.c')
recv = body(wumpa, 'void AP_WumpaReceive(int count)')
inrace = recv.index('AP_WumpaReceiptInRace()')
atonce = re.search(r'if \(AP_WumpaBanking\(\)\)\s*\{\s*g_wumpa_ap_credit \+= count;', recv)
check('outside a race, received fruit banks at once when banking is on (after the in-race branch, before the kart hold)',
      atonce is not None and inrace < atonce.start() < recv.index('g_wumpa_pending += count;'))
check('the in-race receipt still joins the race count, not the bank',
      'g_wumpa_race_pending' in recv[inrace:atonce.start()])
fxwrite = body(text('ap/ap_hooks_received.c'), 'static void AP_FxWrite(')
check('held AP credits are banked only after the effect marker write',
      fxwrite.index('ap_net_fx_store_write(') < fxwrite.index('AP_WumpaCommitReceived();'))
banking = body(wumpa, 'static int AP_WumpaBanking(void)')
check('the unlimited-fruit cheat banks nothing', 'CHEAT_WUMPA' in banking)
kart = body(wumpa, 'static int AP_WumpaRaceTracking(')
check('kart banking is the local human player',
      'driver != gGT->drivers[0]' in kart and 'ACTION_BOT' in kart and 'ctr_cfg_active()' in kart and 'CHEAT_WUMPA' in kart)
fin = body(wumpa, 'void AP_WumpaOnRaceFinish(')
check('finish banking takes the latch; a forced DeathLink loss takes the abandoned payout',
      'forced = AP_RaceAttemptIsForcedLoss();' in fin and
      'AP_WumpaLeaveTake(&g_finish_latch, g_race_wumpa, g_race_start)' in fin and
      'AP_WumpaFinishTake(&g_finish_latch, g_race_wumpa >= 0 ? g_race_wumpa : held, 0)' in fin)
adv = body(wumpa, 'static int AP_WumpaAdventureRace(')
check('banking takes the Adventure race mode test', 'AP_WumpaBankRaceMode(gGT->gameMode1)' in adv)
check('kart and finish banking bank only in an Adventure race',
      '!AP_WumpaAdventureRace(gGT)' in fin and '!AP_WumpaAdventureRace(gGT)' in kart and
      'BATTLE_MODE) != 0' not in fin and 'BATTLE_MODE) != 0' not in kart)
tick = body(wumpa, 'void AP_WumpaTick(')
leave = body(wumpa, 'static void AP_WumpaOnRaceLeave(void)')
check('an abandoned attempt banks through the same latch, minus the starting fruit',
      'AP_WumpaLeaveTake(&g_finish_latch, g_race_wumpa, g_race_start)' in leave and 'AP_WumpaBanking()' in leave and
      'g_wumpa_race_pending' in leave)
check('the tick takes the abandoned payout on the boundary before clearing the race state',
      re.search(r'END_OF_RACE\) != 0\)\s*\{\s*AP_WumpaOnRaceLeave\(\);\s*g_countdown_seen = 0;', tick) is not None)
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
check('the plan verifier Wumpa goal follows reachable income',
      'case WUMPA: return income();' in text('ap/ap_content_verify.hpp'))

save = text('game/232/AH_SaveObj.c')
check('the save screen prepares the WUMPA SHOP row before showing the menu',
      re.search(r'AP_WumpaShopPrepareGreenMenu\(&data\.menuGreenLoadSave\);\s*#endif\s*RECTMENU_Show\(&data\.menuGreenLoadSave\);', save) is not None)
proc = body(text('game/SelectProfile.c'), 'void SelectProfile_AdvPickMode_MenuProc(')
check('the green menu procedure maps the shop row and the retail rows first',
      proc.index('AP_WumpaShopGreenMenuRow(menu, row)') < proc.index('if ((row >= 0) && (row < 3))'))
check('LOAD, SAVE and DELETE toggle the mapped retail row, not rowSelected',
      re.search(r'#ifdef CTR_AP\s*//[^\n]*\n\s*SelectProfile_ToggleMode\(\(u16\)row \| 0x20\);\s*#else', proc) is not None)
ui = text('ap/ap_wumpa_shop_ui.c')
check('the shop rows come from the WUMPA SHOP first table',
      'ap_wshop_green_rows[AP_WSHOP_GREEN_ROWS + 1] = AP_WSHOP_GREEN_ROWS_INIT;' in ui)
prep = body(ui, 'void AP_WumpaShopPrepareGreenMenu(')
check('the save screen opens on its first visible row on every visit',
      'menu->rowSelected = (s16)AP_WumpaShopOpeningRow(menu->rowSelected, showShop);' in prep and
      'showShop = AP_WumpaShopRowShown();' in prep and
      'menu->rows = ap_wshop_green_rows;' in prep and 'menu->rows = &data.rowsGreenLoadSave[0];' in prep)
close = body(ui, 'static void AP_WShopClose(')
frame = body(ui, 'int AP_WumpaShopMenuFrame(')
check('leaving the shop keeps the cursor on WUMPA SHOP',
      'rowSelected' not in close and 'rowSelected' not in frame and
      re.search(r'if \(tap & \(BTN_TRIANGLE \| BTN_START\)\)\s*\{[^}]*AP_WShopClose\(\);\s*return 1;', frame) is not None)
finalize = body(text('game/SelectProfile.c'), 'static void SelectProfile_FinalizeAdventure(')
check('returning from a save selects the visible EXIT row',
      finalize.count('data.menuGreenLoadSave.rowSelected = AP_WumpaShopGreenExitRow(&data.menuGreenLoadSave);') == 2 and
      'return (menu != 0 && menu->rows == ap_wshop_green_rows) ? AP_WSHOP_GREEN_EXIT : 3;' in body(ui, 'int AP_WumpaShopGreenExitRow('))
check('the shop header reads COLLECTED',
      'AP_WumpaShopHeaderLine(line, (int)sizeof line, ap_net_wumpa_balance(), ap_net_wumpa_lifetime());' in ui and
      '"ON SALE AT %d COLLECTED"' in ui and 'BANKED' not in ui)
rect = text('game/RECTMENU.c')
check('the down arrow is beside the last row, clear of the footer',
      'DecalFont_DrawLine("v", 0x1e0, 0xb0, FONT_SMALL,' in ui)
state = body(rect, 'void RECTMENU_ProcessState()')
check('the shop page owns input like the tracker', 'if (AP_WumpaShopMenuFrame()) return;' in state)
check('the row label resolves', 'index == AP_LNG_WUMPA_SHOP' in rect)
check('the race HUD draws the current race total', 'AP_WumpaRaceCount(d)' in text('game/UI/UI_DrawNum.c'))
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
check('the block parser sits before the plan admission',
      cfg.index('#include "ap_wumpa_shop_parser.hpp"') < cfg.index('#include "ap_content_plan_impl.hpp"'))
check('the plan fills the shop through the block parser',
      'ap_wumpa_shop_parse(room, opt, 18, &shop)' in text('ap/ap_content_plan_impl.hpp'))
check('the shop page is a unity member after ap_wumpa.c',
      text('game/game_unity.h').index('ap_wumpa_shop_ui.c') > text('game/game_unity.h').index('ap_wumpa.c"'))
check('the shared wire fixture is present', (root / 'tools/fixtures/wumpa_shop.json').is_file())


ui = text('ap/ap_wumpa_shop_ui.c')
row = body(ui, 'static void AP_WShopDrawRow(')
check('the shop name column ends one font-measured character cell before the price column',
      'AP_WSHOP_RIGHT_X - AP_WSHOP_NAME_X - AP_WShopTextWidth(right, -1, 0) -' in row and
      'AP_WShopTextWidth(" ", 1, 0)' in row)
check('shop widths come from the engine font function, not a character count',
      'DecalFont_GetLineWidthStrlen((char *)s, len, FONT_SMALL)' in ui and 'strlen(name) > 34' not in row)
check('the selected row scrolls and the others clip',
      'DecalFont_DrawLineBox(name,' in row and 'selected ? TEXT_MARQUEE : TEXT_ELLIPSIS' in row)
check('the marquee restarts when the cursor moves or the page opens',
      ui.count('DecalFont_ResetLineBox(TEXT_SLOT_SHOP);') >= 2)

print('FAILED' if failures else 'all passed')
sys.exit(1 if failures else 0)
