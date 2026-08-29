#ifndef NATIVE_CUSTOM_TRACKS_POLICY_H
#define NATIVE_CUSTOM_TRACKS_POLICY_H

// Custom-track loader DECISIONS, ruled for the Baby T Park event spike (rung 1).
// Deliberately freestanding, exactly like ap_cup_box_policy.h: the gather and
// the I/O live in engine (platform/native_custom_tracks.c), the decisions live
// here so tools/test-custom-track-policy.c can pin the whole truth table out of
// engine, with no disc, no display, no config file and no seed.
//
// NINE DECISIONS LIVE HERE.
//
// 1. PAIR AUTO-EXPAND. A retail arcade track occupies a contiguous group of 8
//    BIGFILE subfiles at [levelID*8, levelID*8 + 8) because BI_ARCADETRACKS is
//    0. The group is four (vrm, lev) mode-pairs: even slots 0/2/4/6 are VRMs,
//    odd slots 1/3/5/7 are the per-mode LEVs (1P, 2P, 4P, relic). A community
//    custom track ships exactly ONE .lev and ONE .vrm, so the loader expands
//    that single pair across all four mode slots in memory rather than making
//    the packager write eight files. A 1P race reads only slots 0 and 1, so the
//    expansion costs nothing on the path the spike actually exercises; slots
//    2..7 exist so a 2P/4P/relic entry serves plausible bytes instead of the
//    retail track's, which would be a silent content swap mid-group.
//
// 2. CONTENT VERDICT. Serving a track subfile is serving unverified bytes into
//    the engine's load path. Every source file is hashed and compared against
//    the digest the feature config names BEFORE the first byte is served, and
//    any verdict other than OK refuses the whole track. The verdict enum is
//    ordered by how loudly it should read in a log, not by severity.
//
// 3. PURPLE-DESTINATION REDIRECT. Ruled 2026-08-28: when the spike feature is
//    enabled, the seed's Purple Gem Cup destination becomes a full race-track
//    destination running the custom track -- a SINGLE race, 7 laps, AI on, and
//    winning it awards the Purple Gem through the cup's own award path. The
//    redirect is expressed here as two pure answers -- "does this cup entry
//    become a single race" and "is this leg the cup's last" -- because those
//    are the two forks the engine takes, in two different files, and they must
//    not be able to disagree. They are computed from the same inputs, so a
//    cup that redirected at the pad cannot fail to complete at the results
//    screen.
//
// 4. SERVE CONTEXT (rung 2a). The custom track takes over an existing arcade
//    slot, but the ruling replaces exactly ONE destination -- the event cup --
//    not the host slot's retail race. So the override is conditional on the
//    load in flight actually BEING the redirected race, not merely on the
//    subfile index belonging to the host slot. A retail race pad to the host
//    slot loads retail bytes in the same session where the event cup loads
//    custom bytes.
//
//    The three facts that decide it are all committed before the first subfile
//    read of a level, in this order, in one call chain:
//      gGT->cup.cupID        AH_WarpPad.c, at the pad, frames earlier
//      gGT->gameMode1        MainMain.c, applying Loading.OnBegin.AddBitsConfig0
//      gGT->levelID          LOAD_Level.c, in LOAD_LevelFile
//      Loading.stage = 0     LOAD_Level.c, arming the ten-stage loader
//    Arcade-track subfiles are requested only in ten-stage stage 6, at least six
//    frames later, and LOAD_LevelFile has exactly one caller. There is no
//    prefetch, streaming, speculative or background read of an arcade-track
//    group anywhere else in the tree: every other queue index is a fixed
//    BI_* region, and hub streaming is hard-guarded to hub levelIDs.
//
//    ADVENTURE_CUP is the load-bearing term. It has one setter (the gem-cup
//    branch of the warp pad) and is cleared through Loading.OnBegin.RemBitsConfig0
//    on every exit -- cup played out, exit to map, pause-quit -- which lands at
//    MainMain.c before the next load starts. gGT->cup.cupID, by contrast, is
//    NEVER reset and stays at its last value forever, so it must never be tested
//    on its own. Arcade cups cannot reach this predicate at all: they set
//    CUP_ANY_KIND in gameMode2 and never ADVENTURE_CUP.
//
// 5. AP-BOX GATE (rung 2a). The redirected race sets ADVENTURE_CUP for reward
//    routing, so ap_boxes.c would otherwise treat it as a gem-cup LEG and gate
//    its boxes on the physical retail pad that hosts the mapped slot -- a pad
//    that has nothing to do with the event. The verdict below makes that a
//    deliberate answer instead of a fall-through. Default is ALLOW, per the
//    ruled check set. See the enum for what is still missing before those boxes
//    mean anything.
//
// 6. IS AN ST1 TABLE ENTRY REALLY THERE. Every level carries a SpawnType1
//    pointer table (struct Level::ptrSpawnType1) whose entries are indexed by
//    the ST1_* enum -- minimap, object spawns, end-of-race cameras, the intro
//    camera path, the two ghosts, credits. Retail encodes "this level has no X"
//    by making the table SHORT, so every engine consumer guards with a count
//    threshold and then dereferences the entry unconditionally. Measured across
//    the retail NTSC-U BIGFILE, all 18 arcade tracks have count == 4 with a
//    non-NULL entry in every one of those four slots, and all 7 battle arenas
//    have count == 0 -- so on retail content a count threshold and "the entry is
//    really there" are the same question, and the engine is right.
//
//    A custom track need not agree. A community packager can emit a FULL-WIDTH
//    table and encode absence as a NULL entry instead: the entry's slot is
//    simply left out of the LEV's pointer map, so LOAD_RunPtrMap never relocates
//    it and it arrives as 0. Baby T Park does exactly that -- count == 7 with
//    ST1_MAP, ST1_CAMERA_EOR, ST1_CAMERA_PATH and ST1_CREDITS all NULL -- which
//    walks straight past every count threshold in the engine.
//
//    So the question a consumer actually needs answered is not "is the table
//    long enough" but "is the entry at this index really there", and that is
//    what the predicate below answers. It subsumes the count threshold rather
//    than replacing it, which is why a call site can swap one for the other
//    without changing what retail content does.
//
// 7. IS THERE ROOM FOR ONE MORE PRIMITIVE. The engine gives each frame a fixed
//    primitive arena whose size is looked up BY LEVEL ID
//    (data.primMem_SizePerLEV_1P[levelID] << 10, game/MAIN/MainInit.c). A custom
//    track borrows an arcade slot, so it inherits the budget retail sized for
//    THAT slot's geometry -- a number chosen for entirely different content.
//
//    Most emitters already bound themselves against PrimMem::guardEnd, because
//    retail's own content could get close to it. Two do not, and both take their
//    iteration count straight from the level file, so a custom track sets it:
//    DrawSky (skybox->numFaces) and RenderStars (stars.numStars). Past the end
//    of the arena the native port's GPU-link bridge has no token for the
//    primitive's address and aborts, so an unbounded emitter is a hard crash
//    rather than a visual artifact.
//
//    Measured on the event track against the levelID-6 budget of 0x67 << 10 =
//    105,472 bytes: its skybox carries 2,772 faces in EVERY one of its eight
//    segments, and DrawSky draws four segments per frame, so one frame demands
//    up to 11,088 POLY_G3 = 310,464 bytes -- 294% of the whole arena, before any
//    other draw. Across the 18 retail arcade tracks the same measurement is 69
//    to 385 faces, 1.8% to 9.9% of their budgets. The event track is a 29x
//    outlier, which is why retail never needed the bound.
//
//    Growing the arena is not an available answer: the budget is a u8 << 10, so
//    its ceiling is 261,120 bytes and this one track's sky alone wants more than
//    that. The predicate below therefore answers "does one more primitive fit",
//    and the call sites stop drawing rather than overrun.
//
// 8. HOW BIG THE PRIMITIVE ARENA IS. Decision 7 clamps emitters to the arena.
//    This one asks whether the arena is the right size in the first place, and
//    for a custom track it is not: the retail table is indexed by the borrowed
//    slot, so the track is handed a budget chosen for someone else's geometry.
//
//    The retail table cannot express a bigger one. It is u8 << 10, ceiling
//    261,120 bytes, and the event track's SKY ALONE wants 310,464 -- so under
//    the table the sky is unservable at any table value, and the clamp is not a
//    safety net but the operative path. Outside the table there is no such
//    limit: this is the same argument the MEMPACK expansion already made, and
//    the same answer.
//
//    CTR_CT_PRIM_ARENA_BYTES is 1 MiB, sized from measurement rather than taste:
//      sky, worst 4-segment frame ....................  310,464
//      level geometry, EVERY quadblock at near LOD ...  496,704
//      both at once, which cannot actually happen ....  807,168
//    1,048,576 covers that impossible worst case with 30% to spare, and every
//    real frame by a wide margin. Three ceilings were checked against it rather
//    than assumed, and each is asserted in tools/test-custom-track-policy.c:
//      - GPU link tokens. Ranges are handed tokens counting DOWN from
//        0x00f00000 (platform/native_gpu_links.c), so all registered ranges
//        share a 15,728,640-byte budget. Two prim arenas at 1 MiB plus the OT
//        pair and the swapchain pair come to 4,218,928 -- 27% of it. Over-large
//        would fail LOUDLY: NativeGpuLinks_RegisterRangeChecked aborts at
//        startup rather than silently truncating.
//      - MEMPACK. Measured 5,418,356 bytes free during this race on the 8 MiB
//        arena the custom-track build already selects; the expansion costs
//        1,886,208 of it.
//      - Nothing downstream stores a primitive offset in 16 bits. PrimMem's own
//        fields are u32/void*, and the GPU tags carry 24-bit tokens, not
//        truncated pointers.
//
//    WHICH LOADS GET THE FLOOR. Rung 1 gave it only to a load the loader was
//    serving custom bytes for, on the argument that only a borrowed slot has a
//    budget chosen for someone else's geometry. The 2026-08-29 run showed that
//    argument was too narrow. The adventure hub (levelID 25) takes its arena
//    from the ADVENTURE_ARENA branch of MainInit_GetPrimMemSize -- 0x1c000 =
//    114,688 bytes, a constant, not the table -- and its worst frame in that
//    run spent 106,324 of it, leaving 8,364 free. That is 93% of a budget the
//    PS1 sized for PS1 draw pressure, and the native client draws more per
//    frame than the PS1 build did.
//
//    8,364 free is below DRAW_LEVEL_OVR1P_BUCKET_RESERVE_FULL_DYNAMIC (0x2700
//    = 9,984), the largest reserve DrawLevelOvr1P_HasBucketPrimReserve asks
//    for. What that does NOT establish is that any reserve check actually
//    failed: the reserve is tested during terrain and that figure is measured
//    after the sky, and a frame that DID fail a reserve abandons the rest of
//    level rendering and therefore records a SMALLER spend than a frame that
//    completed. The high-water instrument is structurally unable to answer the
//    question it was built for, which is why decision 8's companion in
//    platform/native_custom_tracks.c now counts refused reserves directly.
//
//    So the floor is widened on headroom, not on a proven exhaustion: every
//    1P level load (numPlyrCurrGame == 1) gets it. 2P/3P/4P and the
//    numPlyrCurrGame == 0 attract path keep their retail arenas, because
//    nothing has been measured for them.
//
//    THE FOURTH CEILING, checked for the widening and asserted with the other
//    three: MEMPACK has room for the floor on EVERY 1P load, not just the
//    custom race. Any load that runs at all under CTR_NATIVE_MEMPACK_RETAIL_
//    PRESSURE fits the retail window, 0x144e10 = 1,330,704 bytes, prim arenas
//    included. A custom-tracks build replaces those arenas inside an 8 MiB
//    pack (8,386,560 usable), so the worst conceivable 1P demand is the whole
//    retail window plus both floors, 1,330,704 + 2,097,152 = 3,427,856, with
//    4,958,704 spare. The custom race is the separate case rung 1 measured:
//    5,418,356 free against a cost of 1,886,208.
//
//    The GPU-token ceiling does not compound across loads.
//    MainFrame_RegisterGpuLinkRanges calls NativeGpuLinks_Reset() before it
//    registers, so exactly six ranges are ever live and each level load starts
//    the token space over -- widening the floor to every 1P load registers the
//    same six ranges it always did, at the same sizes rung 1 checked.
//
//    RETAIL SIZING IS STILL UNTOUCHED WITH THE GUARD OFF. MainInit_GetPrimMemSize
//    is ASM-verified and unmodified; the floor is applied after it, inside
//    #ifdef CTR_CUSTOM_TRACKS, and CTR_CUSTOM_TRACKS is what selects the 8 MiB
//    pack in the first place. A build without the guard allocates exactly the
//    bytes retail allocates, for every level and every player count.
//
// 9. HOW MANY RENDERED QUADBLOCKS FIT. Decision 8 hands terrain a much larger
//    arena, so DrawLevelOvr1P now runs further into a level's geometry before
//    anything stops it. That makes a pre-existing unbounded write reachable.
//
//    DrawLevelOvr1P_AppendRenderedQuadBlock stores a pointer at the scratch
//    cursor and advances it, with no end test. The cursor is seeded to
//    sdata_static.quadBlocksRendered, which is struct QuadBlock *[0x100], and
//    the next member of that struct is the GamepadSystem. Retail bounded this
//    by arithmetic that no longer holds: the arena ran out first. Enlarge the
//    arena and the 257th rendered quadblock in a bucket writes over the pad
//    state instead.
//
//    The bound is the END OF THE ARRAY, not the 0x40 per-player stride. In 1P
//    the base is &quadBlocksRendered[0] and retail lets that one list use all
//    0x100 slots; clamping at 0x40 would refuse work retail does on every
//    frame of every level. Split-screen bases at 0x40/0x80/0xC0 can still run
//    into the next player's region exactly as retail does -- that is retail's
//    own layout and this decision does not change it. What it stops is the
//    write PAST the array, which is memory corruption in any build.
//
//    Two call sites, one predicate. The append asks for two slots, because the
//    entry it is about to write must still leave room for the NULL terminator
//    DrawLevelOvr1P_TerminateRenderedListCursor writes after the last entry;
//    the terminator asks for one. Refusing at 0xFF entries rather than 0x100
//    is the cost of keeping the terminator in bounds, and a bucket that
//    renders 255 quadblocks has already lost the frame to the reserve checks.
//
// WHY THE LOADER-READY TERM IS LOAD-BEARING. ShouldRedirectCup requires
// contentVerified. A track whose hash did not match, or whose file is missing,
// must not merely fall back to the retail bytes for the LEV -- it must also
// leave the Purple Gem Cup as its vanilla four legs, because a 7-lap single
// race on the RETAIL contents of the mapped slot is exactly the "silent wrong
// content" outcome the hash check exists to prevent. Refusing the load and
// keeping the cup is the only coherent pair of answers.
//
// RUNG-2 SEAM. Every field of CustomTrackFeatureConfig is filled from
// config.ini today (platform/native_custom_tracks.c, CustomTrack_Load). A later
// rung fills the same struct from slot_data instead; nothing in this header or
// in any engine call site changes when it does. That is the whole reason the
// config is a struct passed to pure functions rather than a set of globals read
// at each fork.
//
// Compiled ONLY when CTR_CUSTOM_TRACKS is defined, like the rest of the loader.

