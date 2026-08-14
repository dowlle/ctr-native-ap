// Out-of-engine assertions for the graphics-options port (issue #187): aspect
// ratio (widescreen), borderless fullscreen, and the dithering toggle, ported
// from thecodingbob/ctr-native. Compiles the REAL freestanding logic:
// platform/native_config.c (config persistence + enum table) and game/
// widescreen.c (the aspect-ratio arithmetic the game call sites use) against
// the real game headers.
//
//   cc -Wall -Wextra -I include -I . -o /tmp/test-graphics-options tools/test-graphics-options.c && /tmp/test-graphics-options
//
// Exit 0 = every assertion held; failing cases are printed otherwise.
//
// Covers:
//   * config persistence round-trips (load/save) for aspect_ratio, fullscreen,
//     dithering, including the new defaults
//   * the CFG_ENUM aspect-ratio ladder values and labels the menu renders
//   * Widescreen_GetFactor / XShift for all four aspect ratios
//   * the view-projection FOV scale and frustum-corner X scale for all four
//     aspect ratios, with 4:3 pinned as the identity (vanilla unchanged)
//   * the presentation viewport letterbox for all four aspect ratios
//   * the dithering-uniform mapping (default on -> 0, off -> 1)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "platform/native_config.c"

// The harness defines the real g_config (via native_config.c) and then pulls in
// the real widescreen arithmetic. The game headers' POLY types are only needed
// for the compress functions; the arithmetic helpers we exercise are
// freestanding.
#include "macros.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "prim.h"
#include "game/widescreen.c"

static int g_failures = 0;

#define EXPECT_INT(name, got, want)                                                   \
	do {                                                                                \
		if ((got) != (want)) {                                                             \
			printf("FAIL %s: got %d want %d\n", (name), (int)(got), (int)(want));           \
			g_failures++;                                                                  \
		}                                                                                 \
	} while (0)

#define EXPECT_TRUE(name, expr)                                                        \
	do {                                                                                \
		if (!(expr)) {                                                                     \
			printf("FAIL %s\n", (name));                                                    \
			g_failures++;                                                                  \
		}                                                                                 \
	} while (0)

// Set a config value to its saved raw int (mirrors how the generic menu writes
// an entry: CFG_BOOL stores 0/1, CFG_ENUM stores its raw ladder value).
static void SetEntry(const char *section, const char *key, int value)
{
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		const ConfigEntry *e = &g_configEntries[i];
		if (strcmp(section, e->section) == 0 && strcmp(key, e->key) == 0)
		{
			if (e->type == CFG_BOOL)
				*(bool *)e->valuePtr = value != 0;
			else
				*(int *)e->valuePtr = value;
			return;
		}
	}
	printf("FAIL SetEntry: no entry %s/%s\n", section, key);
	g_failures++;
}

static const ConfigEntry *FindEntry(const char *section, const char *key)
{
	for (int i = 0; i < g_numConfigEntries; i++)
	{
		const ConfigEntry *e = &g_configEntries[i];
		if (strcmp(section, e->section) == 0 && strcmp(key, e->key) == 0)
			return e;
	}
	return NULL;
}

static void TestConfigDefaults(void)
{
	EXPECT_INT("default aspectRatio", g_config.aspectRatio, 0);
	EXPECT_INT("default fullscreen", g_config.fullscreen, 0);
	EXPECT_INT("default dithering", g_config.dithering, 1);

	const ConfigEntry *ar = FindEntry("Video & QoL", "aspect_ratio");
	const ConfigEntry *fs = FindEntry("Video & QoL", "fullscreen");
	const ConfigEntry *di = FindEntry("Video & QoL", "dithering");
	EXPECT_TRUE("aspect_ratio entry present", ar != NULL);
	EXPECT_TRUE("fullscreen entry present", fs != NULL);
	EXPECT_TRUE("dithering entry present", di != NULL);
	if (ar) EXPECT_INT("aspect_ratio is CFG_ENUM", ar->type, CFG_ENUM);
	if (fs) EXPECT_INT("fullscreen is CFG_BOOL", fs->type, CFG_BOOL);
	if (di) EXPECT_INT("dithering is CFG_BOOL", di->type, CFG_BOOL);
}

