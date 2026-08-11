#ifdef CTR_AP

#include <common.h> // struct Instance / GameTracker / Driver / Thread, sdata, functions.h
#include <stdio.h>
#include <stdlib.h> // rand(), same source of cosmetic spin the vanilla shatter uses

#include "ap_boxes.h"
#include "ap_spawn.h"   // additive model loader: the crates themselves
#include "ap_author.h"  // the placement table (one reader, shared with #182)
#include "ap_seedcfg.h" // ctr_cfg_active(): is a seed loaded at all
#include "ap_net.h"     // ap_net_location_checked(): server truth
#include "ap_hooks.h"   // AP_LogLine, AP_EmitBoxCheck

// ============================================================================
// AP ITEM BOXES -- runtime. See ap_boxes.h for the ruled semantics, for why the
// collision is a proximity walk rather than a BSP hitbox, and for why the relic
// race needs no engine edit. One translation-unit member of the unity build.
// ============================================================================

// The vanilla crate shatter, reused verbatim so an AP box breaks like a crate.
// Both values are bare literals in RB_Crate.c with no named constant to derive
// from, so they are named here with the site they came from.
#define AP_BOX_EXPLODE_COLOUR 0xfafafa0 // RB_CrateWeapon_ThCollide, RB_Crate.c:249
#define AP_BOX_EXPLODE_SOUND  0x3c      // RB_CrateAny_ExplodeInit, RB_Crate.c:137
#define AP_BOX_EXPLODE_SPIN   0xfff     // RB_CrateAny_ExplodeInit, RB_Crate.c:122

// The shatter thread. Defined non-static in RB_Crate.c:68 but absent from
// functions.h, so it is declared here rather than resting on the unity build's
// include order.
void RB_CrateAny_ThTick_Explode(struct Thread *t);

// Instance name. char[16] rather than a literal because INSTANCE_Birth copies a
// fixed 15 characters out of whatever it is handed (INSTANCE.c:24-27).
static char s_boxName[16] = "apbox";

struct ApBoxLive
{
	int            used;
	int            slot; // 0-based; the frozen name is "Item Box <slot+1>"
	long           code;
	short          x, y, z, rotY; // kept so a spawn can be retried once the model is up
	AP_SpawnHandle spawn;
};

static struct ApBoxLive s_live[AP_BOX_SLOTS_PER_TRACK];
static int              s_liveCount;

static int s_level = -1;    // level the live set was built for
static int s_spawnGen;      // AP_Spawn_Generation() the handles were taken at
static int s_placeGen = -1; // AP_Author_PlacementGeneration() the set was built from
static int s_advMode = -1;  // ADVENTURE_MODE state the set was built under
static int s_modelWarned;   // "no crate model here" logged once per level
static int s_spawnFull;     // the loader refused a box on this level: stop asking
static int s_standDown;     // author mode / no seed: logged once per transition

// Scratch for the set builder. Static rather than automatic: the placement
// table is 512 entries and this is a rebuild path, not a per-frame one.
static AP_BoxPlacement s_scratch[AP_AUTHOR_MAX_PLACEMENTS];

int AP_Boxes_LiveCount(void)
{
	return s_liveCount;
}

// ── seed membership ─────────────────────────────────────────────────────────

