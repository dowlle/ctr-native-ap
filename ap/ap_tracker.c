/* Adventure map tracker. A read-only native overlay, drawn after presentation
 * so its text and pins are not reduced through the PS1 framebuffer. */
#include "ap_tracker.h"
#include "ap_tracker_assets.h"
#include <platform/native_assets.h>
#include <platform/native_disc_image.h>
#include <platform/native_renderer.h>
#include <platform/native_custom_tracks.h>
#include <SDL3/SDL.h>
extern SDL_Window *g_window;

#define TRACKER_W 1280
#define TRACKER_H 800
#define TRACKER_MINT AP_TrackerRGBA(112, 232, 190, 255)
#define TRACKER_WHITE AP_TrackerRGBA(245, 245, 245, 255)
#define TRACKER_GOLD AP_TrackerRGBA(255, 161, 24, 255)
#define TRACKER_GRAY AP_TrackerRGBA(132, 140, 147, 255)
#define TRACKER_GREEN AP_TrackerRGBA(83, 246, 121, 255)

typedef struct { int w, h; uint32_t *pixels; } AP_TrackerSprite;
typedef struct {
	int physical, destination, kind, bit, x, y, cx, cy, cw, ch;
	char title[160], pad[96];
} AP_TrackerNode;

static struct {
	int open, hub, focus, pinned, count, loaded;
	float mouse_x, mouse_y;
	unsigned mouse_buttons;
	AP_TrackerHubAsset hubs[AP_TRACKER_HUBS];
	AP_TrackerSprite glyphs[2][96];
	AP_TrackerNode nodes[AP_TRACKER_PADS + 1];
	uint32_t *canvas;
	int map_x, map_y, map_w, map_h;
} ap_tracker;

