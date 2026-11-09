"""Compile plan-owned pad glow, item-box counts and stage-2 display filtering.

Normal trial/cup enumeration was removed by one generator row 12. The plan
regressions retain exact physical ownership, checked suppression, bounds,
reward grouping and hidden stage-2 mode coverage.
"""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit
root=Path(__file__).resolve().parents[1]
source=ap_hooks_unit.read()
def extract(signature):
 start=source.index(signature+'\n{')
 return source[start:source.index('\n}\n',start)+3]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CTR_AP 1
#include "ap_seedcfg.h"
#include "ap_glow_slots_logic.h"
#include "ap_trial_pad_glow.h"
#include "ap_relic_perfect.h"
#include "ap_pad_glow_items.h"
#include "ap_content_plan.h"
#include "ap_content_modes.h"
static int planOn;static ctr_content_pad cpad;
static int lockedMode=-1;
int AP_ContentModeOpen(const ctr_content_pad *p,int mode){(void)p;return mode!=lockedMode;}
int AP_ContentPadOpen(const ctr_content_pad *p){(void)p;return 1;}
int ap_content_plan_active(void){return planOn;}
const ctr_content_pad *ap_content_plan_pad(int p){return planOn&&p==cpad.physical?&cpad:NULL;}
static long crystalChecked;
int ap_net_location_checked(long c){return c==crystalChecked;}
int ap_net_location_exists(long c){return (c>=35028400 && c<35028480)||(c>=35028300 && c<35028305);}
AP_CrystalChecks ctr_crystal_checks;
AP_CupSweepConfig ctr_cup_sweeps;
#define ADV_REWARD_FIRST_SAPPHIRE_RELIC 0x16
#define ADV_REWARD_FIRST_CTR_TOKEN 0x4c
#define AP_PODIUM_PSEUDO_BASE 0x100
ctr_seed_config ctr_cfg;
static long checked[128]; static int nChecked;
static int relics=1, rung=1, perfectOpen=0;
static int AP_PadBoxChecked(long code,void *ctx){int i;(void)ctx;for(i=0;i<nChecked;i++)if(checked[i]==code)return 1;return 0;}
int AP_TrialTrackConfigured(int d){return (d==16||d==17)&&ctr_cfg.trial_track_valid[d-16];}
int AP_PadUncollectedBits(int d,int *out,int cap){(void)cap;if(!relics)return 0;out[0]=d+ADV_REWARD_FIRST_SAPPHIRE_RELIC;return 1;}
static void AP_AppendTrackRungGlow(int t,int *out,int cap,int *n){(void)t;if(rung&&*n<cap)out[(*n)++]=AP_PODIUM_PSEUDO_BASE+t*CTR_CFG_PODIUM_RUNG_COUNT;}
#define AP_CORTEX_DEST 110
#define AP_CV_APPEND_TIERS 1
#define AP_CV_APPEND_RUNGS 2
#define AP_CV_APPEND_LETTERS 4
#define AP_CV_APPEND_WUMPA 8
#define AP_PAD_DISPLAY_BITS_MAX 256
#define AP_CV_PSEUDO_BASE 0x220
static int AP_PadBoxLive(long code,void *ctx){(void)code;(void)ctx;return 1;}
static int cortexValid=0;
static int AP_CortexDestValid(void){return cortexValid;}
static int slotsFor=15;
static int AP_PadTrackBoxSlots(int l){(void)l;return slotsFor;}
static int cupLegs[4]={0,1,2,3};
int ctr_cfg_active(void){return 1;}
static int AP_CortexPseudoRewardGroup(int b){(void)b;return -1;}
static int AP_CortexPadAppendOpen(const void *t,int m,int *o,int c,int n,int (*l)(long,void*),int (*k)(long,void*),void *x){
 int i;(void)t;(void)l;(void)k;(void)x;
 if(m&AP_CV_APPEND_LETTERS)for(i=0;i<3&&n<c;i++)o[n++]=AP_CV_PSEUDO_BASE+5+i;
 if(m&AP_CV_APPEND_WUMPA&&n<c)o[n++]=AP_CV_PSEUDO_BASE+8;
 return n;}
