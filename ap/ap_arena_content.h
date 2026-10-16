/* Authored arena content (#259). Anchors are stable pickup identities.
 * Mirrored from tools/fixtures/arcade-arenas-v1.json; no retail asset bytes. */
#ifndef AP_ARENA_CONTENT_H
#define AP_ARENA_CONTENT_H
static inline int AP_ArcadeArenaIndex(int level) {
 return level==20 ? 0 : level==22 ? 1 : level==24 ? 2 : -1;
}
static inline const char *AP_ArenaName(int level) {
 switch(level) {
 case 18:return "Nitro Court"; case 19:return "Rampage Ruins";
 case 20:return "Parking Lot"; case 21:return "Skull Rock";
 case 22:return "The North Bowl"; case 23:return "Rocky Road";
 case 24:return "Lab Basement"; default:return 0;
 }
}
static inline int AP_ArenaCompletionCode(int level) {
 int a=AP_ArcadeArenaIndex(level);
 if(a>=0)return 35028500+a;
 return level==21?35013000:level==19?35013001:level==23?35013002:level==18?35013003:0;
}
static inline int AP_ArenaPickupCode(int level,int ordinal) {
 int a=AP_ArcadeArenaIndex(level);
 if(ordinal<0 || ordinal>=20)return 0;
 if(a>=0)return 35028520+20*a+ordinal;
 a=level==21?0:level==19?1:level==23?2:level==18?3:-1;
 return a<0?0:35028400+20*a+ordinal;
}
static inline int AP_ArenaTimeLimit(int level) {
 return AP_ArcadeArenaIndex(level)>=0?576000:level==21?79680:level==19?81600:level==23?76800:level==18?115200:0;
}
static const short ap_arcade_anchors[3][20][3] = {
 { /* Parking Lot */
  {-3000,384,-3000},
  {-4800,384,-3000},
  {-3000,384,-4800},
  {3000,384,-3000},
  {4800,384,-3000},
  {3000,384,-4800},
  {3000,384,3000},
  {4800,384,3000},
  {3000,384,4800},
  {-3000,384,3000},
  {-4800,384,3000},
  {-3000,384,4800},
  {0,0,-4800},
  {4800,0,0},
  {0,0,4800},
  {-4800,0,0},
  {-800,0,-800},
  {800,0,-800},
  {800,0,800},
  {-800,0,800},
 },
 { /* The North Bowl */
  {1000,0,0},
  {0,0,1000},
  {-1000,0,0},
  {0,0,-1000},
  {4000,380,0},
  {2828,-1,2828},
  {0,0,4000},
  {-2828,-1,2828},
  {-4000,0,0},
  {-2828,-650,-2828},
  {0,159,-4000},
  {2828,254,-2828},
  {7000,768,600},
  {5515,768,5515},
  {-600,768,7000},
  {-5515,768,5515},
  {-7000,768,-600},
  {-5515,768,-5515},
  {600,768,-7000},
  {5515,768,-5515},
 },
 { /* Lab Basement */
  {-4416,0,-3800},
  {-4416,0,-2304},
  {-4416,0,0},
  {-4416,0,2304},
  {-4416,0,3800},
  {4416,0,-3800},
  {4416,0,-2304},
  {4416,0,0},
  {4416,0,2304},
  {4416,0,3800},
  {-1800,0,-4352},
  {0,0,-4352},
  {1800,0,-4352},
  {-1800,0,4352},
  {0,0,4352},
  {1800,0,4352},
  {-1100,0,-1100},
  {1100,0,-1100},
  {1100,0,1100},
  {-1100,0,1100},
 },
};
#endif