// THE SPAWN RULE (ruled 2026-08-11, rulings note §7). A box stands ONLY if it
// has a LIVE multiworld location in the connected slot's seed: a real wire code
// (AP_BoxMap_Code != -1) that this world actually created. The placement set is
// GEOMETRY ONLY; the wire data decides liveness. A slot whose seed carries zero
// box locations therefore stands zero boxes, and a seed with a reduced seating
// tier stands exactly the locations it created, no more.
//
// THE DEV FALLBACK IS RETIRED (ruled 2026-08-11, 15:54 addendum). It used to show
// the full authored set on a seed with no box locations, back when the #177 name
// freeze had not merged and a strict reading left the mechanism untestable. With
// #109 merged that purpose is served, and the fallback had teeth: on a slot
// without box locations it SENT checks for location ids that do not exist in that
// world (observed live from the Bandi slot on Coco Park: 35014060/66/68/69).
// MultiServer 0.6.x silently discarded them there, but 0.6.7 hard-drops the
// connection on invalid ids in the scout path, so this was a coin flip across
// server versions, not a harmless diagnostic.
//
// The sole surviving exception is box_author=true, which is EXPLICIT-ON and shows
// the full authored set as author-mode MARKERS (AP_Boxes_OnFrame stands the
// runtime boxes down entirely while it is on) -- markers send nothing, so the
// exception cannot reach the wire.
static int AP_BoxInSeed(long code, void *ctx)
{
	(void)ctx;
	return ap_net_location_exists((long long)code);
}

// Diagnosis only, NEVER policy: does the connected seed carry the item_boxes
// class anywhere? Used to tell the two zero-box cases apart in the log -- "this
// seed has no boxes at all" reads very differently from "this seed HAS boxes but
// this track resolved to none", and the second one is a bug report.
static int s_seedHasBoxClass;

static void AP_BoxProbeSeedClass(void)
{
	int i;

	s_seedHasBoxClass = 0;
	for (i = 0; i < AP_BOX_LOCATION_COUNT; i++)
	{
		if (ap_net_location_exists((long long)(AP_BOX_CODE_BASE + i)))
		{
			s_seedHasBoxClass = 1;
			return;
		}
	}
}

// Already broken on this seed. Server truth, so it survives a reconnect, a
// re-launch and a /send_location from somebody else (the #188 discipline: the
// client never keeps its own idea of what has been checked).
static int AP_BoxChecked(long code, void *ctx)
{
	(void)ctx;
	return ap_net_location_checked((long long)code);
}

// Should this level's race carry AP boxes at all?
//
// ADVENTURE MODE ONLY, and this is a DECISION, not a ruling -- flagged in the
// build note. The ruling says boxes appear in every race type "including relic
// races", which is about the adventure pad modes (trophy / relic / token /
// crystal), all of which pass here. Arcade, VS and battle do not, for two
// reasons:
//
//   * logic. The apworld gives every box its track's pad-access rules for free
//     via region membership (the 08-06 positioning design). An arcade race
//     reaches any track with no pad at all, so a box breakable from Arcade is a
//     box outside its own logic, and a player could sweep 241 checks without
//     opening a single pad.
//   * convention. Native already draws this line: AP_ClassifyRace returns
//     AP_RACE_ARCADE for anything outside ADVENTURE_MODE (ap_hooks.c:3494-3496)
//     and the podium fan-out fires only on an adventure trophy race.
//
// Boss races are adventure races on box tracks, so they are IN. Nothing is
// gained by excluding them: it is the same track and the same location.
static int AP_BoxesRaceCarriesBoxes(struct GameTracker *gGT)
{
	return (gGT->gameMode1 & ADVENTURE_MODE) != 0;
}

// ── the live set ────────────────────────────────────────────────────────────

// Which model an AP box wears. PU_RANDOM_CRATE, the weapon box, per the ruled
// #109 look and per the 2026-08-09 floor-invisible bug: the STATIC_AP logo path
// makes track floors disappear under many-instance author-mode usage, and the
// crate is the intended endgame look anyway, so the diagnostic and the design
// are the same thing. Do NOT re-promote STATIC_AP here.
//
// Returns -1 when the model is not in this level's model list, which is a real
// possibility worth logging rather than assuming: the crate is stripped of
// DRAW_COLLISION_MASK in relic and time-trial modes (INSTANCE.c:384-397) but
// that only touches instance flags, so the MODEL should still be resident. If
// this ever logs, the relic-race half of the ruling has a genuine asset problem.
static int AP_BoxModel(struct GameTracker *gGT)
{
	if (gGT->modelPtr[PU_RANDOM_CRATE] != 0)
		return PU_RANDOM_CRATE;
	return -1;
}

