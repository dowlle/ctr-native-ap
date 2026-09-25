#ifdef CTR_AP

#include <common.h> // struct Driver / GameTracker / Model, sdata, LevelID, FONT_*, DecalFont_DrawLine
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ap_author.h"
#include "ap_author_ready.h"
#include "ap_spawn.h"
#include "ap_marker_model.h"  // STATIC_AP (the AP-logo marker, #124)
#include "ap_box_model.h"     // AP_BoxModel_SpawnPos: the shared crate spawn transform
#include "ap_placement_table.h" // the two tables, the precedence rule, the row shape
#include "ap_version.h"       // CTR_AP_VERSION, stamped into the exported file
#include "ap_hooks.h"         // AP_LogLine
#ifdef CTR_CUSTOM_PACKAGES
#include "ap_author_custom.h"  // custom-track placements: package key, own file
#include <platform/native_custom_offline.h>
#include <platform/native_custom_package.h>
#endif

// HUD placement: top-left, under the schema/verify warning banners (those start
// at y 0x14, ap_hooks.c) so an authoring session never hides a real warning.
#define AP_AUTHOR_HUD_X      0x10
#define AP_AUTHOR_HUD_Y      0x2C
#define AP_AUTHOR_HUD_LINE_H 0xC

// ============================================================================
// BOX PLACEMENT AUTHOR MODE -- implementation. See ap_author.h for the design
// contract and the key bindings. One translation-unit member of the unity build.
// ============================================================================

// Raw keyboard probe (platform/native_input.c, CTR_AP only). Same declaration
// the trap and Shortcutless modules use -- keeps this module SDL-header-free.
int Platform_InputRawKeyDown(int scancode);

// A placement, in exactly the shape the LEV stores one. Position is three
// signed 16-bit world coordinates, which is what struct InstDef holds at 0x30
// (namespace_Instance.h:393-427) and what INSTANCE_LevInitAll copies straight
// into the instance matrix (INSTANCE.c:315). rotY is an engine angle in the same
// space as InstDef.rot (0x1000 = one full turn).
//
// The kart's live position is WIDER than this: Driver.posCurr is a Vec3 of
// three s32 (namespace_Vehicle.h:1042, ctr_math.h:73-81). Narrowing happens at
// the moment of capture and the marker is spawned at the narrowed value, so
// what is on screen is what is in the file (#182 open question 3).
// The row SHAPE itself lives in ap_placement_table.h, shared with the read-only
// view over whichever table is live. s16 and short are the same 16-bit type on
// both targets, so every field access below is unaffected by reading it there.

static AP_PlacementRow    s_place[AP_AUTHOR_MAX_PLACEMENTS];
static AP_SpawnHandle     s_marker[AP_AUTHOR_MAX_PLACEMENTS];
static int                s_placeCount;

static int s_loaded;           // the file has been read (once per run)
static int s_source = AP_PLACEMENT_SRC_EMBEDDED; // which table is live (ap_author.h)
static int s_placeGen;         // bumped on every change to s_place (see ap_author.h)
static int s_enabledPrev;      // last frame's toggle state, for on/off edges
static int s_markerLevel = -1; // level the current marker set was built for
static int s_markerGen;        // AP_Spawn_Generation() the handles were taken at
static int s_markerTableFull;  // the loader refused a marker for this level
static int s_lastDropIndex = -1;
static int s_bossPauseLogged;  // one log line per boss race, not per frame (#194)

// Instance name. A char[16] rather than a string literal because INSTANCE_Birth
// copies a fixed 15 characters out of whatever it is handed (INSTANCE.c:24-27).
static char s_markerName[16] = "apbox";

#ifdef CTR_CUSTOM_PACKAGES
static void AP_AuthorCustomForgetMarkers(void); // custom-track markers, further down
#endif

// Levels a placement may be authored on: the 18 race tracks plus the 7 battle
// arenas (enum LevelID, namespace_Level.h:4-28). Beyond LAB_BASEMENT the ids are
// hubs, intros, menus and credits scenes, none of which can carry a track's box
// set. Names are the enum's own, so the exported file is self-describing without
// inventing a naming scheme the apworld would then have to match.
static const char *const s_levelNames[] = {
	"DINGO_CANYON",   "DRAGON_MINES",  "BLIZZARD_BLUFF", "CRASH_COVE",
	"TIGER_TEMPLE",   "PAPU_PYRAMID",  "ROO_TUBES",      "HOT_AIR_SKYWAY",
	"SEWER_SPEEDWAY", "MYSTERY_CAVES", "CORTEX_CASTLE",  "N_GIN_LABS",
	"POLAR_PASS",     "OXIDE_STATION", "COCO_PARK",      "TINY_ARENA",
	"SLIDE_COLISEUM", "TURBO_TRACK",   "NITRO_COURT",    "RAMPAGE_RUINS",
	"PARKING_LOT",    "SKULL_ROCK",    "THE_NORTH_BOWL", "ROCKY_ROAD",
	"LAB_BASEMENT",
};
#define AP_AUTHOR_LEVEL_COUNT ((int)(sizeof(s_levelNames) / sizeof(s_levelNames[0])))

// Keep the exported placement-file identity discoverable in public binaries
// even though the compiler can prove the authoring UI is disabled there. The
// release-pair gate uses this exact JSON line to match client and apworld.
const char AP_ClientExportIdentity[] = "  \"client\": \"" CTR_AP_VERSION "\",";

// Not static: ap_hooks.c's per-item receipt log reuses this for letter-item
// track names (ap_author.h) rather than carrying a second table.
const char *AP_AuthorLevelName(int level)
{
	if (level < 0 || level >= AP_AUTHOR_LEVEL_COUNT)
		return "?";
	return s_levelNames[level];
}

static int AP_AuthorLevelIsAuthorable(int level)
{
	return level >= 0 && level < AP_AUTHOR_LEVEL_COUNT;
}

int AP_Author_Enabled(void)
{
	// Public AP builds retain the authored placement table because runtime AP
	// boxes consume it, but the placement editor itself is a developer tool.
	// Compile it live only in an explicit authoring build. This also neutralises
	// a stale box_author=true from an older public config.ini.
#ifndef CTR_AP_AUTHORING
	return 0;
#else
	// The menu toggle mutates g_config in place, so this follows it live. Without
	// a config.ini the field holds its compiled default (off), which is the right
	// answer for a player who never opened the menu.
	return g_config.boxAuthor ? 1 : 0;
#endif
}

