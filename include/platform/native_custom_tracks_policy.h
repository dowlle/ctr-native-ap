#ifndef NATIVE_CUSTOM_TRACKS_POLICY_H
#define NATIVE_CUSTOM_TRACKS_POLICY_H

// Custom-track loader DECISIONS, ruled for the Baby T Park event spike. Each
// decision's own heading carries its rung, so this line cannot go stale.
// Deliberately freestanding, exactly like ap_cup_box_policy.h: the gather and
// the I/O live in engine (platform/native_custom_tracks.c), the decisions live
// here so tools/test-custom-track-policy.c can pin the whole truth table out of
// engine, with no disc, no display, no config file and no seed.
//
// ELEVEN DECISIONS LIVE HERE.
//
// 1. PAIR AUTO-EXPAND (rung 1). A retail arcade track occupies a contiguous
//    group of 8 BIGFILE subfiles at [levelID*8, levelID*8 + 8) because
//    BI_ARCADETRACKS is 0. The group is four (vrm, lev) mode-pairs: even slots
//    0/2/4/6 are VRMs, odd slots 1/3/5/7 are the per-mode LEVs (1P, 2P, 4P,
//    relic). A community custom track ships exactly ONE .lev and ONE .vrm, so
//    the loader expands that single pair across all four mode slots in memory
//    rather than making the packager write eight files. A 1P race reads only
//    slots 0 and 1, so the expansion costs nothing on the path the spike
//    actually exercises; slots 2..7 exist so a 2P/4P/relic entry serves
//    plausible bytes instead of the retail track's, which would be a silent
//    content swap mid-group.
//
// 2. CONTENT VERDICT (rung 1). Serving a track subfile is serving unverified
//    bytes into the engine's load path. Every source file is hashed and
//    compared against the digest the feature config names BEFORE the first byte
//    is served, and any verdict other than OK refuses the whole track. The
//    verdict enum is ordered by how loudly it should read in a log, not by
//    severity.
//
// 3. PURPLE-DESTINATION REDIRECT (rung 1). Ruled 2026-08-28: when the spike
//    feature is enabled, the seed's Purple Gem Cup destination becomes a full
//    race-track destination running the custom track -- a SINGLE race, 7 laps,
//    AI on, and winning it awards the Purple Gem through the cup's own award
//    path. The redirect is expressed here as two pure answers -- "does this cup
//    entry become a single race" and "is this leg the cup's last" -- because
//    those are the two forks the engine takes, in two different files, and they
//    must not be able to disagree. They are computed from the same inputs, so a
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
// 6. IS AN ST1 TABLE ENTRY REALLY THERE (rung 2d). Every level carries a
//    SpawnType1 pointer table (struct Level::ptrSpawnType1) whose entries are
//    indexed by the ST1_* enum -- minimap, object spawns, end-of-race cameras,
//    the intro camera path, the two ghosts, credits. Retail encodes "this level
//    has no X" by making the table SHORT, so every engine consumer guards with
//    a count threshold and then dereferences the entry unconditionally.
//    Measured across the retail NTSC-U BIGFILE, all 18 arcade tracks have count
//    == 4 with a non-NULL entry in every one of those four slots, and all 7
//    battle arenas have count == 0 -- so on retail content a count threshold
//    and "the entry is really there" are the same question, and the engine is
//    right.
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
// 7. IS THERE ROOM FOR ONE MORE PRIMITIVE (rung 2d). The engine gives each
//    frame a fixed primitive arena whose size is looked up BY LEVEL ID
//    (data.primMem_SizePerLEV_1P[levelID] << 10, game/MAIN/MainInit.c). A
//    custom track borrows an arcade slot, so it inherits the budget retail
//    sized for THAT slot's geometry -- a number chosen for entirely different
//    content.
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
// 8. HOW BIG THE PRIMITIVE ARENA IS (rung 3a). Decision 7 clamps emitters to
//    the arena. This one asks whether the arena is the right size in the first
//    place, and for a custom track it is not: the retail table is indexed by
//    the borrowed slot, so the track is handed a budget chosen for someone
//    else's geometry.
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
//    WHICH LOADS GET THE FLOOR. The first version of this decision gave it only
//    to a load the loader was serving custom bytes for, on the argument that only
//    a borrowed slot has a budget chosen for someone else's geometry. The
//    2026-08-29 run showed that argument was too narrow. The adventure hub
//    (levelID 25) takes its arena from the ADVENTURE_ARENA branch of
//    MainInit_GetPrimMemSize -- 0x1c000 = 114,688 bytes, a constant, not the
//    table -- and its worst frame in that run spent 106,324 of it, leaving 8,364
//    free. That is 93% of a budget the PS1 sized for PS1 draw pressure, and the
//    native client draws more per frame than the PS1 build did.
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
//    4,958,704 spare. The custom race is the separate case already measured:
//    5,418,356 free against a cost of 1,886,208.
//
//    The GPU-token ceiling does not compound across loads.
//    MainFrame_RegisterGpuLinkRanges calls NativeGpuLinks_Reset() before it
//    registers, so exactly six ranges are ever live and each level load starts
//    the token space over -- widening the floor to every 1P load registers the
//    same six ranges it always did, at the sizes already checked.
//
//    RETAIL SIZING IS STILL UNTOUCHED WITH THE GUARD OFF. MainInit_GetPrimMemSize
//    is ASM-verified and unmodified; the floor is applied after it, inside
//    #ifdef CTR_CUSTOM_TRACKS, and CTR_CUSTOM_TRACKS is what selects the 8 MiB
//    pack in the first place. A build without the guard allocates exactly the
//    bytes retail allocates, for every level and every player count.
//
// 9. HOW MANY RENDERED QUADBLOCKS FIT (rung 3a). Decision 8 hands terrain a
//    much larger arena, so DrawLevelOvr1P now runs further into a level's
//    geometry before anything stops it. That makes a pre-existing unbounded
//    write reachable.
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
// 10. WHAT THE DISPLACED CUP IS CALLED (rung 3b). A displaced cup keeps the
//    retail cup's identity everywhere the engine reasons about it -- cupID,
//    the gem award path, the AdvCups colour -- because that identity is what
//    the Gem hangs off. But it no longer races the retail cup's tracks, so the
//    retail cup's NAME is now the one thing on screen that is simply false:
//    the pad, the race-start banner and the standings title all still say the
//    borrowed cup's name over a single race on someone else's track.
//
//    The name is a CLIENT presentation string, not a wire field. It comes from
//    config.ini alongside the two file paths, deliberately not from the
//    descriptor: the descriptor is the authority on what is SERVED, and no
//    presentation string can change that. A future descriptor field is the
//    proper long-term home -- a seed knows the track's name and every client
//    should agree on it -- but adding one is a schema change, and a schema
//    change to put a label on screen is not a trade worth making days before
//    the event this rung exists for.
//
//    Absence is not an error. No key, an empty value, or a name the layout
//    cannot take all mean "use the retail name", exactly as a client with no
//    config.ini races retail. The only thing that can go wrong here is a name
//    that does not fit, and it is bounded rather than clipped because the draw
//    path has no clip: DecalFont_DrawLine walks to the NUL and keeps emitting
//    glyphs, centred, past both edges of the screen.
//
//    The bound is retail's own widest adventure cup name at FONT_BIG, measured
//    with the engine's own width rule rather than counted in characters,
//    because the rule is not one width per byte: ':' and '.' are narrower and
//    the four PSX button glyphs '@' '[' '^' '*' are wider. See
//    CTR_CT_NAME_MAX_PIXELS.
//
// 11. WHO THE DISPLACED CUP RACES AGAINST (rung 3b). Measured, because the
//    answer decides what is even possible here, and the measurement
//    contradicts the obvious design.
//
//    An AI driver's model is not positional and is not indexed by character id.
//    VehBirth_NonGhost (game/Vehicle/VehBirth.c) turns data.characterIDs[i] into
//    a debug NAME via data.MetaDataCharacters[id].name_Debug and hands it to
//    VehBirth_GetModelByName, which linear-searches data.driverModelExtras (3
//    slots) and then sdata->PLYROBJECTLIST -- the ONE MPK pack the load queued.
//    A name that is not in that pack returns NULL, and the caller dereferences
//    it immediately. So a character id outside the loaded pack is not a wrong
//    model, it is a null-pointer crash on the first frame of the race.
//
//    That makes the pack, not the engine, the constraint. LOAD_DriverMPK
//    queues exactly one pack per load, and the retail BIGFILE ships no pack
//    carrying all sixteen characters:
//      BI_1PARCADEPACK + playerChar   16 packs, each the player plus the seven
//                                     opponents LOAD_Robots1P names for them,
//                                     all drawn from characters 0..7
//      BI_2PARCADEPACK + 7            the four bosses, and only those four
//    Every other family (2P robot sets, time-trial, adventure) is smaller or
//    equally closed. So "any of the sixteen, per race" is not reachable from
//    this rung: it needs asset work -- sideloading models into the three
//    driverModelExtras slots, or a purpose-built pack -- not a policy change.
//
//    What IS reachable is a different pack. The displaced race inherits the
//    boss branch of LOAD_DriverMPK only because it is still cupID 4; take the
//    ordinary 1P arcade branch instead and the pool becomes the seven
//    characters LOAD_Robots1P wrote, which the pack is guaranteed to carry
//    BECAUSE LOAD_Robots1P is what the pack was built around. Four AI slots
//    drawn from seven candidates is 35 distinct fields instead of one.
//
//    The shuffle is therefore expressed as a PERMUTATION OF WHAT LOAD_Robots1P
//    ALREADY PRODUCED, never as a selection from the roster. That is what makes
//    it safe by construction rather than by checking: LOAD_Robots1P writes
//    seven distinct ids, none of them the player's, all of them in the pack, so
//    every permutation of them is seven distinct ids, none of them the player's,
//    all of them in the pack. No duplicate can appear, the player cannot appear
//    twice on track, and no id can escape the pack, without the shuffle having
//    to know any of those three things.
//
//    HOW MANY KARTS (rung 3c). Ruled 2026-08-29: race the whole field, not four
//    of it. The size is not a constant but follows the track, because the
//    descriptor's spawn count is the only thing that knows how many grid slots
//    the packager authored:
//
//        field = clamp(descriptor spawns, CTR_CT_FIELD_MIN, CTR_CT_FIELD_MAX)
//
//    Following the track rather than flipping 5 to 8 is what keeps the REFUSAL
//    EDGE where it was. RequiredSpawns still asks for five, so a descriptor that
//    would have been served before is still served; a track reporting six grids
//    six instead of being refused for not reporting eight. Refusing a whole
//    event because a track is one grid slot short of a number the client picked
//    is the failure mode this shape exists to avoid.
//
//    Eight is the ceiling because struct Level::DriverSpawn is a FIXED,
//    INLINE array of exactly 8 (include/namespace_Level.h) with no count field
//    and no range check anywhere -- indexing is
//    level->DriverSpawn[kartSpawnOrderArray[driverID]] in VehBirth.c, and both
//    indices are bounded by construction. So the ceiling is structural, and an
//    over-large field would not crash, it would silently spawn karts on
//    whatever bytes the packager left in the unauthored slots. That is the
//    outcome the clamp exists to prevent, and it is why the spawn count is
//    believed rather than the array length.
//
//    Five is the floor because it is what the cup grids today, so a descriptor
//    that under-reports can never make the event race SMALLER than the version
//    that shipped.
//
//    Four engine facts were measured before raising the number, and three of
//    them needed nothing:
//      - the grid shuffle (AH_WarpPad.c) always permutes seven entries and is
//        keyed on neither the field size nor the cup, so it already fills eight
//        slots; the five-kart grid simply left three of them empty.
//      - BOTS_Adv_CopySpawnOrder overwrites that shuffle for cupID 4 with
//        data.kartSpawnOrder.purple_cup_1/_2 = 0x03020100 / 0x07060504, which
//        decodes to the IDENTITY mapping over all eight slots. Correct for any
//        field size; it is why the vanilla Purple grid is deterministic.
//      - the points table is int[8] = {9,6,3,1,0,0,0,0} and both readers clamp
//        to four regardless of field size, so positions 5..8 already scored
//        zero. Nothing in the gem award, the cup ranks or the AP check dispatch
//        reads a kart count at all -- winning is
//        data.cupPositionPerPlayer[0] == drivers[0]->driverID, which is
//        field-size blind.
//      - the item-set switch in VehPhysGeneral.c already has a `case 8`, so
//        item quality by position moves to the standard eight-kart curve.
//
//    Two did need changing, and both are the same shape as the pack swap above:
//    a `cupID == 4` term that assumes the boss field.
//      - the standings icon layout, which lays five in one row and collapses
//        every icon past the fifth to (0,0). The ordinary eight-kart layout
//        two rows of four is already there and already handles 0..7; the fork
//        now asks the field size rather than the cup.
//      - the audio banks (HOWL_Music.c), which exclude cupID 4 from bank 54
//        the "8 drivers" bank and instead load five INDIVIDUAL character banks,
//        one per kart. That exists because the boss field is characters 8..11,
//        which bank 54 does not cover. Once the roster is base characters the
//        exclusion is backwards, and at eight karts it is a hole: drivers 5..7
//        would have no bank loaded at all.
//
//    The champion-pole term in AH_WarpPad.c was measured and needs nothing: its
//    loop is already 1..8 and its ordered branch produces the identity mapping,
//    which is correct at any field size.
//
//    WHAT THE SHUFFLE MEANS AT A FULL FIELD. With seven candidates in seven
//    slots the permutation no longer selects who races -- everyone races. It
//    still decides which character sits in which grid slot, because
//    CopySpawnOrder maps driver i to slot i, so it remains a live grid-order
//    roll rather than dead code. It is kept for the smaller fields an adaptive
//    size can still produce, where it does choose the racers.
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