#ifdef CTR_CUSTOM_TRACKS

// Arcade tracks are levelID 0..17 (NITRO_COURT == 18 is the first battle
// arena). The loader refuses anything outside that range for two independent
// reasons beyond the BIGFILE grouping: data.ArcadeDifficulty is
// struct Difficulty[18] and is indexed by gGT->levelID with no range check
// (game/BOTS.c, BOTS_Adv_AdjustDifficulty), and data.metaDataLEV[0x41] must
// have a real entry for the slot or the results and HUD paths read garbage.
// A custom track therefore always takes over an existing arcade slot.
#define CTR_CT_MAX_LEVELS  18
#define CTR_CT_GROUP_SIZE  8

// The primitive arena floor, in bytes, for a served custom track and for every
// 1P level load. See decision 8 for the measurements behind it and the four
// ceilings it was checked against.
#define CTR_CT_PRIM_ARENA_BYTES 0x100000uL

// The player count MainInit_GetPrimMemSize treats as a single-player level
// load, and the only one decision 8 widens the floor to.
#define CTR_CT_PRIM_ARENA_ONE_PLAYER 1

// How many entries sdata_static.quadBlocksRendered holds. Decision 9 bounds
// the append against the end of that array; tools/test-custom-track-load.c
// asserts this against the engine's own declaration so the two cannot drift.
#define CTR_CT_RENDERED_QUADBLOCK_SLOTS 0x100uL

