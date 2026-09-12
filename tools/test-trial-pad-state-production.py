"""Exercise actual trial/retail state gather with the shared decision table."""
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
#include "ap_pad_state.h"
#define CTR_CFG_TRIAL_TROPHY 0
#define ADV_REWARD_FIRST_TROPHY 0
static int active=1,configured=1,trophyOwned=1,trophyChecked=0;
static int stage1=1,stage2=1,racer=1,remaining=1,boxes=0,letters=0,wumpa=0;
static int physical=3,destination=16;
int ctr_cfg_active(void){return active;}
int AP_TrialTrackConfigured(int d){return configured&&(d==16||d==17);}
long AP_TrialTrackLocation(int d,int c){return trophyOwned?35015000: -1;}
int AP_TrialTrackLocationChecked(int d,int c){assert(d==destination);return trophyChecked;}
int AP_LocationCheckedByBit(int bit){assert(bit<16);return trophyChecked;}
int AP_PadStage1Met(int p){assert(p==physical);return stage1;}
int ctr_cfg_racer_lock_met(int p){assert(p==physical);return racer;}
int ctr_cfg_warp_stage2_unlocked(int p){assert(p==physical);return stage2;}
int AP_PadUncollectedGlowBits(int d,int *out,int n){assert(d==destination);return remaining;}
int AP_PadUncollectedBoxCount(int d){return boxes;}
int AP_PadUncollectedLetterCount(int d){return letters;}
int AP_PadUncollectedWumpaCount(int d){return wumpa;}
int AP_TrialTrackUncheckedCount(int d){return 0;}
'''
tests=r'''
int main(void){
 for(destination=16;destination<=17;destination++){
  trophyChecked=0;stage1=0;assert(AP_PadState(physical,destination)==1);
  stage1=1;racer=0;assert(AP_PadState(physical,destination)==1);
  racer=1;assert(AP_PadState(physical,destination)==2);
  trophyChecked=1;stage2=0;assert(AP_PadState(physical,destination)==3);
  boxes=1;assert(AP_PadState(physical,destination)==2);
  assert(AP_PadPhase1ReRaceable(physical,destination));
  boxes=0;stage2=1;assert(AP_PadState(physical,destination)==4);
  remaining=0;letters=1;assert(AP_PadState(physical,destination)==4);
  letters=0;assert(AP_PadState(physical,destination)==5);
  remaining=1;trophyOwned=0;stage2=0;
  assert(AP_PadState(physical,destination)==2); /* relic-only/legacy */
  trophyOwned=1;active=0;assert(AP_PadState(physical,destination)==0);active=1;
 }
 destination=0;trophyChecked=1;stage2=1;
 assert(AP_PadState(physical,destination)==4);
 puts("Trial and retail production pad transitions passed");
}
'''
signatures=['static int AP_DestIsRace(int destLevelID)','static int AP_DestTrophyChecked(int destLevelID)','static int AP_DestKnown(int destLevelID)','int AP_PadState(int physLevelID, int destLevelID)','int AP_PadPhase1ReRaceable(int physLevelID, int destLevelID)']
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'fixture.c';exe=Path(tmp)/'fixture'
 src.write_text(fixture+'\n'.join(extract(s) for s in signatures)+tests)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined','-I',str(root/'ap'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