int AP_PadUncollectedRelicPerfectCount(int d){(void)d;return perfectOpen;}
static int servable=1;
int AP_TrialChallengeServable(int d){(void)d;return servable;}
int ctr_cfg_cup_displaced(int c){(void)c;return 0;}
int ctr_cfg_cup_leg(int c,int l){(void)c;return cupLegs[l];}
'''
tests=r'''
static int has(const int *b,int n,int bit){int i;for(i=0;i<n;i++)if(b[i]==bit)return 1;return 0;}
int main(void){
 int offBits[4];
 assert(AP_PadUncollectedGlowBits(3,offBits,4)==0);
 assert(AP_PadUncollectedDisplayBits(3,offBits,4)==0);
 /* #133 PR 4e: a content-plan pad lists its Item Box checks on the race
    slot, by its own codes; the legacy appends (which key on a retail LevelID
    the physical pad is not) and the retail box count stay out. */
 {
  int bits[AP_PAD_DISPLAY_BITS_MAX],n,i,slots3[3],phase,boxes=0;
  planOn=1;memset(&cpad,0,sizeof cpad);nChecked=0;
  cpad.physical=3;cpad.occupied=1;cpad.mode_count=1;cpad.modes[0]=AP_CONTENT_MODE_TROPHY;cpad.stages[0]=1;
  cpad.trophy_location=35014990; /* an unchecked Trophy code */
  cpad.box_count=8;for(i=0;i<8;i++)cpad.box_locations[i]=35201620+i;
  ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_PER_TRACK;
  n=AP_PadUncollectedGlowBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==1+8&&bits[0]==AP_CONTENT_BIT_TROPHY+3);
  for(i=1;i<n;i++){int ph,sl;assert(ap_content_box_bit_decode(bits[i],&ph,&sl)&&ph==3&&sl==i-1);
   assert(AP_GlowBitRewardGroup(bits[i])==0);boxes++;}
  assert(boxes==8);
  assert(AP_PadUncollectedDisplayBits(3,bits,AP_PAD_DISPLAY_BITS_MAX)==n); /* no retail boxes, letters or Wumpa of LevelID 3 */
  for(phase=0;phase<16;phase++){AP_GlowSlots_Select(bits,n,phase,1,AP_GlowBitRewardGroup,slots3);
   assert(slots3[1]<0&&slots3[2]<0&&slots3[0]>=0);}
  assert(AP_PadUncollectedBoxCount(3)==8);
  /* the glow reads ap_net_location_checked, the count AP_PadBoxChecked */
  checked[nChecked++]=35201621;crystalChecked=35201621;
  n=AP_PadUncollectedGlowBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1+7);
  for(i=0;i<n;i++)assert(bits[i]!=AP_CONTENT_BIT_BOX+3*AP_CONTENT_BOX_STRIDE+1);
  crystalChecked=0;
  assert(AP_PadUncollectedBoxCount(3)==7);
  assert(AP_PadUncollectedGlowBits(3,bits,4)==4);
  assert(AP_PadUncollectedBoxCount(4)==0); /* a pad the plan does not know */
  cpad.box_count=0;assert(AP_PadUncollectedGlowBits(3,bits,AP_PAD_DISPLAY_BITS_MAX)==1&&AP_PadUncollectedBoxCount(3)==0);
  planOn=0;nChecked=0;ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_GLOBAL;
 }
 /* Decision 26: a content-plan mode still locked behind stage 2 stays out of the glow. */
 {
  int bits[AP_PAD_DISPLAY_BITS_MAX],n;
  planOn=1;memset(&cpad,0,sizeof cpad);nChecked=0;crystalChecked=0;
  cpad.physical=3;cpad.occupied=1;cpad.mode_count=2;
  cpad.modes[0]=AP_CONTENT_MODE_TROPHY;cpad.stages[0]=1;
  cpad.modes[1]=AP_CONTENT_MODE_RELIC;cpad.stages[1]=2;
  cpad.trophy_location=35014990;cpad.relic_locations[0]=35014991;cpad.relic_locations[1]=35014992;cpad.relic_locations[2]=35014993;
  lockedMode=-1;
  n=AP_PadUncollectedGlowBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(has(bits,n,AP_CONTENT_BIT_TROPHY+3)&&has(bits,n,AP_CONTENT_BIT_RELIC+3*3)&&has(bits,n,AP_CONTENT_BIT_RELIC+3*3+2));
  n=AP_PadUncollectedDisplayBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(has(bits,n,AP_CONTENT_BIT_TROPHY+3)&&has(bits,n,AP_CONTENT_BIT_RELIC+3*3));
  lockedMode=AP_CONTENT_MODE_RELIC;
  n=AP_PadUncollectedDisplayBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==1&&bits[0]==AP_CONTENT_BIT_TROPHY+3);
  /* the pad state and the tracker count every open location, locked or not */
  n=AP_PadUncollectedGlowBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==4&&has(bits,n,AP_CONTENT_BIT_RELIC+3*3));
  lockedMode=AP_CONTENT_MODE_TROPHY;
  n=AP_PadUncollectedDisplayBits(3,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(!has(bits,n,AP_CONTENT_BIT_TROPHY+3)&&has(bits,n,AP_CONTENT_BIT_RELIC+3*3));
  lockedMode=-1;planOn=0;
 }
 puts("Content-plan glow production gather passed");
}
'''
signatures=['int AP_PadUncollectedBoxCount(int destLevelID)',
 'static int AP_GlowPush(int *outBits, int64_t *outCodes, int count, int bit, int64_t code)',
 'static int AP_ContentPadGlowBits(int destLevelID, int *outBits, int64_t *outCodes, int cap, int openOnly)',
 'int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap)',
 'int AP_PadUncollectedDisplayBits(int destLevelID, int *outBits, int cap)',
 'static int AP_GlowBitRewardGroup(int globalBit)']
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'fixture.c';exe=Path(tmp)/'fixture'
 src.write_text(fixture+'\n'.join(extract(s) for s in signatures)+tests)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined','-I',str(root/'ap'),'-I',str(root/'include'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

# The glow pass reads the display enumeration; the pad state, the tracker and the
# tier picker keep the state enumeration, so nothing is counted twice.
warp=(root/'game/232/AH_WarpPad.c').read_text()
tracker=(root/'ap/ap_tracker.c').read_text()
state=extract('int AP_PadState(int physLevelID, int destLevelID)')
assert 'AP_PadUncollectedDisplayBits(warppadObj->levelID, apUncBits,' in warp
assert 'int apUncBits[AP_PAD_DISPLAY_BITS_MAX];' in warp
assert 'AP_PadUncollectedDisplayBits' not in tracker and 'AP_PadUncollectedDisplayBits' not in state
assert 'AP_PadUncollectedGlowBits(physLevelID,' in state and 'AP_ContentTrackPadState(pad)' in state
header=(root/'ap/ap_hooks.h').read_text()
assert '#define AP_PAD_DISPLAY_BITS_MAX 256' in header