// How many slots an append has to see free: the entry itself, plus the one the
// NULL terminator will need after it.
#define CTR_CT_RENDERED_APPEND_SLOTS 2uL

// Which half of a mode-pair a BIGFILE subfile slot is.
enum CustomTrackSubfileRole
{
	CTR_CT_ROLE_NONE = 0, // not part of a mapped track's group
	CTR_CT_ROLE_VRM = 1,  // even slot: the track's VRAM/texture payload
	CTR_CT_ROLE_LEV = 2   // odd slot: the track's level payload
};

// Why a track's source file was accepted or refused.
enum CustomTrackVerdict
{
	CTR_CT_VERDICT_OK = 0,             // hashed and matched the expected digest
	CTR_CT_VERDICT_NO_PATH = 1,        // feature config named no file for this role
	CTR_CT_VERDICT_FILE_MISSING = 2,   // named a file that does not exist or is empty
	CTR_CT_VERDICT_NO_EXPECTED = 3,    // named a file but no expected digest for it
	CTR_CT_VERDICT_BAD_EXPECTED = 4,   // expected digest is not 64 hex digits
	CTR_CT_VERDICT_READ_FAILED = 5,    // file existed but could not be read whole
	CTR_CT_VERDICT_HASH_MISMATCH = 6   // read fine, contents are not what was promised
};

