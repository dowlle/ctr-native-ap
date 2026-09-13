// cc -std=c99 -Wall -Wextra -I . tools/test-tracker-assets.c -o /tmp/test-tracker-assets
// Optional local asset gate: /tmp/test-tracker-assets /path/to/NTSC-U/BIGFILE.BIG
#include "ap/ap_tracker_assets.h"
#include <assert.h>
#include <stdio.h>

static void put16(unsigned char *p,unsigned n) { p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8); }
static void put32(unsigned char *p,unsigned n) { put16(p,n);put16(p+2,n>>16); }

static void synthetic(void)
{
	uint16_t *vram=(uint16_t *)calloc(AP_TRACKER_VRAM_WORDS,sizeof *vram);
	unsigned char t[12]={0},tim[28]={0},lev[1028]={0}; AP_TrackerHubAsset hub; int x,y;
	assert(vram);
	/* Packed 4-bit texels with a CLUT outside the texture page. */
	put16(t+2,64);vram[0]=0x21;vram[1024+1]=0x001f;vram[1024+2]=0x03e0;
	assert(AP_TrackerTexel(vram,t,0,0)==AP_TrackerRGBA(255,0,0,255));
	assert(AP_TrackerTexel(vram,t,1,0)==AP_TrackerRGBA(0,255,0,255));
	assert(AP_TrackerTexel(vram,t,2,0)==0);
	/* 8-bit and opaque black versus transparent zero. */
	put16(t+6,128);vram[0]=0x0201;vram[1024+2]=0x8000;
	assert(AP_TrackerTexel(vram,t,1,0)==AP_TrackerRGBA(0,0,0,255));
	put16(t+6,256);vram[0]=0x7c00;
	assert(AP_TrackerTexel(vram,t,0,0)==AP_TrackerRGBA(0,0,255,255));
	put16(t+6,384);assert(!AP_TrackerTexel(vram,t,0,0));
	/* Reject truncated/out-of-VRAM blocks before copying. */
	put16(tim+16,2);put16(tim+18,2);put16(tim+20,0x1234);
	assert(AP_TrackerDecodeVram(vram,tim,sizeof tim));assert(vram[0]==0x1234);
	assert(!AP_TrackerDecodeVram(vram,tim,27));
	put16(tim+12,1023);assert(!AP_TrackerDecodeVram(vram,tim,sizeof tim));
	/* A valid tiny LEV view, two map halves and one shuffled pad identity. */
	put32(lev,1024);put32(lev+4+0x3c,400);put32(lev+4+400,2);put32(lev+4+404,420);
	put32(lev+4+0x134,500);put32(lev+4+500,1);put32(lev+4+504,520);
	put16(lev+4+520,100);put16(lev+4+522,100);put16(lev+4+528,2);put16(lev+4+530,2);
	put16(lev+4+532,500);put16(lev+4+534,195);
	put32(lev+4+0xc,1);put32(lev+4+0x10,600);memcpy(lev+4+600,"warppad#9",10);
	put32(lev+4+420+16,3);put32(lev+4+452+16,4);
	lev[4+420+24]=2;lev[4+420+29]=2;lev[4+452+24]=2;lev[4+452+29]=2;
	assert(AP_TrackerParseHub(&hub,lev,sizeof lev,vram));assert(hub.pad_count==1&&hub.pads[0].physical==9);
	assert(hub.width==2&&hub.height==4);
	AP_TrackerMapPoint(&hub,0,0,&x,&y);assert(x==2&&y==-12);free(hub.pixels);
	put32(lev+4+404,1023);assert(!AP_TrackerParseHub(&hub,lev,sizeof lev,vram));
	put32(lev+4+404,420);put16(lev+4+520,0);assert(!AP_TrackerParseHub(&hub,lev,sizeof lev,vram));
	assert(!AP_TrackerSpan(8,9,1,1));assert(!AP_TrackerSpan(8,0,(size_t)-1,32));
	free(vram);
}
static void retail(const char *path)
{
	FILE *f=fopen(path,"rb");unsigned char header[8192];int h,total=0;
	uint16_t *vram=(uint16_t *)malloc(AP_TRACKER_VRAM_WORDS*sizeof *vram);assert(f&&vram);
	assert(fread(header,1,sizeof header,f)==sizeof header);
	for(h=0;h<5;h++) {
		unsigned sizes[2],offs[2];unsigned char *files[2];int k,i,x,y;AP_TrackerHubAsset hub;
		for(k=0;k<2;k++) {
			unsigned entry=200+h*3+k;
			offs[k]=AP_TrackerU32(header+8+entry*8);sizes[k]=AP_TrackerU32(header+12+entry*8);
			files[k]=(unsigned char *)malloc(sizes[k]);assert(files[k]);
			assert(!fseek(f,(long)offs[k]*2048,SEEK_SET));assert(fread(files[k],1,sizes[k],f)==sizes[k]);
		}
		assert(AP_TrackerDecodeVram(vram,files[0],sizes[0]));
		assert(AP_TrackerParseHub(&hub,files[1],sizes[1],vram));assert(hub.pad_count==(h?5:7));
		for(i=0;i<hub.pad_count;i++) {
			AP_TrackerMapPoint(&hub,hub.pads[i].world_x,hub.pads[i].world_z,&x,&y);
			assert(x>=-4&&x<=hub.width+4&&y>=-8&&y<=hub.height+8);
		}
		for(i=0;i<hub.width*hub.height;i++) if(hub.pixels[i]) total++;
		printf("hub %d: %dx%d, %d physical pads decoded\n",h,hub.width,hub.height,hub.pad_count);
		free(hub.pixels);free(files[0]);free(files[1]);
	}
	assert(total>1000);free(vram);fclose(f);
}
int main(int argc,char **argv)
{
	synthetic();if(argc>1)retail(argv[1]);puts("tracker assets: PASS");return 0;
}
