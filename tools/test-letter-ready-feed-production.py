"""Actual feed readiness scan: global/hub transitions and reconnect baseline."""
from pathlib import Path
import tempfile
import subprocess
root=Path(__file__).resolve().parents[1]
source=(root/'ap/ap_hooks.c').read_text()
start=source.index('static void AP_FeedLetterReadyUpdates(void)\n{')
end=source.index('\n}\n',start)+3
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CTR_AP 1
#include "ap_lettersanity.h"
#define CTR_CFG_LETTER_TRACK_COUNT 18
#define LIGHT_GREEN 1
struct {int lettersanity_mode;long lettersanity_locations[18][3];} ctr_cfg;
static unsigned char ap_letter_received[18][3],ap_feed_letters_ready[18];
static int ap_feed_primed,ap_hub_feed_on=1,count;
int ctr_cfg_active(void){return 1;}
void AP_FeedEnqueue(const char *s,int color,int sound){
 assert(!strcmp(s,count==0?"Slide Coliseum: Got CTR letters":"Turbo Track: Got CTR letters"));count++;
}
'''
tests=r'''
int main(void){
 ctr_cfg.lettersanity_mode=2;
 for(int t=16;t<18;t++)for(int l=0;l<3;l++)ctr_cfg.lettersanity_locations[t][l]=100+t*3+l;
 AP_FeedLetterReadyUpdates();assert(!count);
 ap_feed_primed=1;
 ap_letter_received[16][0]=ap_letter_received[16][1]=1;
 AP_FeedLetterReadyUpdates();assert(!count);
 ap_letter_received[16][2]=1;AP_FeedLetterReadyUpdates();assert(count==1);
 for(int i=0;i<50;i++)AP_FeedLetterReadyUpdates();assert(count==1);
 memset(ap_letter_received[17],1,3);AP_FeedLetterReadyUpdates();assert(count==2);
 /* Reconnect initial inventory is absorbed, not announced again. */
 ap_feed_primed=0;memset(ap_feed_letters_ready,0,18);AP_FeedLetterReadyUpdates();
 ap_feed_primed=1;AP_FeedLetterReadyUpdates();assert(count==2);
 /* Feed off and vanilla/location-only modes do not produce notices. */
 ap_hub_feed_on=0;memset(ap_feed_letters_ready,0,18);AP_FeedLetterReadyUpdates();assert(count==2);
 ap_hub_feed_on=1;ctr_cfg.lettersanity_mode=1;memset(ap_feed_letters_ready,0,18);
 AP_FeedLetterReadyUpdates();assert(count==2);
 puts("Production all-track letter-ready feed transitions passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'fixture.c';exe=Path(tmp)/'fixture'
 src.write_text(fixture+source[start:end]+tests)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined','-I',str(root/'ap'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