// ── the marker model ────────────────────────────────────────────────────────

// Which model stands in for an AP box.
//
// First choice is the weapon box, PU_RANDOM_CRATE. Every arcade track's LEV
// carries it, so on a track it is essentially always available, and it is the
// ruled #109 look, so the authoring preview and the real box are the same shape.
//
// ⚠ THE ORDER MATTERS AND IS NOT A STYLE CHOICE. The #124 AP-logo marker
// (STATIC_AP) used to be first here, and under author mode's many-instance usage
// it makes track FLOORS disappear -- reported from a live test 2026-08-09, resolved
// 2026-08-10 by exactly this swap and confirmed live. See the vault note
// "2026-08-09 -- Bug -- AP logo placement markers make floor invisible". The
// logo path stays as a second choice because it costs nothing to keep, but if it
// is ever promoted back to first, that bug is the reason not to.
//
// Returning -1 means "nothing to draw yet"; the placement is still recorded and
// the marker request simply waits (ap_spawn.c holds unresolved model ids).
static int AP_AuthorMarkerModel(struct GameTracker *gGT)
{
	if (gGT->modelPtr[PU_RANDOM_CRATE] != 0)
		return PU_RANDOM_CRATE;
	if (AP_MarkerModel_IsRegistered() && gGT->modelPtr[STATIC_AP] != 0)
		return STATIC_AP;
	return -1;
}

static void AP_AuthorSpawnMarker(struct GameTracker *gGT, int i)
{
	Vec3  pos;
	SVec3 rot;
	int   modelID;

	if (s_marker[i] != AP_SPAWN_INVALID)
		return;

	// The loader has already said no for this level. Without this latch the
	// retry below would ask again every frame, and each refusal writes a log
	// line -- which is an fopen per line (ap_hooks.c), i.e. file IO in the frame
	// loop for as long as the track is loaded.
	if (s_markerTableFull)
		return;

	modelID = AP_AuthorMarkerModel(gGT);
	if (modelID < 0)
		return; // model not up yet; retried next frame, silently

	// THE SAME TRANSFORM THE RUNTIME BOX USES (ap_box_model.c). A placement row
	// is a ground anchor and a crate model's origin is its centre, so the marker
	// is lifted by the marker model's own measured base offset -- which puts its
	// lowest face on the anchor, exactly where the runtime crate's lowest face
	// will be. Author mode therefore previews the shipped height rather than the
	// half-buried one, without either half owning a private copy of the rule.
	AP_BoxModel_SpawnPos(gGT->modelPtr[modelID], s_place[i].x, s_place[i].y, s_place[i].z, &pos);
	rot.x = 0;
	rot.y = s_place[i].rotY;
	rot.z = 0;

	// LIFE_LEVEL: the marker set belongs to the level that is loaded, and it is
	// rebuilt from s_place after every level change anyway (AP_AuthorRebuildMarkers).
	s_marker[i] = AP_Spawn_Add(modelID, &pos, &rot, AP_SPAWN_LIFE_LEVEL, s_markerName);
	if (s_marker[i] == AP_SPAWN_INVALID)
		s_markerTableFull = 1;
}

static void AP_AuthorClearMarkers(void)
{
	int i;
	for (i = 0; i < s_placeCount; i++)
	{
		if (s_marker[i] != AP_SPAWN_INVALID)
		{
			AP_Spawn_Remove(s_marker[i]);
			s_marker[i] = AP_SPAWN_INVALID;
		}
	}
}

// Every handle we hold is void, because the pool reset that bumped the loader's
// generation may already have handed the slot to someone else. Forget them
// without calling AP_Spawn_Remove -- the loader dropped the entries itself.
static void AP_AuthorForgetMarkers(void)
{
	int i;
	for (i = 0; i < AP_AUTHOR_MAX_PLACEMENTS; i++)
		s_marker[i] = AP_SPAWN_INVALID;
#ifdef CTR_CUSTOM_PACKAGES
	AP_AuthorCustomForgetMarkers();
#endif
	s_markerGen = AP_Spawn_Generation();
	s_markerTableFull = 0;
}

static void AP_AuthorRebuildMarkers(struct GameTracker *gGT, int level)
{
	int i;

	AP_AuthorClearMarkers();
	s_markerLevel = level;
	s_markerTableFull = 0;

	if (!AP_AuthorLevelIsAuthorable(level))
		return;

	for (i = 0; i < s_placeCount; i++)
	{
		if (s_place[i].level == (s16)level)
			AP_AuthorSpawnMarker(gGT, i);
	}
}

// ── the file ────────────────────────────────────────────────────────────────

static void AP_AuthorSave(void)
{
	FILE *f;
	int   i;

	f = fopen(AP_AUTHOR_FILE, "w");
	if (f == 0)
	{
		AP_LogLine("[AP AUTHOR] could not write " AP_AUTHOR_FILE "\n");
		return;
	}

	fputs("{\n", f);
	fputs("  \"format\": \"ctr-ap-box-placements\",\n", f);
	fputs("  \"version\": 1,\n", f);
	fputs("  \"client\": \"" CTR_AP_VERSION "\",\n", f);
	fputs("  \"units\": \"pos is LEV InstDef world units (signed 16-bit); "
	      "rot_y is an engine angle, 0x1000 = one full turn\",\n", f);
	fputs("  \"placements\": [\n", f);

	// ONE placement per line, and every field on that line. The reader below
	// depends on that, and so does anyone diffing this file in a pull request.
	for (i = 0; i < s_placeCount; i++)
	{
		fprintf(f, "    {\"level_id\": %d, \"level\": \"%s\", \"pos\": [%d, %d, %d], \"rot_y\": %d}%s\n",
		        (int)s_place[i].level, AP_AuthorLevelName((int)s_place[i].level),
		        (int)s_place[i].x, (int)s_place[i].y, (int)s_place[i].z,
		        (int)s_place[i].rotY, (i + 1 < s_placeCount) ? "," : "");
	}

	fputs("  ]\n", f);
	fputs("}\n", f);
	fclose(f);

	// The file now exists, so by the existence rule it is the live table from
	// here on -- including within this session, so the runtime half follows what
	// is being authored instead of the compiled-in default. Bumping the
	// generation is what makes that rebuild happen (see ap_author.h).
	if (s_source != AP_PLACEMENT_SRC_FILE)
	{
		s_source = AP_PLACEMENT_SRC_FILE;
		s_placeGen++;
		AP_LogLine("[AP AUTHOR] placements: " AP_AUTHOR_FILE " written -- it now overrides the built-in default\n");
	}
}