// --- decision 10: what the displaced cup is called -------------------------

// Storage for the configured name, terminator included. This is a BUFFER bound,
// not the layout bound: a name is accepted on its rendered width, and this only
// says how much room the config parse keeps one in.
#define CTR_CT_NAME_MAX 64

// --- decision 11: what a recording says it was recorded on -----------------
//
// A package owns a permanent 16-byte UUID and a navigation compatibility
// revision, and a recorded AI line is only replayed on the identity it was
// recorded against (ap/ap_navrec_format.h, AP_NavRecFormat_IdentityMatches).
//
// The identity is author-controlled and coordinator-minted. It is deliberately
// NOT derived from the track name, the host slot, or either content digest: a
// presentation-only update -- a retexture, a name change, a repack that leaves
// the racing line alone -- must keep every recording made against the package
// valid, and any derived identity would break them all. The revision is what
// moves, and only when the navigable geometry moves.
//
// Like the display name (decision 10), this reaches the client from config.ini
// rather than from slot_data, and for the same reason: it changes nothing about
// what is SERVED. That also makes it the same kind of provisional home. A
// descriptor field is the proper long-term home for both, so that every client
// on a seed agrees without each packager having to hand-edit an ini.
#define CTR_CT_NAV_UUID_BYTES 16

// Canonical 8-4-4-4-12 text form, terminator included: 32 hex digits and four
// hyphens. The parse accepts this shape and nothing else.
#define CTR_CT_NAV_UUID_TEXT_LEN 36