static void AP_TrackerRect(int x, int y, int w, int h, uint32_t color)
{
	int i, j;
	if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
	if (x + w > TRACKER_W) w = TRACKER_W - x;
	if (y + h > TRACKER_H) h = TRACKER_H - y;
	for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) ap_tracker.canvas[j * TRACKER_W + i] = color;
}
static void AP_TrackerLine(int x0, int y0, int x1, int y1, uint32_t color, int thick)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, e;
	for (;;) {
		AP_TrackerRect(x0 - thick / 2, y0 - thick / 2, thick, thick, color);
		if (x0 == x1 && y0 == y1) break;
		e = 2 * err; if (e >= dy) { err += dy; x0 += sx; } if (e <= dx) { err += dx; y0 += sy; }
	}
}
static void AP_TrackerBorder(int x, int y, int w, int h, uint32_t color, int thick)
{
	AP_TrackerRect(x, y, w, thick, color); AP_TrackerRect(x, y + h - thick, w, thick, color);
	AP_TrackerRect(x, y, thick, h, color); AP_TrackerRect(x + w - thick, y, thick, h, color);
}
static void AP_TrackerCircle(int x, int y, int radius, uint32_t color, int outline)
{
	int i, j, d;
	for (j = -radius; j <= radius; j++) for (i = -radius; i <= radius; i++) {
		d = i * i + j * j;
		if (d <= radius * radius && (!outline || d >= (radius - outline) * (radius - outline)))
			AP_TrackerRect(x + i, y + j, 1, 1, color);
	}
}
static void AP_TrackerBlit(const uint32_t *pixels, int sw, int sh, int x, int y, int w, int h, uint32_t tint)
{
	int i, j; unsigned char mod[4]; memcpy(mod, &tint, 4);
	if (!pixels || sw < 1 || sh < 1 || w < 1 || h < 1) return;
	for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
		unsigned char src[4]; uint32_t pixel = pixels[(j * sh / h) * sw + i * sw / w];
		if (x + i < 0 || x + i >= TRACKER_W || y + j < 0 || y + j >= TRACKER_H) continue;
		memcpy(src, &pixel, 4); if (!src[3]) continue;
		ap_tracker.canvas[(y + j) * TRACKER_W + x + i] = AP_TrackerRGBA(src[0] * mod[0] / 255,
			src[1] * mod[1] / 255, src[2] * mod[2] / 255, src[3]);
	}
}
static void AP_TrackerText(const char *text, int x, int y, int height, uint32_t color, int max_width)
{
	int index = height >= 25 ? 1 : 0, start = x;
	const char *p; int natural = 0;
	for (p=text; *p; p++) {
		unsigned ch=(unsigned char)*p; AP_TrackerSprite *g;
		if(ch>='a' && ch<='z') ch-='a'-'A';
		if(ch<32 || ch>127) ch='?';
		g=&ap_tracker.glyphs[index][ch-32];
		natural += ch==' ' ? height/3 : (g->w ? g->w*height*3/(4*(g->h?g->h:height)) : height/3)+1;
	}
	if(natural>max_width) height=height*max_width/natural;
	for (; *text; text++) {
		unsigned ch = (unsigned char)*text; AP_TrackerSprite *g; int w;
		if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
		if (ch < 32 || ch > 127) ch = '?';
		g = &ap_tracker.glyphs[index][ch - 32];
		w = ch == ' ' ? height / 3 : (g->w ? g->w * height * 3 / (4 * (g->h ? g->h : height)) : height / 3);
		if (x + w - start > max_width) break;
		AP_TrackerBlit(g->pixels, g->w, g->h, x + 2, y + 2, w, height, AP_TrackerRGBA(0,0,0,255));
		AP_TrackerBlit(g->pixels, g->w, g->h, x, y, w, height, color);
		x += w + 1;
	}
}
static void AP_TrackerTick(int x, int y, int state)
{
	if (state == 2) {
		AP_TrackerLine(x, y + 7, x + 6, y + 13, TRACKER_GREEN, 4);
		AP_TrackerLine(x + 6, y + 13, x + 18, y, TRACKER_GREEN, 4);
	} else if (state == 1) AP_TrackerCircle(x + 8, y + 6, 6, TRACKER_WHITE, 2);
	else AP_TrackerLine(x, y + 6, x + 14, y + 6, TRACKER_GRAY, 3);
}
static void AP_TrackerArrow(int x,int y,int angle,int size,uint32_t color)
{
	int s=MATH_Sin(angle),c=MATH_Cos(angle),i;
	int px[3]={0,-size,size},py[3]={-size,size,size},tx[3],ty[3];
	for(i=0;i<3;i++) { tx[i]=x+(px[i]*c+py[i]*s)/4096; ty[i]=y+(py[i]*c-px[i]*s)/4096; }
	for(i=0;i<3;i++) { AP_TrackerLine(tx[i],ty[i],tx[(i+1)%3],ty[(i+1)%3],AP_TrackerRGBA(0,0,0,255),7);
		AP_TrackerLine(tx[i],ty[i],tx[(i+1)%3],ty[(i+1)%3],color,4); }
}
static void AP_TrackerPrize(int x,int y,int kind,int state)
{
	uint32_t color=state?TRACKER_GOLD:TRACKER_GRAY;
	if(kind==0) {
		AP_TrackerBorder(x+3,y,22,14,color,3);
		AP_TrackerLine(x+3,y+12,x+14,y+20,color,3); AP_TrackerLine(x+14,y+20,x+25,y+12,color,3);
		AP_TrackerLine(x+14,y+20,x+14,y+25,color,3); AP_TrackerRect(x+7,y+25,15,3,color);
		AP_TrackerBorder(x-2,y+2,6,10,color,2); AP_TrackerBorder(x+25,y+2,6,10,color,2);
	} else if(kind==1) {
		AP_TrackerCircle(x+14,y+13,14,color,2); AP_TrackerText("CTR",x+3,y+8,10,color,25);
	} else {
		uint32_t tier=state?kind==2?AP_TrackerRGBA(97,202,255,255):kind==3?TRACKER_GOLD:AP_TrackerRGBA(202,211,255,255):TRACKER_GRAY;
		AP_TrackerLine(x+14,y,x+24,y+12,tier,3); AP_TrackerLine(x+24,y+12,x+14,y+27,tier,3);
		AP_TrackerLine(x+14,y+27,x+4,y+12,tier,3); AP_TrackerLine(x+4,y+12,x+14,y,tier,3);
		AP_TrackerText(kind==2?"S":kind==3?"G":"P",x+10,y+7,12,tier,15);
	}
	AP_TrackerTick(x+38,y+11,state);
}
static int AP_TrackerCodeState(long code)
{
	return code >= 0 && ap_net_location_exists(code) ? (ap_net_location_checked(code) ? 2 : 1) : 0;
}
static int AP_TrackerBitState(int bit)
{ return AP_LocationExistsByBit(bit) ? (AP_LocationCheckedByBit(bit) ? 2 : 1) : 0; }

