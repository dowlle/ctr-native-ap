#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_content_verify.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define LEV_MAX (16u*1024u*1024u)
#define VRM_MAX (4u*1024u*1024u)
#define INST_N 0x0c
#define INST_P 0x10
#define SPAWN_P 0x6c
#define CHECK_N 0x148
#define CHECK_P 0x14c
#define NAV_P 0x188
#define INST_SIZE 0x40
#define INST_MODEL_P 0x10
#define MODEL_ID 0x10
#define CHECK_SIZE 0x0c
#define NAV_HEAD 0x4c
#define NAV_FRAME 0x14
#ifndef CTR_CCV_FAULT
#define CTR_CCV_FAULT(stage) 0
#endif

enum { PU_WUMPA=2, PU_TNT=6, PU_FRUIT=7, PU_RANDOM=8, PU_TIME1=9, PU_TIME2=10,
	PU_TIME3=11, START=0x15, FINISH=0x23, STATIC_TNT=0x27, STATIC_TIME1=0x5c,
	CRYSTAL=0x60, STATIC_TIME2=0x64, STATIC_TIME3=0x65, LETTER_C=0x93,
	LETTER_T=0x94, LETTER_R=0x95 };

struct LevView { const unsigned char *p; size_t n,map; const unsigned char *slots; size_t slotCount; };
static void seterr(char*d,size_t n,const char*s){if(d&&n)snprintf(d,n,"%s",s);}
static uint32_t rd32(const unsigned char*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int rd16s(const unsigned char*p){unsigned v=p[0]|((unsigned)p[1]<<8);return(v&0x8000)?(int)v-0x10000:(int)v;}
static int fits(size_t at,size_t n,size_t limit){return at<=limit&&n<=limit-at;}
static void digest(const unsigned char*p,size_t n,char out[NATIVE_SHA256_HEX_BYTES]){struct NativeSha256Ctx c;unsigned char d[NATIVE_SHA256_DIGEST_BYTES];(void)NativeSha256_HexEquals;NativeSha256_Init(&c);NativeSha256_Update(&c,p,n);NativeSha256_Final(&c,d);NativeSha256_ToHex(d,out);}

static int read_file(const char*path,size_t max,unsigned char**out,size_t*n,char*e,size_t en)
{
#ifdef _WIN32
	HANDLE file;BY_HANDLE_FILE_INFORMATION info;wchar_t*wide;int length;DWORD got;unsigned char*p,extra;size_t size;
	if(!path||strlen(path)>32700){seterr(e,en,"file path is missing or too long");return 0;}
	length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
	if(!length){seterr(e,en,"file path is not UTF-8");return 0;}
	wide=malloc((size_t)length*sizeof(*wide));if(!wide){seterr(e,en,"allocation failed");return 0;}
	if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,length)){free(wide);seterr(e,en,"file path conversion failed");return 0;}
	file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);free(wide);
	if(file==INVALID_HANDLE_VALUE){seterr(e,en,"file could not be opened");return 0;}
	if(!GetFileInformationByHandle(file,&info)||GetFileType(file)!=FILE_TYPE_DISK||
	   (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
	   info.nFileSizeHigh||!info.nFileSizeLow||(size_t)info.nFileSizeLow>max)
	{CloseHandle(file);seterr(e,en,"file is reparse-pointed, not regular, or outside size limit");return 0;}
	size=info.nFileSizeLow;p=malloc(size);if(!p){CloseHandle(file);seterr(e,en,"allocation failed");return 0;}
	if(!ReadFile(file,p,(DWORD)size,&got,NULL)||got!=size||!ReadFile(file,&extra,1,&got,NULL)||got)
	{free(p);CloseHandle(file);seterr(e,en,"file changed or short-read during acquisition");return 0;}
	CloseHandle(file);*out=p;*n=size;return 1;
#else
	struct stat a,b;FILE*f;unsigned char*p;size_t got;
	if(!path||lstat(path,&a)||!S_ISREG(a.st_mode)){seterr(e,en,"file is missing, symlinked, or not regular");return 0;}
	if(a.st_size<=0||(uint64_t)a.st_size>max){seterr(e,en,"file size is outside configured limit");return 0;}
	f=fopen(path,"rb");if(!f){seterr(e,en,"file could not be opened");return 0;}p=malloc((size_t)a.st_size);if(!p){fclose(f);seterr(e,en,"allocation failed");return 0;}
	got=fread(p,1,(size_t)a.st_size,f);if(got!=(size_t)a.st_size||fgetc(f)!=EOF||fstat(fileno(f),&b)||a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||a.st_size!=b.st_size){free(p);fclose(f);seterr(e,en,"file changed or short-read during acquisition");return 0;}
	fclose(f);*out=p;*n=got;return 1;
#endif
}