// The spike's whole runtime configuration, in one struct. Filled from
// config.ini today and from slot_data in a later rung.
struct CustomTrackFeatureConfig
{
	// --- loader ---
	int mappedLevelID; // arcade slot the custom track takes over, or -1 for none
	int contentVerified; // 1 once BOTH source files hashed OK

	// --- Purple-destination-as-race seam ---
	int raceEnabled;  // runtime flag: is the event destination active at all
	int raceCupID;    // cup whose destination is replaced (4 == Purple Gem Cup)
	int raceLaps;     // lap count for the single race (7, per the ruling)
	int raceBoxes;    // 1 = AP boxes allowed on the event race (the ruled default)
};

// How many karts the engine will put on the grid for a redirected cup's race.
// MainInit_Drivers gives the Purple Gem Cup (cupID 4) numPlyrCurrGame + 4 rather
// than a full field, and LOAD_Assets forces that roster to the four bosses; any
// other adventure cup gets the usual eight. The number matters because a track's
// measured spawn count has to cover it -- a grid with more karts than the track
// has DriverSpawn slots has nowhere to put the surplus.
static int CustomTrackPolicy_RequiredSpawns(int cupID)
{
	return (cupID == 4) ? 5 : 8;
}

// The facts about the load currently in flight that decide whether a subfile
// read is the event race's. Gathered in engine (game/LOAD/LOAD_File.c) from
// gGT and passed in, so this header and the loader stay engine-free and the
// harness can drive every combination without a GameTracker.
struct CustomTrackLoadContext
{
	int levelID;            // gGT->levelID: the level being loaded
	int adventureCupActive; // (gGT->gameMode1 & ADVENTURE_CUP) != 0
	int cupID;              // gGT->cup.cupID -- MEANINGLESS without the flag above
};

