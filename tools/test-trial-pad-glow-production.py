"""Issue #343: compile the actual pad glow gather and prize-slot grouping.

Also the 2026-09-30 display enumeration (boxes, letters, Wumpa on every pad).

Slide Coliseum / Turbo Track pads used to enumerate only relic tiers and
podium rungs, so their Trophy and CTR Challenge items never appeared.
"""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'ap/ap_hooks.c').read_text()
def extract(signature):
 start=source.index(signature+'\n{')
 return source[start:source.index('\n}\n',start)+3]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#define CTR_AP 1
#include "ap_seedcfg.h"
#include "ap_glow_slots_logic.h"
#include "ap_trial_pad_glow.h"
#include "ap_relic_perfect.h"
#include "ap_pad_glow_items.h"
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
#define AP_PAD_DISPLAY_BITS_MAX 96
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
 int d;
 for(d=16;d<=17;d++){
  int t=d-16,bits[24],n,slot[3],phase,ctrShown=0,trophyShown=0;
  int trophy=AP_TrialPseudoBit(t,CTR_CFG_TRIAL_TROPHY),ctr=AP_TrialPseudoBit(t,CTR_CFG_TRIAL_CTR);
  ctr_cfg.podium_enabled=1;ctr_cfg.trial_track_valid[t]=1;
  ctr_cfg.trial_track_locations[t][0]=35015000+t*2;ctr_cfg.trial_track_locations[t][1]=35015001+t*2;
  nChecked=0;relics=1;rung=1;
  n=AP_PadUncollectedGlowBits(d,bits,24);
  assert(n==4&&has(bits,n,trophy)&&has(bits,n,ctr));
  assert(AP_GlowBitRewardGroup(trophy)==0&&AP_GlowBitRewardGroup(ctr)==2);
  for(phase=0;phase<8;phase++){AP_GlowSlots_Select(bits,n,phase,1,AP_GlowBitRewardGroup,slot);
   if(slot[2]==ctr)ctrShown=1; if(slot[0]==trophy)trophyShown=1; assert(slot[1]==d+ADV_REWARD_FIRST_SAPPHIRE_RELIC);}
  assert(ctrShown&&trophyShown);
  /* Trophy-only mode, Trophy checked, then everything checked */
  ctr_cfg.trial_track_locations[t][1]=-1;n=AP_PadUncollectedGlowBits(d,bits,24);
  assert(n==3&&has(bits,n,trophy)&&!has(bits,n,ctr));
  ctr_cfg.trial_track_locations[t][1]=35015001+t*2;checked[0]=35015000+t*2;nChecked=1;
  n=AP_PadUncollectedGlowBits(d,bits,24);assert(n==3&&!has(bits,n,trophy)&&has(bits,n,ctr));
  checked[1]=35015001+t*2;nChecked=2;relics=0;rung=0;
  assert(AP_PadUncollectedGlowBits(d,bits,24)==0);
  /* podium off still lists the trial checks; unconfigured trial lists none */
  nChecked=0;ctr_cfg.podium_enabled=0;n=AP_PadUncollectedGlowBits(d,bits,24);
  assert(n==2&&has(bits,n,trophy)&&has(bits,n,ctr));
  /* 2026-10-09: an unservable CTR Challenge (refused letter assets) is not
     advertised, so the pad cannot stay open for a race it never offers. */
  servable=0;n=AP_PadUncollectedGlowBits(d,bits,24);
  assert(n==1&&has(bits,n,trophy)&&!has(bits,n,ctr));servable=1;
  ctr_cfg.trial_track_valid[t]=0;assert(AP_PadUncollectedGlowBits(d,bits,24)==0);
 }
 /* #439 follow-up: an open Relic Race Perfect is advertised on its pad, in the
    relic slot, right after the tier bit; only while open. */
 for(d=0;d<18;d+=d<3?3:(d==3?12:1)){
  int bits[24],n,slot[3],phase,shown=0,pb=AP_RelicPerfectPseudoBit(d);
  ctr_cfg.podium_enabled=0;ctr_cfg.trial_track_valid[0]=ctr_cfg.trial_track_valid[1]=0;
  relics=1;rung=0;perfectOpen=1;
  n=AP_PadUncollectedGlowBits(d,bits,24);
  assert(n==2&&bits[0]==d+ADV_REWARD_FIRST_SAPPHIRE_RELIC&&bits[1]==pb);
  assert(AP_RelicPerfectPseudoLevel(pb)==d&&AP_GlowBitRewardGroup(pb)==1);
  for(phase=0;phase<8;phase++){AP_GlowSlots_Select(bits,n,phase,1,AP_GlowBitRewardGroup,slot);
   if(slot[1]==pb)shown=1;}
  assert(shown);
  /* the perfect alone still shows, in the relic slot and in the one pile */
  relics=0;n=AP_PadUncollectedGlowBits(d,bits,24);assert(n==1&&bits[0]==pb);
  AP_GlowSlots_Select(bits,n,0,1,AP_GlowBitRewardGroup,slot);assert(slot[1]==pb&&slot[0]<0&&slot[2]<0);
  AP_GlowSlots_Select(bits,n,0,0,AP_GlowBitRewardGroup,slot);assert(slot[0]==pb);
  /* checked, off or not in the seed: nothing extra */
  perfectOpen=0;assert(AP_PadUncollectedGlowBits(d,bits,24)==0);
 }
 assert(AP_RelicPerfectPseudoLevel(AP_RELIC_PERFECT_PSEUDO_BASE-1)==-1);
 assert(AP_RelicPerfectPseudoLevel(AP_RELIC_PERFECT_PSEUDO_BASE+18)==-1);
 assert(AP_RELIC_PERFECT_PSEUDO_BASE>=AP_CV_PSEUDO_BASE+14);

 /* 2026-09-30: the display enumeration lists every open location. */
 {
  int bits[AP_PAD_DISPLAY_BITS_MAX],base[AP_PAD_DISPLAY_BITS_MAX],n,nb,i,slot,leg,boxes,letters,wumpa,slots3[3],phase;
  ctr_cfg.podium_enabled=0;ctr_cfg.trial_track_valid[0]=ctr_cfg.trial_track_valid[1]=0;
  relics=1;rung=0;perfectOpen=0;nChecked=0;cortexValid=0;slotsFor=15;
  ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_PER_TRACK;
  for(i=0;i<18;i++){ctr_cfg.wumpa.tracks[i]=35016100+i;
   for(slot=0;slot<3;slot++)ctr_cfg.lettersanity_locations[i][slot]=35013000+i*3+slot;}
  /* race pad: 1 tier + 15 boxes + 3 letters + wumpa; the state enumeration is untouched */
  d=3;
  nb=AP_PadUncollectedGlowBits(d,base,AP_PAD_DISPLAY_BITS_MAX);assert(nb==1);
  n=AP_PadUncollectedDisplayBits(d,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==1+15+3+1&&bits[0]==base[0]);
  for(i=0;i<nb;i++)assert(!AP_PadGlowIsItemBit(base[i]));
  boxes=letters=wumpa=0;
  for(i=0;i<n;i++){if(AP_PadGlowBoxCode(bits[i])>=0)boxes++;
   {int l,c;if(AP_PadGlowLetterDecode(bits[i],&l,&c)){letters++;assert(l==d);}}
   if(AP_PadGlowWumpaLevel(bits[i])==d)wumpa++;}
  assert(boxes==15&&letters==3&&wumpa==1);
  for(i=0;i<n;i++)for(slot=i+1;slot<n;slot++)assert(bits[i]!=bits[slot]);
  /* by_reward_type: race slot cycles tier trophy-less list boxes+wumpa, token slot letters */
  for(phase=0;phase<40;phase++){
   AP_GlowSlots_Select(bits,n,phase,1,AP_GlowBitRewardGroup,slots3);
   assert(slots3[1]==base[0]);
   assert(AP_PadGlowBoxCode(slots3[0])>=0||AP_PadGlowWumpaLevel(slots3[0])>=0);
   assert(AP_PadGlowLetterDecode(slots3[2],0,0));}
  /* checked locations drop out; all checked and no relic -> nothing */
  checked[0]=AP_PadGlowItemCode(&ctr_cfg,AP_PadGlowWumpaBit(d));nChecked=1;
  for(slot=0;slot<15;slot++)checked[nChecked++]=AP_BoxMap_Code(d,slot);
  for(slot=0;slot<3;slot++)checked[nChecked++]=35013000+d*3+slot;
  n=AP_PadUncollectedDisplayBits(d,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1);
  nChecked=0;relics=0;ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_GLOBAL;
  for(slot=0;slot<15;slot++)checked[nChecked++]=AP_BoxMap_Code(d,slot);
  n=AP_PadUncollectedDisplayBits(d,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==3);for(i=0;i<n;i++)assert(AP_PadGlowLetterDecode(bits[i],0,0)); /* wumpa off, boxes checked */
  ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_PER_TRACK;relics=1;nChecked=0;
  /* boxes without geometry are not listed (same predicate as the count) */
  slotsFor=4;n=AP_PadUncollectedDisplayBits(d,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1+4+3+1);
  slotsFor=15;
  /* trial pad 16: boxes, letters and wumpa join the trial checks */
  ctr_cfg.trial_track_valid[0]=1;ctr_cfg.trial_track_locations[0][0]=35015000;ctr_cfg.trial_track_locations[0][1]=35015001;
  n=AP_PadUncollectedDisplayBits(16,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1+2+15+3+1);
  /* refused letter assets: no CTR Challenge and no trial letters listed */
  servable=0;n=AP_PadUncollectedDisplayBits(16,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1+1+15+1);
  for(i=0;i<n;i++)assert(!AP_PadGlowLetterDecode(bits[i],0,0));
  servable=1;
  ctr_cfg.trial_track_valid[0]=0;
  /* Gem cup: four distinct legs -> boxes and wumpa of every leg, no letters */
  cupLegs[0]=0;cupLegs[1]=1;cupLegs[2]=2;cupLegs[3]=3;
  n=AP_PadUncollectedDisplayBits(100,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==1+4*15+4);
  for(i=0;i<n;i++){int l,c;assert(!AP_PadGlowLetterDecode(bits[i],&l,&c));
   for(slot=i+1;slot<n;slot++)assert(bits[i]!=bits[slot]);}
  /* podium rungs on: one stub rung per leg, still inside the buffer */
  ctr_cfg.podium_enabled=1;rung=1;
  n=AP_PadUncollectedDisplayBits(100,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1+4+4*15+4&&n<=AP_PAD_DISPLAY_BITS_MAX);
  ctr_cfg.podium_enabled=0;rung=0;
  /* a repeated leg counts once */
  cupLegs[0]=2;cupLegs[1]=2;cupLegs[2]=5;cupLegs[3]=5;
  n=AP_PadUncollectedDisplayBits(100,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1+2*15+2);
  for(i=0;i<n;i++)for(slot=i+1;slot<n;slot++)assert(bits[i]!=bits[slot]);
  /* Cortex Vortex leg contributes only its wumpa; a cup with no cortex leg has none */
  cortexValid=1;cupLegs[0]=110;cupLegs[1]=1;cupLegs[2]=2;cupLegs[3]=3;
  n=AP_PadUncollectedDisplayBits(100,bits,AP_PAD_DISPLAY_BITS_MAX);
  assert(n==1+3*15+3+1);
  { int cv=0;for(i=0;i<n;i++)if(bits[i]==AP_CV_PSEUDO_BASE+8)cv++;assert(cv==1);}
  /* Cortex Vortex pad: its letters and wumpa on top of the tiers */
  n=AP_PadUncollectedDisplayBits(110,bits,AP_PAD_DISPLAY_BITS_MAX);
  (void)leg;assert(has(bits,n,AP_CV_PSEUDO_BASE+5)&&has(bits,n,AP_CV_PSEUDO_BASE+7)&&has(bits,n,AP_CV_PSEUDO_BASE+8));
  ctr_cfg.wumpa.mode=CTR_CFG_WUMPA_GLOBAL;
  n=AP_PadUncollectedDisplayBits(110,bits,AP_PAD_DISPLAY_BITS_MAX);assert(!has(bits,n,AP_CV_PSEUDO_BASE+8)&&has(bits,n,AP_CV_PSEUDO_BASE+6));
  cortexValid=0;
  /* arena and unknown destinations list nothing extra */
  n=AP_PadUncollectedDisplayBits(18,bits,AP_PAD_DISPLAY_BITS_MAX);assert(n==1);
  assert(AP_PadGlowItemCode(&ctr_cfg,AP_PadGlowBoxBit(AP_BOX_CODE_BASE))==AP_BOX_CODE_BASE);
 }
 puts("Trial pad glow production gather passed");
}
'''
signatures=['int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap)','int AP_PadUncollectedDisplayBits(int destLevelID, int *outBits, int cap)','static int AP_GlowBitRewardGroup(int globalBit)']
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
assert 'AP_PadUncollectedGlowBits(destLevelID,' in state and 'int uncBits[24];' in state
header=(root/'ap/ap_hooks.h').read_text()
assert '#define AP_PAD_DISPLAY_BITS_MAX 96' in header