static void AP_BoxesForget(void)
{
	int i;

	// Every handle is void after a pool reset -- the slot may already belong to
	// somebody else -- so drop them WITHOUT calling AP_Spawn_Remove.
	for (i = 0; i < AP_BOX_SLOTS_PER_TRACK; i++)
	{
		s_live[i].used = 0;
		s_live[i].spawn = AP_SPAWN_INVALID;
	}
	s_liveCount = 0;
	s_spawnGen = AP_Spawn_Generation();
	s_modelWarned = 0;
	s_spawnFull = 0;
}

static void AP_BoxesClear(void)
{
	int i;

	for (i = 0; i < AP_BOX_SLOTS_PER_TRACK; i++)
	{
		if (s_live[i].used && s_live[i].spawn != AP_SPAWN_INVALID)
			AP_Spawn_Remove(s_live[i].spawn);
		s_live[i].used = 0;
		s_live[i].spawn = AP_SPAWN_INVALID;
	}
	s_liveCount = 0;
	s_modelWarned = 0;
	s_spawnFull = 0;
}

// Pull the shared placement table into the builder's shape.
static int AP_BoxesSnapshotPlacements(void)
{
	int n = AP_Author_PlacementCount();
	int i, kept = 0;

	if (n > AP_AUTHOR_MAX_PLACEMENTS)
		n = AP_AUTHOR_MAX_PLACEMENTS;

	for (i = 0; i < n; i++)
	{
		int   level;
		short x, y, z, rotY;

		if (!AP_Author_PlacementGet(i, &level, &x, &y, &z, &rotY))
			continue;

		s_scratch[kept].level = level;
		s_scratch[kept].x = x;
		s_scratch[kept].y = y;
		s_scratch[kept].z = z;
		s_scratch[kept].rotY = rotY;
		kept++;
	}

	return kept;
}

static void AP_BoxesSpawnOne(struct GameTracker *gGT, int i)
{
	Vec3  pos;
	SVec3 rot;
	int   modelID;

	if (s_live[i].spawn != AP_SPAWN_INVALID)
		return;

	// The loader has already said no for this level. Without this latch the
	// per-frame retry in AP_BoxesTick would ask again every frame and every
	// refusal writes a log line, which is an fopen per line (ap_hooks.c) -- file
	// IO in the frame loop for as long as the track is loaded. Same latch, same
	// reason, as ap_author.c's marker path.
	if (s_spawnFull)
		return;

	modelID = AP_BoxModel(gGT);
	if (modelID < 0)
	{
		if (!s_modelWarned)
		{
			char msg[112];
			s_modelWarned = 1;
			snprintf(msg, sizeof msg,
			         "[AP BOX] level %d has no PU_RANDOM_CRATE model; %d box(es) cannot be drawn\n",
			         s_level, s_liveCount);
			AP_LogLine(msg);
		}
		return; // retried next frame once the level's models are up
	}

	pos.x = s_live[i].x;
	pos.y = s_live[i].y;
	pos.z = s_live[i].z;
	rot.x = 0;
	rot.y = s_live[i].rotY;
	rot.z = 0;

	s_live[i].spawn = AP_Spawn_Add(modelID, &pos, &rot, AP_SPAWN_LIFE_LEVEL, s_boxName);
	if (s_live[i].spawn == AP_SPAWN_INVALID)
		s_spawnFull = 1;
}

// How many placements this level carries in the LIVE table, capped at the frozen
// ceiling: the number of box slots this track could possibly stand if every one
// of them were live and unchecked. The denominator of the zero-box diagnosis.
static int AP_BoxesGeometryHere(const AP_BoxPlacement *scratch, int count, int level)
{
	int i, here = 0;

	for (i = 0; i < count; i++)
	{
		if (scratch[i].level != level)
			continue;
		if (here >= AP_BOX_SLOTS_PER_TRACK)
			break;
		here++;
	}
	return here;
}