static unsigned char *AP_TrackerReadSubfile(int index, size_t *size)
{
	struct BigHeader *big = sdata->ptrBigfile1; struct BigEntry *e; unsigned char *bytes; FILE *f;
	struct NativeDiscImageFile disc; size_t rounded;
	if (!big || index < 0 || index >= big->numEntry) return NULL;
	e = &((struct BigEntry *)BIG_GETENTRY(big))[index];
	if (e->offset < 0 || e->size < 1 || e->size > 8 * 1024 * 1024) return NULL;
	rounded = ((size_t)e->size + 2047) & ~(size_t)2047;
	bytes = (unsigned char *)malloc(rounded); if (!bytes) return NULL;
	f = NativeAssets_OpenHostBigfile("rb");
	if (f) {
		int ok = fseek(f, (long)e->offset * 2048, SEEK_SET) == 0 && fread(bytes, 1, e->size, f) == (size_t)e->size;
		fclose(f); if (!ok) { free(bytes); return NULL; }
	} else if (!NativeDiscImage_FindFile("BIGFILE.BIG", &disc) ||
	           !NativeDiscImage_ReadDataSectors(&disc, e->offset, (unsigned)(rounded / 2048), bytes)) {
		free(bytes); return NULL;
	}
	*size = (size_t)e->size; return bytes;
}
static int AP_TrackerLoadAssets(void)
{
	int hub, font, ch; uint16_t *vram;
	if (ap_tracker.loaded) return 1;
	vram = (uint16_t *)malloc(AP_TRACKER_VRAM_WORDS * sizeof *vram); if (!vram) return 0;
	for (hub = 0; hub < AP_TRACKER_HUBS; hub++) {
		size_t lev_size = 0, tim_size = 0;
		unsigned char *lev = AP_TrackerReadSubfile(BI_ADVENTUREHUB + hub * 3 + 1, &lev_size);
		unsigned char *tim = AP_TrackerReadSubfile(BI_ADVENTUREHUB + hub * 3, &tim_size);
		int ok = lev && tim && AP_TrackerDecodeVram(vram, tim, tim_size) &&
			AP_TrackerParseHub(&ap_tracker.hubs[hub], lev, lev_size, vram);
		free(lev); free(tim);
		if (!ok) { free(vram); AP_LogLine("[AP TRACKER] Failed to decode retail hub assets\n"); return 0; }
		{
			AP_TrackerHubAsset *h=&ap_tracker.hubs[hub]; int i;
			for(i=0;i<h->width*h->height;i++) {
				unsigned char b[4]; memcpy(b,&h->pixels[i],4);
				/* Retail map CLUT has opaque black outside its white paths. */
				if(b[0]<32 && b[1]<32 && b[2]<32) h->pixels[i]=0;
			}
		}
	}
	/* Font art comes from the game's own active VRAM and glyph mapping. */
	NativeRenderer_ReadVRAM(vram, 0, 0, 1024, 512);
	for (font = 0; font < 2; font++) {
		int type = font ? FONT_BIG : FONT_SMALL;
		struct IconGroup *group = sdata->gGT->iconGroup[data.font_IconGroupID[type]];
		struct Icon **icons;
		if (!group) { free(vram); return 0; } icons = ICONGROUP_GETICONS(group);
		for (ch = 33; ch < 128; ch++) {
			unsigned id = data.font_characterIconID[ch - 33]; AP_TrackerSprite *g = &ap_tracker.glyphs[font][ch - 32];
			int x, y; const unsigned char *t;
			if (id >= (unsigned)group->numIcons) continue;
			t = (const unsigned char *)&icons[id]->texLayout;
			g->w = (int)t[4] - t[0] + 1; g->h = (int)t[9] - t[1] + 1;
			if (g->w <= 0 || g->h <= 0 || g->w > 128 || g->h > 128) continue;
			g->pixels = (uint32_t *)malloc((size_t)g->w * g->h * 4); if (!g->pixels) { free(vram); return 0; }
			for (y = 0; y < g->h; y++) for (x = 0; x < g->w; x++) {
				uint32_t pixel = AP_TrackerTexel(vram, t, x, y); unsigned char b[4]; unsigned value;
				memcpy(b, &pixel, 4); value = b[0] > b[1] ? b[0] : b[1]; if (b[2] > value) value = b[2];
				/* Native font CLUT is dimmed for PSX modulation. Preserve its
				 * outline while restoring a readable face in the overlay. */
				if(value>48) value=255;
				g->pixels[y * g->w + x] = AP_TrackerRGBA(value, value, value, b[3]);
			}
		}
	}
	free(vram);
	ap_tracker.canvas = (uint32_t *)malloc(TRACKER_W * TRACKER_H * 4);
	if (!ap_tracker.canvas) return 0;
	ap_tracker.loaded = 1; return 1;
}

