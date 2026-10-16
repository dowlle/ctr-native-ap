"""Execute the production custom check reachability switch cases."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_verify.c").read_text()
start = source.index("\t\t\tcase AP_VF_CUSTOM_LETTER:")
end = source.index("\t\t\tcase AP_VF_PODIUM:", start)
branch = source[start:end]
bank_start = source.index("static void ap_vf_bank_own(")
bank_end = source.index("\nstatic int ap_vf_required_character(", bank_start)
bank_function = source[bank_start:bank_end]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include "ap_lettersanity.h"
#define CTR_CFG_LETTER_COUNT 3
#define AP_VF_CUSTOM_LETTER 1
#define AP_VF_CUSTOM_CTR 2
#define AP_ITEM_BASE 35010000LL
#define AP_VF_ITEM_COUNT 200
struct Loc { int kind,track,detail; } locs[1];
struct { int custom_lettersanity_mode; long custom_letter_items[3]; int custom_tracks_ok;
         struct { int slot; int named_block; } custom_track; } ctr_cfg;
/* Same rule as ap_seedcfg.h: -1 unless an admitted track names a block (#133). */
static int AP_CustomLetterNamedBlock(void) { return ctr_cfg.custom_tracks_ok ? ctr_cfg.custom_track.named_block : -1; }
static int opened, capable, difficulty_ok, floor_ok;
typedef struct { int unused; } AP_VerifyOptions;
static AP_VerifyOptions ap_vf_options(void) { AP_VerifyOptions o = {0}; return o; }
static int ap_vf_required_character(int pad) { assert(pad==5); return 7; }
/* The custom Trophy Race's difficulty term (rung -1) and the CTR challenge's
   first-boost floor, both with the displaced cup pad's racer. */
static int AP_VerifyDifficultyRungTerm(const AP_VerifyOptions *o, const int *counts,
 int rung, int racer) { (void)o; (void)counts; assert(rung==-1 && racer==7); return difficulty_ok; }
static int AP_VerifyBoostTerm(const AP_VerifyOptions *o, const int *counts,
 int racer, int boost) { (void)o; (void)counts; assert(racer==7 && boost==1); return floor_ok; }
static long long scout_item;
static int scout_player=1, scout_known=1;
int ap_net_self_slot(void) { return 1; }
int ap_net_scout_known(long code, long long *item, int *player, unsigned *flags) {
 assert(code==123); *item=scout_item; *player=scout_player; *flags=0; return scout_known;
}
'''
fixture += bank_function + r'''
int ap_vf_pad_open(int pad, const int *counts) { (void)counts; assert(pad==5); return opened; }
int ap_vf_cup_capable(int cup, const int *counts, const int *pads) {
 (void)counts; (void)pads; assert(cup==4); return capable;
}
static int evaluate(const int *counts) {
 int i=0,lid=0,pad=0,ok=0,pad_for_dest[105]={0};
 pad_for_dest[104]=5;
 switch(locs[i].kind) {
'''
tests = r'''
 }
 return ok;
}
int main(void) {
 int tested=0;
 locs[0].track=104;
 for(int slot=1;slot<=132;slot++)
 for(int mode=0;mode<4;mode++)
 for(int receipt=0;receipt<8;receipt++)
 for(opened=0;opened<2;opened++)
 for(capable=0;capable<2;capable++)
 for(difficulty_ok=0;difficulty_ok<2;difficulty_ok++)
 for(floor_ok=0;floor_ok<2;floor_ok++)
 for(int detail=-1;detail<3;detail++) {
  if(detail>=0 && (mode==0 || mode==3 || detail==1)) continue;
  int counts[596]={0};
  ctr_cfg.custom_lettersanity_mode=mode;
  locs[0].kind=detail<0 ? AP_VF_CUSTOM_CTR : AP_VF_CUSTOM_LETTER;
  locs[0].detail=detail;
  int met=1;
  for(int l=0;l<3;l++) {
   int required=mode==3 || (mode==2 && l!=1);
   ctr_cfg.custom_letter_items[l]=required ? 35021000L+(slot-1)*3+l : -1;
   counts[200+(slot-1)*3+l]=(receipt>>l)&1;
   if(required && (detail<0 || detail==l) && !((receipt>>l)&1)) met=0;
  }
  assert(evaluate(counts)==(opened && capable && difficulty_ok &&
   (detail>=0 || floor_ok) && met)); tested++;
 }
 printf("PASS: %d production custom verifier gate cases\n",tested);
 for(int slot=1;slot<=132;slot++)
 for(int letter=0;letter<3;letter++) {
  int counts[596]={0};
  scout_item=35021000LL+(slot-1)*3+letter;
  ap_vf_bank_own(123,counts);
  for(int index=0;index<596;index++)
   assert(counts[index]==(index==200+(slot-1)*3+letter));
  ap_vf_bank_own(123,counts);
  assert(counts[200+(slot-1)*3+letter]==1);
 }
 int counts[596]={0};
 scout_item=35021000LL; scout_player=2;
 ap_vf_bank_own(123,counts); assert(counts[200]==0);
 scout_player=1; scout_known=0;
 ap_vf_bank_own(123,counts); assert(counts[200]==0);
 scout_known=1;
 scout_item=35020999LL; ap_vf_bank_own(123,counts);
 scout_item=35021396LL; ap_vf_bank_own(123,counts);
 for(int index=0;index<596;index++) assert(counts[index]==0);
 scout_item=35010027LL; ap_vf_bank_own(123,counts); assert(counts[27]==1);
 ap_vf_bank_own(123,counts); assert(counts[27]==2);
 /* Own self-locked letters do not enter the inventory before reachability. */
 ctr_cfg.custom_lettersanity_mode=2;
 ctr_cfg.custom_letter_items[0]=35021000L;
 ctr_cfg.custom_letter_items[1]=-1; ctr_cfg.custom_letter_items[2]=-1;
 locs[0].kind=AP_VF_CUSTOM_LETTER; locs[0].detail=0;
 opened=capable=difficulty_ok=floor_ok=1;
 assert(!evaluate(counts));
 scout_item=35021000LL;
 /* A different reachable location banks the needed item and opens the letter. */
 ap_vf_bank_own(123,counts); assert(evaluate(counts));
 puts("PASS: 396 sparse own banking identities, refusal boundaries, retail counters, self-lock edge");
 /* #133: a named track's letter items (named block item offsets 0..2) bank
    and gate on the admitted track's slot, exactly like its generic codes. */
 int named_cases=0;
 static const int blocks[3]={0,8,9999};
 static const int slots[3]={1,7,132};
 scout_player=1; scout_known=1;
 for(int b=0;b<3;b++)
 for(int s=0;s<3;s++) {
  int slot=slots[s];
  long long first=35100000LL+5LL*blocks[b];
  ctr_cfg.custom_tracks_ok=1; ctr_cfg.custom_track.slot=slot; ctr_cfg.custom_track.named_block=blocks[b];
  for(int letter=0;letter<3;letter++) {
   int c[596]={0};
   scout_item=first+letter; ap_vf_bank_own(123,c);
   for(int index=0;index<596;index++) assert(c[index]==(index==200+(slot-1)*3+letter));
   named_cases++;
  }
  { int c[596]={0};
    scout_item=first+3; ap_vf_bank_own(123,c); /* offsets 3 and 4 are not letters */
    scout_item=first-1; ap_vf_bank_own(123,c);
    for(int index=0;index<596;index++) assert(c[index]==0); }
  ctr_cfg.custom_tracks_ok=0;
  { int c[596]={0};
    scout_item=first; ap_vf_bank_own(123,c); /* no admitted track: never banked */
    for(int index=0;index<596;index++) assert(c[index]==0); }
  ctr_cfg.custom_tracks_ok=1;
  for(int receipt=0;receipt<8;receipt++) {
   int c[596]={0};
   ctr_cfg.custom_lettersanity_mode=3;
   locs[0].kind=AP_VF_CUSTOM_CTR; locs[0].detail=-1;
   opened=capable=difficulty_ok=floor_ok=1;
   for(int l=0;l<3;l++) {
    ctr_cfg.custom_letter_items[l]=(long)(first+l);
    c[200+(slot-1)*3+l]=(receipt>>l)&1;
   }
   assert(evaluate(c)==(receipt==7)); named_cases++;
  }
 }
 ctr_cfg.custom_tracks_ok=0;
 printf("PASS: %d named-block letter banking and gate cases\n",named_cases);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "verify.c", Path(tmp) / "verify"
    src.write_text(fixture + branch + tests)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-I" + str(root / "ap"),
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
