// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DCTR_CUSTOM_TRACKS -DBUILD=926 -I include -I . tools/test-custom-manager.c -o /tmp/test-custom-manager
/* Production manager input and font-bound checks; workers/rendering are stubbed. */
#include <stdio.h>
#include <string.h>
#include <common.h>
#include <platform/native_input.h>
#include <platform/native_assets.h>
#include <platform/native_custom_track_library.h>
#include <platform/native_saphi_catalogue.h>
struct sData sdata_static;
struct Data data;
#define DecalFont_DrawLineOT UnusedDraw
#include "../game/DecalFont.c"
#undef DecalFont_DrawLineOT
#include "../game/230/MM_CustomText.c"
#define CONN_PAD_COMMIT (RAW_BTN_CROSS | RAW_BTN_START)
#define CONN_PAD_CANCEL RAW_BTN_TRIANGLE
static struct CustomTrackLibrary s_contentLibrary;
static int s_contentLibraryTab, s_contentSearchEditing, s_contentSearchPadPrev, s_contentStoreWanted;
static char s_contentSearchBackup[81], s_contentInstallPin[65];
static struct CustomSaphiCatalogue *s_saphiCatalogue;
static int s_saphiRefreshBusy, s_saphiLoaded, s_saphiDownloading;
static char s_saphiMessage[512], s_saphiQuery[81];
static size_t s_saphiSelection[2];
static int checks, failures, refreshResult, installResult, starts, textResult;
static struct CustomSaphiRevision rows[9];
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)
static void MM_CustomLibrary_Tick(int reload) { (void)reload; }
static int MM_CustomLibrary_Busy(void) { return s_saphiDownloading; }
int Platform_InputRawGamepadButtons(void) { return 0; }
int NativeText_Result(void) { return textResult; }
void NativeText_Resolve(int result) { textResult=result; }
void NativeText_End(void) { textResult=0; }
void NativeText_Begin(char *b,int c,int x,int y,int w,int h,int p)
{ (void)b;(void)c;(void)x;(void)y;(void)w;(void)h;(void)p; }
const char *NativeAssets_GetAssetDir(void) { return "/test"; }
int OtherFX_Play(u32 a,int b) { (void)a;(void)b;return 0; }
int CustomSaphi_StartRefresh(void) { return 1; }
int CustomSaphi_PollRefresh(struct CustomSaphiCatalogue **out,char *error,size_t n)
{ if(refreshResult==1)*out=(void*)2; if(refreshResult==-2)snprintf(error,n,"Offline"); return refreshResult; }
void CustomSaphi_Free(struct CustomSaphiCatalogue **p) { *p=NULL; }
size_t CustomSaphi_Count(const struct CustomSaphiCatalogue *p) { return p ? 9:0; }
const struct CustomSaphiRevision *CustomSaphi_Row(const struct CustomSaphiCatalogue *p,size_t i)
{ (void)p;return &rows[i]; }
int CustomSaphi_StartInstall(const struct CustomSaphiRevision *r,const char *a)
{ (void)r;(void)a;starts++;return 1; }
int CustomSaphi_PollInstall(char pin[65],char *e,size_t n)
{ (void)e;(void)n;if(installResult==1)strcpy(pin,"fixture");return installResult; }
void DecalFont_DrawLineOT(char *s,int x,int y,s16 f,int flags,uint32_t *ot)
{ (void)ot; int w=DecalFont_GetLineWidth(s,f);if(flags&JUSTIFY_CENTER)x-=w/2;CHECK(x>=0&&x+w<=512&&y>=0&&y+8<=216); }
void RECTMENU_DrawInnerRect(RECT *r,int x,uint32_t *ot)
{ (void)x;(void)ot;CHECK(r->x>=0&&r->x+r->w<=512&&r->y+r->h<=216); }
void CTR_Box_DrawClearBox(const RECT *r,const Color *c,int t,uint32_t *ot)
{ (void)c;(void)t;(void)ot;CHECK(r->x+r->w<=512); }
#include "../game/230/MM_CustomManager.c"
static void tick(int buttons) { struct GamepadBuffer pad={0};pad.buttonsTapped=buttons;MM_ConfigProc_CustomContent(NULL,NULL,&pad); }
int main(void)
{
 data.font_charPixWidth[FONT_SMALL]=13;data.font_charPixWidth[FONT_BIG]=17;
 data.font_puncPixWidth[FONT_SMALL]=7;data.font_puncPixWidth[FONT_BIG]=11;
 data.font_buttonPixWidth[FONT_SMALL]=2;data.font_buttonPixWidth[FONT_BIG]=16;
 for(int i=0;i<9;i++){rows[i].trackID=i+1;memset(rows[i].title,'W',640);strcpy(rows[i].version,"1.0.0");strcpy(rows[i].author,"Very long author display name");}
 tick(0);CHECK(s_saphiRefreshBusy);
 refreshResult=1;tick(0);CHECK(s_saphiCatalogue==(void*)2);
 tick(BTN_UP);CHECK(s_saphiSelection[0]==8);tick(BTN_DOWN);CHECK(s_saphiSelection[0]==0);
 tick(BTN_L2);refreshResult=-2;tick(0);CHECK(s_saphiCatalogue==(void*)2&&!s_saphiRefreshBusy);
 strcpy(rows[0].disabledReason,"Files not hosted");tick(BTN_CROSS);CHECK(starts==0);
 rows[0].disabledReason[0]=0;tick(BTN_CROSS);CHECK(starts==1&&s_saphiDownloading);
 tick(BTN_CROSS);CHECK(starts==1);
 installResult=1;tick(0);CHECK(!s_saphiDownloading&&s_contentStoreWanted);
 tick(BTN_R1);CHECK(s_contentLibraryTab==1);
 struct CustomTrackLibraryEntry installed={0};installed.installed=1;strcpy(installed.title,"Installed track");
 s_contentLibrary.entries=&installed;s_contentLibrary.count=1;s_contentStoreWanted=0;
 tick(BTN_CROSS);CHECK(s_contentStoreWanted&&starts==1);
 tick(BTN_R2);CHECK(s_contentSearchEditing);strcpy(s_saphiQuery,"no match");textResult=2;tick(0);CHECK(!s_contentSearchEditing&&!s_saphiQuery[0]);
 /* One track row for two revisions; latest downloadable beats an unavailable current. */
 tick(BTN_R1);s_saphiSelection[0]=0;
 rows[1].trackID=rows[0].trackID;strcpy(rows[0].version,"1.9.0");strcpy(rows[1].version,"1.10.0");
 rows[1].lev.id=42;rows[1].vrm.id=43;
 tick(BTN_UP);CHECK(s_saphiSelection[0]==7);tick(BTN_DOWN);
 CHECK(MM_CustomSourcePreferred(&rows[1],&rows[0]));
 rows[0].current=1;strcpy(rows[0].disabledReason,"Files not hosted");CHECK(MM_CustomSourcePreferred(&rows[1],&rows[0]));
 rows[0].current=0;rows[0].disabledReason[0]=0;
 tick(BTN_SQUARE);CHECK(s_saphiVersionTrack==rows[0].trackID);tick(0);
 tick(BTN_UP);CHECK(s_saphiSelection[0]==1);tick(BTN_TRIANGLE);CHECK(!s_saphiVersionTrack&&s_saphiSelection[0]==0);
 installed.local.trackID=rows[1].trackID;installed.local.levID=42;installed.local.vrmID=43;strcpy(installed.version,"1.10.0");
 int older;CHECK(MM_CustomSourceInstalled(&rows[1],&older)==&installed);
 int before=starts;tick(BTN_CROSS);CHECK(starts==before);
 CHECK(CustomRevision_Compare("2.0.0","1.99.0")>0);
 CHECK(CustomRevision_Compare("1.0.0","1.0.0-rc1")>0);
 printf("custom manager: %d checks, %d failures\n",checks,failures);return failures!=0;
}