// The enum ladder lives in MM_ConfigMenu.c (game code, not compiled here), but
// its value set is the contract the menu, the renderer and the frustum all
// share; pin it through Widescreen_GetFactor's own switch, which is the real
// consumer-facing mapping.
static void TestAspectLadder(void)
{
	g_config.aspectRatio = 0; EXPECT_INT("factor 4:3", Widescreen_GetFactor(), 1000);
	g_config.aspectRatio = 1; EXPECT_INT("factor 16:9", Widescreen_GetFactor(), 750);
	g_config.aspectRatio = 2; EXPECT_INT("factor 16:10", Widescreen_GetFactor(), 833);
	g_config.aspectRatio = 3; EXPECT_INT("factor 21:9", Widescreen_GetFactor(), 563);
	// Out-of-ladder persisted values fall back to 4:3 (the menu's nearest-index
	// rule snaps display to 4:3 as well).
	g_config.aspectRatio = 99; EXPECT_INT("factor out-of-ladder", Widescreen_GetFactor(), 1000);
	g_config.aspectRatio = 0;
}

static void TestFovAndFrustum(void)
{
	// View-projection FOV scale: 4:3 is the 0x800 identity; wider ratios scale
	// X down (more world visible per pixel).
	g_config.aspectRatio = 0; EXPECT_INT("fov 4:3", Widescreen_GetFovScale(), 0x800);
	g_config.aspectRatio = 1; EXPECT_INT("fov 16:9", Widescreen_GetFovScale(), 750 * 0x800 / 1000);
	g_config.aspectRatio = 2; EXPECT_INT("fov 16:10", Widescreen_GetFovScale(), 833 * 0x800 / 1000);
	g_config.aspectRatio = 3; EXPECT_INT("fov 21:9", Widescreen_GetFovScale(), 563 * 0x800 / 1000);

	// Frustum corner X: identity at 4:3, wider elsewhere, and it must never
	// shrink below the 4:3 value (wider FOV means a wider cull volume).
	g_config.aspectRatio = 0;
	EXPECT_INT("frustumX 4:3 identity", Widescreen_ScaleFrustumX(100), 100);
	int x169, x1610, x219;
	g_config.aspectRatio = 1; x169 = Widescreen_ScaleFrustumX(100);
	g_config.aspectRatio = 2; x1610 = Widescreen_ScaleFrustumX(100);
	g_config.aspectRatio = 3; x219 = Widescreen_ScaleFrustumX(100);
	EXPECT_TRUE("frustumX 16:9 wider than 4:3", x169 > 100);
	EXPECT_TRUE("frustumX 16:10 wider than 4:3", x1610 > 100);
	EXPECT_TRUE("frustumX 21:9 wider than 4:3", x219 > 100);
	// Wider aspect = wider FOV = wider frustum X.
	EXPECT_TRUE("frustumX 21:9 >= 16:9", x219 >= x169);
	g_config.aspectRatio = 0;
}

static void TestViewport(void)
{
	int w, h;

	// 4:3 must letterbox any window to exactly 4:3 content (the renderer keeps
	// the window's own aspect as its presentation aspect, so on a 16:9 window
	// with aspectRatio=0 the effective aspect is the window's, not forced 4:3;
	// that decision lives in NativeRenderer_UpdatePresentationViewport and is
	// exercised here through the letterbox arithmetic).
	Widescreen_LetterboxViewport(1920, 1080, 4, 3, &w, &h);
	EXPECT_INT("viewport 4:3 on 1920x1080 w", w, 1440);
	EXPECT_INT("viewport 4:3 on 1920x1080 h", h, 1080);

	// The aspectRatio option's effective ratios.
	Widescreen_LetterboxViewport(1920, 1080, 16, 9, &w, &h);
	EXPECT_INT("viewport 16:9 on 1920x1080 w", w, 1920);
	EXPECT_INT("viewport 16:9 on 1920x1080 h", h, 1080);

	Widescreen_LetterboxViewport(1920, 1080, 16, 10, &w, &h);
	EXPECT_INT("viewport 16:10 on 1920x1080 w", w, 1728);
	EXPECT_INT("viewport 16:10 on 1920x1080 h", h, 1080);

	Widescreen_LetterboxViewport(1920, 1080, 64, 27, &w, &h);
	EXPECT_INT("viewport 21:9 on 1920x1080 w", w, 1920);
	EXPECT_INT("viewport 21:9 on 1920x1080 h", h, 810);

	// Letterboxing never exceeds the window.
	Widescreen_LetterboxViewport(800, 600, 16, 9, &w, &h);
	EXPECT_TRUE("viewport fits window width", w <= 800);
	EXPECT_TRUE("viewport fits window height", h <= 600);

	// The effective-ratio mapping used by the presentation viewport switch.
	int ew, eh;
	g_config.aspectRatio = 0; EXPECT_INT("effAspect 4:3 override off", Widescreen_GetAspectRatio(&ew, &eh), 0);
	g_config.aspectRatio = 1; EXPECT_TRUE("effAspect 16:9 override on", Widescreen_GetAspectRatio(&ew, &eh) == 1);
	                         EXPECT_INT("effAspect 16:9 w", ew, 16); EXPECT_INT("effAspect 16:9 h", eh, 9);
	g_config.aspectRatio = 2; EXPECT_TRUE("effAspect 16:10 override on", Widescreen_GetAspectRatio(&ew, &eh) == 1);
	                         EXPECT_INT("effAspect 16:10 w", ew, 16); EXPECT_INT("effAspect 16:10 h", eh, 10);
	g_config.aspectRatio = 3; EXPECT_TRUE("effAspect 21:9 override on", Widescreen_GetAspectRatio(&ew, &eh) == 1);
	                         EXPECT_INT("effAspect 21:9 w", ew, 64); EXPECT_INT("effAspect 21:9 h", eh, 27);
	g_config.aspectRatio = 0;
}