// Read the integer that follows "key" on this line, e.g. "level_id" in
// {"level_id": 3, ...}. Returns 0 when the key is not on the line.
//
// Why a scanner and not a JSON parser: the writer above is the only producer, so
// the only thing that has to be tolerated is a human editing values, reordering
// fields or reflowing whitespace -- all of which this survives. What it does NOT
// do is parse arbitrary JSON, and a file that has been restructured by hand into
// something else will be read as empty rather than misread.
static int AP_AuthorScanInt(const char *line, const char *key, int *out)
{
	const char *p = strstr(line, key);

	if (p == 0)
		return 0;

	p += strlen(key);
	while (*p != '\0' && *p != ':')
		p++;
	if (*p != ':')
		return 0;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '[')
		p++;
	if (*p != '-' && (*p < '0' || *p > '9'))
		return 0;

	*out = (int)strtol(p, 0, 10);
	return 1;
}

// Read the three integers of a "pos": [x, y, z] array.
static int AP_AuthorScanVec(const char *line, int *x, int *y, int *z)
{
	const char *p = strstr(line, "\"pos\"");
	char       *end;
	int         v[3];
	int         i;

	if (p == 0)
		return 0;

	p = strchr(p, '[');
	if (p == 0)
		return 0;
	p++;

	for (i = 0; i < 3; i++)
	{
		while (*p == ' ' || *p == '\t' || *p == ',')
			p++;
		if (*p != '-' && (*p < '0' || *p > '9'))
			return 0;
		v[i] = (int)strtol(p, &end, 10);
		if (end == p)
			return 0;
		p = end;
	}

	*x = v[0];
	*y = v[1];
	*z = v[2];
	return 1;
}

static void AP_AuthorLoad(void)
{
	FILE *f;
	char  line[256];
	char  msg[256]; // sized for the desync-guard lines below, the longest we emit

	s_loaded = 1;
	s_placeCount = 0;
	AP_AuthorForgetMarkers();

	f = fopen(AP_AUTHOR_FILE, "r");
	if (f == 0)
	{
		// The normal shipped case, and not an error: no external file, so the
		// compiled-in FINAL set is the table. ONE line, naming the live source.
		s_source = AP_PLACEMENT_SRC_EMBEDDED;
		s_placeGen++;
		snprintf(msg, sizeof msg,
		         "[AP AUTHOR] placements: built-in default, %d placement(s) (no %s next to the exe)\n",
		         AP_EMBEDDED_PLACEMENT_COUNT, AP_AUTHOR_FILE);
		AP_LogLine(msg);
		return;
	}

	// The file opened, so the file is the table -- see the precedence note in
	// ap_author.h. Everything below fills the FILE table only; the embedded
	// default is left untouched and simply stops being consulted.
	s_source = AP_PLACEMENT_SRC_FILE;

	while (fgets(line, sizeof line, f) != 0)
	{
		int level, x, y, z, rotY;

		if (!AP_AuthorScanInt(line, "\"level_id\"", &level))
			continue;
		if (!AP_AuthorScanVec(line, &x, &y, &z))
			continue;
		if (!AP_AuthorScanInt(line, "\"rot_y\"", &rotY))
			rotY = 0;

		if (s_placeCount >= AP_AUTHOR_MAX_PLACEMENTS)
		{
			snprintf(msg, sizeof msg, "[AP AUTHOR] file holds more than %d placements, rest ignored\n",
			         AP_AUTHOR_MAX_PLACEMENTS);
			AP_LogLine(msg);
			break;
		}

		s_place[s_placeCount].level = (s16)level;
		s_place[s_placeCount].x = (s16)x;
		s_place[s_placeCount].y = (s16)y;
		s_place[s_placeCount].z = (s16)z;
		s_place[s_placeCount].rotY = (s16)rotY;
		s_marker[s_placeCount] = AP_SPAWN_INVALID;
		s_placeCount++;
	}

	fclose(f);
	s_placeGen++;

	// ONE line, naming the live source, so a support log answers "which set is
	// this player actually running" without a second question.
	snprintf(msg, sizeof msg,
	         "[AP AUTHOR] placements: external %s OVERRIDES the built-in default, %d placement(s) (built-in has %d)\n",
	         AP_AUTHOR_FILE, s_placeCount, AP_EMBEDDED_PLACEMENT_COUNT);
	AP_LogLine(msg);

	// DESYNC GUARD. An external file is an override, not a patch: it replaces the
	// whole table, so a file authored against a different client (or a partial
	// hand-made one) silently re-points every "Item Box N" name on the tracks it
	// covers. Say so out loud rather than letting a player wonder why their boxes
	// and the tracker disagree. Both directions are worth a line: an EMPTY file is
	// a legitimate "no boxes" instruction, but it is also what an accidental
	// truncation or a hand-restructured JSON looks like to the scanner reader.
	if (s_placeCount == 0)
	{
		snprintf(msg, sizeof msg,
		         "[AP BOX] WARNING: %s is present but holds ZERO placements -- NO boxes will spawn anywhere. "
		         "Delete the file to fall back to the built-in %d.\n",
		         AP_AUTHOR_FILE, AP_EMBEDDED_PLACEMENT_COUNT);
		AP_LogLine(msg);
	}
	else if (s_placeCount != AP_EMBEDDED_PLACEMENT_COUNT)
	{
		snprintf(msg, sizeof msg,
		         "[AP BOX] NOTE: the override holds %d placement(s), the built-in default %d -- "
		         "box positions and per-track counts may differ from the shipped set.\n",
		         s_placeCount, AP_EMBEDDED_PLACEMENT_COUNT);
		AP_LogLine(msg);
	}
}