static int CustomTrackPolicy_HexDigit(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	return -1;
}

// Parse the canonical hyphenated UUID text into 16 bytes. Returns 1 only for a
// completely well-formed value; every malformed shape returns 0 and leaves out
// untouched, which the caller turns into "no NAV3 identity" rather than into a
// refusal of the track. A misconfigured identity must never cost the player the
// race -- it costs the recordings, which is the recoverable half.
static int CustomTrackPolicy_ParseNavUuid(const char *text, unsigned char out[CTR_CT_NAV_UUID_BYTES])
{
	unsigned char bytes[CTR_CT_NAV_UUID_BYTES];
	int           i;
	int           n = 0;

	if (text == NULL || out == NULL)
		return 0;

	for (i = 0; i < CTR_CT_NAV_UUID_TEXT_LEN; i++)
	{
		if (text[i] == '\0')
			return 0; // shorter than the canonical form
	}
	if (text[CTR_CT_NAV_UUID_TEXT_LEN] != '\0')
		return 0; // longer than the canonical form

	for (i = 0; i < CTR_CT_NAV_UUID_TEXT_LEN; i++)
	{
		unsigned char c = (unsigned char)text[i];

		if (i == 8 || i == 13 || i == 18 || i == 23)
		{
			if (c != '-')
				return 0;
			continue;
		}

		if (c == '-')
			return 0; // a hyphen anywhere else is not this shape
		{
			int hi, lo;

			hi = CustomTrackPolicy_HexDigit(c);
			if (hi < 0)
				return 0;
			i++;
			lo = CustomTrackPolicy_HexDigit((unsigned char)text[i]);
			if (lo < 0)
				return 0;
			if (n >= CTR_CT_NAV_UUID_BYTES)
				return 0;
			bytes[n++] = (unsigned char)((hi << 4) | lo);
		}
	}

	if (n != CTR_CT_NAV_UUID_BYTES)
		return 0;

	for (i = 0; i < CTR_CT_NAV_UUID_BYTES; i++)
		out[i] = bytes[i];
	return 1;
}

