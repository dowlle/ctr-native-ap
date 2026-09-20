#ifndef AP_TRIAL_LETTER_LOGIC_H
#define AP_TRIAL_LETTER_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <platform/native_sha256.h>

// Confirmed editor anchors, ctr-world-s16, C/T/R order. No FP8
// conversion, terrain projection or AP-box ground offset belongs here.
static const short AP_TRIAL_LETTER_POS[2][3][3] = {
    {{-5948,178,-5331}, {-7941,214,3053}, {-4507,1,8477}},
    {{-2319,1,-1555}, {-10955,1,-10325}, {-2420,1,7794}}
};
static const short AP_TRIAL_LETTER_YAW[2][3] = {
    {2662,1853,2133}, {4031,3658,1509}
};

// Exact NTSC-U Crash Cove 1P LEV/VRM, BIGFILE entries 25/24. The pinned
// digest is the graph contract, not a general-purpose untrusted LEV parser.
// No extracted game bytes are shipped. Refuse other revisions before fixup.
#define AP_TRIAL_LEV_SIZE 766928
#define AP_TRIAL_VRM_SIZE 458808
#define AP_TRIAL_LEV_SHA "f6fb070b1a706389f882f4c337c5c7e49a45b705a3469afe67f8ba1eeba71330"
#define AP_TRIAL_VRM_SHA "ab798b82c339e1e10d21fd0b67034311056f7da5013b85b46d2e3dfad7af8aa5"
static const unsigned AP_TRIAL_MODEL_OFFSET[3] = {0x94430,0x94060,0x9384c};
static const unsigned AP_TRIAL_LAYOUT_COUNT[3] = {72,27,68};

static int AP_TrialDigest(const unsigned char *bytes, size_t size, const char *expected)
{
    struct NativeSha256Ctx ctx;
    unsigned char digest[32];
    char hex[65];
    if (!bytes) return 0;
    NativeSha256_Init(&ctx);
    NativeSha256_Update(&ctx, bytes, size);
    NativeSha256_Final(&ctx, digest);
    NativeSha256_ToHex(digest, hex);
    return NativeSha256_HexEquals(expected, hex);
}

static unsigned AP_TrialU32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1]<<8) |
           ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24);
}

// Validate the whole relocation table before making a single write. The
// engine permits tag bits in the low two bits of each patch slot.
static int AP_TrialPointerMapValid(const unsigned char *file, size_t size)
{
    unsigned map, bytes, i, slot, target;
    const unsigned char *body;
    if (!file || size < 8) return 0;
    body = file+4;
    map = AP_TrialU32(file);
    if (map < 4 || (map & 3) || map > size-8) return 0;
    bytes = AP_TrialU32(body+map);
    if ((bytes & 3) || bytes > size-8-map) return 0;
    for (i=0; i<bytes; i+=4) {
        slot = AP_TrialU32(body+map+4+i) & ~3u;
        if (slot > map-4) return 0;
        target = AP_TrialU32(body+slot);
        if (target >= map) return 0;
    }
    return 1;
}

// Retail C/T/R letters are class-4 TOUCH hitboxes. The engine first intersects
// the kart's swept 25-unit AABB with the authored BSP box, then dispatches the
// retail pickup callback. Reuse that exact predicate with the donor box moved
// to the authored trial-track position.
static int AP_TrialLetterHit(const SVec3 *prev, const SVec3 *curr,
                             const struct BoundingBox *box)
{
    const int radius=0x19;
    int minX=(prev->x<curr->x?prev->x:curr->x)-radius;
    int minY=(prev->y<curr->y?prev->y:curr->y)-radius;
    int minZ=(prev->z<curr->z?prev->z:curr->z)-radius;
    int maxX=(prev->x>curr->x?prev->x:curr->x)+radius;
    int maxY=(prev->y>curr->y?prev->y:curr->y)+radius;
    int maxZ=(prev->z>curr->z?prev->z:curr->z)+radius;
    return minX<=box->max.x && box->min.x<=maxX &&
           minY<=box->max.y && box->min.y<=maxY &&
           minZ<=box->max.z && box->min.z<=maxZ;
}

// Pixel lookup in the pinned packed-TIM VRM, without uploading the donor
// track over the current track's textures. Bounds checked independently.
static int AP_TrialVramWord(const unsigned char *vrm, size_t size, unsigned x, unsigned y, unsigned *out)
{
    size_t off=4;
    if (!vrm || size<4 || AP_TrialU32(vrm)!=0x20) return 0;
    while (off+4<=size) {
        unsigned n=AP_TrialU32(vrm+off), rx,ry,w,h;
        size_t pixel;
        if (!n) return 0;
        if (n<20 || n>size-off-4) return 0;
        rx=vrm[off+16]|(vrm[off+17]<<8); ry=vrm[off+18]|(vrm[off+19]<<8);
        w=vrm[off+20]|(vrm[off+21]<<8); h=vrm[off+22]|(vrm[off+23]<<8);
        if ((size_t)w*h > (n-20)/2) return 0;
        if (x>=rx && x-rx<w && y>=ry && y-ry<h) {
            pixel=off+24+2*((size_t)(y-ry)*w+x-rx);
            *out=vrm[pixel]|(vrm[pixel+1]<<8); return 1;
        }
        off+=n+4;
    }
    return 0;
}

static int AP_TrialTextureDecode(const unsigned char *vrm, size_t size, unsigned char rgba[64*2*4])
{
    unsigned x,y,word,color,index;
    for (y=0; y<2; ++y) for (x=0; x<64; ++x) {
        unsigned char *p=rgba+4*(y*64+x);
        if (!AP_TrialVramWord(vrm,size,512+x/4,244+y,&word)) return 0;
        index=(word >> ((x%4)*4)) & 15;
        if (!AP_TrialVramWord(vrm,size,800+index,x<32?291:292,&color)) return 0;
        p[0]=(color&31)*255/31; p[1]=((color>>5)&31)*255/31;
        p[2]=((color>>10)&31)*255/31; p[3]=color?255:0;
    }
    return 1;
}
#endif
