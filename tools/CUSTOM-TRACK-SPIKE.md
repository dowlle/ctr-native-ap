# Custom-track loader — Baby T Park event spike (rung 1)

Build-time flag: `CTR_CUSTOM_TRACKS`. Off by default; with it off the build is
identical to `main` (see [Guard-off identity](#guard-off-identity) for the
evidence).

With the flag on and a `[CustomTracks]` section in `config.ini`, the game
hash-verifies one community custom track's `.lev`/`.vrm` pair and serves those
bytes in place of an arcade track's BIGFILE subfile group. Separately, a runtime
flag turns the configured Gem Cup destination into a single 7-lap race on that
track, awarding its gem through the cup's own award path.

## What this rung does and does not do

Does: load a verified custom track, guard the one custom-track crash the engine
has on the render path, expand the MEMPACK arena so a >2 MiB track fits, and
wire the Purple Gem Cup destination to a single race.

Does not: read anything from slot_data (rung 2 replaces the config parse with
it, see [Rung-2 seam](#rung-2-seam)), place AP boxes or CTR letters on the custom
track, handle relic races on it, or load the track's music (`.sca`).

## Configuration

All keys live in a `[CustomTracks]` section of the `config.ini` next to the
executable. Paths are resolved from the working directory the game runs in.

```ini
[CustomTracks]
; --- loader ---
custom_track_level      = 6
custom_track_vrm        = tracks/baby-t-park/baby-t-park_v1.0.0.vrm
custom_track_lev        = tracks/baby-t-park/baby-t-park_v1.0.0.lev
custom_track_vrm_sha256 = 2dcaa0fe93359c7ae00fb93842a581210e0dcc2db73f4de43508375834092e83
custom_track_lev_sha256 = 96ad9f74f51a02eafcc207cd02c97052d674c950e0f24b6440a227494a705fe8

; --- event destination ---
custom_track_race      = 1
custom_track_race_cup  = 4
custom_track_race_laps = 7
```

| Key | Meaning | Default |
|---|---|---|
| `custom_track_level` | arcade slot the track takes over, 0..17 | none (loader stays disarmed) |
| `custom_track_vrm` / `custom_track_lev` | the two source files | none |
| `custom_track_vrm_sha256` / `custom_track_lev_sha256` | 64 hex digits each, case-insensitive | none — **required**, an absent digest is a refusal, not a skip |
| `custom_track_race` | 1 turns the event destination on | 0 |
| `custom_track_race_cup` | which cup's destination is replaced | 4 (Purple Gem Cup) |
| `custom_track_race_laps` | lap count for the single race, 1..7 | 7 |

Missing keys, an empty value, no `[CustomTracks]` section, or no `config.ini` at
all each mean "no custom track", and the build then behaves like retail.

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
- **The HUD reads "TRACK 1/4".** `UI_RaceFlow.c` prints the leg counter from the
  cup machinery, which still thinks it is on leg 1 of 4. Cosmetic, and left alone
  in rung 1 rather than adding a fifth touch point.

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
redirect's five terms shown independently load-bearing, and a 7,200-configuration
sweep asserting the warp-pad fork and the cup-standings fork agree on every
reachable config.

`test-custom-track-load` compiles the real loader and drives it against real
files: the disarmed states, the happy path end to end, and every refusal (wrong
hash, truncated file, missing file, missing folder, absent digest, malformed
digest, out-of-range levelID) — each asserting on the loader's own log output,
because a silent fallback is the failure mode this feature exists to prevent.

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
- Whichever arcade track's slot is taken over is replaced **everywhere** in the
  game, not just in the event destination.
- The track's music (`.sca`) is not loaded; the retail slot's music plays.