// A revision of 0 is reserved for "no identity", so a configured revision must
// be positive. Anything the decimal parse cannot account for in full -- an empty
// value, a sign, a stray character, an overflow -- is refused, and the caller
// falls back to the default revision rather than guessing at intent.
#define CTR_CT_NAV_REV_MAX 0x7FFFFFFFuL

static int CustomTrackPolicy_ParseNavRevision(const char *text, unsigned int *out)
{
	unsigned long v = 0;
	int           digits = 0;

	if (text == NULL || out == NULL || text[0] == '\0')
		return 0;

	for (; *text != '\0'; text++)
	{
		unsigned long digit;
		if (*text < '0' || *text > '9')
			return 0;
		digit = (unsigned long)(*text - '0');
		// Check before multiplying. On the shipped 32-bit Windows target an
		// unsigned long wraps before a post-update comparison can see it.
		if (v > (CTR_CT_NAV_REV_MAX - digit) / 10uL)
			return 0;
		v = (v * 10uL) + digit;
		digits++;
	}

	if (digits == 0 || v == 0uL)
		return 0;

	*out = (unsigned int)v;
	return 1;
}

// FONT_BIG's three width constants, from data.font_charPixWidth[FONT_BIG],
// data.font_puncPixWidth[FONT_BIG] and data.font_buttonPixWidth[FONT_BIG]
// (game/zGlobal_DATA.c). All three cup-name draw sites pass FONT_BIG, so it is
// the only font this bound has to model.
#define CTR_CT_FONT_BIG_CHAR_PX   17
#define CTR_CT_FONT_BIG_PUNC_PX   11
#define CTR_CT_FONT_BIG_BUTTON_PX 16

