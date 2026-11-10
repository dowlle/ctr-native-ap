// cc -std=c99 -Wall -Wextra -I include tools/test-text-box.c -o /tmp/test-text-box
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <text_box_logic.h>

static unsigned tick(TextBoxClock *s, unsigned frame, int ms, int players)
{
    return TextBox_Tick(s, TextBox_Key("Long title"), frame, ms, 240, 0, 0, 512, 216, players);
}

int main(void)
{
    int l,t,r,b;
    assert(TextBox_Cycle(425,240)==7125);
    assert(TextBox_Offset(425,240,1499)==0);
    assert(TextBox_Offset(425,240,1500)==0);
    assert(TextBox_Offset(425,240,2500)==40);
    assert(TextBox_Offset(425,240,6125)==185);
    assert(TextBox_Offset(425,240,7124)==185);
    assert(TextBox_Offset(425,240,7125)==0);
    assert(TextBox_Offset(240,240,9000)==0);
    for (int fps=30;fps<=60;fps+=30) {
        TextBoxClock paused={0}, playback={0};
        for (unsigned frame=0;frame<=(unsigned)fps*3;frame++) {
            unsigned ticks=frame*60/fps;
            unsigned ms=TextBox_TickVBlank(&paused,1,frame,ticks,240,0,0,512,216,1);
            assert(ms==frame*1000/fps);
            assert(ms==TextBox_TickVBlank(&playback,1,frame,ticks,240,0,0,512,216,1));
            assert(!memcmp(&paused,&playback,sizeof paused));
        }
        assert(TextBox_Offset(425,240,paused.ms)==60);
        TextBoxClock a={0}, replay={0};
        unsigned previous=0;
        tick(&a,0,0,1);tick(&replay,0,0,1);
        for (unsigned frame=1;frame<=(unsigned)fps*12;frame++) {
            unsigned now=frame*1000/fps;
            unsigned ms=tick(&a,frame,(int)(now-previous),1);
            assert(ms==now);
            assert(ms==tick(&a,frame,64,1)); /* duplicate draw */
            assert(ms==tick(&replay,frame,(int)(now-previous),1));
            assert(!memcmp(&a,&replay,sizeof a));
            if(now==3000) assert(TextBox_Offset(425,240,ms)==60);
            previous=now;
        }
        assert(tick(&a,fps*12+1,33,2)==0); /* split-screen transition */
        assert(tick(&a,fps*12+2,33,2)==33);
        assert(tick(&a,fps*12+4,33,2)==0); /* hidden frame */
        assert(TextBox_Tick(&a,1,9999,33,169,0,108,512,108,2)==0);
        assert(TextBox_Tick(&a,2,10000,33,169,0,108,512,108,2)==0);
    }
    TextBox_Clip(-20,-5,240,20,512,216,&l,&t,&r,&b);
    assert(l==0&&t==0&&r==220&&b==15);
    TextBox_Clip(500,210,240,20,512,216,&l,&t,&r,&b);
    assert(l==500&&t==210&&r==512&&b==216);
    TextBox_Clip(600,300,240,20,512,216,&l,&t,&r,&b);
    assert(l==r&&t==b);
    puts("PASS: holds, 40 px/s at 30/60 fps, replay determinism, duplicate draws, viewport reset, clip intersection");
}
