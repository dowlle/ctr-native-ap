"""Trial pad entry route agrees with the pad state (2026-10-09).

A Slide Coliseum player had the CTR Challenge checked by a Collect while its
letters were still open. AP_PadState kept the pad open for the letters, but the
trial entry route only looked at the CTR Challenge location, decided Done, and
held the kart in the warp beam forever.

This compiles the production gather (AP_PadState, AP_PadUncollectedLetterCount,
AP_TrialChallengeServable and the trial location helpers from ap/ap_hooks.c)
with the shared decision header, and pins:
  1. CTR Challenge checked, letters open -> the entry route is the CTR Challenge;
  2. letters open plus a relic tier open -> the token/relic menu;
  3. everything done -> the pad is Done (hard-locked), never an open pad with a
     Done route;
  4. refused letter assets -> letters and the CTR Challenge leave the pad state
     as well as the route, so state and route still agree;
  5. for every combination of the trial inputs, an open pad (not state 5) has an
     entry route that is not Done, unless only a Hit opportunity is left (the
     Hit chooser loads the plain rerace for it before the route is decided);
  6. the engine glue: every Done route in AH_WarpPad.c goes to the entry
     fail-safe, which releases the kart, and the trial route uses the shared
     token-side helper.
"""
from pathlib import Path
import itertools
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'ap/ap_hooks.c').read_text()
warp = (root / 'game/232/AH_WarpPad.c').read_text()


def extract(signature):
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}\n', start) + 3]


fixture = r'''
#include <assert.h>
#include <stdio.h>
#define CTR_AP 1
#include "ap_seedcfg.h"
#include "ap_pad_state.h"
#define ADV_REWARD_FIRST_TROPHY 0
#define AP_CORTEX_DEST 110
enum { AP_CV_SLOT_TROPHY = 0, AP_CV_SLOT_LETTER0 = 5 };
ctr_seed_config ctr_cfg;
static long checked[16]; static int nChecked;
static int assetsReady = 1, prepareCalls = 0;
static int relicTiers = 0, perfect = 0, boxes = 0, wumpa = 0, hit = 0;
int ctr_cfg_active(void){return 1;}
int ap_net_location_exists(long long c){return c > 0;}
int ap_net_location_checked(long long c){int i;for(i=0;i<nChecked;i++)if(checked[i]==c)return 1;return 0;}
int AP_TrialLetters_Prepare(void){prepareCalls++;return assetsReady;}
static int AP_PadBoxLive(long c,void *x){(void)x;return c>0;}
static int AP_PadBoxChecked(long c,void *x){(void)x;return ap_net_location_checked(c);}
static int AP_CortexOpenCount(const void *t,int s,int n,int (*l)(long,void*),int (*k)(long,void*),void *x){(void)t;(void)s;(void)n;(void)l;(void)k;(void)x;return 0;}
static int AP_CortexDestValid(void){return 0;}
static long AP_CortexTrackCode(int s){(void)s;return -1;}
int AP_CortexTrackChecked(int s){(void)s;return 0;}
int AP_LocationCheckedByBit(int b){(void)b;return 0;}
int AP_PadStage1Met(int p){(void)p;return 1;}
int ctr_cfg_racer_lock_met(int p){(void)p;return 1;}
int ctr_cfg_warp_stage2_unlocked(int p){(void)p;return 1;}
int AP_PadUncollectedBoxCount(int d){(void)d;return boxes;}
int AP_PadUncollectedWumpaCount(int d){(void)d;return wumpa;}
int AP_PadUncollectedRelicPerfectCount(int d){(void)d;return perfect;}
int AP_HitPadOpportunity(int p,int d){(void)p;(void)d;return hit;}
int AP_ItemsanityPadOpportunity(int d){(void)d;return 0;}
int AP_TrialChallengeServable(int destLevelID);
int AP_TrialTrackLocationChecked(int levelID, int challenge);
/* Glow stub with the production trial gate (pinned in
 * tools/test-trial-pad-glow-production.py): relic tiers, the Trophy, and the
 * CTR Challenge only while it is servable. */
int AP_PadUncollectedGlowBits(int d,int *out,int cap){
 int n=relicTiers;(void)out;(void)cap;
 if(!AP_TrialTrackLocationChecked(d,CTR_CFG_TRIAL_TROPHY))n++;
 if(AP_TrialChallengeServable(d)&&!AP_TrialTrackLocationChecked(d,CTR_CFG_TRIAL_CTR))n++;
 return n;}
'''