// One-time export of the compiled-in default into the external file, so author
// mode always has a real file to edit. No-op when a file is already live -- an
// existing file is never overwritten by this, only by an explicit drop/undo/list
// save, which is what keeps a hand-authored set safe from a stray toggle.
static void AP_AuthorSeedFileFromEmbedded(void)
{
	int  i, n;
	char msg[128];

	if (s_source == AP_PLACEMENT_SRC_FILE)
		return;

	n = AP_EMBEDDED_PLACEMENT_COUNT;
	if (n > AP_AUTHOR_MAX_PLACEMENTS)
		n = AP_AUTHOR_MAX_PLACEMENTS; // cannot happen at 241 of 512; not a silent bound

	for (i = 0; i < n; i++)
	{
		s_place[i].level = AP_EMBEDDED_PLACEMENTS[i].level;
		s_place[i].x = AP_EMBEDDED_PLACEMENTS[i].x;
		s_place[i].y = AP_EMBEDDED_PLACEMENTS[i].y;
		s_place[i].z = AP_EMBEDDED_PLACEMENTS[i].z;
		s_place[i].rotY = AP_EMBEDDED_PLACEMENTS[i].rotY;
		s_marker[i] = AP_SPAWN_INVALID;
	}
	s_placeCount = n;
	s_lastDropIndex = -1;
	s_placeGen++;

	snprintf(msg, sizeof msg,
	         "[AP AUTHOR] no %s yet -- exported the built-in %d placement(s) into it to author against\n",
	         AP_AUTHOR_FILE, n);
	AP_LogLine(msg);

	AP_AuthorSave(); // flips the live source to the file
}

// ── the actions ─────────────────────────────────────────────────────────────

// Narrow one live world coordinate to the 16 bits a placement stores. Real
// track geometry fits (every LEV InstDef position is already s16), so a clamp
// here means the capture happened somewhere a placement cannot describe, which
// is worth saying out loud rather than silently wrapping.
static s16 AP_AuthorNarrow(int v, int *clamped)
{
	if (v > 32767)
	{
		*clamped = 1;
		return 32767;
	}
	if (v < -32768)
	{
		*clamped = 1;
		return -32768;
	}
	return (s16)v;
}

static void AP_AuthorDrop(struct GameTracker *gGT)
{
	struct Driver *d = gGT->drivers[0];
	int            level = (int)gGT->levelID;
	int            clamped = 0;
	char           msg[128];

	if (d == 0)
	{
		AP_LogLine("[AP AUTHOR] no driver on this screen -- drive a track first\n");
		return;
	}

	if (!AP_AuthorLevelIsAuthorable(level))
	{
		snprintf(msg, sizeof msg, "[AP AUTHOR] level %d is not a track or arena, no placement dropped\n", level);
		AP_LogLine(msg);
		return;
	}

	if (s_placeCount >= AP_AUTHOR_MAX_PLACEMENTS)
	{
		snprintf(msg, sizeof msg, "[AP AUTHOR] placement table full (%d), drop refused\n",
		         AP_AUTHOR_MAX_PLACEMENTS);
		AP_LogLine(msg);
		return;
	}

	s_place[s_placeCount].level = (s16)level;
	s_place[s_placeCount].x = AP_AuthorNarrow(d->posCurr.x, &clamped);
	s_place[s_placeCount].y = AP_AuthorNarrow(d->posCurr.y, &clamped);
	s_place[s_placeCount].z = AP_AuthorNarrow(d->posCurr.z, &clamped);
	s_place[s_placeCount].rotY = d->rotCurr.y;
	s_marker[s_placeCount] = AP_SPAWN_INVALID;
	s_lastDropIndex = s_placeCount;
	s_placeCount++;
	s_placeGen++;

	// Clear the latch for this one attempt: a drop is an explicit request, and
	// if the loader is genuinely full the refusal is worth exactly one log line
	// here rather than none.
	s_markerTableFull = 0;
	AP_AuthorSpawnMarker(gGT, s_lastDropIndex);

	snprintf(msg, sizeof msg, "[AP AUTHOR] %s: placement at %d %d %d rot_y %d\n",
	         AP_AuthorLevelName(level),
	         (int)s_place[s_lastDropIndex].x, (int)s_place[s_lastDropIndex].y,
	         (int)s_place[s_lastDropIndex].z, (int)s_place[s_lastDropIndex].rotY);
	AP_LogLine(msg);

	if (clamped)
		AP_LogLine("[AP AUTHOR] WARNING: kart position did not fit 16 bits and was clamped\n");

	// Saved on every change: an authoring session ends in a crash or an alt-F4
	// often enough that "the file is always current" is worth one small write per
	// placement.
	AP_AuthorSave();
}

static void AP_AuthorUndo(struct GameTracker *gGT)
{
	int  level = (int)gGT->levelID;
	int  i, last = -1;
	char msg[128];

	for (i = 0; i < s_placeCount; i++)
	{
		if (s_place[i].level == (s16)level)
			last = i;
	}

	if (last < 0)
	{
		snprintf(msg, sizeof msg, "[AP AUTHOR] %s has no placements to delete\n",
		         AP_AuthorLevelName(level));
		AP_LogLine(msg);
		return;
	}

	snprintf(msg, sizeof msg, "[AP AUTHOR] %s: deleted placement at %d %d %d\n",
	         AP_AuthorLevelName(level), (int)s_place[last].x, (int)s_place[last].y,
	         (int)s_place[last].z);
	AP_LogLine(msg);

	if (s_marker[last] != AP_SPAWN_INVALID)
		AP_Spawn_Remove(s_marker[last]);

	for (i = last; i + 1 < s_placeCount; i++)
	{
		s_place[i] = s_place[i + 1];
		s_marker[i] = s_marker[i + 1];
	}
	s_placeCount--;
	s_marker[s_placeCount] = AP_SPAWN_INVALID;
	s_placeGen++;

	s_lastDropIndex = -1;
	AP_AuthorSave();
}

static void AP_AuthorList(struct GameTracker *gGT)
{
	int  level = (int)gGT->levelID;
	int  i, n = 0;
	char msg[128];

	snprintf(msg, sizeof msg, "[AP AUTHOR] --- %s (level %d) ---\n",
	         AP_AuthorLevelName(level), level);
	AP_LogLine(msg);

	for (i = 0; i < s_placeCount; i++)
	{
		if (s_place[i].level != (s16)level)
			continue;
		n++;
		snprintf(msg, sizeof msg, "[AP AUTHOR]   %2d: %d %d %d rot_y %d\n",
		         n, (int)s_place[i].x, (int)s_place[i].y, (int)s_place[i].z,
		         (int)s_place[i].rotY);
		AP_LogLine(msg);
	}

	snprintf(msg, sizeof msg, "[AP AUTHOR] --- %d here, %d total, written to %s ---\n",
	         n, s_placeCount, AP_AUTHOR_FILE);
	AP_LogLine(msg);

	AP_AuthorSave();
}

