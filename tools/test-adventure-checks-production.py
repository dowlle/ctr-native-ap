"""Drive production sender families through retail mode flags and stale bits.

Each CI command selects a family. Real emitter and mode policy are compiled;
network, identity lookup and presentation are deterministic test seams.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
hooks = ap_hooks_unit.read()


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


family = sys.argv[1]
fixture = r'''
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "ap/ap_seedcfg.h"
#include "ap/ap_class_check_policy.h"
#include "ap/ap_wumpa_dispatch.h"
#include "ap/ap_itemsanity_logic.h"
#include "ap/ap_boss_won_flags.h"
#include "ap/ap_relic_perfect.h"
#include "ap/ap_content_plan.h"
#include "ap/ap_content_modes.h"
#include "ap/ap_cortex_track.h"
#include "ap/ap_checkdiag_once.h"
struct Driver { int numWumpas,numTimeCrates; };
struct GameTracker { unsigned gameMode1,gameMode2; int levelID,numPlyrCurrGame,timeCratesInLEV,podiumRewardID; struct Driver *drivers[1]; };
static struct { struct GameTracker *gGT; } data, *sdata=&data;
ctr_seed_config ctr_cfg;
static struct GameTracker gt;
static struct Driver local;
static int sent, perf, feed, ledger, ap_state_gen;
static unsigned ap_notified_mask[6];
static unsigned char ap_itemsanity_owned[AP_ITEMSANITY_WEAPON_COUNT];
static int ap_oxide_first_beaten, ap_oxide_final_beaten;
static int goalEvents;
static int cortexActive;
static ap_checkdiag_once_state ap_checkdiag_once;
int ctr_cfg_active(void) { return 1; }
int ap_net_is_connected(void) { return 1; }
int ap_net_location_checked(long long code) { (void)code; return 0; }
int ap_net_location_exists(long long code) { return code > 0; }
void ap_net_send_location(long long code) { assert(code > 0); sent++; }
void AP_PerfNoteCheckSent(void) { perf++; }
static void AP_AppendLog(const char *s) { (void)s; }
static void AP_FeedOnLocationSent(long code) { (void)code; feed++; }
static void AP_CeremonyLedgerAdd(long code, int bit, int tag) { (void)code;(void)bit;(void)tag;ledger++; }
static void AP_FeedOnRungSent(long code, int tag) { (void)code;(void)tag; }
static long AP_LookupLocationCode(int bit) { return 35000000 + bit; }
static const char *AP_TrophyName(int bit) { (void)bit; return "reward"; }
static void AP_SendPodiumChecks(int track, int position) { (void)track;(void)position; }
unsigned ap_net_boss_won_bits(void) { return 0; }
void ap_net_boss_won_record(unsigned bit) { (void)bit; goalEvents++; }
static void AP_GoalArmLiveEvent(void) { goalEvents++; }
static void AP_EvaluateGoal(void) { goalEvents++; }
int AP_RaceAttempt_ProducerBlocked(int p) { (void)p; return 0; }
long AP_LetterLocation(int track,int letter) { (void)track;(void)letter;return 35000100; }
static int AP_CortexLetterTrack(int track) { (void)track;return 0; }
static int AP_CustomLetterSlot(int track) { (void)track;return 1; }
int AP_LetterTokenEarned(int track,int win,int collected) { (void)track;return win && collected==3; }
int AP_CortexTrackActive(void) { return cortexActive; }
long AP_CortexTrackCode(int slot) { return AP_CortexSlotCode(&ctr_cfg.cortex_track,slot); }
static int AP_RelicPerfectServingCustom(struct GameTracker *g) { (void)g;return 0; }
static long AP_RelicPerfectCustomCode(int slot) { (void)slot;return -1; }
long AP_TrialTrackLocation(int level,int challenge) { (void)level;(void)challenge;return 35000100; }
static void AP_WumpaGatherFacts(struct GameTracker *g, struct AP_WumpaDispatchFacts *f) {
 memset(f,0,sizeof *f); f->wumpa=&ctr_cfg.wumpa;f->destLevelID=g->levelID;
}
#define AP_LETTER_TOAST_SENT_ITEM 1
#define AP_GOAL_BIT_OXIDE_FIRST 115
#define AP_GOAL_BIT_OXIDE_SECOND 116
#define ADV_REWARD_FIRST_TROPHY 6
#define ADV_REWARD_FIRST_SAPPHIRE_RELIC 22
#define ADV_REWARD_FIRST_BOSS_KEY 94
#define AP_RESULT_PRODUCER_ADV_REWARD 1
#define AP_RESULT_PRODUCER_GOAL 2
#define AP_RESULT_PRODUCER_TRIAL_TRACK 3
#define AP_RESULT_PRODUCER_RELIC_PERFECT 4
#define AP_TRIAL_PODIUM_LOGICAL_BASE 48
#define TOKEN_RACE 8
#define AP_RESULT_PRODUCER_CUP_AGGREGATE 5
#define AP_RESULT_PRODUCER_CORTEX_RACE 6
#define STATIC_TROPHY 62
'''
modes = (root/'include/namespace_Main.h').read_text()
start = modes.index('enum GameMode1\n')
fixture = modes[start:modes.index('};', start)+2] + fixture
fixture += function(hooks, 'int AP_LocationChecksAllowed(void)')
fixture += function(hooks, 'static int AP_EmitClassCheck(long code,')

calls = {
    'wumpa': ('void AP_WumpaReachedTen(', '{ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_GLOBAL;AP_WumpaReachedTen(&local);ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_PER_TRACK;AP_WumpaReachedTen(&local);}'),
    'letters': ('void AP_LetterCollected(', 'AP_LetterCollected(3,0);'),
    'boxes': ('void AP_EmitBoxCheck(', 'AP_EmitBoxCheck(3,0,35000100);'),
    'hit': ('int AP_EmitHitCharacterCheck(', 'AP_EmitHitCharacterCheck(35000100);'),
    'itemsanity': ('void AP_ItemsanityOnUse(', 'AP_ItemsanityOnUse(&local,0);'),
    'rewards': ('static void AP_NotifyAdvRewardImpl(', '{int bits[]={6,22,40,58,76,94,98,106,111};for(unsigned b=0;b<sizeof bits/sizeof *bits;b++)AP_NotifyAdvRewardImpl(bits[b]);}'),
    'goal': ('void AP_NotifyGoal(', 'AP_NotifyGoal(0);'),
    'results': ('void AP_NotifyTrialTrackRace(', 'AP_NotifyTrialTrackRace(16,CTR_CFG_TRIAL_TROPHY);'),
    'perfect': ('void AP_NotifyRelicPerfect(', 'AP_NotifyRelicPerfect();'),
    'custom': ('static void AP_NotifyCustomTrackCtrImpl(', 'AP_NotifyCustomTrackCtrImpl(1,3);'),
    'cup': ('int AP_ContentCupFinish(', 'AP_ContentCupFinish(&gt,1);'),
    'cortex': ('void AP_NotifyCortexTrackRace(', 'AP_NotifyCortexTrackRace(0);AP_NotifyCortexTrackRace(1);'),
    'podium': ('static void AP_EmitRung(', 'AP_EmitRung(3,35000100,0,1,"held");AP_EmitRung(3,35000101,3,1,"finish");'),
}
if family == 'itemsanity':
    fixture += 'static int AP_ItemsanityActive(void) { return 1; }\n'
if family == 'goal':
    fixture += function(hooks, 'static void AP_NotifyAdvRewardImpl(')
    fixture += function(hooks, 'void AP_NotifyAdvReward(')
if family == 'cup':
    fixture += r'''
static ctr_content_pad pad;
static AP_CupSweepAttempt ap_content_cup_attempt;
static const ctr_content_pad *AP_ContentCupPad(void) { return &pad; }
static int AP_ContentGarageCupRunning(void) { return -1; }
void AP_NotifyCupAggregateReward(int bit) { (void)bit;assert(0); }
void AP_SkipPodium(int model) { (void)model; }
'''
if family == 'shop':
    fixture += r'''
static struct { int hub,cursor; long long buying; } ap_wshop;
AP_WumpaShopConfig ctr_wumpa_shop;
static void AP_WShopSay(const char *s) { (void)s; }
int ap_net_wumpa_buy(long long code,int price,int unlock) { (void)code;(void)price;(void)unlock;sent++;return 0; }
void OtherFX_Play(unsigned int id,int flags) { (void)id;(void)flags; }
'''
    fixture += function((root/'ap/ap_wumpa_shop_ui.c').read_text(), 'static void AP_WShopBuy(void)')
    call = 'AP_WShopBuy();'
else:
    signature, call = calls[family]
    if signature:
        fixture += function(hooks, signature)

fixture += r'''
int main(void) {
 unsigned rejected[] = {0, ARCADE_MODE, TIME_TRIAL, BATTLE_MODE,
   ARCADE_MODE|RELIC_RACE, TIME_TRIAL|RELIC_RACE, BATTLE_MODE|RELIC_RACE,
   ARCADE_MODE|ADVENTURE_MODE, TIME_TRIAL|ADVENTURE_MODE, BATTLE_MODE|ADVENTURE_MODE};
 sdata->gGT=&gt;gt.drivers[0]=&local;gt.levelID=3;gt.numPlyrCurrGame=1;local.numWumpas=10;
 memset(ap_itemsanity_owned,1,sizeof ap_itemsanity_owned);
 ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_GLOBAL;ctr_cfg.wumpa.global_code=35016100;
 ctr_cfg.wumpa.tracks[3]=35016101;
 ctr_cfg.custom_ctr_enabled=1;ctr_cfg.custom_ctr_location=35000100;gt.gameMode2=TOKEN_RACE;
 ctr_cfg.relic_perfect_enabled=1;ctr_cfg.relic_perfect[3]=35000100;
 local.numTimeCrates=gt.timeCratesInLEV=10;
'''
if family == 'shop':
    fixture += 'ctr_wumpa_shop.state=AP_WUMPA_SHOP_VALID;ctr_wumpa_shop.slots=1;\n'
if family == 'cup':
    fixture += 'pad.trophy_location=35000100;pad.sweep_location=35000101;\n'
if family == 'cortex':
    fixture += 'cortexActive=1;ctr_cfg.cortex_track.valid=1;ctr_cfg.cortex_track.trophy=35000100;ctr_cfg.cortex_track.ctr_token=35000101;\n'
fixture += 'for (unsigned i=0;i<sizeof rejected/sizeof *rejected;i++) {\n'
fixture += 'gt.gameMode1=rejected[i];' + call
fixture += r'''
 assert(sent==0 && perf==0 && feed==0 && ledger==0 && goalEvents==0);
 for(int w=0;w<6;w++)assert(ap_notified_mask[w]==0);
 }
 // Versus has no dedicated bit and may inherit the relic flag.
 for (int players=2;players<=4;players++) {
  gt.numPlyrCurrGame=players;gt.gameMode1=RELIC_RACE;
'''
fixture += call + '\nassert(sent==0);\n}\ngt.numPlyrCurrGame=1;\n'
fixture += r'''
 // A refused crossing must not consume next Adventure race's check.
 gt.gameMode1=ADVENTURE_MODE;
'''
if family == 'shop':
    fixture += 'gt.gameMode1|=ADVENTURE_ARENA;\n'
if family == 'perfect':
    fixture += 'gt.gameMode1|=RELIC_RACE;\n'
fixture += call + '\nassert(sent>0);\n'
if family not in ('shop', 'rewards', 'goal'):
    fixture += 'sent=0;gt.gameMode1=RELIC_RACE;' + call + '\nassert(sent>0);\n'
fixture += 'return 0;\n}\n'
with tempfile.TemporaryDirectory() as tmp:
    source, exe = Path(tmp)/'sender.c', Path(tmp)/'sender'
    source.write_text(fixture)
    subprocess.run(['cc', '-std=gnu99', '-DCTR_AP', '-I', str(root), '-I', str(root/'include'),
                    '-I', str(root/'ap'), '-fsanitize=undefined', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(f'{family}: production mode refusals and subsequent Adventure acceptance passed')
