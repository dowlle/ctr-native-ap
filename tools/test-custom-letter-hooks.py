"""Compile production letter hooks with controlled serving-load identity."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ap/ap_hooks.c").read_text()
functions = []
for first, following in (
    ("int AP_LetterAvailable(", "long AP_LetterLocation("),
    ("long AP_LetterLocation(", "void AP_LetterCollected("),
    ("int AP_LettersRequiredMet(", "int AP_LettersRequiredCount("),
    ("int AP_LettersRequiredCount(", "int AP_LetterTokenEarned("),
    ("int AP_LetterTokenEarned(", "static int AP_ItemsanityActive("),
):
    start = source.index(first)
    functions.append(source[start:source.index(following, start)])
fixture = r'''
#include <assert.h>
#include "ap_lettersanity.h"
#define CTR_CFG_LETTER_TRACK_COUNT 18
static int serving_slot, active=1;
struct { int lettersanity_mode,custom_lettersanity_mode;
 long lettersanity_locations[18][3],custom_letter_locations[3]; } ctr_cfg;
static unsigned char ap_letter_received[18][3],ap_custom_letter_received[132][3];
int ctr_cfg_active(void) { return active; }
int AP_CustomLetterSlot(int track) { assert(track==6 || serving_slot==0); return serving_slot; }
'''
tests = r'''
int main(void) {
 ctr_cfg.lettersanity_mode=2; ctr_cfg.custom_lettersanity_mode=2;
 ctr_cfg.lettersanity_locations[6][0]=35012503;
 ap_letter_received[6][0]=1;
 for(int slot=1;slot<=132;slot++) {
  serving_slot=slot;
  for(int l=0;l<3;l++)
   ctr_cfg.custom_letter_locations[l]=l!=1 ? 35020000L+(slot-1)*3+l : -1;
  assert(!AP_LetterAvailable(6,0));
  assert(!AP_LettersRequiredMet(6));
  assert(AP_LettersRequiredCount(6)==2);
  assert(AP_LetterLocation(6,0)==35020000L+(slot-1)*3);
  assert(AP_LetterLocation(6,1)==-1);
  assert(!AP_LetterTokenEarned(6,1,2));
  ap_custom_letter_received[slot-1][0]=1;
  assert(AP_LetterAvailable(6,0) && !AP_LettersRequiredMet(6));
  ap_custom_letter_received[slot-1][2]=1;
  assert(AP_LettersRequiredMet(6) && AP_LetterTokenEarned(6,1,2));
  assert(!AP_LetterAvailable(6,1));
  assert(!AP_LetterTokenEarned(6,0,2) && !AP_LetterTokenEarned(6,1,3));
 }
 serving_slot=132;
 for(int mode=0;mode<4;mode++) {
  ctr_cfg.custom_lettersanity_mode=mode;
  for(int l=0;l<3;l++) {
   ctr_cfg.custom_letter_locations[l]=mode==1 || (mode==2 && l!=1) ? 35020393L+l : -1;
   ap_custom_letter_received[131][l]=0;
  }
  assert(AP_LettersRequiredCount(6)==(mode==2 ? 2 : 3));
  assert(AP_LettersRequiredMet(6)==(mode<2));
  assert(AP_LetterAvailable(6,0)==(mode<2));
  assert(AP_LetterTokenEarned(6,1,3)==(mode<2));
  for(int l=0;l<3;l++) ap_custom_letter_received[131][l]=1;
  if(mode==2) {
   assert(AP_LetterAvailable(6,0) && !AP_LetterAvailable(6,1));
   assert(AP_LettersRequiredMet(6) && AP_LetterTokenEarned(6,1,2));
   assert(AP_LetterLocation(6,0)==35020393L && AP_LetterLocation(6,2)==35020395L);
  }
  if(mode==3) {
   assert(AP_LetterAvailable(6,0) && AP_LettersRequiredMet(6));
   assert(AP_LetterTokenEarned(6,1,3) && AP_LetterLocation(6,0)==-1);
  }
 }
 serving_slot=0;
 assert(AP_LetterAvailable(6,0) && AP_LetterLocation(6,0)==35012503);
 assert(AP_LettersRequiredMet(-1) && AP_LettersRequiredCount(18)==3);
 assert(AP_LetterLocation(-1,0)==-1 && AP_LetterLocation(18,0)==-1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "hooks.c", Path(tmp) / "hooks"
    src.write_text(fixture + "\n".join(functions) + tests)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-I" + str(root / "ap"),
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 132 production custom slot hooks, host-receipt isolation, mid-race receipt and bounds")