#ifdef CTR_CUSTOM_PACKAGES
// ============================================================================
// CUSTOM TRACKS (box authoring build only). A package raced from the Arcade
// custom pages borrows host slot 6, so its placements are keyed by the served
// package (UUID + LEV and VRM digests, ap_author_custom.h) and live in their
// own file, AP_CUSTOM_BOX_FILE. The retail table above, s_place and the retail
// file are never read or written from here, and the host slot's retail markers
// are never shown on custom geometry.
// ============================================================================

// s_markerLevel while the custom marker set is up: outside every LevelID.
#define AP_AUTHOR_CUSTOM_MARKER_LEVEL 1000

static AP_CustomBoxRow s_cplace[AP_AUTHOR_MAX_PLACEMENTS];
static AP_SpawnHandle  s_cmarker[AP_AUTHOR_MAX_PLACEMENTS];
static int             s_cplaceCount;
static int             s_cloaded;
static int             s_clastDropIndex = -1;
static AP_CustomBoxKey s_cmarkerKey;    // the track the custom marker set was built for
static int             s_cpauseLogged;  // one line per "custom track did not load"

static void AP_AuthorCustomForgetMarkers(void)
{
	int i;
	for (i = 0; i < AP_AUTHOR_MAX_PLACEMENTS; i++)
		s_cmarker[i] = AP_SPAWN_INVALID;
}

static void AP_AuthorCustomClearMarkers(void)
{
	int i;
	for (i = 0; i < s_cplaceCount; i++)
	{
		if (s_cmarker[i] != AP_SPAWN_INVALID)
		{
			AP_Spawn_Remove(s_cmarker[i]);
			s_cmarker[i] = AP_SPAWN_INVALID;
		}
	}
}

// What is on screen: 0 = not a custom-page race (retail authoring applies),
// 1 = the package's bytes are loaded (key/title/version filled), 2 = a custom
// race whose package did not finish loading (author mode pauses rather than
// guess which geometry this is), 3 = the resident geometry and the load's
// identity disagree (author mode pauses; only a savestate restore could do
// this, and the checkpoint refuses those, so this is the last line).
#define AP_AUTHOR_CUSTOM_MISMATCH 3
static int AP_AuthorCustomMode(AP_CustomBoxKey *key, char *title, size_t titleCap, char *version, size_t versionCap)
{
	struct CustomPackageManifest manifest;
	const char *lev = NULL, *vrm = NULL;
	unsigned int i;
	// LOAD_TenStages names the level "custom" exactly when it loads a package,
	// and gGT (with this name) is what a savestate brings back. Every key choice
	// below checks it against the live identity, so neither author path can
	// file a drop for geometry it does not own.
	int customGeometry = sdata != 0 && sdata->gGT != 0 && strcmp(sdata->gGT->levelName, "custom") == 0;

	if (!MainRaceTrack_OfflineCustomLoad())
		return customGeometry ? AP_AUTHOR_CUSTOM_MISMATCH : 0;
	if (!customGeometry)
		return AP_AUTHOR_CUSTOM_MISMATCH;
	if (!CustomOffline_RuntimeLoaded() || !CustomOffline_RuntimeManifest(&manifest))
		return 2;
	for (i = 0; i < manifest.count && i < CTR_PACKAGE_FILE_MAX; i++)
	{
		if (strcmp(manifest.files[i].role, "lev") == 0)
			lev = manifest.files[i].sha256;
		else if (strcmp(manifest.files[i].role, "vrm") == 0)
			vrm = manifest.files[i].sha256;
	}
	if (!AP_CustomBoxKey_Make(key, manifest.uuid, lev, vrm))
		return 2;
	if (title != NULL)
		AP_CustomBox_CopyText(title, titleCap, manifest.title);
	if (version != NULL)
		AP_CustomBox_CopyText(version, versionCap, manifest.version);
	return 1;
}

static void AP_AuthorCustomSave(void)
{
	FILE *f = fopen(AP_CUSTOM_BOX_FILE, "w");
	int   ok;

	if (f == 0)
	{
		AP_LogLine("[AP AUTHOR] could not write " AP_CUSTOM_BOX_FILE "\n");
		return;
	}
	ok = AP_CustomBoxFile_Write(f, s_cplace, s_cplaceCount, CTR_AP_VERSION);
	if (fclose(f) != 0)
		ok = 0;
	if (!ok)
		AP_LogLine("[AP AUTHOR] WARNING: " AP_CUSTOM_BOX_FILE " was not written completely\n");
}

static void AP_AuthorCustomEnsureLoaded(void)
{
	FILE *f;
	int   rejected = 0, overflow = 0, i;
	char  msg[192];

	if (s_cloaded)
		return;
	s_cloaded = 1;
	s_cplaceCount = 0;
	AP_AuthorCustomForgetMarkers();
	f = fopen(AP_CUSTOM_BOX_FILE, "r");
	if (f == 0)
	{
		AP_LogLine("[AP AUTHOR] custom tracks: no " AP_CUSTOM_BOX_FILE " yet, starting empty\n");
		return;
	}
	s_cplaceCount = AP_CustomBoxFile_Read(f, s_cplace, AP_AUTHOR_MAX_PLACEMENTS, &rejected, &overflow);
	fclose(f);
	for (i = 0; i < s_cplaceCount; i++)
		s_cmarker[i] = AP_SPAWN_INVALID;
	snprintf(msg, sizeof msg, "[AP AUTHOR] custom tracks: %d placement(s) from %s\n",
	         s_cplaceCount, AP_CUSTOM_BOX_FILE);
	AP_LogLine(msg);
	if (rejected > 0 || overflow)
	{
		// The next save rewrites the file from what was read, so say loudly
		// what that would drop before the helper places anything.
		snprintf(msg, sizeof msg,
		         "[AP AUTHOR] WARNING: %d unreadable line(s)%s in %s; they are dropped on the next save\n",
		         rejected, overflow ? " and rows past the table limit" : "", AP_CUSTOM_BOX_FILE);
		AP_LogLine(msg);
	}
}