static int has_slot(const struct LevView*l,size_t wanted){size_t i;for(i=0;i<l->slotCount;i++)if((((size_t)rd32(l->slots+4*i)>>2)<<2)==wanted)return 1;return 0;}
static int lev_map(const unsigned char*f,size_t n,struct LevView*l,char*e,size_t en)
{
	size_t i,end;uint32_t nb;unsigned char*seen;if(n<8){seterr(e,en,"LEV too small");return 0;}memset(l,0,sizeof(*l));l->p=f+4;l->n=n-4;l->map=rd32(f);
	if((l->map&3)||l->map<0x190||!fits(l->map,4,l->n)){seterr(e,en,"invalid LEV pointer-map offset");return 0;}nb=rd32(l->p+l->map);
	if((nb&3)||nb/4>l->map/4){seterr(e,en,"invalid LEV pointer-map byte count");return 0;}end=l->map+4+(size_t)nb;
	if(end!=l->n){seterr(e,en,"truncated pointer map or trailing LEV bytes");return 0;}l->slots=l->p+l->map+4;l->slotCount=nb/4;
	seen=(unsigned char*)calloc((l->map+3)/4,1);if(!seen){seterr(e,en,"pointer-map validation allocation failed");return 0;}
	for(i=0;i<l->slotCount;i++){uint32_t raw=rd32(l->slots+4*i);size_t s=((size_t)raw>>2)<<2;uint32_t t;if(raw&3u){free(seen);seterr(e,en,"unaligned pointer-map slot");return 0;}if(!fits(s,4,l->map)){free(seen);seterr(e,en,"pointer-map slot outside retained payload");return 0;}if(seen[s/4]){free(seen);seterr(e,en,"duplicate pointer-map slot");return 0;}seen[s/4]=1;t=rd32(l->p+s);if((t&0xf0000000u)||t>=l->map){free(seen);seterr(e,en,"patched or out-of-range pointer target");return 0;}}
	free(seen);
	return 1;
}
static int pointer(const struct LevView*l,size_t slot,size_t need,size_t*out,char*e,size_t en){uint32_t v;if(!fits(slot,4,l->map)){seterr(e,en,"required pointer slot outside LEV");return 0;}v=rd32(l->p+slot);if(!v){*out=0;return 1;}if(!has_slot(l,slot)||!fits(v,need,l->map)){seterr(e,en,"required pointer missing from map or out of range");return 0;}*out=v;return 1;}

static int vrm_image(const unsigned char*p,size_t n,char*e,size_t en){unsigned x,y,w,h;size_t px;if(n<20){seterr(e,en,"VRM header truncated");return 0;}x=p[12]|p[13]<<8;y=p[14]|p[15]<<8;w=p[16]|p[17]<<8;h=p[18]|p[19]<<8;if(!w||!h||x+w>1024||y+h>512||(size_t)w>SIZE_MAX/(size_t)h/2){seterr(e,en,"VRM rectangle invalid");return 0;}px=(size_t)w*h*2;if(n!=20+px){seterr(e,en,"VRM pixel size invalid");return 0;}return 1;}
static int vrm_valid(const unsigned char*p,size_t n,char*e,size_t en){size_t at;if(n<4){seterr(e,en,"VRM too small");return 0;}if(rd32(p)!=0x20)return vrm_image(p,n,e,en);at=4;for(;;){size_t z;if(!fits(at,4,n)){seterr(e,en,"packed VRM missing terminator");return 0;}z=rd32(p+at);at+=4;if(!z)return at==n?1:(seterr(e,en,"packed VRM trailing bytes"),0);if(z<20||!fits(at,z,n)||!vrm_image(p+at,z,e,en))return 0;at+=z;}}

