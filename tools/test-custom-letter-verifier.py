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
struct { int custom_lettersanity_mode; long custom_letter_items[3]; } ctr_cfg;
static int opened, capable;
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
  assert(evaluate(counts)==(opened && capable && met)); tested++;
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
 opened=capable=1;
 assert(!evaluate(counts));
 scout_item=35021000LL;
 /* A different reachable location banks the needed item and opens the letter. */
 ap_vf_bank_own(123,counts); assert(evaluate(counts));
 puts("PASS: 396 sparse own banking identities, refusal boundaries, retail counters, self-lock edge");
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