static void AP_TrackerProject(int wx, int wz, int *x, int *y)
{
	AP_TrackerHubAsset *h = &ap_tracker.hubs[ap_tracker.hub]; int mx, my;
	AP_TrackerMapPoint(h, wx, wz, &mx, &my);
	*x = ap_tracker.map_x + mx * ap_tracker.map_w / h->width;
	*y = ap_tracker.map_y + my * ap_tracker.map_h / h->height;
}
static void AP_TrackerBuildNodes(void)
{
	AP_TrackerHubAsset *h = &ap_tracker.hubs[ap_tracker.hub]; int i, left = 0, right = 0;
	int order[AP_TRACKER_PADS + 1];
	ap_tracker.map_w = 420; ap_tracker.map_h = h->height * ap_tracker.map_w * 4 / (h->width * 3);
	/* The game's 512x240 UI mapped to this 1280x800 canvas has a 4/3
	 * vertical-to-horizontal pixel ratio. Keep paths and pins in one space. */
	if (ap_tracker.map_h > 560) { ap_tracker.map_h = 560; ap_tracker.map_w = h->width * ap_tracker.map_h * 3 / (h->height * 4); }
	ap_tracker.map_x = (TRACKER_W - ap_tracker.map_w) / 2; ap_tracker.map_y = 115 + (560 - ap_tracker.map_h) / 2;
	ap_tracker.count = h->pad_count;
	for (i = 0; i < h->pad_count; i++) {
		AP_TrackerNode *n = &ap_tracker.nodes[i]; int physical = h->pads[i].physical, dest = ctr_cfg_warp_dest(physical);
		memset(n, 0, sizeof *n); n->physical = physical; n->destination = dest; n->kind = 0; n->bit = -1;
		AP_TrackerProject(h->pads[i].world_x, h->pads[i].world_z, &n->x, &n->y);
		if (dest >= 100 && dest < 105) {
			const char *custom = NULL;
#ifdef CTR_CUSTOM_TRACKS
			custom = CustomTrack_CupDisplayName(dest - 100, 1);
#endif
			n->kind = 2; n->bit = dest - 100 + ADV_REWARD_FIRST_GEM;
			snprintf(n->title, sizeof n->title, "%s", custom ? custom : sdata->lngStrings[data.AdvCups[dest - 100].lngIndex_CupName]);
		} else if (dest >= 0 && dest < 65) {
			snprintf(n->title, sizeof n->title, "%s", sdata->lngStrings[data.metaDataLEV[dest].name_LNG]);
			if (dest >= 18 && dest < 25) { n->kind = 1; n->bit = R232.battleTrackArr[dest - 18] + ADV_REWARD_FIRST_PURPLE_TOKEN; }
		} else snprintf(n->title, sizeof n->title, "DESTINATION %d", dest);
		if (physical >= 100 && physical < 105) snprintf(n->pad, sizeof n->pad, "%s PAD", sdata->lngStrings[data.AdvCups[physical - 100].lngIndex_CupName]);
		else if (physical >= 0 && physical < 65) snprintf(n->pad, sizeof n->pad, "%s PAD", sdata->lngStrings[data.metaDataLEV[physical].name_LNG]);
		order[i] = i;
	}
	/* Garage and exits are canonical hub item coordinates, independent of the
 * currently loaded hub. Include a checked garage rather than hiding it. */
	{
		s16 *item = D232.hubItemsXY_ptrArray[ap_tracker.hub];
		for (; item[0] != -1; item += 4) if (item[3] >= 0 && item[3] <= 4) {
			AP_TrackerNode *n = &ap_tracker.nodes[ap_tracker.count];
			static const char *names[] = {"RIPPER ROO", "PAPU PAPU", "KOMODO JOE", "PINSTRIPE"};
			memset(n, 0, sizeof *n); n->physical = -1; n->destination = -1; n->kind = 3;
			n->bit = ap_tracker.hub ? ADV_REWARD_FIRST_BOSS_KEY + ap_tracker.hub - 1 : AP_GOAL_BIT_OXIDE_FIRST;
			snprintf(n->title, sizeof n->title, "%s", ap_tracker.hub ? names[ap_tracker.hub - 1] : "N. OXIDE");
			AP_TrackerProject(item[0], item[1], &n->x, &n->y);
			order[ap_tracker.count] = ap_tracker.count; ap_tracker.count++; break;
		}
	}
	/* Stable vertical sorting keeps connectors uncrossed on each side. */
	for (i = 1; i < ap_tracker.count; i++) {
		int j = i, value = order[i]; while (j && ap_tracker.nodes[order[j - 1]].y > ap_tracker.nodes[value].y) { order[j] = order[j - 1]; j--; } order[j] = value;
	}
	for (i = 0; i < ap_tracker.count; i++) {
		AP_TrackerNode *n = &ap_tracker.nodes[order[i]]; int side = n->x < TRACKER_W / 2;
		/* Balance clustered pads (Gem Stone Valley has eight nodes). */
		if (side && left >= (ap_tracker.count+1)/2) side = 0;
		if (!side && right >= ap_tracker.count/2) side = 1;
		n->cx = side ? 28 : 876; n->cy = 112 + (side ? left++ : right++) * 142;
		n->cw = 376; n->ch = 126;
	}
	if (ap_tracker.focus >= ap_tracker.count) ap_tracker.focus = 0;
}