static void AP_AuthorCustomSpawnMarker(struct GameTracker *gGT, int i)
{
	Vec3  pos;
	SVec3 rot;
	int   modelID;

	if (s_cmarker[i] != AP_SPAWN_INVALID || s_markerTableFull)
		return;
	modelID = AP_AuthorMarkerModel(gGT);
	if (modelID < 0)
		return;
	AP_BoxModel_SpawnPos(gGT->modelPtr[modelID], s_cplace[i].x, s_cplace[i].y, s_cplace[i].z, &pos);
	rot.x = 0;
	rot.y = s_cplace[i].rotY;
	rot.z = 0;
	s_cmarker[i] = AP_Spawn_Add(modelID, &pos, &rot, AP_SPAWN_LIFE_LEVEL, s_markerName);
	if (s_cmarker[i] == AP_SPAWN_INVALID)
		s_markerTableFull = 1;
}

static void AP_AuthorCustomDrop(struct GameTracker *gGT, const AP_CustomBoxKey *key,
                                const char *title, const char *version)
{
	struct Driver *d = gGT->drivers[0];
	int            clamped = 0;
	char           msg[256];
	char           name[48];

	if (d == 0)
		return;
	if (s_cplaceCount >= AP_AUTHOR_MAX_PLACEMENTS)
	{
		AP_LogLine("[AP AUTHOR] custom placement table full, drop refused\n");
		return;
	}
	memset(&s_cplace[s_cplaceCount], 0, sizeof s_cplace[s_cplaceCount]);
	s_cplace[s_cplaceCount].key = *key;
	AP_CustomBox_CopyText(s_cplace[s_cplaceCount].title, sizeof s_cplace[s_cplaceCount].title, title);
	AP_CustomBox_CopyText(s_cplace[s_cplaceCount].version, sizeof s_cplace[s_cplaceCount].version, version);
	s_cplace[s_cplaceCount].x = AP_AuthorNarrow(d->posCurr.x, &clamped);
	s_cplace[s_cplaceCount].y = AP_AuthorNarrow(d->posCurr.y, &clamped);
	s_cplace[s_cplaceCount].z = AP_AuthorNarrow(d->posCurr.z, &clamped);
	s_cplace[s_cplaceCount].rotY = d->rotCurr.y;
	s_cmarker[s_cplaceCount] = AP_SPAWN_INVALID;
	s_clastDropIndex = s_cplaceCount;
	s_cplaceCount++;

	s_markerTableFull = 0;
	AP_AuthorCustomSpawnMarker(gGT, s_clastDropIndex);

	AP_CustomBox_HudName(name, sizeof name, title);
	snprintf(msg, sizeof msg, "[AP AUTHOR] %s (custom %s): placement at %d %d %d rot_y %d\n",
	         name, key->uuid, (int)s_cplace[s_clastDropIndex].x, (int)s_cplace[s_clastDropIndex].y,
	         (int)s_cplace[s_clastDropIndex].z, (int)s_cplace[s_clastDropIndex].rotY);
	AP_LogLine(msg);
	if (clamped)
		AP_LogLine("[AP AUTHOR] WARNING: kart position did not fit 16 bits and was clamped\n");
	AP_AuthorCustomSave();
}

static void AP_AuthorCustomUndo(const AP_CustomBoxKey *key, const char *title)
{
	int  last = AP_CustomBox_LastIndexFor(s_cplace, s_cplaceCount, key);
	int  i;
	char msg[192];
	char name[48];

	AP_CustomBox_HudName(name, sizeof name, title);
	if (last < 0)
	{
		snprintf(msg, sizeof msg, "[AP AUTHOR] %s has no custom placements to delete\n", name);
		AP_LogLine(msg);
		return;
	}
	snprintf(msg, sizeof msg, "[AP AUTHOR] %s: deleted custom placement at %d %d %d\n", name,
	         (int)s_cplace[last].x, (int)s_cplace[last].y, (int)s_cplace[last].z);
	AP_LogLine(msg);
	if (s_cmarker[last] != AP_SPAWN_INVALID)
		AP_Spawn_Remove(s_cmarker[last]);
	for (i = last; i + 1 < s_cplaceCount; i++)
		s_cmarker[i] = s_cmarker[i + 1];
	s_cplaceCount = AP_CustomBox_Remove(s_cplace, s_cplaceCount, last);
	s_cmarker[s_cplaceCount] = AP_SPAWN_INVALID;
	s_clastDropIndex = -1;
	AP_AuthorCustomSave();
}

static void AP_AuthorCustomList(const AP_CustomBoxKey *key, const char *title)
{
	int  i, n = 0;
	char msg[256];
	char name[48];

	AP_CustomBox_HudName(name, sizeof name, title);
	snprintf(msg, sizeof msg, "[AP AUTHOR] --- %s (custom %s, lev %.12s, vrm %.12s) ---\n", name,
	         key->uuid, key->levSha256, key->vrmSha256);
	AP_LogLine(msg);
	for (i = 0; i < s_cplaceCount; i++)
	{
		if (!AP_CustomBoxKey_Equal(&s_cplace[i].key, key))
			continue;
		n++;
		snprintf(msg, sizeof msg, "[AP AUTHOR]   %2d: %d %d %d rot_y %d\n", n, (int)s_cplace[i].x,
		         (int)s_cplace[i].y, (int)s_cplace[i].z, (int)s_cplace[i].rotY);
		AP_LogLine(msg);
	}
	snprintf(msg, sizeof msg, "[AP AUTHOR] --- %d here, %d on all custom tracks, written to %s ---\n", n,
	         s_cplaceCount, AP_CUSTOM_BOX_FILE);
	AP_LogLine(msg);
	AP_AuthorCustomSave();
}