static void AP_BoxesRebuild(struct GameTracker *gGT, int level)
{
	AP_BoxSlot slots[AP_BOX_SLOTS_PER_TRACK];
	int        placements, here;
	int        n, i, overflow = 0;
	char       msg[192];

	AP_BoxesClear();
	s_level = level;
	s_placeGen = AP_Author_PlacementGeneration();
	s_advMode = AP_BoxesRaceCarriesBoxes(gGT);

	if (AP_BoxMap_ApTrack(level) < 0)
		return; // hub, arena, menu: not a box track
	if (!AP_BoxesRaceCarriesBoxes(gGT))
		return; // arcade / VS / battle: outside the boxes' own logic

	AP_BoxProbeSeedClass();
	placements = AP_BoxesSnapshotPlacements();
	here = AP_BoxesGeometryHere(s_scratch, placements, level);

	n = AP_BoxMap_BuildSet(s_scratch, placements, level,
	                       AP_BoxInSeed, AP_BoxChecked, 0,
	                       slots, AP_BOX_SLOTS_PER_TRACK, &overflow);

	for (i = 0; i < n; i++)
	{
		s_live[i].used = 1;
		s_live[i].slot = slots[i].slot;
		s_live[i].code = slots[i].code;
		s_live[i].spawn = AP_SPAWN_INVALID;
		s_live[i].x = slots[i].x;
		s_live[i].y = slots[i].y;
		s_live[i].z = slots[i].z;
		s_live[i].rotY = slots[i].rotY;
		s_liveCount++;
	}

	for (i = 0; i < n; i++)
		AP_BoxesSpawnOne(gGT, i);

	// ONE line per rebuild, and it names the live placement SOURCE: a support log
	// has to answer "which set is this player running" without a second question.
	snprintf(msg, sizeof msg,
	         "[AP BOX] level %d (ap track %d): %d of %d placement(s) standing, placements from %s, seed %s\n",
	         level, AP_BoxMap_ApTrack(level), s_liveCount, here,
	         AP_Author_PlacementSource() == AP_PLACEMENT_SRC_FILE
	             ? "the external " AP_AUTHOR_FILE
	             : "the built-in default",
	         s_seedHasBoxClass ? "carries the item_boxes class"
	                           : "carries NO box locations");
	AP_LogLine(msg);

	// THE ZERO CASE, LOUD (packaging item 3). With the dev fallback retired a
	// track can legitimately stand nothing, and the three reasons look identical
	// on screen -- an empty track -- but mean completely different things:
	//
	//   * no geometry here            -> the placement set does not cover this
	//                                    track. A packaging problem.
	//   * geometry, seed has NO boxes -> itemsanity is off in this slot's seed.
	//                                    Correct and silent-worthy, but still
	//                                    worth one line so nobody hunts a bug.
	//   * geometry, seed HAS boxes    -> this slot's world created box locations
	//                                    but none of THIS track's are live and
	//                                    unchecked. Either every box here is
	//                                    already broken (fine) or the code block
	//                                    and the seed disagree (a real bug).
	//
	// Never silent (Lessons Learned §4): the case that used to be papered over by
	// the fallback is exactly the case that needs saying out loud.
	if (s_liveCount == 0)
	{
		if (here == 0)
			snprintf(msg, sizeof msg,
			         "[AP BOX] WARNING: level %d (ap track %d) has NO placements in the live set -- "
			         "no box can ever stand here\n",
			         level, AP_BoxMap_ApTrack(level));
		else if (!s_seedHasBoxClass)
			snprintf(msg, sizeof msg,
			         "[AP BOX] level %d: %d placement(s) here, but this slot's seed carries no box "
			         "locations at all -- itemsanity is off for this slot, so nothing stands\n",
			         level, here);
		else
			snprintf(msg, sizeof msg,
			         "[AP BOX] WARNING: level %d: %d placement(s) here and this seed DOES carry box "
			         "locations, yet ZERO stand -- all already checked, or the code block and the "
			         "seed disagree\n",
			         level, here);
		AP_LogLine(msg);
	}

	// Never let a bound drop data quietly (Lessons Learned §4).
	if (overflow > 0)
	{
		snprintf(msg, sizeof msg,
		         "[AP BOX] level %d: %d placement(s) past the %d-slot ceiling, no name, not spawned\n",
		         level, overflow, AP_BOX_SLOTS_PER_TRACK);
		AP_LogLine(msg);
	}
}