/* All enabled race-family checks are shown, including individual relic tiers.
 * Remaining podium / perfect checks are included in the summary below them. */
static void AP_TrackerRaceStates(int dest, int states[5])
{
	static const int bases[] = {ADV_REWARD_FIRST_TROPHY, ADV_REWARD_FIRST_CTR_TOKEN,
		ADV_REWARD_FIRST_SAPPHIRE_RELIC, ADV_REWARD_FIRST_GOLD_RELIC, ADV_REWARD_FIRST_PLATINUM_RELIC}; int i;
	for (i = 0; i < 5; i++) states[i] = dest >= 0 && dest < 18 ? AP_TrackerBitState(dest + bases[i]) : 0;
	if (dest == 16 || dest == 17) {
		states[0] = AP_TrackerCodeState(ctr_cfg.trial_track_locations[dest - 16][0]);
		states[1] = AP_TrackerCodeState(ctr_cfg.trial_track_locations[dest - 16][1]);
	}
}
static void AP_TrackerDrawNode(int index)
{
	AP_TrackerNode *n = &ap_tracker.nodes[index]; int x = n->cx, y = n->cy, i, states[5]; char text[96];
	int selected = index == ap_tracker.focus, boxes = AP_PadUncollectedBoxCount(n->destination);
	uint32_t body = AP_TrackerRGBA(10, 15, 18, 226);
	AP_TrackerRect(x, y, n->cw, n->ch, body); AP_TrackerBorder(x,y,n->cw,n->ch,TRACKER_MINT,selected ? 3 : 2);
	if (selected) AP_TrackerRect(x+5,y+5,n->cw-10,29,AP_TrackerRGBA(200,65,4,255));
	AP_TrackerText(n->title,x+12,y+9,23,TRACKER_GOLD,n->cw-24);
	AP_TrackerText(n->pad,x+12,y+39,14,TRACKER_WHITE,n->cw-24);
	if (n->kind == 0) {
		AP_TrackerRaceStates(n->destination,states);
		for (i=0;i<5;i++) {
			int bx = x+12+i*70;
			AP_TrackerPrize(bx,y+61,i,states[i]);
		}
	} else {
		int state = AP_TrackerBitState(n->bit);
#ifdef CTR_CUSTOM_TRACKS
		if (AP_CustomPadOwnsDestination(&ctr_cfg, n->destination)) state = AP_TrackerCodeState(ctr_cfg.custom_track.trophy_location);
#endif
		AP_TrackerText(n->kind==3?"BOSS CHECK":n->kind==1?"CRYSTAL CHECK":"CUP CHECK",x+12,y+64,18,TRACKER_GOLD,300);
		AP_TrackerTick(x+315,y+65,state);
	}
	if (n->kind != 3) {
		long wumpa = n->destination>=0 && n->destination<18 ? ctr_cfg.wumpa.tracks[n->destination] : -1;
		int mode=ctr_cfg.lettersanity_mode;
		if (ctr_cfg.wumpa.mode == CTR_CFG_WUMPA_GLOBAL && n->kind == 0) wumpa = ctr_cfg.wumpa.global_code;
		snprintf(text,sizeof text,"BOX %d",boxes); AP_TrackerText(text,x+12,y+96,16,TRACKER_WHITE,110);
		AP_TrackerText(ctr_cfg.wumpa.mode == CTR_CFG_WUMPA_GLOBAL ? "10W*" : "10W",x+130,y+96,16,TRACKER_GOLD,55); AP_TrackerTick(x+179,y+99,AP_TrackerCodeState(wumpa));
		if (n->kind==0 && n->destination>=0 && n->destination<18) for(i=0;i<3;i++) {
			long code=AP_LetterLocation(n->destination,i); int state=AP_TrackerCodeState(code), bx=x+216+i*49;
			int enabled=mode==3 || state>0; char letter[2] = {"CTR"[i],0};
			AP_TrackerText(enabled?letter:"-",bx,y+95,20,enabled && AP_LetterAvailable(n->destination,i)?TRACKER_GOLD:TRACKER_GRAY,22);
			if(state==2) AP_TrackerTick(bx+17,y+103,2);
			else if(enabled && !AP_LetterAvailable(n->destination,i)) {
				AP_TrackerBorder(bx+19,y+103,11,10,TRACKER_GRAY,2); AP_TrackerCircle(bx+24,y+103,4,TRACKER_GRAY,2);
			}
		}
	}
}
static void AP_TrackerDraw(void)
{
	AP_TrackerHubAsset *h = &ap_tracker.hubs[ap_tracker.hub]; int i; char line[256];
	memset(ap_tracker.canvas,0,TRACKER_W*TRACKER_H*4);
	AP_TrackerRect(16,24,1248,744,AP_TrackerRGBA(0,5,10,172));
	AP_TrackerBorder(16,24,1248,744,TRACKER_MINT,3);
	AP_TrackerLine(16,96,1264,96,TRACKER_MINT,2);
	AP_TrackerText(sdata->lngStrings[data.metaDataLEV[GEM_STONE_VALLEY+ap_tracker.hub].name_LNG],280,45,36,TRACKER_GOLD,750);
	AP_TrackerText("<",220,47,32,AP_TrackerRGBA(255,52,30,255),50);
	AP_TrackerText(">",1050,47,32,AP_TrackerRGBA(255,52,30,255),50);
	AP_TrackerBlit(h->pixels,h->width,h->height,ap_tracker.map_x+4,ap_tracker.map_y+6,ap_tracker.map_w,ap_tracker.map_h,AP_TrackerRGBA(0,0,0,255));
	AP_TrackerBlit(h->pixels,h->width,h->height,ap_tracker.map_x,ap_tracker.map_y,ap_tracker.map_w,ap_tracker.map_h,TRACKER_WHITE);
	for(i=0;i<ap_tracker.count;i++) {
		AP_TrackerNode *n=&ap_tracker.nodes[i]; int edge=n->cx<640?n->cx+n->cw:n->cx;
		int st=n->physical>=0?AP_PadState(n->physical,n->destination):0;
		uint32_t color=st==5?TRACKER_GRAY:st==1?AP_TrackerRGBA(255,57,43,255):st==3?TRACKER_GOLD:st==4?AP_TrackerRGBA(140,160,255,255):TRACKER_GREEN;
		AP_TrackerLine(n->x,n->y,edge,n->cy+24,TRACKER_MINT,2);
		AP_TrackerCircle(n->x,n->y,12,AP_TrackerRGBA(0,0,0,255),0);
		AP_TrackerCircle(n->x,n->y,8,color,0);
		if(i==ap_tracker.focus) { AP_TrackerCircle(n->x,n->y,21,TRACKER_MINT,3); AP_TrackerCircle(n->x,n->y,26,TRACKER_MINT,2); }
	}
	/* Exits stay at their physical map positions. */
	{
		s16 *item=D232.hubItemsXY_ptrArray[ap_tracker.hub];
		for(;item[0]!=-1;item+=4) if(item[3]<0) {
			int x,y; AP_TrackerProject(item[0]-0x200,item[1]-0x100,&x,&y);
			AP_TrackerArrow(x,y,0x1000-item[2],9,TRACKER_WHITE);
		}
	}
	if(sdata->gGT->levelID==GEM_STONE_VALLEY+ap_tracker.hub && sdata->gGT->drivers[0] && sdata->gGT->drivers[0]->instSelf) {
		int x,y; MATRIX *m=&sdata->gGT->drivers[0]->instSelf->matrix;
		AP_TrackerProject(m->t[0],m->t[2],&x,&y);
		AP_TrackerArrow(x,y,sdata->gGT->drivers[0]->rotCurr.y+0x800,10,TRACKER_GOLD);
	}
	for(i=0;i<ap_tracker.count;i++) AP_TrackerDrawNode(i);
	AP_TrackerLine(16,708,1264,708,TRACKER_MINT,2);
	if(ap_tracker.pinned && ap_tracker.count) {
		AP_TrackerNode *n=&ap_tracker.nodes[ap_tracker.focus]; int bits[48], remaining=n->physical>=0?AP_PadUncollectedGlowBits(n->destination,bits,48):0;
		snprintf(line,sizeof line,"%s: %d RACE CHECKS, %d BOXES, %d LETTERS LEFT",n->title,remaining,
			AP_PadUncollectedBoxCount(n->destination),AP_PadUncollectedLetterCount(n->destination));
	} else snprintf(line,sizeof line,"CHECK: O PENDING / TICK DONE    LETTER: ORANGE OWNED / LOCK NEEDED / - OFF%s",ap_net_is_connected()?"":"    OFFLINE");
	AP_TrackerText(line,32,718,14,TRACKER_WHITE,1210);
	AP_TrackerText("L1/R1 HUB   D-PAD FOCUS   * INSPECT   MOUSE HOVER/CLICK   ^ BACK",32,744,16,TRACKER_WHITE,1210);
}