// The custom-track half of AP_Author_OnFrame. Returns 1 when it owned the
// frame (a custom-page race, loaded or not), 0 to fall through to retail.
static int AP_AuthorCustomOnFrame(struct GameTracker *gGT)
{
	static int      prevDrop = 0, prevUndo = 0, prevList = 0;
	AP_CustomBoxKey key;
	char            title[AP_CUSTOM_BOX_TEXT_MAX];
	char            version[AP_CUSTOM_BOX_TEXT_MAX];
	int             mode = AP_AuthorCustomMode(&key, title, sizeof title, version, sizeof version);
	int             drop, undo, list, i;

	if (mode == 0)
	{
		if (s_markerLevel == AP_AUTHOR_CUSTOM_MARKER_LEVEL)
		{
			AP_AuthorCustomClearMarkers();
			s_markerLevel = -1;
		}
		s_cpauseLogged = 0;
		return 0;
	}

	// Never show the host slot's retail markers on custom geometry.
	if (s_markerLevel >= 0 && s_markerLevel != AP_AUTHOR_CUSTOM_MARKER_LEVEL)
	{
		AP_AuthorClearMarkers();
		s_markerLevel = -1;
	}

	if (mode == 2 || mode == AP_AUTHOR_CUSTOM_MISMATCH)
	{
		if (s_markerLevel == AP_AUTHOR_CUSTOM_MARKER_LEVEL)
		{
			AP_AuthorCustomClearMarkers();
			s_markerLevel = -1;
		}
		if (!s_cpauseLogged)
		{
			s_cpauseLogged = 1;
			AP_LogLine(mode == 2 ? "[AP AUTHOR] custom track did not finish loading: author mode is paused on it\n"
			                     : "[AP AUTHOR] the loaded track and its identity disagree: author mode is paused "
			                       "until a level loads\n");
		}
		return 1;
	}
	s_cpauseLogged = 0;

	AP_AuthorCustomEnsureLoaded();
	if (s_markerLevel != AP_AUTHOR_CUSTOM_MARKER_LEVEL || !AP_CustomBoxKey_Equal(&key, &s_cmarkerKey))
	{
		AP_AuthorCustomClearMarkers();
		s_markerLevel = AP_AUTHOR_CUSTOM_MARKER_LEVEL;
		s_cmarkerKey = key;
		s_markerTableFull = 0;
		s_clastDropIndex = -1;
	}
	for (i = 0; i < s_cplaceCount; i++)
	{
		if (s_cmarker[i] == AP_SPAWN_INVALID && AP_CustomBoxKey_Equal(&s_cplace[i].key, &key))
			AP_AuthorCustomSpawnMarker(gGT, i);
	}

	drop = Platform_InputRawKeyDown(AP_AUTHOR_KEY_DROP);
	undo = Platform_InputRawKeyDown(AP_AUTHOR_KEY_UNDO);
	list = Platform_InputRawKeyDown(AP_AUTHOR_KEY_LIST);
	if (drop && !prevDrop)
		AP_AuthorCustomDrop(gGT, &key, title, version);
	if (undo && !prevUndo)
		AP_AuthorCustomUndo(&key, title);
	if (list && !prevList)
		AP_AuthorCustomList(&key, title);
	prevDrop = drop;
	prevUndo = undo;
	prevList = list;
	return 1;
}

// HUD for the custom half. Returns 1 when it drew.
static int AP_AuthorCustomDrawHud(void)
{
	static char     line1[64];
	static char     line2[64];
	AP_CustomBoxKey key;
	char            title[AP_CUSTOM_BOX_TEXT_MAX];
	char            name[32];
	int             mode = AP_AuthorCustomMode(&key, title, sizeof title, NULL, 0);

	if (mode == 0)
		return 0;
	if (mode == 2)
	{
		DecalFont_DrawLine("BOX AUTHOR  CUSTOM TRACK NOT LOADED", AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y,
		                   FONT_SMALL, WHITE);
		return 1;
	}
	if (mode == AP_AUTHOR_CUSTOM_MISMATCH)
	{
		DecalFont_DrawLine("BOX AUTHOR  TRACK UNCLEAR, LOAD IT AGAIN", AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y,
		                   FONT_SMALL, WHITE);
		return 1;
	}
	AP_CustomBox_HudName(name, sizeof name, title);
	snprintf(line1, sizeof line1, "BOX AUTHOR  %s  %d HERE", name,
	         AP_CustomBox_CountFor(s_cplace, s_cplaceCount, &key));
	DecalFont_DrawLine(line1, AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y, FONT_SMALL, WHITE);
	if (s_clastDropIndex >= 0 && s_clastDropIndex < s_cplaceCount &&
	    AP_CustomBoxKey_Equal(&s_cplace[s_clastDropIndex].key, &key))
	{
		snprintf(line2, sizeof line2, "LAST %d %d %d", (int)s_cplace[s_clastDropIndex].x,
		         (int)s_cplace[s_clastDropIndex].y, (int)s_cplace[s_clastDropIndex].z);
		DecalFont_DrawLine(line2, AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y + AP_AUTHOR_HUD_LINE_H, FONT_SMALL, WHITE);
	}
	return 1;
}
#endif // CTR_CUSTOM_PACKAGES

// ── per-frame ───────────────────────────────────────────────────────────────

