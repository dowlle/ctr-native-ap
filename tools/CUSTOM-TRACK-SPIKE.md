# Custom-track loader — Baby T Park event spike (rungs 1, 2a, 2c)

Build-time flag: `CTR_CUSTOM_TRACKS`. Off by default; with it off the build is
identical to `main` (see [Guard-off identity](#guard-off-identity) for the
evidence).

With the flag on, a `[CustomTracks]` section in `config.ini` naming two files,
and a **seed that carries a `custom_tracks` block**, the game hash-verifies one
community custom track's `.lev`/`.vrm` pair, turns the seed's named Gem Cup
destination into a single race on that track, and serves the custom bytes **for
that race only**. The arcade slot the track borrows keeps its retail race
everywhere else in the same session.

Rung 2c moved the descriptor to slot_data. `config.ini` now says only **where the
two files are**; the seed says everything else — both digests, the lap count, the
host slot, which cup is replaced, and whether AP boxes are allowed. **No block on
the wire means the feature is fully off, whatever `config.ini` holds.**

## What this rung does and does not do

Does: load a verified custom track for the event race only, guard the one
custom-track crash the engine has on the render path, expand the MEMPACK arena
so a >2 MiB track fits, wire the Purple Gem Cup destination to a single race,
make the AP-box gate on that race a deliberate answer, and correct the cup leg
counter for a one-leg cup.

Does not: read anything from slot_data (rung 2b replaces the config parse with
it), supply AP-box or CTR-letter **placement** for the custom track, handle
relic races on it, or load the track's music (`.sca`).

## Configuration

### Client side: `config.ini` — two paths, nothing else

```ini
[CustomTracks]
custom_track_vrm = tracks/baby-t-park/baby-t-park_v1.0.0.vrm
custom_track_lev = tracks/baby-t-park/baby-t-park_v1.0.0.lev
```

Paths are resolved from the working directory the game runs in. No paths, no
`[CustomTracks]` section, or no `config.ini` at all each mean "this client has no
custom track files", and the build behaves like retail.

Everything else the loader needs is deliberately **not** here. The seed is the
single authority on what gets served, so a local file cannot talk this client
into racing content the seed did not name.

### Seed side: the `custom_tracks` slot_data block (schema 8)

```jsonc
"custom_tracks": {
  "enabled": true,
  "version": 1,                     // the block's OWN shape guard
  "tracks": [{
    "id": "baby-t-park",
    "lev_sha256": "96ad…", "vrm_sha256": "2dca…",
    "laps": 7,                      // 1..7
    "host_level_id": 6,             // 0..17, the arcade slot the bytes borrow
    "replaces_cup_level_id": 104,   // Purple Gem Cup, as a LevelID
    "boxes": false,                 // AP boxes on the event race
    "flags": { "crates": true, "ctr_letters": true, "relic_crates": true,
               "ai_nav": true, "minimap": false, "ghosts": false,
               "spawns": 8, "checkpoints": 35 }
  }]
}
```

`version` is the block's own guard, independent of `schema_version`.
`schema_version` answers "may this native trust this seed at all"; `version`
answers "does it understand this block's fields". A version this build does not
know is **refused**, not read field by field — and it also raises `schema_newer`,
because "update the client" is genuinely the right advice and the loud banner
already says it. A malformed block at a *known* version refuses without the
banner: that is a generation bug, and telling the player to update misdirects
them.

Refusal is **total** and never partial. Any problem — unknown version, missing
field, malformed digest, out-of-range value, more than one entry — leaves the
feature entirely off and the named cup at its vanilla four legs. There is no
half-understood state, because a client that displaced a cup it cannot serve
would leave the Gem unreachable, and one that served without displacing would
race four retail legs the seed's logic says do not exist.

The wire list may hold more than one entry so that a second bound track is a data
change rather than a redesign. **This build has exactly one loader slot and
refuses a list of two**, rather than silently serving one of them.

### Precedence

| | |
|---|---|
| no seed / no block / unreadable block | feature fully off, whatever `config.ini` says |
| block present and readable | its values win; `config.ini` supplies only the file paths |
| block readable, but this client has no files for it | refused loudly, cup left vanilla — the player can fix this by adding the files |

### Measured flags

All eight are required on the wire: a descriptor that omits one is not
self-describing, and a silently defaulted capability is the same class of
plausible-but-wrong state the digests guard against.

Only **two** gate the race today, and both are hard requirements of the ruled
semantics rather than preferences:

- `ai_nav` — the ruling says AI bots on, and a track with no `LevNavTable` has no
  paths for them to drive.
- `spawns` — one `DriverSpawn` slot per kart on the grid. The Purple Gem Cup
  grids **five** (`MainInit_Drivers` gives cupID 4 `numPlyrCurrGame + 4`, and
  `LOAD_Assets` forces the roster to the four bosses); every other adventure cup
  grids eight.

The other six are parsed, logged and inert. They exist so the check rungs above
the Gem have an honest input when they land; refusing on a flag this build cannot
act on would reject tracks it can perfectly well serve.

### Displacement

A cup bound to a custom track **legs nothing**. The wire still carries its
complete four-track `gem_cup_legs` row — it has to, or the block stops being the
complete mapping every other consumer relies on — so that row is a don't-care
this client must *actively* not care about.

`ap/ap_verify.c` takes the same view as the apworld's logic map, in both places
it reads cup legs:

- `ap_vf_cup_capable` returns "capable" immediately for a displaced cup, instead
  of ANDing four leg capability terms belonging to tracks the player never races.
- the podium scan skips a displaced cup entirely, because that loop credits a cup
  as an *additive* route to a track's podium rungs, and a displaced cup grants no
  such route.

Emptying a cup's legs is always safe for solvability: a cup leg is only ever an
additive path to a track's podium rungs and the track's own warp pad stays an
independent path, so removing legs can never orphan a rung.

### Serving is conditional on the event race

The custom track borrows an arcade slot, but the ruling replaces one
destination, not the slot. So the override is keyed on the load in flight, not
on the subfile index: a race pad to the host track loads BIGFILE bytes in the
very same session where the event cup loads custom bytes, from the same eight
subfile indices.

Three facts decide it, read from `gGT` inside `LOAD_ReadFile_ex`: the level being
loaded, whether `ADVENTURE_CUP` is set, and `cup.cupID`. All three are committed
before the first subfile read of any level, in one call chain:

| Order | What | Where |
|---|---|---|
| 1 | `gGT->cup.cupID` | `AH_WarpPad.c`, at the pad, frames earlier |
| 2 | `gGT->gameMode1` gets `AddBitsConfig0` | `MainMain.c`, `LOAD_REQUESTED` handling |
| 3 | `gGT->levelID` | `LOAD_Level.c`, in `LOAD_LevelFile` |
| 4 | ten-stage loader armed | `LOAD_Level.c` |

Arcade-track subfiles are requested only in ten-stage **stage 6**, at least six
frames later, and `LOAD_LevelFile` has exactly one caller. There is no prefetch,
streaming, speculative or background read of an arcade-track group anywhere else
in the tree: every other queue index is a fixed `BI_*` region, and hub streaming
is hard-guarded to hub levelIDs. The main-menu track preview reads BIGFILE
sectors directly and never enters this path at all, so a custom track has no
preview video.

`ADVENTURE_CUP` is the load-bearing term. It has one setter (the gem-cup branch
of the warp pad) and is cleared through `Loading.OnBegin.RemBitsConfig0` on every
exit — cup played out, exit to map, pause-quit — which lands in `MainMain.c`
before the next load starts. `gGT->cup.cupID`, by contrast, is **never reset** and
holds its last value forever, so it must never be tested on its own. Arcade cups
cannot reach the predicate at all: they signal through `CUP_ANY_KIND` in
`gameMode2` and never set `ADVENTURE_CUP`.

Two known holes, neither reachable today: `ap/ap_retail_asset.c` reads BIGFILE
sectors directly and would bypass this gate if it ever gained a track-index
caller, and the gate does not test the `BigHeader` pointer because this build
loads exactly one archive (`ptrBigfile1` and `ptrBigfileCdPos_2` are the same
object). A second archive would make a subfile index ambiguous.

### Why the levelID must be 0..17

A custom track always takes over an existing arcade slot rather than getting a
new levelID of its own. Two engine tables force this, independently of the
BIGFILE grouping:

- `data.ArcadeDifficulty` is `struct Difficulty[18]` and `BOTS_Adv_AdjustDifficulty`
  indexes it with `gGT->levelID` and no range check.
- `data.metaDataLEV` is `[0x41]` and must have a real entry for the slot, or the
  results and HUD paths read garbage track names and hub IDs.

The loader refuses an out-of-range slot loudly rather than clamping it.

### Why both files must be hashed

Serving a track subfile is serving unverified bytes straight into the engine's
load path. Both files are hashed at startup and compared to the configured
digests before anything is served. Any failure — missing file, missing digest,
malformed digest, unreadable file, or a mismatch — leaves the loader **disarmed**,
which does two things at once:

1. track reads fall back to the retail BIGFILE bytes, and
2. the event destination stays vanilla, so the Gem Cup keeps its four retail legs.

Both halves matter. Falling back for the LEV read while still running the 7-lap
single race would put the player on the *retail* contents of the mapped slot for
a race the seed thinks is the custom track — exactly the silent wrong-content
outcome the hash check exists to prevent.

After startup, each serve re-checks that the file is still the size that was
verified, which catches a file swapped or truncated while the game is running.
Re-hashing on every subfile read was rejected: it would put a multi-MiB digest
inside the level-load path.

## Pair auto-expand

A retail arcade track occupies eight contiguous BIGFILE subfiles at
`[levelID*8, levelID*8 + 8)`. That group is four `(vrm, lev)` mode-pairs — even
slots 0/2/4/6 are VRMs, odd slots 1/3/5/7 are the per-mode LEVs for 1P, 2P, 4P
and relic.

A community custom track ships exactly one `.lev` and one `.vrm`, so the loader
expands that single pair across all four mode slots in memory. The packager
never writes eight files. A 1P race reads only slots 0 and 1, so the expansion
costs nothing on the path this spike exercises; slots 2..7 exist so a 2P/4P/relic
entry serves plausible bytes rather than the retail track's, which would be a
silent content swap in the middle of a group.

The per-mode retail LEVs genuinely differ, so 2P/4P/relic on a custom track
remain untested territory.

## The event destination

Ruled 2026-08-28: when the feature is on, the configured cup's destination stops
being four retail legs and becomes one race on the custom track — 7 laps, AI on,
and winning it awards that cup's gem through the path the cup would have used.

Two engine forks implement it, both reading the same predicate so they cannot
disagree:

- `game/232/AH_WarpPad.c`, in the gem-cup branch: writes `gGT->numLaps` and loads
  the custom track's levelID instead of `ctr_cfg_cup_leg(cupID, 0)`.
- `game/UI/UI_CupStandings.c`: answers "the cup is over" after one race instead
  of after four, which drops straight into the existing gem-award block.

`ADVENTURE_CUP` is deliberately left **set**. Clearing it to get plain-race
behaviour would route the results screen to the trophy branch in `game/222.c`,
awarding `ADV_REWARD_FIRST_TROPHY` for the levelID — the wrong progress bit, the
wrong AP location and a trophy on the podium. The gem only exists on the cup
path, so the cup path is what the spike keeps; "single race" is expressed purely
as "this cup has one leg".

Keeping the cup identity carries two consequences worth knowing:

- **The field is 5 karts, not 8.** `MainInit_Drivers` gives the Purple cup 4 AI
  rather than 7, and `LOAD_Assets.c` forces the roster to Ripper Roo, Papu Papu,
  Komodo Joe and Pinstripe. Both key on `cupID == 4`. This is inherited, not
  chosen; breaking it would mean touching four more files.
- **The HUD used to read "TRACK 1/4".** Fixed in rung 2a: both sites — the
  pre-race banner in `UI_RaceFlow.c` and the standings screen in
  `UI_CupStandings.c` — take the denominator from the same predicate as the
  completion fork, so a one-leg cup reads "TRACK 1/1". The `"4"` was a literal
  inside the `sprintf` format string in both places.

### AP boxes on the event race

The event race sets `ADVENTURE_CUP`, so `ap_boxes.c` treated it as a gem-cup leg
and gated its boxes on `ctr_cfg_warp_phys(level)` — the physical retail pad that
happens to host the mapped slot, which has nothing to do with the event. That
fall-through is now an explicit verdict in the policy header:

| Verdict | Meaning |
|---|---|
| `CTR_CT_BOX_UNCHANGED` | not the event race; the existing cup-leg policy stands |
| `CTR_CT_BOX_ALLOW` | the event race, boxes on — takes `ap_boxes.c`'s non-cup path, so no unrelated pad gates it |
| `CTR_CT_BOX_DENY` | the event race, boxes off — the set is stood down, so nothing spawns and no check can dispatch |

The wire default is ALLOW, per the ruled check set, and the seed chooses per
race. **The event seed emits `boxes: false`**, and the reason is not cosmetic:
`AP_BoxMap_ApTrack` keys on engine LevelID alone, so allowing boxes would light
the HOST slot's retail track box locations at retail coordinates. The ALLOW
branch deliberately bypasses the pad gate for the event race, so those would be
live `Roo's Tubes: Item Box N` sends available to a player who never reached
Roo's Tubes' physical pad — an out-of-logic send hazard, not just wrong scenery.

**What ALLOW does not yet buy:**
placement still resolves through the host slot's retail identity
(`AP_BoxMap_ApTrack` maps the mapped levelID to the retail track's AP-track id),
so until the apworld descriptor supplies this track's own placements, allowing
boxes spawns the *retail* track's boxes at *retail* coordinates on custom
geometry. That is why the verdict is configurable rather than hard-coded — the
gate is deliberate, the data behind it is not there yet.

### Lap-count restore

`gGT->numLaps` is written once at cup entry and nothing in adventure re-derives
it between races, so the 7-lap override has to be undone or the next
trophy/boss/relic race entered from the hub inherits it. It is restored to 3 at
all three exits, so the feature is equally correct in a clean and an AP build:

- `game/UI/UI_CupStandings.c` — the cup played out;
- `game/UI/UI_RaceFlow.c` — exit to map;
- `game/MAIN/MainFreeze.c` — pause and quit.

3 is the boot default and what `MM_MenuFlow.c` writes on every main-menu row
press, so it is the correct resting value.

## MEMPACK

The custom track's resident payload is 2,470,836 bytes. The retail-pressure
MEMPACK window is 1,330,192 bytes inside a 2 MiB backing store — the payload does
not merely overflow it, it is larger than the whole backing store. So
`CTR_CUSTOM_TRACKS` also sets `CTR_NATIVE_MEMPACK_RETAIL_PRESSURE=0`, selecting
the 8 MiB arena. The two are not separable and the CMake option turns on both.

The `platform/native_memory.c` TODO asked for an audit of the paths that assume
retail sizes before trusting the expanded arena. What was checked, and what came
back:

**Safe.**

- `struct PlatformMempackArena` and `struct Mempack` are 32-bit throughout
  (`int` sizes, `void *` pointers). At 8 MiB every field is 0.4 % of `INT_MAX`.
- `MEMPACK_AllocMem`/`ReallocMem`/`AllocHighMem`/`GetFreeBytes` are a header-less
  bump allocator; no field can overflow below 2 GiB.
- `LOAD_ReadFile_ex`'s sector arithmetic. `sectorCount = (eSize + 0x7ffU) >> 0xb`
  promotes to unsigned, so the shift is logical and the expression cannot
  overflow. `sectorSize = sectorCount << 0xb` goes negative only at
  `eSize >= 0x7FFFF801` (~2 GiB). Our payload gives `sectorCount = 1207` and
  `sectorSize = 2,471,936` — three orders of magnitude of headroom.
- `LOAD_DramFileCallback`. `ptrMapOffset` is a full `int`. The real ceiling is the
  `DRAM_SET_PATCHED` flag at bit 28 sharing that word, i.e. 256 MiB, 113× our
  payload.
- `LOAD_RunPtrMap`, the pointer-map fixup — the path most likely to break, and it
  does not. Offsets are stored as full 32-bit `int` (not `u16`), `(x >> 2) << 2`
  is an align-down that preserves any offset below 2^31, and the base-add is
  exact on `-m32`.
- Hub sub-pack carve-up, `PatchMem` scratch (already moved out of MEMPACK with a
  loud `abort()` guard), arena bound checks (`uintptr_t`, wrap-checked), and
  checkpoint region sizing (`u32`, ~0.25 % used).
- GPU 24-bit OT links: under `CTR_NATIVE` these are bridge tokens, not truncated
  pointers, and the token budget does not scale with arena size.
- No `u16`/`s16` field anywhere stores a pointer or a byte size. The remaining
  `& 0xffffff` / `& 0x80000000` hits in the tree are RGB colours, distance
  sentinels, or PS1-only `#else` branches.

**Fixed here.** The expanded arena had `SIZE == BUFFER_SIZE`, putting
`endOfAllocator` exactly at the end of the backing array. Retail computes
`ramSize - startOffset - 0x800` and every read path derived from it assumes that
slack exists: CD reads always write whole 2 KiB sectors but not every allocation
is sector-rounded (the language buffer is `0x3F04`), so a read at the top of the
pack can legally overrun by up to 2047 bytes. Under retail pressure that lands in
unused tail; without the subtraction it would write past the array into
neighbouring `.bss`. `CTR_NATIVE_MEMPACK_SIZE` now subtracts `0x800` in the
expanded branch too. This changes only the `RETAIL_PRESSURE=0` branch, so default
builds are untouched.

**Known deviations that come with the expanded arena.** Neither is fixed here;
both are recorded so they are known rather than discovered.

- `DrawLevelOvr1P_TryConvertNativeMempackPointerToPsxWord`
  (`game/226/226_00_DrawLevelOvr1P.c`) hard-codes `psxRamSize = 0x200000` and
  reconstructs a retail PSX address as `endOfMemory - 0x200000 + offset`. That is
  an exact identity **only** at the retail window's size and start offset. With
  the 8 MiB arena it returns 0 for every level pointer, and
  `ReadRetailQuadBlockByte` falls back to reading raw host-address bytes, giving
  a wrong `otIndex` for quad faces 4–15 — z-sorting artifacts on mid-detail
  geometry. It is **unfixable in principle** for a >2 MiB resident LEV: the retail
  byte pattern does not exist for data that could not have fit in PS1 RAM. Visual
  only; the value stays `s8`-bounded and clamped.
- `NativeCheckpoint_RegisterPointerSlot` (`platform/native_checkpoint.c`, compiled
  in because `CTR_INTERNAL` is unconditional) dedupes with an O(n) linear scan per
  registered pointer against a 65,536-entry cap, and the list is reset only in
  `MEMPACK_Init` — once per process, never between level loads. A much larger LEV
  registers far more pointers, so expect a slow level load and, past the cap,
  silently dropped slots that make savestates and replays restore incorrectly.
  Orthogonal to arena size but triggered by the same change.

Separately, the LEV *format* has its own ceilings a large track can hit, all
independent of MEMPACK: `QuadBlock::index` is `u16[9]` (65,536 vertices max),
`blockID` is `s16` (32,767 quadblocks), and `BspChildId` is `s16` (32,767 BSP
nodes).

## Lifecycle

`CustomTrack_Load()` runs once at startup and reads only the two paths. slot_data
arrives at connect, so the descriptor is pushed in later, from `AP_OnFrame`'s
per-frame watcher block in `ap/ap_hooks.c` — the parse has no hook of its own.
`CustomTrack_ApplySeedDescriptor` is therefore **idempotent by content**: an
unchanged descriptor costs one `memcmp` and re-hashes nothing, which is what
makes a per-frame call affordable against a multi-MiB file.

A seed that goes away, or one with no block, calls
`CustomTrack_ClearSeedDescriptor` and the whole feature switches off on the next
read. A different seed replaces the descriptor outright rather than merging into
it, and a *refused* replacement leaves nothing of the previous one armed.

Hashing happens on the frame a new descriptor arrives. For a 2.4 MB track that is
a one-off stall at connect, while the player is in the hub or the menu.

**A build with `CTR_CUSTOM_TRACKS` off still parses the block** (the parse lives
in `ap/ap_seedcfg.cpp`, under `CTR_AP`), so its verifier stays correct about
displacement, and it logs once that the seed binds a track it cannot load.

## Building

```
cmake -S . -B build-ct -DCMAKE_BUILD_TYPE=Release -DCTR_AP=ON -DCTR_CUSTOM_TRACKS=ON \
      -DCMAKE_C_FLAGS="-m32 -msse" -DCMAKE_CXX_FLAGS="-m32"
cmake --build build-ct -j2
```

`-DCTR_AP=ON` is independent — the loader works in a clean build too.

## Tests

Both harnesses follow the repo's `tools/test-*.c` pattern: no disc, no display,
no seed, exit 0 on success.

```
cc -Wall -Wextra -DCTR_CUSTOM_TRACKS -I include \
   -o /tmp/test-custom-track-policy tools/test-custom-track-policy.c && /tmp/test-custom-track-policy

cc -Wall -Wextra -m32 -DCTR_CUSTOM_TRACKS -DCTR_NATIVE -DBUILD=926 -I . -I include \
   -o /tmp/test-custom-track-load tools/test-custom-track-load.c && /tmp/test-custom-track-load
```

`test-custom-track-policy` pins the decisions and the digest primitive out of
engine: SHA-256 against the published NIST vectors, the digest comparison's
accept/reject edges, pair auto-expand, the mappable-levelID bound, each of the
redirect's five terms shown independently load-bearing, the serve context, the
box verdict, the HUD leg count, and a 7,200-configuration sweep. The sweep
asserts, for every configuration, that the warp-pad and cup-standings forks
agree, that the box verdict speaks only for loads it owns, that the leg counter
matches the redirect, and — the rung-2a invariant — that a load identical to the
event race except that no gem cup is in progress is **never** served.

`test-custom-track-load` compiles the real loader and drives it against real
files: the disarmed states, the happy path end to end, every refusal (wrong hash,
truncated file, missing file, missing folder, absent digest, malformed digest,
out-of-range levelID) each asserting on the loader's own log output, box denial,
"race off serves nothing", and an end-to-end retail-pad-stays-retail scenario
showing the same eight subfile indices answered three different ways in one
armed session.

## Guard-off identity

With `CTR_CUSTOM_TRACKS` off, the preprocessed `main.c` translation unit (6.4 MB,
the whole unity build) differs from `main` only in the `__LINE__`-derived names
of five unused `CTR_STATIC_ASSERT` typedefs, shifted by the added `#include`
blocks. Those typedefs generate no code. There are no other differences.

To reproduce, preprocess `main.c` with the guard-off AP flags in this tree and in
a worktree at `origin/main`, normalising `CTR_NATIVE_BUILD_ID` and
`CTR_NATIVE_VERSION` (they embed the git hash and would otherwise differ), then
diff. A whole-binary comparison additionally requires both trees to be clean at
the same commit, since a dirty tree makes the build ID longer and shifts every
address.

## Caveats

- The custom track keeps the RETAIL slot's identity for ghosts and saved times,
  because the slot's levelID is unchanged. Do not save ghosts or times on it.
- The track's music (`.sca`) is not loaded; the retail slot's music plays.
- A custom track has no track-preview video: the main menu reads preview sectors
  from BIGFILE directly, bypassing the loader entirely.
