// Draw the AP readout through the retail font and HUD primitive writers.
// cc -m32 -msse -DBUILD=926 -DCTR_NATIVE -DCTR_AP -Iinclude -I. \
//   -ffunction-sections -fdata-sections -Wl,--gc-sections \
//   -o /tmp/test-hud-layout tools/test-hud-layout.c

#include "../game/UI/UI_Speedometer.c"
#include "../game/DecalFont.c"
#include "../game/DecalHUD.c"
#include "../game/widescreen.c"

static u32 orange[4] = {0xff, 0xff, 0xff, 0xff};
static u32 green[4] = {0xff00, 0xff00, 0xff00, 0xff00};

struct Data data = {
	.font_charPixWidth = {[FONT_BIG] = 17},
	.font_IconGroupID = {[FONT_BIG] = 4},
	.ptrColor = {[ORANGE] = orange, [LIGHT_GREEN] = green},
};
struct sData sdata_static;
NativeConfig g_config;

uint32_t NativeGpuLinks_FromHostPointer(const void *prim)
{
	(void)prim;
	return 0; // The test reads emitted packets directly from the arena.
}

struct Bounds
{
	int left, top, right, bottom;
};

static int failures;

static void expect(const char *name, int ok)
{
	if (!ok)
	{
		fprintf(stderr, "FAIL %s (aspect setting %d)\n", name, g_config.aspectRatio);
		failures++;
	}
}

static int disjoint(struct Bounds a, struct Bounds b)
{
	return a.right <= b.left || a.left >= b.right || a.bottom <= b.top || a.top >= b.bottom;
}

int main(void)
{
	// A measured 15x15 big digit with a 17-column advance. The font's
	// icon lookup uses indices 26..35 for digits in the NTSC-U build.
	struct Icon digit = {.texLayout = {.u1 = 15, .v2 = 15}};
	struct
	{
		struct IconGroup group;
		struct Icon *icons[36];
	} font = {.group = {.numIcons = 36}};
	for (int i = 0; i < 10; i++)
	{
		font.icons[26 + i] = &digit;
		data.font_characterIconID['0' + i - '!'] = 26 + i;
	}

	POLY_GT4 glyphs[8];
	uint32_t orderingTable = 0;
	struct DB buffer = {0};
	struct GameTracker tracker = {.backBuffer = &buffer, .numPlyrCurrGame = 1};
	tracker.iconGroup[4] = &font.group;
	tracker.pushBuffer_UI.ptrOT = &orderingTable;
	sdata->gGT = &tracker;

	// 1P boost anchor from zGlobal_DATA.c. Boxes are in logical HUD
	// coordinates and include the bar outlines. The dot covers the repro
	// at (431,195), with margin on every side. A wide custom map had a
	// marker at (366,207). The map texture ends at 195 in either case.
	const int boostX = 490, boostY = 206;
	const struct Bounds boost = {440, 198, 491, 207};
	const struct Bounds reserves = {440, 207, 491, 212};
	const struct Bounds map = {0, 0, 501, 196};
	const struct Bounds dot = {421, 191, 444, 204};
	const struct Bounds customDot = {366, 207, 380, 216};
	const struct Bounds lap = {400, 8, 485, 34};
	const struct Bounds position = {0, 55, 67, 202};

	struct Driver driver = {.const_AccelSpeed_ClassStat = FP8(40)};
	const int speeds[] = {-1, 42, 127};
	for (g_config.aspectRatio = 0; g_config.aspectRatio <= 3; g_config.aspectRatio++)
	{
		for (int i = 0; i < 3; i++)
		{
			buffer.primMem.cursor = glyphs;
			orderingTable = 0;
			driver.speedometerNeedleValue = FP8(speeds[i]);
			UI_DrawSpeedNum(boostX, boostY, &driver);
			int count = (POLY_GT4 *)buffer.primMem.cursor - glyphs;
			expect("expected digit count", count == i + 1);
			for (int j = 0; j < count; j++)
			{
				struct Bounds glyph = {glyphs[j].x0, glyphs[j].y0, glyphs[j].x1, glyphs[j].y2};
				expect("glyph inside the race draw area",
				       glyph.left >= 0 && glyph.top >= 0 && glyph.right <= 512 && glyph.bottom <= 216);
				expect("glyph clear of the boost and reserves bars", disjoint(glyph, boost) && disjoint(glyph, reserves));
				expect("glyph clear of the minimap and racer dots",
				       disjoint(glyph, map) && disjoint(glyph, dot) && disjoint(glyph, customDot));
				expect("glyph clear of the lap and position displays", disjoint(glyph, lap) && disjoint(glyph, position));
				expect("glyph color follows class top speed", i == 0 ? glyphs[j].r0 == 0xff : glyphs[j].g0 == 0xff);
			}
			if (count > 0)
				expect("last digit keeps the same right edge", glyphs[count - 1].x1 == 352 - Widescreen_XShift(15));

			sdata->HudAndDebugFlags = 8;
			buffer.primMem.cursor = glyphs;
			UI_DrawSpeedNum(boostX, boostY, &driver);
			expect("analog needle suppresses the readout", buffer.primMem.cursor == glyphs);
			sdata->HudAndDebugFlags = 0;
		}
	}
	printf("%d failure(s)\n", failures);
	return failures != 0;
}