static void evidence(struct CustomContentVerification*o,int m,int value,const char*why){o->fileAnalysis[m].result=value;snprintf(o->fileAnalysis[m].reason,sizeof o->fileAnalysis[m].reason,"%s",why);}
static void derive(struct CustomContentVerification*o){struct CustomContentMeasurements*m=&o->measured;evidence(o,0,m->checkpoints?CTR_CCV_DETECTED:CTR_CCV_NOT_DETECTED,m->checkpoints?"recognized checkpoint graph detected":"recognized checkpoint graph not detected");evidence(o,1,m->checkpoints&&m->relicCrates?CTR_CCV_DETECTED:CTR_CCV_NOT_DETECTED,m->checkpoints&&m->relicCrates?"recognized checkpoint graph and relic crates detected":"recognized relic-race structure not detected");evidence(o,2,m->checkpoints&&m->letterC&&m->letterT&&m->letterR?CTR_CCV_DETECTED:CTR_CCV_NOT_DETECTED,m->checkpoints&&m->letterC&&m->letterT&&m->letterR?"recognized checkpoint graph and C/T/R instances detected":"recognized CTR structure not detected");evidence(o,3,m->checkpoints&&m->spawns>=2&&m->navPaths?CTR_CCV_DETECTED:CTR_CCV_NOT_DETECTED,m->checkpoints&&m->spawns>=2&&m->navPaths?"recognized checkpoint, spawn, and navigation structures detected":"recognized arcade structure not detected");evidence(o,4,m->spawns&&m->crystals?CTR_CCV_DETECTED:CTR_CCV_NOT_DETECTED,m->spawns&&m->crystals?"recognized spawn and crystals detected":"recognized crystal structure not detected");}