void AP_Author_OnFrame(struct GameTracker *gGT)
{
	static int prevDrop = 0, prevUndo = 0, prevList = 0;
	int        drop, undo, list;
	int        enabled;
	int        level;

	if (gGT == 0)
		return;

	// First, before anything reads a handle. A pool reset (a level load OR a
	// race restart, which does not change the level) voided every handle we
	// hold and may already have re-issued the slot, so calling AP_Spawn_Remove
	// on one from here would delete somebody else's spawn.
	if (AP_Spawn_Generation() != s_markerGen)
	{
		AP_AuthorForgetMarkers();
		s_markerLevel = -1;
	}

	enabled = AP_Author_Enabled();

	if (!enabled)
	{
		if (s_enabledPrev)
		{
			// Turning the mode off takes the markers with it, so the option
			// behaves like a switch rather than like something that needs a
			// restart to undo.
			AP_AuthorClearMarkers();
#ifdef CTR_CUSTOM_PACKAGES
			AP_AuthorCustomClearMarkers();
#endif
			s_markerLevel = -1;
			s_enabledPrev = 0;
			AP_LogLine("[AP AUTHOR] mode off\n");
		}
		return;
	}

	// #194: author markers use the same additive spawn pool as runtime boxes.
	// During a level load or race restart the pool and PLAYER thread chain are
	// being rebuilt; queuing markers or accepting author input in that window can
	// leave work aimed at the old level and was the remaining safety asymmetry
	// with AP_Boxes_OnFrame. Boss races remain authorable once their driver is
	// born; this gate is about lifecycle readiness, not race type.
	if (!AP_AuthorRuntimeReady(
	        sdata != 0,
	        sdata != 0 && sdata->Loading.stage == LOAD_IDLE,
	        gGT->drivers[0] != 0,
	        gGT->drivers[0] != 0 && gGT->drivers[0]->instSelf != 0))
		return;

	// #194: author mode stands down in boss races (ap_author_ready.h). Checked
	// after the readiness gate, so any handle still held here belongs to the
	// current pool generation and is safe to remove.
	if (!AP_AuthorRaceAllowsAuthoring(IS_BOSS_RACE(gGT->gameMode1)))
	{
		if (s_markerLevel >= 0)
		{
			AP_AuthorClearMarkers();
			s_markerLevel = -1;
		}
		if (!s_bossPauseLogged)
		{
			s_bossPauseLogged = 1;
			AP_LogLine("[AP AUTHOR] boss race: author mode is paused here (#194); "
			           "markers and keys return in the normal race on this track\n");
		}
		return;
	}
	s_bossPauseLogged = 0;

	AP_Author_EnsureLoaded();

	if (!s_enabledPrev)
	{
		s_enabledPrev = 1;
		s_markerLevel = -1; // force a rebuild for whatever level is loaded
		AP_LogLine("[AP AUTHOR] mode ON -- Numpad 9 drop, Numpad 0 delete last, "
		           "Numpad . list + save\n");

		// Author mode reads and writes the EXTERNAL FILE ONLY (packaging item 5),
		// so switching it on with no file present would otherwise show an empty
		// track while 241 compiled-in placements sit right there -- and the first
		// save would then freeze that emptiness into the file. Seed the file from
		// the built-in default instead, once, on the edge: after this the mode is
		// editing a real file and every rule below is the plain file case.
		AP_AuthorSeedFileFromEmbedded();
	}

#ifdef CTR_CUSTOM_PACKAGES
	// A custom-page race owns the frame: its own key, file and markers.
	if (AP_AuthorCustomOnFrame(gGT))
		return;
#endif

	level = (int)gGT->levelID;
	if (level != s_markerLevel)
		AP_AuthorRebuildMarkers(gGT, level);
	else
	{
		// A marker whose model was not loaded yet when it was requested gets its
		// retry here, which is what makes the set correct a few frames after a
		// level finishes loading rather than never.
		int i;
		for (i = 0; i < s_placeCount; i++)
		{
			if (s_place[i].level == (s16)level && s_marker[i] == AP_SPAWN_INVALID)
				AP_AuthorSpawnMarker(gGT, i);
		}
	}

	drop = Platform_InputRawKeyDown(AP_AUTHOR_KEY_DROP);
	undo = Platform_InputRawKeyDown(AP_AUTHOR_KEY_UNDO);
	list = Platform_InputRawKeyDown(AP_AUTHOR_KEY_LIST);

	if (drop && !prevDrop)
		AP_AuthorDrop(gGT);
	if (undo && !prevUndo)
		AP_AuthorUndo(gGT);
	if (list && !prevList)
		AP_AuthorList(gGT);

	prevDrop = drop;
	prevUndo = undo;
	prevList = list;
}

void AP_Author_DrawHud(void)
{
	static char line1[64];
	static char line2[64];
	struct GameTracker *gGT;
	int level, i, n = 0;

	if (!AP_Author_Enabled())
		return;
	if (sdata == 0 || sdata->gGT == 0)
		return;

	gGT = sdata->gGT;
	level = (int)gGT->levelID;

	// #194: say why nothing happens instead of showing a count the keys cannot edit.
	if (!AP_AuthorRaceAllowsAuthoring(IS_BOSS_RACE(gGT->gameMode1)))
	{
		DecalFont_DrawLine("BOX AUTHOR  PAUSED IN BOSS RACES", AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y,
		                   FONT_SMALL, WHITE);
		return;
	}

#ifdef CTR_CUSTOM_PACKAGES
	if (AP_AuthorCustomDrawHud())
		return;
#endif

	for (i = 0; i < s_placeCount; i++)
	{
		if (s_place[i].level == (s16)level)
			n++;
	}

	snprintf(line1, sizeof line1, "BOX AUTHOR  %s  %d HERE", AP_AuthorLevelName(level), n);
	DecalFont_DrawLine(line1, AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y, FONT_SMALL, WHITE);

	// The stored value, not the live one: the point of showing it is to author
	// against the number the file will actually hold.
	if (s_lastDropIndex >= 0 && s_lastDropIndex < s_placeCount)
	{
		snprintf(line2, sizeof line2, "LAST %d %d %d",
		         (int)s_place[s_lastDropIndex].x, (int)s_place[s_lastDropIndex].y,
		         (int)s_place[s_lastDropIndex].z);
		DecalFont_DrawLine(line2, AP_AUTHOR_HUD_X, AP_AUTHOR_HUD_Y + AP_AUTHOR_HUD_LINE_H,
		                   FONT_SMALL, WHITE);
	}
}

// ── the shared placement table ──────────────────────────────────────────────

void AP_Author_EnsureLoaded(void)
{
	if (!s_loaded)
		AP_AuthorLoad();
}

int AP_Author_PlacementSource(void)
{
	return s_source;
}

// Below this line the two tables merge into one read-only view, and the merge
// itself is NOT written here: it is AP_PlacementTable_* in ap_placement_table.h,
// which tools/test-box-map.c exercises directly. This file only says which source
// won and where the file's rows are, so the tested rule and the shipped rule are
// the same code.
//
// Author mode never comes through here -- it edits s_place directly -- which is
// what keeps the embedded default un-editable and the file the only thing a save
// can touch.

static AP_PlacementTable AP_AuthorLiveTable(void)
{
	AP_PlacementTable t;

	t.source = s_source;
	t.file = s_place;
	t.fileCount = s_placeCount;
	return t;
}

int AP_Author_PlacementCount(void)
{
	AP_PlacementTable t = AP_AuthorLiveTable();

	return AP_PlacementTable_Count(&t);
}

int AP_Author_PlacementGet(int index, int *level, short *x, short *y, short *z, short *rotY)
{
	AP_PlacementTable t = AP_AuthorLiveTable();

	return AP_PlacementTable_Get(&t, index, level, x, y, z, rotY);
}

int AP_Author_PlacementGeneration(void)
{
	return s_placeGen;
}

#endif // CTR_AP