tests = r'''
/* The trial branch of AH_WarpPad_ThTick after its Trophy is checked, minus the
 * engine glue. Returns the route, or -1 when the Hit chooser takes a plain
 * rerace first (AP_HIT_CHOOSER_PLAIN). */
static int trialRoute(int d){
 int tokenLeft=AP_TrialTokenSideLeft(AP_TrialChallengeServable(d),
     !AP_TrialTrackLocationChecked(d,CTR_CFG_TRIAL_CTR),AP_PadUncollectedLetterCount(d));
 int relicLeft=AP_PadRelicSideLeft(relicTiers>0,perfect);
 int route=AP_PadTier2RouteDecide(tokenLeft,relicLeft,boxes+wumpa);
 if(route==AP_PAD_TIER2_DONE&&hit)return -1;
 return route;}
static void setChecked(int d,int trophy,int ctr,int lettersOpen){
 int t=d-16,i;nChecked=0;
 if(trophy)checked[nChecked++]=ctr_cfg.trial_track_locations[t][0];
 if(ctr)checked[nChecked++]=ctr_cfg.trial_track_locations[t][1];
 for(i=lettersOpen;i<3;i++)checked[nChecked++]=ctr_cfg.lettersanity_locations[d][i];}
int main(void){
 int d;
 for(d=16;d<=17;d++){
  int t=d-16,i;
  ctr_cfg.trial_track_valid[t]=1;ctr_cfg.trial_track_mode[t]=2;
  ctr_cfg.trial_track_locations[t][0]=35015000+t*2;ctr_cfg.trial_track_locations[t][1]=35015001+t*2;
  for(i=0;i<3;i++)ctr_cfg.lettersanity_locations[d][i]=35012600+d*3+i;
  assetsReady=1;relicTiers=perfect=boxes=wumpa=hit=0;

  /* 1. Bethany's case: Trophy won, CTR Challenge collected, letters open. */
  setChecked(d,1,1,2);
  assert(AP_PadState(d,d)==4);
  assert(AP_PadUncollectedLetterCount(d)==2);
  assert(trialRoute(d)==AP_PAD_TIER2_TOKEN);
  /* the route the old code took for the same inputs */
  assert(AP_PadTier2RouteDecide(0,0,0)==AP_PAD_TIER2_DONE);

  /* 2. letters and a relic tier open -> menu; relic only -> relic */
  relicTiers=1;assert(trialRoute(d)==AP_PAD_TIER2_MENU);
  setChecked(d,1,1,0);assert(trialRoute(d)==AP_PAD_TIER2_RELIC);relicTiers=0;

  /* 3. all done -> Done state, so the entry route is never consulted */
  assert(AP_PadState(d,d)==5);

  /* CTR Challenge open, letters done -> token, as before */
  setChecked(d,1,0,0);assert(trialRoute(d)==AP_PAD_TIER2_TOKEN);

  /* 4. refused assets: letters and the CTR Challenge leave the state too */
  assetsReady=0;setChecked(d,1,1,3);
  assert(AP_PadUncollectedLetterCount(d)==0);
  assert(AP_PadState(d,d)==5);
  relicTiers=1;assert(AP_PadState(d,d)==4&&trialRoute(d)==AP_PAD_TIER2_RELIC);relicTiers=0;
  /* Trophy-only mode: same */
  assetsReady=1;ctr_cfg.trial_track_mode[t]=1;
  assert(AP_PadUncollectedLetterCount(d)==0&&AP_PadState(d,d)==5);
  ctr_cfg.trial_track_mode[t]=2;
  /* no CTR Challenge location placed: same */
  ctr_cfg.trial_track_locations[t][1]=-1;
  assert(AP_PadUncollectedLetterCount(d)==0&&AP_PadState(d,d)==5);
  ctr_cfg.trial_track_locations[t][1]=35015001+t*2;

  /* 5. every combination: an open pad always has a route that loads */
  {
   int a,c,l,r,p,b,w,h;
   for(a=0;a<2;a++)for(c=0;c<2;c++)for(l=0;l<4;l++)for(r=0;r<2;r++)
   for(p=0;p<2;p++)for(b=0;b<2;b++)for(w=0;w<2;w++)for(h=0;h<2;h++){
    int st;
    assetsReady=a;relicTiers=r;perfect=p;boxes=b;wumpa=w;hit=h;
    setChecked(d,1,c,l);
    st=AP_PadState(d,d);
    if(st!=5)assert(trialRoute(d)!=AP_PAD_TIER2_DONE);
    else assert(trialRoute(d)==AP_PAD_TIER2_DONE);
   }
  }
  assetsReady=1;relicTiers=perfect=boxes=wumpa=hit=0;
 }
 puts("Trial pad entry route agrees with pad state");
 return 0;
}
'''

signatures = [
    'static long AP_TrialTrackLocation(int levelID, int challenge)',
    'int AP_TrialTrackConfigured(int levelID)',
    'int AP_TrialTrackLocationChecked(int levelID, int challenge)',
    'int AP_TrialChallengeServable(int destLevelID)',
    'int AP_PadUncollectedLetterCount(int destLevelID)',
    'static int AP_DestIsRace(int destLevelID)',
    'static int AP_DestTrophyChecked(int destLevelID)',
    'static int AP_DestKnown(int destLevelID)',
    'int AP_PadState(int physLevelID, int destLevelID)',
]
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'fixture.c'
    exe = Path(tmp) / 'fixture'
    src.write_text(fixture + '\n'.join(extract(s) for s in signatures) + tests)
    subprocess.run(['cc', '-std=c11', '-Wall', '-fsanitize=undefined', '-I', str(root / 'ap'),
                    '-I', str(root / 'include'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# 6. Engine glue. The trial route takes the token side from the shared helper,
# and no AP entry route parks the kart on a Done decision any more.
trial = warp[warp.index('// Slide Col or Turbo Track'):warp.index('// Battle Tracks')]
assert 'AP_TrialTokenSideLeft(' in trial
assert 'AP_TrialChallengeServable(levelID)' in trial
assert 'AP_PadUncollectedLetterCount(levelID)' in trial
assert 'AP_TrialLetters_Prepare()' not in trial
assert 'AP_PadLogRoute(levelID, levelID' not in trial
assert len(re.findall(r'goto WarpPad_RefuseEntry;', warp)) == 3
refuse = warp[warp.index('\nWarpPad_RefuseEntry:'):warp.index('\nWarpPad_TrophyAnimateOnly:')]
for needed in ('warppadObj->boolEnteredWarppad = 0;', 'warppadObj->framesWarping = 0;',
               'AH_WarpPad_WarpRestore(gGT);', 'VehPhysProc_Driving_Init',
               'apEntryRefusedDest = warppadObj->levelID;', 'AP_PadLogEntryRefused('):
    assert needed in refuse, needed
assert 'defensive: Done is hard-locked upstream' not in warp
print('Trial pad entry glue checks passed')