// What the AP-box layer should do about the level being loaded.
enum CustomTrackBoxVerdict
{
	// Not the event race: ap_boxes.c's existing cup-leg policy stands.
	CTR_CT_BOX_UNCHANGED = 0,

	// The event race, boxes on. ap_boxes.c treats it as a non-cup race rather
	// than a gem-cup leg, so its boxes are not gated on the physical retail pad
	// that happens to host the mapped slot.
	//
	// WHAT THIS DOES NOT YET MEAN. Box PLACEMENT still comes from the host
	// slot's retail identity (AP_BoxMap_ApTrack resolves the mapped levelID to
	// the retail track's AP-track id), so until the apworld descriptor supplies
	// the custom track's own placements, allowing boxes here spawns the RETAIL
	// track's boxes at RETAIL coordinates on custom geometry. That is why the
	// verdict is configurable: this gate is deliberate, the data behind it is
	// not there yet.
	CTR_CT_BOX_ALLOW = 1,

	// The event race, boxes off. Stands the set down entirely -- nothing spawns,
	// so nothing can collide and no check can dispatch.
	CTR_CT_BOX_DENY = 2
};

// A subfile index's role within a mapped track's group, or CTR_CT_ROLE_NONE if
// the index does not belong to the mapped track. This is the pair auto-expand:
// slot parity alone decides which of the two source files answers, so one .lev
// and one .vrm cover all four mode-pairs.
static int CustomTrackPolicy_SubfileRole(int subfileIndex, int mappedLevelID)
{
	int base;
	int slot;

	if (subfileIndex < 0)
		return CTR_CT_ROLE_NONE;

	if (mappedLevelID < 0 || mappedLevelID >= CTR_CT_MAX_LEVELS)
		return CTR_CT_ROLE_NONE;

	base = mappedLevelID * CTR_CT_GROUP_SIZE;
	slot = subfileIndex - base;

	if (slot < 0 || slot >= CTR_CT_GROUP_SIZE)
		return CTR_CT_ROLE_NONE;

	return (slot & 1) ? CTR_CT_ROLE_LEV : CTR_CT_ROLE_VRM;
}