// The widest retail ADVENTURE cup name at FONT_BIG, in pixels. "YELLOW GEM CUP"
// and "PURPLE GEM CUP" are both 14 characters and every glyph in them is
// CTR_CT_FONT_BIG_CHAR_PX wide, so 14 * 17 = 238.
//
// Retail sets the ceiling because all three draw sites are retail layouts
// composed around these exact strings, and none of them clips: DecalFont_DrawLine
// walks to the NUL and keeps emitting centred glyphs past both screen edges.
#define CTR_CT_NAME_MAX_PIXELS 238

// The printable range a configured name may use. Everything outside it is
// refused rather than rendered: the bytes below 0x20 are the font's own control
// codes (DecalFont_GetLineWidthStrlen skips widths for c <= 2 and treats the
// rest as glyphs), and bytes above 0x7e are not ASCII, so neither has a width
// this bound could honestly compute.
#define CTR_CT_NAME_FIRST_PRINTABLE 0x20
#define CTR_CT_NAME_LAST_PRINTABLE  0x7e

// --- decision 11: who the displaced cup races against ----------------------

// The slots LOAD_Robots1P fills, and therefore the slots the shuffle permutes:
// data.characterIDs[1 .. CTR_CT_ROBOT_SLOTS], the seven opponents the 1P arcade
// pack was built around. Slot 0 is the player and is never touched.
#define CTR_CT_ROBOT_FIRST_SLOT 1
#define CTR_CT_ROBOT_SLOTS      7

