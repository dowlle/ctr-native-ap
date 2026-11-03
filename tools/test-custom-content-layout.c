// cc -m32 -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -DBUILD=926 -I include -I . tools/test-custom-content-layout.c -o /tmp/test-custom-content-layout
/* The Custom Content package page's layout (game/230/MM_CustomContentView.c),
   minimum fix from the Deck menu scan of 2026-11-03: with the retail font
   widths, every text stays inside the menu frame, no two texts overlap
   ("RELIC TIMES: NOT SET" ran into "SELECT: IMPORT TRACK FILES"), the status
   and seed lines are whole, the help message is not cut mid-word at three
   lines, and the dimmed panel is drawn. */
#include <stdio.h>
#include <string.h>
#include <common.h>
struct sData sdata_static;
struct Data data;
#define DecalFont_DrawLineOT UnusedDraw
#include "../game/DecalFont.c"
#undef DecalFont_DrawLineOT

static int checks, failures, panels;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
#define CHECKM(x, m) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s (%s)\n", __LINE__, #x, m); } } while (0)
struct Box { int x, y, w, h; char text[200]; };
static struct Box boxes[64];
static int nboxes;
static char drawn[4096];
void DecalFont_DrawLineOT(char *s, int x, int y, s16 f, int flags, uint32_t *ot)
{
	int w = DecalFont_GetLineWidth(s, f);
	(void)ot;
	CHECK(panels == 0);
	if (flags & JUSTIFY_CENTER) x -= w / 2;
	else if (flags & JUSTIFY_RIGHT) x -= w;
	CHECKM(x >= 0x18 && x + w <= 0x1E8 && y + 8 <= 0xCE, s);
	if (nboxes < 64)
	{
		struct Box *b = &boxes[nboxes++];
		b->x = x; b->y = y; b->w = w; b->h = f == FONT_BIG ? 17 : 8;
		snprintf(b->text, sizeof b->text, "%s", s);
	}
	if (strlen(drawn) + strlen(s) + 2 < sizeof drawn) { strcat(drawn, s); strcat(drawn, "\n"); }
}
void RECTMENU_DrawInnerRect(RECT *r, int t, uint32_t *ot)
{
	(void)t; (void)ot;
	CHECK(r->x >= 0x10 && r->x + r->w <= 0x1F0 && r->y >= 0x2E && r->y + r->h <= 0xD0);
	panels++;
}
static int highlights;
void CTR_Box_DrawClearBox(const RECT *r, const Color *c, int t, uint32_t *ot)
{
	(void)c; (void)t; (void)ot;
	CHECK(panels == 0);
	CHECK(r->x >= 0x18 && r->x + r->w <= 0x1E8);
	highlights++;
}

#include "../game/230/MM_CustomContentView.c"

static const char *const actions[8] = {
	"Download Saphi", "Rescan", "Verify / Setup", "Open Source",
	"Copy YAML", "Save YAML", "Relic Times", "Clear Relic Times",
};

static void draw(struct CustomContentView *v, const char *where)
{
	nboxes = 0; panels = 0; highlights = 0; drawn[0] = 0;
	CustomContentView_Draw(v, NULL);
	CHECKM(panels == 1, where);
	for (int i = 0; i < nboxes; i++)
		for (int j = i + 1; j < nboxes; j++)
		{
			const struct Box *a = &boxes[i], *b = &boxes[j];
			const int hit = a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
			if (hit) printf("  %s: \"%s\" overlaps \"%s\"\n", where, a->text, b->text);
			CHECKM(!hit, where);
		}
}

int main(void)
{
	data.font_charPixWidth[FONT_SMALL] = 13; data.font_charPixWidth[FONT_BIG] = 17;
	data.font_puncPixWidth[FONT_SMALL] = 7; data.font_puncPixWidth[FONT_BIG] = 11;
	data.font_buttonPixWidth[FONT_SMALL] = 2; data.font_buttonPixWidth[FONT_BIG] = 16;

	/* The Deck screenshot's state: Baby T Park, not installed, no relic times. */
	struct CustomContentView v = {0};
	v.title = "Baby T Park"; v.version = "1.0.2"; v.author = "Lockheart";
	v.state = "Not installed"; v.seed = "OPTIONAL"; v.relic = "Relic times: not set, no Relic Race";
	v.message = "Download from Saphi, or add the original LEV and VRM files, then press Verify / Setup.";
	v.actions = actions; v.actionCount = 8; v.actionRows = 4;
	for (v.selected = 0; v.selected < 8; v.selected++)
	{
		draw(&v, "deck state");
		CHECK(highlights == 1);
	}
	CHECK(strstr(drawn, "Status: Not installed\n") && strstr(drawn, "Seed: OPTIONAL\n"));
	CHECK(strstr(drawn, "Relic times: not set, no Relic Race\n") && strstr(drawn, "Select: Import track files\n"));
	CHECK(strstr(drawn, "Clear Relic Times\n") && strstr(drawn, "Verify / Setup.\n"));
	CHECK(strstr(drawn, "...") == NULL);

	/* Set times, a required package, a long author and title. */
	v.relic = "Relics: 01:23.45 / 01:30.00 / 01:40.99"; /* MM_ConfigMenu.c's format */
	v.required = 1; v.seed = "REQUIRED"; v.state = "Wrong version";
	v.author = "A contributor with a very long display name";
	v.title = "A custom track whose title is longer than the page is wide";
	draw(&v, "long");
	CHECK(strstr(drawn, "Relics: 01:23.45 / 01:30.00 / 01:40.99\n") != NULL);

	/* The relic editor replaces the actions and the importer hint. */
	v.editing = 1;
	draw(&v, "editing");
	CHECK(highlights == 0 && strstr(drawn, "Rescan") == NULL && strstr(drawn, "Select: Import") == NULL);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