// Is a levelID one the loader is allowed to take over? See the range note above
// CTR_CT_MAX_LEVELS: this is not a stylistic bound, it is the point past which
// data.ArcadeDifficulty and data.metaDataLEV stop having an entry.
static int CustomTrackPolicy_LevelIDIsMappable(int levelID)
{
	return (levelID >= 0 && levelID < CTR_CT_MAX_LEVELS) ? 1 : 0;
}

// Does entering cup `cupID` become a single race on the custom track?
//
// Every term is independently load-bearing:
//   raceEnabled     -- the runtime flag; off means vanilla, in every build.
//   contentVerified -- see "WHY THE LOADER-READY TERM IS LOAD-BEARING" above.
//   mappedLevelID   -- a redirect with no mapped slot has nowhere to send the
//                      player; refusing here is what keeps the two engine forks
//                      from disagreeing when config is half-filled.
//   isAdventureCup  -- gGT->cup.cupID is reused by BOTH cup families: adventure
//                      gem cups 0..4 and arcade/VS cups 0..3 (data.ArcadeCups is
//                      [4]). Only the adventure family has a gem to award, and
//                      raceCupID is configurable, so without this term a config
//                      naming cup 2 would also cut Arcade Cup 2 down to one
//                      race. The caller passes (gGT->gameMode2 & CUP_ANY_KIND)
//                      == 0.
//   cupID match     -- only the configured cup's destination is replaced.
static int CustomTrackPolicy_ShouldRedirectCup(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (cfg == NULL)
		return 0;

	if (!cfg->raceEnabled)
		return 0;

	if (!cfg->contentVerified)
		return 0;

	if (!isAdventureCup)
		return 0;

	if (!CustomTrackPolicy_LevelIDIsMappable(cfg->mappedLevelID))
		return 0;

	return (cupID == cfg->raceCupID) ? 1 : 0;
}