static int analyze(const unsigned char*f,size_t n,struct CustomContentVerification*o,char*e,size_t en)
{
	struct LevView l;size_t inst=0,cp=0,nav=0,i;uint32_t count;if(!lev_map(f,n,&l,e,en))return 0;
	count=rd32(l.p+INST_N);if(count>65536||!pointer(&l,INST_P,(size_t)count*INST_SIZE,&inst,e,en)||(count&&!inst)){seterr(e,en,"invalid instance table");return 0;}o->measured.instances=count;
	for(i=0;i<count;i++){size_t model=0;uint32_t m;if(!pointer(&l,inst+i*INST_SIZE+INST_MODEL_P,MODEL_ID+2,&model,e,en)||!model){seterr(e,en,"invalid instance model pointer");return 0;}m=(uint32_t)l.p[model+MODEL_ID]|((uint32_t)l.p[model+MODEL_ID+1]<<8);switch(m){case PU_WUMPA:o->measured.looseWumpa++;break;case PU_FRUIT:o->measured.fruitCrates++;o->measured.ordinaryCrates++;break;case PU_RANDOM:case PU_TNT:case STATIC_TNT:o->measured.ordinaryCrates++;break;case PU_TIME1:case PU_TIME2:case PU_TIME3:case STATIC_TIME1:case STATIC_TIME2:case STATIC_TIME3:o->measured.relicCrates++;break;case LETTER_C:o->measured.letterC++;break;case LETTER_T:o->measured.letterT++;break;case LETTER_R:o->measured.letterR++;break;case CRYSTAL:o->measured.crystals++;break;case START:o->measured.startLines++;break;case FINISH:o->measured.finishLines++;break;}}
	count=rd32(l.p+CHECK_N);if(count){unsigned char seen[255]={0};size_t cur=0,steps=0;if(count>255||!pointer(&l,CHECK_P,(size_t)count*CHECK_SIZE,&cp,e,en)||!cp){seterr(e,en,"invalid checkpoint table");return 0;}for(i=0;i<count;i++){size_t k;for(k=8;k<12;k++){unsigned v=l.p[cp+i*12+k];if(v!=0xff&&v>=count){seterr(e,en,"checkpoint index out of range");return 0;}}}while(!seen[cur]&&steps<count){seen[cur]=1;cur=l.p[cp+cur*12+8];steps++;if(cur==0xff||cur>=count){seterr(e,en,"checkpoint chain does not cycle");return 0;}}o->measured.checkpoints=count;}
	for(i=0;i<8;i++){size_t j;int nz=0;for(j=0;j<12;j++)nz|=l.p[SPAWN_P+i*12+j];if(nz)o->measured.spawns++;}
	if(!pointer(&l,NAV_P,12,&nav,e,en))return 0;
	if(nav)for(i=0;i<3;i++){size_t h=0;unsigned points;if(!pointer(&l,nav+i*4,NAV_HEAD,&h,e,en))return 0;if(!h||rd16s(l.p+h)!=-0x1303)continue;points=l.p[h+2]|l.p[h+3]<<8;if(!points||!fits(h,NAV_HEAD+((size_t)points+1)*NAV_FRAME,l.map)){seterr(e,en,"navigation frame range invalid");return 0;}o->measured.navPaths++;}
	derive(o);o->loadable=1;return 1;
}

static void reset_report(struct CustomContentOwnedPair*p){char a[NATIVE_SHA256_HEX_BYTES],b[NATIVE_SHA256_HEX_BYTES];snprintf(a,sizeof a,"%s",p->report.levSha256);snprintf(b,sizeof b,"%s",p->report.vrmSha256);memset(&p->report,0,sizeof p->report);snprintf(p->report.levSha256,sizeof p->report.levSha256,"%s",a);snprintf(p->report.vrmSha256,sizeof p->report.vrmSha256,"%s",b);p->report.levBytes=p->levBytes;p->report.vrmBytes=p->vrmBytes;}
static void analysis_error(struct CustomContentVerification*o,const char*why){int i;for(i=0;i<CTR_CCV_MODE_COUNT;i++)evidence(o,i,CTR_CCV_ERROR,why);}
int CustomContentVerify_AcquirePair(const char*a,const char*b,struct CustomContentOwnedPair*p,char*e,size_t en){if(!p){seterr(e,en,"output pair required");return 0;}memset(p,0,sizeof(*p));if(!read_file(a,LEV_MAX,&p->lev,&p->levBytes,e,en)||CTR_CCV_FAULT(CTR_CCV_FAULT_AFTER_LEV_COPY)){seterr(e,en,"LEV acquisition fault");CustomContentVerify_FreePair(p);return 0;}if(!read_file(b,VRM_MAX,&p->vrm,&p->vrmBytes,e,en)||CTR_CCV_FAULT(CTR_CCV_FAULT_AFTER_VRM_COPY)){seterr(e,en,"VRM acquisition fault");CustomContentVerify_FreePair(p);return 0;}digest(p->lev,p->levBytes,p->report.levSha256);digest(p->vrm,p->vrmBytes,p->report.vrmSha256);if(CTR_CCV_FAULT(CTR_CCV_FAULT_AFTER_HASHING)){seterr(e,en,"hashing fault");CustomContentVerify_FreePair(p);return 0;}p->report.levBytes=p->levBytes;p->report.vrmBytes=p->vrmBytes;seterr(e,en,"");return 1;}
int CustomContentVerify_AnalyzePair(struct CustomContentOwnedPair*p,char*e,size_t en){if(!p||!p->lev||!p->vrm){seterr(e,en,"acquired pair required");return 0;}reset_report(p);if(!analyze(p->lev,p->levBytes,&p->report,e,en)){analysis_error(&p->report,e&&e[0]?e:"LEV analysis failed");p->report.loadable=0;return 0;}p->report.loadable=0;seterr(e,en,"");return 1;}
int CustomContentVerify_ValidateLoadablePair(struct CustomContentOwnedPair*p,char*e,size_t en){if(!CustomContentVerify_AnalyzePair(p,e,en))return 0;if(!vrm_valid(p->vrm,p->vrmBytes,e,en)){p->report.loadable=0;return 0;}p->report.loadable=1;seterr(e,en,"");return 1;}
int CustomContentVerify_ReadPair(const char*a,const char*b,struct CustomContentOwnedPair*p,char*e,size_t en){if(!CustomContentVerify_AcquirePair(a,b,p,e,en))return 0;if(!CustomContentVerify_ValidateLoadablePair(p,e,en)){CustomContentVerify_FreePair(p);return 0;}return 1;}
void CustomContentVerify_FreePair(struct CustomContentOwnedPair*p){if(!p)return;free(p->lev);free(p->vrm);memset(p,0,sizeof(*p));}
int CustomContentVerify_Files(const char*a,const char*b,struct CustomContentVerification*o,char*e,size_t en){struct CustomContentOwnedPair p;if(!o){seterr(e,en,"output required");return 0;}if(!CustomContentVerify_ReadPair(a,b,&p,e,en)){memset(o,0,sizeof(*o));return 0;}*o=p.report;CustomContentVerify_FreePair(&p);return 1;}
/* Most basic missing structure first; every text fits two 13-character menu
   lines, and the two usual ones fit one. Time Trial content often has a lap
   graph but no AI paths; its kart starts at the origin (all eight start slots
   zero, measured as none). */