// ── the break ───────────────────────────────────────────────────────────────

// The vanilla crate shatter, at the box's matrix. Mirrors RB_CrateAny_ExplodeInit
// (RB_Crate.c:88-139) in the same order, including hiding nothing here: the box
// instance is removed by the caller instead of being scaled to zero, because an
// AP box never grows back.
static void AP_BoxExplode(struct GameTracker *gGT, struct Instance *boxInst)
{
	struct Instance *fx;
	MATRIX           rotMatrix;
	SVec3            rot;

	if (gGT->modelPtr[STATIC_CRATE_EXPLOSION] == 0)
		return; // no shatter model on this level: the check still fires

	fx = INSTANCE_BirthWithThread(STATIC_CRATE_EXPLOSION, 0, SMALL, OTHER,
	                              RB_CrateAny_ThTick_Explode, 0, 0);
	if (fx == 0)
		return; // pool full; INSTANCE_BirthWithThread already logged it

	fx->colorRGBA = AP_BOX_EXPLODE_COLOUR;
	fx->alphaScale = 0x1000;

	fx->matrix.t[0] = boxInst->matrix.t[0];
	fx->matrix.t[1] = boxInst->matrix.t[1];
	fx->matrix.t[2] = boxInst->matrix.t[2];

	rot.x = 0;
	rot.y = (s16)(rand() % AP_BOX_EXPLODE_SPIN);
	rot.z = 0;
	ConvertRotToMatrix(&rotMatrix, &rot);
	MatrixRotate(&fx->matrix, &boxInst->matrix, &rotMatrix);

	PlaySound3D(AP_BOX_EXPLODE_SOUND, fx);
}

static void AP_BoxBreak(struct GameTracker *gGT, int i, struct Instance *boxInst)
{
	AP_BoxExplode(gGT, boxInst);

	// Emitted BEFORE the despawn so a failure to remove cannot cost the check.
	AP_EmitBoxCheck(s_level, s_live[i].slot, s_live[i].code);

	if (s_live[i].spawn != AP_SPAWN_INVALID)
		AP_Spawn_Remove(s_live[i].spawn);
	s_live[i].spawn = AP_SPAWN_INVALID;
	s_live[i].used = 0;
	s_liveCount--;
}

// The local human player, if this instance is one. The PLAYER bucket holds
// human drivers only (AI live in ROBOT, BOTS.c:3097), so the ACTION_BOT test is
// belt and braces rather than the isolation itself; the drivers[0] test is what
// keeps a second local player in splitscreen from sending our checks, matching
// the convention every other AP module uses.
static int AP_BoxHitByLocalPlayer(struct GameTracker *gGT, struct Instance *hitInst)
{
	struct Driver *d;

	if (hitInst == 0 || hitInst->thread == 0)
		return 0;

	d = (struct Driver *)hitInst->thread->object;
	if (d == 0 || d != gGT->drivers[0])
		return 0;
	if ((d->actionsFlagSet & ACTION_BOT) != 0)
		return 0;

	return 1;
}

// ── per-frame ───────────────────────────────────────────────────────────────