// The levelID a redirected cup entry loads. Callers must have asked
// ShouldRedirectCup first; this returns -1 rather than a plausible-looking
// levelID when they did not, so a missed guard fails loudly at the load rather
// than quietly racing slot 0.
static int CustomTrackPolicy_RaceLevelID(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (!CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return -1;

	return cfg->mappedLevelID;
}

// The lap count a redirected cup entry races. Returns 0 when the redirect is
// not active, so the caller writing gGT->numLaps can treat 0 as "do not write".
// gGT->numLaps is a char, and the retail lap ladder (D230.lapRowVal) tops out
// at 7, so anything outside 1..7 is refused rather than truncated into the
// field.
static int CustomTrackPolicy_RaceLaps(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (!CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return 0;

	if (cfg->raceLaps < 1 || cfg->raceLaps > 7)
		return 0;

	return cfg->raceLaps;
}

// Has the cup finished, given the leg index AFTER UI_CupStandings' increment?
//
// Vanilla answers "trackIndex >= 4": four legs, then the gem. A redirected cup
// ran exactly one race, so it is finished at trackIndex 1. Expressing both
// answers in one function is what guarantees the results screen cannot decide
// to load leg 1 of a cup the warp pad turned into a single race -- the fork at
// game/UI/UI_CupStandings.c and the fork at game/232/AH_WarpPad.c read the same
// config through the same predicate.
static int CustomTrackPolicy_CupIsComplete(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup, int trackIndexAfterIncrement)
{
	if (CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return (trackIndexAfterIncrement >= 1) ? 1 : 0;

	return (trackIndexAfterIncrement >= 4) ? 1 : 0;
}

// How many legs the HUD should say this cup has: 1 for a redirected cup, 4
// otherwise. Same predicate as CupIsComplete, so the counter can never disagree
// with the leg the game will actually load next.
static int CustomTrackPolicy_CupLegCount(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	return CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup) ? 1 : 4;
}

// Is the load in flight the event race, so that its subfile group should be
// served from the custom track rather than the BIGFILE?
//
// This is decision 4 above. Every term is independently load-bearing:
//   adventureCupActive -- the only reliable "a gem cup is in progress" signal.
//                         Without it, cupID's staleness (it is never reset)
//                         would make every later race on the host slot serve
//                         custom bytes, which is the bug this decision exists
//                         to remove.
//   ShouldRedirectCup  -- folds in raceEnabled, contentVerified and the cupID
//                         match. Reusing it rather than restating the terms is
//                         what keeps serving and redirecting from disagreeing:
//                         the loader cannot serve a race the pad did not send
//                         the player to, and cannot refuse one it did.
//   levelID match      -- a gem cup whose legs were shuffled onto other tracks
//                         still loads those legs from the BIGFILE.
//
// isAdventureCup is passed as 1 rather than gathered: ADVENTURE_CUP is set only
// by the adventure gem-cup branch of the warp pad, and arcade cups signal
// through CUP_ANY_KIND in gameMode2 instead, so adventureCupActive already
// implies the adventure family.
static int CustomTrackPolicy_ShouldServe(const struct CustomTrackFeatureConfig *cfg, const struct CustomTrackLoadContext *ctx)
{
	if (cfg == NULL || ctx == NULL)
		return 0;

	if (!ctx->adventureCupActive)
		return 0;

	if (!CustomTrackPolicy_ShouldRedirectCup(cfg, ctx->cupID, 1))
		return 0;

	return (ctx->levelID == cfg->mappedLevelID) ? 1 : 0;
}

// Can the engine actually run the event race on a track with these measured
// capabilities? Returns 1 when it can, or 0 with *outWhy set to a short reason.
//
// Only two of the eight measured flags are load-bearing today, and both are
// hard requirements of the ruled semantics rather than preferences:
//   ai_nav -- the ruling says AI bots on, and a track with no LevNavTable has no
//             paths for them to drive. Racing it alone would silently drop half
//             the ruled behaviour.
//   spawns -- the grid needs a DriverSpawn slot per kart (see RequiredSpawns).
// The other six are recorded, logged and inert: they exist so the check rungs
// above the Gem have an honest input when they land. Refusing on a flag this
// build cannot act on would reject tracks it can perfectly well serve.
static int CustomTrackPolicy_FlagsSupportRace(int aiNav, int spawns, int cupID, const char **outWhy)
{
	if (!aiNav)
	{
		if (outWhy)
			*outWhy = "the track reports no AI nav paths, and the event race runs bots";
		return 0;
	}

	if (spawns < CustomTrackPolicy_RequiredSpawns(cupID))
	{
		if (outWhy)
			*outWhy = "the track has fewer driver spawns than this cup's grid needs";
		return 0;
	}

	return 1;
}

// What the AP-box layer should do about this load. See the verdict enum.
static int CustomTrackPolicy_BoxVerdict(const struct CustomTrackFeatureConfig *cfg, const struct CustomTrackLoadContext *ctx)
{
	if (!CustomTrackPolicy_ShouldServe(cfg, ctx))
		return CTR_CT_BOX_UNCHANGED;

	return cfg->raceBoxes ? CTR_CT_BOX_ALLOW : CTR_CT_BOX_DENY;
}

// Does the level's SpawnType1 table really carry an entry at `index`? This is
// decision 6 above: the question every ST1 consumer means to ask, in place of
// the count threshold it currently asks instead.
//
// Takes plain scalars rather than a struct SpawnType1 * so this header stays
// engine-free. `count` is the table's own count field, `index` an ST1_* value,
// and `entries` the pointer array that follows the count (ST1_GETPOINTERS at
// the call site, which is address arithmetic and dereferences nothing). The
// bounds test is evaluated BEFORE entries[index], so passing the array of a
// short table is safe: this never reads past the count the caller reported.
//
// The count half is exactly the threshold it replaces -- `index < count` for
// ST1_CAMERA_PATH (3) is `count >= 4`, and for ST1_CAMERA_EOR (2) is
// `count >= 3` -- so a call site that swaps its threshold for this predicate
// changes behaviour only on a table that is long enough AND has a hole in it,
// which no retail level has.
static int CustomTrackPolicy_St1EntryPresent(int count, int index, const void *const *entries)
{
	if (entries == NULL || index < 0 || index >= count)
		return 0;

	return (entries[index] != NULL) ? 1 : 0;
}

// Is there room for one more primitive of `primSize` bytes before the frame's
// primitive arena runs out? This is decision 7 above.
//
// `cursor` is PrimMem::cursor and `guard` is PrimMem::guardEnd, passed as void *
// so this header stays engine-free. The sense matches the engine's own house
// idiom -- `(char *)cursor + primSize >= (char *)guardEnd` means "no room" -- so
// a call site reads the same way as the fifteen emitters that already bound
// themselves this way.
//
// Two edge cases are answers, not defensiveness. A cursor already at or past
// the guard returns 0 rather than computing a negative span, which matters
// because an emitter earlier in the frame can leave it there and this one must
// then refuse rather than wrap. And the comparison is strict (`>`), so a
// primitive that would land exactly on the guard is refused, preserving the
// 0x100 bytes retail reserves past it for the CD reader's sector-rounded tail.
static int CustomTrackPolicy_PrimFits(const void *cursor, unsigned long primSize, const void *guard)
{
	const char *c = (const char *)cursor;
	const char *g = (const char *)guard;

	if ((c == NULL) || (g == NULL) || (c >= g))
		return 0;

	return ((unsigned long)(g - c) > primSize) ? 1 : 0;
}

// The frame's primitive arena size in bytes: what MainInit_GetPrimMemSize
// computed, raised to CTR_CT_PRIM_ARENA_BYTES when this load qualifies for the
// floor. Decision 8 above.
//
// `retailBytes` is the retail answer for this load -- a table lookup
// (data.primMem_SizePerLEV_1P and its 2P/4P siblings) or one of the four
// constants MainInit_GetPrimMemSize returns for the non-arcade cases.
//
// Two independent reasons to raise it, either one sufficient:
//   servingCustomTrack -- the slot's budget was chosen for someone else's
//     geometry, and the event track's sky alone exceeds what the table can
//     express.
//   numPlyrCurrGame == 1 -- measured headroom. The hub's worst frame sat at
//     93% of a constant the PS1 chose, and a frame that crosses it loses the
//     rest of level rendering silently.
// Everything else keeps its retail bytes exactly: split-screen, the attract
// path, and every load in a build without CTR_CUSTOM_TRACKS.
//
// The expansion is a floor, not a replacement: a load whose retail budget is
// already larger than CTR_CT_PRIM_ARENA_BYTES keeps it, so no load can ever be
// given LESS room than retail gave it.
static unsigned long CustomTrackPolicy_PrimArenaBytes(int servingCustomTrack, int numPlyrCurrGame, unsigned long retailBytes)
{
	if (!servingCustomTrack && (numPlyrCurrGame != CTR_CT_PRIM_ARENA_ONE_PLAYER))
		return retailBytes;

	return (retailBytes > CTR_CT_PRIM_ARENA_BYTES) ? retailBytes : CTR_CT_PRIM_ARENA_BYTES;
}

// Is there room for `slots` more entries of `slotBytes` each before the
// rendered-quadblock list runs off the end of its array? Decision 9 above.
//
// `cursor` is the scratch cursor DrawLevelOvr1P_AppendRenderedQuadBlock is
// about to write through and `listEnd` one past the last entry of
// sdata_static.quadBlocksRendered, both passed as void * so this header stays
// engine-free.
//
// Same shape and same edge behaviour as CustomTrackPolicy_PrimFits, for the
// same reason: a cursor already at or past the end answers "no" rather than
// computing a negative span, because a previous frame's bucket can leave it
// there and this one must refuse rather than wrap. The comparison is >= rather
// than > because `slots` already counts every entry the caller intends to
// write, terminator included -- landing exactly on the end is the last legal
// write, not one past it.
static int CustomTrackPolicy_RenderedSlotsFit(const void *cursor, const void *listEnd, unsigned long slotBytes, unsigned long slots)
{
	const char *c = (const char *)cursor;
	const char *e = (const char *)listEnd;

	if ((c == NULL) || (e == NULL) || (c >= e))
		return 0;

	return ((unsigned long)(e - c) >= (slotBytes * slots)) ? 1 : 0;
}

#endif // CTR_CUSTOM_TRACKS

#endif // NATIVE_CUSTOM_TRACKS_POLICY_H