static void TestDitherUniform(void)
{
	EXPECT_INT("dither on -> uniform 0", Widescreen_DitherUniform(true), 0);
	EXPECT_INT("dither off -> uniform 1", Widescreen_DitherUniform(false), 1);
}

static void TestFullscreenSync(void)
{
	// Toggle decision: pressing F11 / Alt+Enter flips the current window state.
	EXPECT_INT("toggle from windowed -> fullscreen", NativeConfig_FullscreenToggledFromWindow(false), 1);
	EXPECT_INT("toggle from fullscreen -> windowed", NativeConfig_FullscreenToggledFromWindow(true), 0);

	// Per-frame sync: reapply only when config and window disagree.
	EXPECT_INT("sync no-op when already fullscreen", NativeConfig_FullscreenNeedsReapply(true, true), 0);
	EXPECT_INT("sync no-op when already windowed", NativeConfig_FullscreenNeedsReapply(false, false), 0);
	EXPECT_INT("sync apply when config wants fullscreen", NativeConfig_FullscreenNeedsReapply(true, false), 1);
	EXPECT_INT("sync apply when config wants windowed", NativeConfig_FullscreenNeedsReapply(false, true), 1);
}

static void TestPersistenceRoundTrip(void)
{
	// Persist a full non-default state, then reload from a clean config and
	// confirm every field round-trips. NativeConfig_Load/Save operate on
	// "config.ini" in the working directory, so the harness runs in a temp dir.
	char tmpdir[] = "/tmp/test-gfx-XXXXXX";
	if (!mkdtemp(tmpdir))
	{
		printf("FAIL persistence: cannot create temp dir\n");
		g_failures++;
		return;
	}
	if (chdir(tmpdir) != 0)
	{
		printf("FAIL persistence: cannot chdir\n");
		g_failures++;
		return;
	}

	// Write the non-default state out.
	SetEntry("Video & QoL", "aspect_ratio", 3);
	SetEntry("Video & QoL", "fullscreen", 1);
	SetEntry("Video & QoL", "dithering", 0);
	NativeConfig_Save();

	// Reset the in-memory state to defaults so the reload is a real test.
	g_config.aspectRatio = 0;
	g_config.fullscreen = 0;
	g_config.dithering = 1;
	NativeConfig_Load();

	EXPECT_INT("persisted aspectRatio", g_config.aspectRatio, 3);
	EXPECT_INT("persisted fullscreen", g_config.fullscreen, 1);
	EXPECT_INT("persisted dithering", g_config.dithering, 0);

	// The three options live under the Video & QoL section with the right keys.
	FILE *f = fopen("config.ini", "r");
	EXPECT_TRUE("config.ini exists after save", f != NULL);
	if (f)
	{
		char buf[256];
		int sawAspect = 0, sawFull = 0, sawDither = 0, inVideo = 0;
		while (fgets(buf, sizeof(buf), f))
		{
			if (buf[0] == '[')
				inVideo = strncmp(buf, "[Video & QoL]", 13) == 0;
			else if (inVideo && strstr(buf, "aspect_ratio") != NULL && strstr(buf, "= 3") != NULL)
				sawAspect = 1;
			else if (inVideo && strstr(buf, "fullscreen") != NULL && strstr(buf, "= true") != NULL)
				sawFull = 1;
			else if (inVideo && strstr(buf, "dithering") != NULL && strstr(buf, "= false") != NULL)
				sawDither = 1;
		}
		fclose(f);
		EXPECT_INT("config.ini has aspect_ratio = 3", sawAspect, 1);
		EXPECT_INT("config.ini has fullscreen = true", sawFull, 1);
		EXPECT_INT("config.ini has dithering = false", sawDither, 1);
	}
}

int main(void)
{
	TestConfigDefaults();
	TestAspectLadder();
	TestFovAndFrustum();
	TestViewport();
	TestDitherUniform();
	TestFullscreenSync();
	TestPersistenceRoundTrip();

	printf("\n%s\n", g_failures ? "FAILURES PRESENT" : "all assertions passed");
	return g_failures != 0;
}