static void AP_BoxesTick(struct GameTracker *gGT)
{
	int i;

	for (i = 0; i < AP_BOX_SLOTS_PER_TRACK; i++)
	{
		struct Instance *inst;
		struct Instance *hit;

		if (!s_live[i].used)
			continue;

		// Server truth first: the checked set can arrive or move underneath us
		// (connect handshake mid-level, another client, a manual /send_location),
		// and a box whose location is checked must not be standing.
		if (ap_net_location_checked((long long)s_live[i].code))
		{
			if (s_live[i].spawn != AP_SPAWN_INVALID)
				AP_Spawn_Remove(s_live[i].spawn);
			s_live[i].spawn = AP_SPAWN_INVALID;
			s_live[i].used = 0;
			s_liveCount--;
			continue;
		}

		if (s_live[i].spawn == AP_SPAWN_INVALID)
		{
			AP_BoxesSpawnOne(gGT, i); // model was not up yet when we asked
			continue;
		}

		inst = AP_Spawn_Instance(s_live[i].spawn);
		if (inst == 0)
			continue; // waiting for its model, or between a pool reset and a birth

		// The proximity walk the engine uses for its own non-BSP objects. PLAYER
		// bucket only, which is the whole of the AI pass-through rule.
		hit = LinkedCollide_Radius(inst, 0, gGT->threadBuckets[PLAYER].thread,
		                           AP_BOX_HIT_RADIUS_SQUARED);
		if (hit == 0)
			continue;

		if (!AP_BoxHitByLocalPlayer(gGT, hit))
			continue;

		AP_BoxBreak(gGT, i, inst);
	}
}

void AP_Boxes_OnFrame(struct GameTracker *gGT)
{
	int level;

	if (gGT == 0)
		return;

	// Load gate (2026-08-11, five field crashes, root cause from the
	// independent crash-research session): during a level load or race
	// restart the PLAYER thread bucket can still reference the CLEARED thread
	// pool, and ANY walk over it dereferences stale thread nodes -- three
	// crashes in LinkedCollide_Radius, then two more inside a triage guard
	// that tried to null-check the same list first. A freed list cannot be
	// made safe to walk node-by-node; the whole box system (rebuild, spawn,
	// collision) stands down until loading is idle and the local driver is
	// fully born, which is the invariant the engine's own bucket walkers get
	// for free by running from object threads born after the drivers.
	if (sdata == 0 || sdata->Loading.stage != LOAD_IDLE)
		return;
	if (gGT->drivers[0] == 0 || gGT->drivers[0]->instSelf == 0)
		return;

	// Before anything reads a handle. A pool reset (a level load OR a race
	// restart, which does not change the level) voided every handle we hold.
	if (AP_Spawn_Generation() != s_spawnGen)
	{
		AP_BoxesForget();
		s_level = -1;
	}

	// Author mode owns the markers while it is on, and shows the FULL authored
	// set regardless of seed state (ruling 2). Standing down is how that ruling
	// is implemented: two crates inside each other would help nobody.
	if (AP_Author_Enabled())
	{
		if (s_liveCount > 0 || s_level >= 0)
		{
			AP_BoxesClear();
			s_level = -1;
		}
		if (!s_standDown)
		{
			s_standDown = 1;
			AP_LogLine("[AP BOX] author mode on: runtime boxes stand down, markers are the set\n");
		}
		return;
	}

	// Visibility follows the seed (ruling 1): no seed, no boxes.
	if (!ctr_cfg_active())
	{
		if (s_liveCount > 0 || s_level >= 0)
		{
			AP_BoxesClear();
			s_level = -1;
		}
		return;
	}

	s_standDown = 0;

	AP_Author_EnsureLoaded();

	level = (int)gGT->levelID;
	if (level != s_level || AP_Author_PlacementGeneration() != s_placeGen ||
	    AP_BoxesRaceCarriesBoxes(gGT) != s_advMode)
		AP_BoxesRebuild(gGT, level);

	if (s_liveCount > 0)
		AP_BoxesTick(gGT);
}

#endif // CTR_AP