// How many karts the event race grids, as a range rather than a constant.
//
// The ceiling is structural: struct Level::DriverSpawn is a fixed inline array
// of exactly 8 with no count and no range check, and data.characterIDs is
// s16[8], so eight is what the engine can seat. The floor is what the cup
// gridded before this rung, so an under-reporting descriptor can never make the
// event race smaller than the version that shipped.
#define CTR_CT_FIELD_MIN 5
#define CTR_CT_FIELD_MAX 8

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

	// --- decision 10: the displaced cup's display name ---
	//
	// What to call the cup on screen while it is displaced, or "" for "use the
	// retail name". Unlike every field above it this comes from config.ini, not
	// from the seed, and it therefore does NOT belong to the armed state: it is
	// read once at startup and outlives any number of descriptors, exactly like
	// the two file paths. CustomTrack_ResetArmedState leaves it alone for that
	// reason. A stale name can never reach the screen anyway, because
	// CustomTrackPolicy_CupDisplayName answers only for a cup the redirect
	// predicate says is displaced right now.
	char raceName[CTR_CT_NAME_MAX];

	// How many karts the event race grids, player included: the descriptor's
	// measured spawn count clamped into CTR_CT_FIELD_MIN..CTR_CT_FIELD_MAX. It
	// is derived once, when the descriptor is applied, so every consumer -- the
	// driver count, the standings layout, the harnesses -- reads one number
	// rather than re-clamping the raw spawn count and risking a disagreement.
	int raceFieldSize;

	// --- decision 11: the package's recording identity ---
	//
	// Read from config.ini exactly like raceName, and non-armed for the same
	// reason: it describes the PACKAGE, not the seed's decision to serve it, so
	// it is read once at startup and outlives any number of descriptors.
	// CustomTrack_ResetArmedState leaves it alone.
	//
	// navIdentityValid is 0 when no UUID was configured or the configured one
	// was malformed. Both mean the same thing to every reader: this build has no
	// NAV3 identity to stamp or match, so recordings fall back to the legacy
	// retail interpretation. Neither ever stops the track being served.
	int           navIdentityValid;
	unsigned char navTrackUuid[CTR_CT_NAV_UUID_BYTES];
	unsigned int  navRevision;
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