int CustomContentVerify_ArcadeRaceReason(const struct CustomContentVerification*r,char*why,size_t n)
{
	const struct CustomContentMeasurements*m;
	char text[CTR_CCV_ARCADE_REASON_MAX];
	int ok=0;
	if(!r||!r->loadable||r->fileAnalysis[CTR_CCV_ARCADE].result==CTR_CCV_ERROR)snprintf(text,sizeof text,"Files failed checks");
	else if(m=&r->measured,r->fileAnalysis[CTR_CCV_ARCADE].result==CTR_CCV_DETECTED&&m->spawns>=8){text[0]=0;ok=1;}
	else if(!m->checkpoints)snprintf(text,sizeof text,"No lap checkpoints");
	else if(!m->navPaths)snprintf(text,sizeof text,"No AI paths");
	else if(m->spawns<8)snprintf(text,sizeof text,"Grid: %lu of 8",m->spawns);
	else snprintf(text,sizeof text,"No Arcade structures");
	seterr(why,n,text);
	return ok;
}
int CustomContentVerify_TimeTrialReason(const struct CustomContentVerification*r,char*why,size_t n)
{
	const char*text="";
	if(!r||!r->loadable||r->fileAnalysis[CTR_CCV_TIME_TRIAL].result==CTR_CCV_ERROR)text="Files failed checks";
	else if(r->fileAnalysis[CTR_CCV_TIME_TRIAL].result!=CTR_CCV_DETECTED)text="No lap checkpoints";
	seterr(why,n,text);
	return !text[0];
}
const char*CustomContentVerify_ModeSlug(int m){static const char*s[]={"time_trial","relic_race","ctr_challenge","arcade","crystal_challenge"};return m>=0&&m<5?s[m]:"unknown";}
const char*CustomContentVerify_EvidenceText(int r){if(r==CTR_CCV_DETECTED)return"Detected";if(r==CTR_CCV_NOT_DETECTED)return"Not detected";if(r==CTR_CCV_ERROR)return"Error";return"Indeterminate";}
#endif
