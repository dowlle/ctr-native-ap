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
    ("int AP_LettersRequiredCount(", "static void AP_FeedLetterReadyUpdates(void)"),
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
/* Schema 15 Cortex Vortex seam: the production helpers sit outside the
 * extracted ranges, so the fixture supplies them with a controllable identity. */
#define CTR_CFG_CORTEX_HOST_LEVEL 13
enum { AP_CV_SLOT_LETTER0 = 5 };
static int cortex_active;
static long cortex_letters[3] = {-1, -1, -1};
static unsigned char ap_cortex_letter_received[3];
static int AP_CortexLetterTrack(int track) { return track == CTR_CFG_CORTEX_HOST_LEVEL && cortex_active; }
static long AP_CortexTrackCode(int slot) { return cortex_letters[slot - AP_CV_SLOT_LETTER0]; }
static void AP_CortexLetterCodes(long codes[3]) { for (int l = 0; l < 3; l++) codes[l] = cortex_letters[l]; }
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

 /* Cortex Vortex off: level 13 is Oxide Station's row, exactly as before. */
 ctr_cfg.lettersanity_mode=2;
 ctr_cfg.lettersanity_locations[13][0]=35012545;
 ctr_cfg.lettersanity_locations[13][1]=-1;
 ctr_cfg.lettersanity_locations[13][2]=-1;
 cortex_letters[0]=35026006; cortex_letters[1]=35026007; cortex_letters[2]=-1;
 cortex_active=0;
 assert(AP_LetterLocation(13,0)==35012545 && AP_LetterLocation(13,1)==-1);
 assert(!AP_LetterAvailable(13,0) && AP_LettersRequiredCount(13)==1);
 ap_letter_received[13][0]=1;
 assert(AP_LetterAvailable(13,0) && AP_LettersRequiredMet(13) && AP_LetterTokenEarned(13,1,1));
 ap_cortex_letter_received[0]=ap_cortex_letter_received[1]=1;

 /* Cortex Vortex on: level 13 means the track's own letters and items, and
  * Oxide Station's received row no longer satisfies anything. */
 cortex_active=1;
 ap_cortex_letter_received[0]=ap_cortex_letter_received[1]=0;
 assert(AP_LetterLocation(13,0)==35026006 && AP_LetterLocation(13,1)==35026007);
 assert(AP_LetterLocation(13,2)==-1 && AP_LetterLocation(13,3)==-1);
 assert(!AP_LetterAvailable(13,0) && !AP_LetterAvailable(13,2));
 assert(AP_LettersRequiredCount(13)==2 && !AP_LettersRequiredMet(13));
 assert(!AP_LetterTokenEarned(13,1,2));
 ap_cortex_letter_received[0]=ap_cortex_letter_received[1]=1;
 assert(AP_LetterAvailable(13,0) && AP_LettersRequiredMet(13) && AP_LetterTokenEarned(13,1,2));
 assert(!AP_LetterTokenEarned(13,0,2) && !AP_LetterTokenEarned(13,1,3));
 ctr_cfg.lettersanity_mode=1;
 assert(AP_LetterAvailable(13,2) && AP_LettersRequiredCount(13)==3 && AP_LetterTokenEarned(13,1,3));
 ctr_cfg.lettersanity_mode=2;
 /* Other tracks never take the Cortex Vortex branch. */
 assert(AP_LetterLocation(6,0)==35012503 && AP_LetterAvailable(6,0));
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
print("PASS: 132 production custom slot hooks, host-receipt isolation, mid-race receipt, bounds, Cortex Vortex off/on letter rows")