// Which RETAIL trophy-track identity may receive podium-rung checks for this
// load, or -1 when the load is serving custom bytes.
//
// A custom track borrows an arcade levelID only as a byte-serving vehicle. That
// physical slot is not the custom destination's AP identity: reporting its
// position ladder would, for example, award Roo's Tubes rungs while racing
// Baby T Park on host levelID 6. Genuine custom-track rungs need their own
// datapackage identity later. Until then, a served custom load has no retail
// podium identity at all.
//
// Every non-served load returns its real levelID unchanged. The AP caller keeps
// the existing 0..15 trophy-track range and race-type gates, so this helper does
// not broaden podium eligibility for hubs, trials or other modes.
static int CustomTrackPolicy_RetailPodiumLevelID(const struct CustomTrackFeatureConfig *cfg,
                                                 const struct CustomTrackLoadContext *ctx)
{
	if (ctx == NULL)
		return -1;

	if (CustomTrackPolicy_ShouldServe(cfg, ctx))
		return -1;

	return ctx->levelID;
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

// --- decision 10 ------------------------------------------------------------

// The rendered width of `name` at FONT_BIG, in pixels, by the same rule
// DecalFont_GetLineWidthStrlen uses (game/DecalFont.c): the four PSX button
// glyphs are charge plus a button surcharge, ':' and '.' swap the character
// width for the narrower punctuation width, and everything else is one
// character width. Only the NTSC-U rule is modelled; the '~' escape exists on
// later builds only and CustomTrackPolicy_NameFits refuses every byte the rule
// above does not cover, so there is nothing else for this to get wrong.
static int CustomTrackPolicy_NameWidthPixels(const char *name)
{
	int width = 0;
	const unsigned char *p;

	if (name == NULL)
		return 0;

	for (p = (const unsigned char *)name; *p != '\0'; p++)
	{
		unsigned char c = *p;

		if (c == '@' || c == '[' || c == '^' || c == '*')
			width += CTR_CT_FONT_BIG_BUTTON_PX;

		if (c == ':' || c == '.')
			width += CTR_CT_FONT_BIG_PUNC_PX - CTR_CT_FONT_BIG_CHAR_PX;

		width += CTR_CT_FONT_BIG_CHAR_PX;
	}

	return width;
}

// May this configured name go on screen? Returns 1 when it may, or 0 with
// *outWhy set. Refusing is never an error state -- the caller falls back to the
// retail cup name, which is what a client with no name configured shows.
//
// Three ways to be refused, and the width is only one of them. A name is also
// refused for holding a byte the width rule cannot honestly measure, and for
// not fitting the config parse's own buffer, because a name truncated to fit
// would put a different string on screen than the one that was configured.
static int CustomTrackPolicy_NameFits(const char *name, const char **outWhy)
{
	const unsigned char *p;
	int len = 0;

	if (name == NULL || name[0] == '\0')
	{
		if (outWhy)
			*outWhy = "no name configured";
		return 0;
	}

	for (p = (const unsigned char *)name; *p != '\0'; p++, len++)
	{
		if (*p < CTR_CT_NAME_FIRST_PRINTABLE || *p > CTR_CT_NAME_LAST_PRINTABLE)
		{
			if (outWhy)
				*outWhy = "the name holds a byte the font has no width for";
			return 0;
		}
	}

	if (len >= CTR_CT_NAME_MAX)
	{
		if (outWhy)
			*outWhy = "the name is longer than the name buffer";
		return 0;
	}

	if (CustomTrackPolicy_NameWidthPixels(name) > CTR_CT_NAME_MAX_PIXELS)
	{
		if (outWhy)
			*outWhy = "the name is wider than the widest retail cup name the layout was built for";
		return 0;
	}

	return 1;
}

// What to call cup `cupID` on screen: the configured name while that cup is
// displaced, or NULL for "use the retail name".
//
// Gated on the SAME redirect predicate as the lap count, the leg counter and
// the completion fork, so the label cannot name a track the player is not about
// to race. A refused or absent name answers NULL, which is the retail name --
// there is no partial state where the cup is displaced but called something
// broken.
static const char *CustomTrackPolicy_CupDisplayName(const struct CustomTrackFeatureConfig *cfg, int cupID, int isAdventureCup)
{
	if (!CustomTrackPolicy_ShouldRedirectCup(cfg, cupID, isAdventureCup))
		return NULL;

	if (!CustomTrackPolicy_NameFits(cfg->raceName, NULL))
		return NULL;

	return cfg->raceName;
}

// --- decision 11 ------------------------------------------------------------

// Which of the seven opponent slots the i'th step of the shuffle swaps with.
// `rng` is one RngDeadCoed draw and `remaining` is how many slots are still
// unplaced, so the result is a slot offset in [0, remaining).
//
// The masking is AH_WarpPad.c's own grid-shuffle idiom, kept identical on
// purpose: RngDeadCoed returns a full 32-bit word whose high bits carry the
// xor constant, and the low twelve are what that shuffle has always reduced.
static int CustomTrackPolicy_ShufflePick(int rng, int remaining)
{
	if (remaining <= 1)
		return 0;

	return (rng & 0xfff) % remaining;
}

// How many karts a track reporting `spawns` grid slots should race, player
// included. Clamped rather than refused: the descriptor's spawn count is the
// only measurement of how many grid slots the packager authored, and
// struct Level::DriverSpawn has no count field for the engine to check it
// against, so a field larger than the track authored would seat karts on
// unauthored bytes rather than fail.
//
// Following the count keeps the refusal edge where it was. RequiredSpawns still
// asks for CTR_CT_FIELD_MIN, so nothing that was served before is refused now,
// and a track reporting six grids six instead of being refused for not
// reporting eight.
static int CustomTrackPolicy_FieldSizeForSpawns(int spawns)
{
	if (spawns < CTR_CT_FIELD_MIN)
		return CTR_CT_FIELD_MIN;

	if (spawns > CTR_CT_FIELD_MAX)
		return CTR_CT_FIELD_MAX;

	return spawns;
}

// How many karts this load grids, or 0 when it is not the event race.
//
// Asked by MainInit_Drivers for the driver count and by UI_CupStandings.c for
// the icon layout, through the same serve predicate as the byte serving, the
// arena sizing and the roster -- so the karts a race seats, the icons its
// standings lay out and the bytes it is served cannot disagree about which race
// this is.
static int CustomTrackPolicy_EventFieldSize(const struct CustomTrackFeatureConfig *cfg, const struct CustomTrackLoadContext *ctx)
{
	if (!CustomTrackPolicy_ShouldServe(cfg, ctx))
		return 0;

	// Defensive: a config that somehow carries an unclamped size is clamped
	// again here rather than trusted, because this number indexes a fixed
	// 8-entry array two call sites downstream.
	return CustomTrackPolicy_FieldSizeForSpawns(cfg->raceFieldSize);
}

// Does the cup-standings screen lay its icons out with the Purple cup's
// five-in-a-row layout, rather than the ordinary two-rows-of-four?
//
// The Purple layout places i < 5 across one row and collapses every icon past
// the fifth to (0, 0), so it is correct for exactly five karts and silently
// stacks anything more in the corner. The question the fork therefore has to
// ask is the FIELD SIZE, not the cup: a vanilla Purple cup always grids five
// and keeps the branch it always took, and a displaced cup clamped down to five
// keeps it too, while a displaced cup that grids six or more drops into the
// eight-kart layout that already handles 0..7.
static int CustomTrackPolicy_StandingsUsesNarrowLayout(int cupID, int numDrivers)
{
	return (cupID == 4 && numDrivers <= CTR_CT_FIELD_MIN) ? 1 : 0;
}

// How many drivers MainInit_Drivers seats for a load, given the retail count it
// would otherwise use and the event field size (0 when this is not the event
// race). Kept here rather than inline so the arithmetic that turns a field size
// into a driver count is pinned out of engine.
//
// numPlyrCurrGame is added rather than assumed to be 1 because the retail branch
// this replaces is itself written as numPlyrCurrGame + 4.
static int CustomTrackPolicy_DriverCount(int retailCount, int numPlyrCurrGame, int eventFieldSize)
{
	if (eventFieldSize <= 0)
		return retailCount;

	return numPlyrCurrGame + (eventFieldSize - 1);
}

// Fisher-Yates over ids[0..count), consuming one draw per step from draws[],
// which must hold count-1 entries. In place, no allocation, no engine types:
// the caller copies data.characterIDs in and back out, so this header stays
// compilable with nothing but the C it is written in.
//
// This is a PERMUTATION FOR ANY DRAW VALUES, including adversarial ones, and
// that is the property the safety argument rests on rather than on the draws
// being well distributed. Whatever comes out is the same seven ids that went
// in, so it is still seven distinct ids, still none of them the player's, and
// still every one of them in the pack the load queued. A bad draw can only
// make the field boring; it cannot make it invalid.
static void CustomTrackPolicy_PermuteRoster(int *ids, int count, const int *draws)
{
	int i;

	if (ids == NULL || draws == NULL || count < 2)
		return;

	for (i = 0; i < count - 1; i++)
	{
		int pick = i + CustomTrackPolicy_ShufflePick(draws[i], count - i);
		int tmp = ids[i];

		ids[i] = ids[pick];
		ids[pick] = tmp;
	}
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