int AP_TrackerOpen(void) { return ap_tracker.open; }
static void AP_TrackerClose(void)
{
	ap_tracker.open=0; ap_tracker.pinned=0;
	if(g_window && (SDL_GetWindowFlags(g_window)&SDL_WINDOW_FULLSCREEN)) SDL_HideCursor();
}
int AP_TrackerMenuFrame(void)
{
	struct GameTracker *gt=sdata->gGT; int tap=sdata->buttonTapPerPlayer[0], i;
	int context=gt && ctr_cfg_active() && (gt->gameMode1&ADVENTURE_ARENA) &&
		(gt->gameMode1&PAUSE_1) && LOAD_IsOpen_AdvHub() && sdata->ptrActiveMenu &&
		sdata->ptrActiveMenu->funcPtr==MainFreeze_MenuPtrDefault && !sdata->ptrDesiredMenu;
	float mx,my; unsigned buttons;
	if(!context) { if(ap_tracker.open) AP_TrackerClose(); return 0; }
	if(!ap_tracker.open) {
		if(!(tap&BTN_SQUARE)) return 0;
		if(!AP_TrackerLoadAssets()) { AP_TrackerShutdown(); return 0; }
		AH_Pause_Destroy(); ap_tracker.open=1; ap_tracker.hub=gt->levelID-GEM_STONE_VALLEY;
		if(ap_tracker.hub<0 || ap_tracker.hub>=5) ap_tracker.hub=0;
		ap_tracker.focus=0; ap_tracker.pinned=0; AP_TrackerBuildNodes();
		ap_tracker.mouse_buttons=SDL_GetMouseState(&ap_tracker.mouse_x,&ap_tracker.mouse_y);
		SDL_ShowCursor(); AP_LogLine("[AP TRACKER] enlarged hub map opened\n");
	} else {
		if(tap&(BTN_TRIANGLE|BTN_CIRCLE|BTN_START|BTN_SQUARE)) { AP_TrackerClose(); return 1; }
		if(tap&(BTN_L1|BTN_R1)) {
			ap_tracker.hub=(ap_tracker.hub+((tap&BTN_L1)?4:1))%5;
			ap_tracker.focus=0; ap_tracker.pinned=0; AP_TrackerBuildNodes();
		}
		if(tap&(BTN_UP|BTN_DOWN|BTN_LEFT|BTN_RIGHT)) {
			ap_tracker.focus=(ap_tracker.focus+((tap&(BTN_UP|BTN_LEFT))?ap_tracker.count-1:1))%ap_tracker.count; ap_tracker.pinned=0;
		}
		if(tap&BTN_CROSS) ap_tracker.pinned=!ap_tracker.pinned;
	}
	buttons=SDL_GetMouseState(&mx,&my);
	if(mx!=ap_tracker.mouse_x || my!=ap_tracker.mouse_y || (buttons&SDL_BUTTON_LMASK)) {
		int vx,vy,vw,vh;
		if(NativeRenderer_DisplayRectToWindow(0,0,activeDispEnv.disp.w,activeDispEnv.disp.h,&vx,&vy,&vw,&vh) && vw>0 && vh>0) {
			int x=(int)((mx-vx)*TRACKER_W/vw), y=(int)((my-vy)*TRACKER_H/vh);
			for(i=0;i<ap_tracker.count;i++) {
				AP_TrackerNode *n=&ap_tracker.nodes[i]; int dx=x-n->x,dy=y-n->y;
				if(dx*dx+dy*dy<=28*28 || (x>=n->cx && x<n->cx+n->cw && y>=n->cy && y<n->cy+n->ch)) {
					ap_tracker.focus=i;
					if((buttons&SDL_BUTTON_LMASK) && !(ap_tracker.mouse_buttons&SDL_BUTTON_LMASK)) ap_tracker.pinned=!ap_tracker.pinned;
					break;
				}
			}
			if((buttons&SDL_BUTTON_LMASK) && !(ap_tracker.mouse_buttons&SDL_BUTTON_LMASK) && y>=30 && y<96) {
				if(x>=180 && x<280) ap_tracker.hub=(ap_tracker.hub+4)%5;
				else if(x>=1020 && x<1120) ap_tracker.hub=(ap_tracker.hub+1)%5;
				AP_TrackerBuildNodes();
			}
		}
	}
	ap_tracker.mouse_x=mx; ap_tracker.mouse_y=my; ap_tracker.mouse_buttons=buttons;
	AP_TrackerBuildNodes(); AP_TrackerDraw(); return 1;
}
void AP_TrackerPresent(void)
{
	if(ap_tracker.open && (!sdata->gGT || !(sdata->gGT->gameMode1&PAUSE_1) || !ctr_cfg_active())) AP_TrackerClose();
	if(ap_tracker.open && ap_tracker.canvas) NativeRenderer_PresentOverlayRGBA((const unsigned char *)ap_tracker.canvas,TRACKER_W,TRACKER_H);
}
void AP_TrackerShutdown(void)
{
	int h,f,c; AP_TrackerClose();
	for(h=0;h<5;h++) free(ap_tracker.hubs[h].pixels);
	for(f=0;f<2;f++) for(c=0;c<96;c++) free(ap_tracker.glyphs[f][c].pixels);
	free(ap_tracker.canvas); memset(&ap_tracker,0,sizeof ap_tracker);
}
