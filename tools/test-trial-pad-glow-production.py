"""Issue #343: compile the actual pad glow gather and prize-slot grouping.

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
#define ADV_REWARD_FIRST_SAPPHIRE_RELIC 0x16
#define ADV_REWARD_FIRST_CTR_TOKEN 0x4c
#define AP_PODIUM_PSEUDO_BASE 0x100
ctr_seed_config ctr_cfg;
static long checked[4]; static int nChecked;
static int relics=1, rung=1;
static int AP_PadBoxChecked(long code,void *ctx){int i;(void)ctx;for(i=0;i<nChecked;i++)if(checked[i]==code)return 1;return 0;}
int AP_TrialTrackConfigured(int d){return (d==16||d==17)&&ctr_cfg.trial_track_valid[d-16];}
int AP_PadUncollectedBits(int d,int *out,int cap){(void)cap;if(!relics)return 0;out[0]=d+ADV_REWARD_FIRST_SAPPHIRE_RELIC;return 1;}
static void AP_AppendTrackRungGlow(int t,int *out,int cap,int *n){(void)t;if(rung&&*n<cap)out[(*n)++]=AP_PODIUM_PSEUDO_BASE+t*CTR_CFG_PODIUM_RUNG_COUNT;}
int ctr_cfg_cup_displaced(int c){(void)c;return 0;}
int ctr_cfg_cup_leg(int c,int l){(void)c;return l;}
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
  ctr_cfg.trial_track_valid[t]=0;assert(AP_PadUncollectedGlowBits(d,bits,24)==0);
 }
 puts("Trial pad glow production gather passed");
}
'''
signatures=['int AP_PadUncollectedGlowBits(int destLevelID, int *outBits, int cap)','static int AP_GlowBitRewardGroup(int globalBit)']
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'fixture.c';exe=Path(tmp)/'fixture'
 src.write_text(fixture+'\n'.join(extract(s) for s in signatures)+tests)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined','-I',str(root/'ap'),'-I',str(root/'include'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
