"""Compile actual held/finish fan-out and verify isolated trial/custom banks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks.c").read_text()
def extract(signature):
    start = source.index(signature + "\n{")
    end = source.index("\n}\n", start)+3
    return source[start:end]

fixture = r'''
#include <assert.h>
#include <stddef.h>
#define CTR_AP 1
#define CTR_CUSTOM_TRACKS 1
#include "ap_seedcfg.h"
#include "ap_custom_pad_logic.h"
#include "ap_cortex_track.h" /* schema 15: logical podium track 50 */
#define CTR_CUSTOM_TRACKS 1
ctr_seed_config ctr_cfg;
int ctr_cfg_active(void) { return 1; }
#define AP_RUNG_HELD_1ST 0
#define AP_RUNG_HELD_3RD 1
#define AP_RUNG_HELD_5TH 2
#define AP_RUNG_FINISH_PODIUM 3
#define AP_RUNG_FINISH_ANY 4
static int seen[5];
static long base;
static void AP_EmitRung(int track,long code,int tag,int pos,const char *phase) {
 assert(code==base+tag); seen[tag]++;
}
'''
tests = r'''
int main(void) {
 ctr_cfg.podium_enabled=1;
 for(int t=0;t<2;t++) {
  base=35015200+t*5;
  ctr_cfg.podium[16+t]=(ctr_podium_rungs){base,base+1,base+2,base+3,base+4};
  for(int pos=1;pos<=8;pos++) {
   for(int i=0;i<5;i++) seen[i]=0;
   AP_SendHeldChecks(48+t,pos);
   assert(seen[0]==(pos==1) && seen[1]==(pos<=3) && seen[2]==(pos<=5));
   assert(!seen[3] && !seen[4]);
   for(int i=0;i<5;i++) seen[i]=0;
   AP_SendPodiumChecks(48+t,pos);
   assert(seen[0]==(pos==1) && seen[1]==(pos<=3) && seen[2]==(pos<=5));
   assert(seen[3]==(pos<=3) && seen[4]==1);
  }
  for(int rung=0;rung<5;rung++)
   assert(AP_PodiumPseudoLocationCode(&ctr_cfg,256+(48+t)*5+rung)==base+rung);
 }
 /* Cortex Vortex (logical track 50): a refused block sends nothing ... */
 base=35026010;
 ctr_cfg.cortex_track.podium=(ctr_podium_rungs){base,base+1,base+2,base+3,base+4};
 ctr_cfg.cortex_track.valid=0;
 for(int i=0;i<5;i++) seen[i]=0;
 AP_SendHeldChecks(AP_CV_PODIUM_LOGICAL_TRACK,1);
 AP_SendPodiumChecks(AP_CV_PODIUM_LOGICAL_TRACK,1);
 for(int i=0;i<5;i++) assert(!seen[i]);
 /* ... and an accepted one fans out its own 35026010..014 ladder. */
 ctr_cfg.cortex_track.valid=1;
 for(int pos=1;pos<=8;pos++) {
  for(int i=0;i<5;i++) seen[i]=0;
  AP_SendHeldChecks(AP_CV_PODIUM_LOGICAL_TRACK,pos);
  assert(seen[0]==(pos==1) && seen[1]==(pos<=3) && seen[2]==(pos<=5) && !seen[3] && !seen[4]);
  for(int i=0;i<5;i++) seen[i]=0;
  AP_SendPodiumChecks(AP_CV_PODIUM_LOGICAL_TRACK,pos);
  assert(seen[3]==(pos<=3) && seen[4]==1);
 }
 /* Oxide Station (13) and the trials keep their own banks with the block on. */
 base=35015065;
 ctr_cfg.podium[13]=(ctr_podium_rungs){base,base+1,base+2,base+3,base+4};
 for(int i=0;i<5;i++) seen[i]=0;
 AP_SendPodiumChecks(13,1);
 for(int i=0;i<5;i++) assert(seen[i]==1);
 base=35015200;
 for(int i=0;i<5;i++) seen[i]=0;
 AP_SendPodiumChecks(48,1);
 for(int i=0;i<5;i++) assert(seen[i]==1);
 ctr_cfg.custom_tracks_ok=1;
 for(int slot=1;slot<=32;slot++) {
  ctr_cfg.custom_track.slot=slot;
  assert(AP_PodiumRungsForLogicalTrack(&ctr_cfg,16+slot-1)==&ctr_cfg.custom_track.podium);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp)/"fixture.c", Path(tmp)/"fixture"
    src.write_text(fixture+extract("static void AP_SendPodiumChecks(int track, int placement)")+
                   extract("static void AP_SendHeldChecks(int track, int position)")+tests)
    subprocess.run(["cc", "-std=c11", "-fsanitize=undefined", "-I", str(root/"ap"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("16 production placement ladders, ten trial identities, the Cortex Vortex bank (refused and accepted) and 32 frozen custom banks passed")
