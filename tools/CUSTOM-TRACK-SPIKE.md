# Custom-track loader — Baby T Park event spike

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

Does: load a verified custom track for the event race only, guard the
custom-track crashes the engine has on the render path, on its two race cameras
and on its per-level primitive budget, expand the MEMPACK arena so a >2 MiB
track fits, size the frame's primitive arena from measured demand instead of the
borrowed slot's retail table on every 1P load, bound the rendered-quadblock list
that a bigger arena makes reachable, report one render summary per level load
with the three drop counters a high-water mark cannot show, wire the Purple Gem
Cup destination to a single race, make the AP-box gate on that race a deliberate
answer, correct the cup leg counter for a one-leg cup, call the displaced cup by
the track's own name everywhere the player reads it, grid as many karts as the
track reports spawn slots for instead of the boss field's five, and draw the AI
opponents from the loaded pack instead of the fixed four bosses.

Does not: read anything from slot_data (rung 2b replaces the config parse with
it), supply AP-box or CTR-letter **placement** for the custom track, handle
relic races on it, load the track's music (`.sca`), size split-screen or attract
loads (nothing has been measured for them), bound
`RenderLists_PushChild`'s 51-record drop (counted, not clamped — see
[the rendered-quadblock bound](#the-rendered-quadblock-bound)), put the event
track's name on the wire (it is a client string this rung, see
[the displaced cup's name](#the-displaced-cups-name)), race AI drawn from all
sixteen characters (no shipped asset pack allows it, see
[the field](#the-field)), grid more than the eight karts
`struct Level::DriverSpawn` can seat (see
[how many karts](#how-many-karts)), or claim the hub
black patches are fixed. The hub floor is widened on measured **headroom**; the
run that follows this rung is what says whether the hub was crossing its budget
at all.

## Configuration

### Client side: `config.ini` — two paths, a label, and an identity

```ini
[CustomTracks]
custom_track_vrm = tracks/baby-t-park/baby-t-park_v1.0.0.vrm
custom_track_lev = tracks/baby-t-park/baby-t-park_v1.0.0.lev
custom_track_name = BABY T PARK
custom_track_nav_uuid = 898a9315-693f-4ed3-b6a0-fbe50db8bc40
custom_track_nav_rev = 1
```

Paths are resolved from the working directory the game runs in. No paths, no
`[CustomTracks]` section, or no `config.ini` at all each mean "this client has no
custom track files", and the build behaves like retail.

Everything else the loader needs is deliberately **not** here. The seed is the
single authority on what gets **served**, so a local file cannot talk this client
into racing content the seed did not name.

`custom_track_name` is the first exception, and it is an exception precisely
because it changes nothing about what is served — see
[the displaced cup's name](#the-displaced-cups-name). It is optional; missing,
empty, or unusable all mean "show the retail cup name".

`custom_track_nav_uuid` and `custom_track_nav_rev` are the second, for the same
reason and with the same caveat — see
[the package's recording identity](#the-packages-recording-identity). Both are
optional. A missing or malformed UUID means this build stamps and matches no
custom-track recording identity; the track is still served either way.

Baby T Park's minted values are the ones shown above.

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
  rather than 7, keyed on `cupID == 4`. That is inherited and is left inherited;
  see [the field](#the-field) for what it would cost to change and why the
  composition was changed instead.
- **The HUD used to read "TRACK 1/4".** Fixed in rung 2a: both sites — the
  pre-race banner in `UI_RaceFlow.c` and the standings screen in
  `UI_CupStandings.c` — take the denominator from the same predicate as the
  completion fork, so a one-leg cup reads "TRACK 1/1". The `"4"` was a literal
  inside the `sprintf` format string in both places.

### The displaced cup's name

A displaced cup keeps the retail cup's identity everywhere the engine reasons
about it, because that identity is what the Gem hangs off. It no longer races
that cup's tracks, though, so the retail cup's **name** is the one thing on
screen that is simply false.

Three sites put an adventure cup's name in front of a 1P player, and they are
all of them — the whole tree was swept for reads of
`data.AdvCups[].lngIndex_CupName` and `data.advCupStringIndex`:

| Site | Function | When the player reads it |
|---|---|---|
| `game/232/AH_WarpPad.c` | `AH_WarpPad_ThTick` | standing at the gem-cup pad in the hub |
| `game/UI/UI_RaceFlow.c` | `UI_RaceStart_IntroText1P` | the race-start banner, above `TRACK 1/1` |
| `game/UI/UI_CupStandings.c` | `UI_CupStandings_InputAndDraw` | the standings title after the race |

Nothing else shows one. The adventure pause menu titles from
`metaDataLEV[hubID].name_LNG` or a fixed `LNG_*`; the results screens, the podium
and the gem ceremony are icon-based and read `data.AdvCups[].color` only; the map
and HUD draw no cup name at all. `MM_CupSelect.c` and the `ArcadeCups` branch of
`UI_CupStandings.c` are arcade-only and unreachable from 1P adventure.

All three now ask `CustomTrack_CupDisplayName`, gated on the **same redirect
predicate** as the lap count, the leg counter and the completion fork, so the
label cannot name a track the player is not about to race. The pad passes its own
cup index rather than `gGT->cup.cupID`, because standing at a pad is the moment
*before* `cupID` is written and that field is never reset.

**Where the name comes from.** `config.ini`, not the descriptor. The descriptor
is the authority on what is served, and a presentation string must not be able to
reach that decision; a schema bump to put a label on screen is also not a trade
worth making days before the event. A descriptor field is the proper long-term
home — a seed knows the track's name and every client should agree on it — and
this is recorded as decision 10 rather than left implied.

**Why it is bounded and not clipped.** `DecalFont_DrawLine` walks to the NUL and
keeps emitting centred glyphs; there is no clip anywhere in the font path. So an
over-long name does not truncate, it runs off both edges of a 512-pixel screen.
The bound is retail's own widest adventure cup name at `FONT_BIG` — `PURPLE GEM
CUP` and `YELLOW GEM CUP`, 14 glyphs at 17px = **238 pixels** — measured with the
engine's own width rule rather than counted in characters, because the rule is
not one width per byte: `:` and `.` are 11px, and the four PSX button glyphs
`@ [ ^ *` are charged 16px **plus** the character width, so eight of them are
264px in eight characters. A name is also refused for holding a byte outside
`0x20..0x7e`, which is what makes the width rule exact for everything left, and
for filling the 64-byte config buffer, because a truncated name is a different
string than the one configured.

Refusal is announced once, at parse time, and the cup keeps its retail name. A
name can never refuse a track: an unusable `custom_track_name` still arms the
loader.

### The package's recording identity

Decision 11. Recorded AI lines (`ap/ap_navrec.c`) are replayed only on the track
they were recorded against, and a custom package is identified by a permanent
**16-byte UUID** plus a **navigation compatibility revision** rather than by the
engine slot it happens to occupy.

**Why the physical slot cannot be the identity.** The custom track borrows a
retail arcade slot, so the slot says nothing about which geometry is loaded. Two
recordings can carry the same `levelId` and describe completely different
corridors: one from the retail track, one from the custom track that displaced
it in some other session. A slot-keyed identity accepts both for both, which
puts bots on a racing line for geometry that is not there.

**Why it is not derived.** The identity is author-controlled and coordinator-
minted, deliberately **not** a function of the track name, the host slot, or
either content digest. A presentation-only update — a retexture, a name change, a
repack that leaves the racing line alone — must keep every recording made against
the package valid, and any derived identity would invalidate all of them on a
change that did not move a single navigation node. The **revision** is the field
that moves, and it moves only when the navigable geometry does.

Baby T Park's minted identity is `898a9315-693f-4ed3-b6a0-fbe50db8bc40` at
revision **1**.

**Where it comes from.** `config.ini`, not the descriptor, for exactly the reason
the name is — it changes nothing about what is served, and a schema bump days
before the event is not a trade worth making. A descriptor field is the proper
long-term home, for a stronger reason than the name has: a per-client identity
means two players on one seed can disagree about whether a recording is valid,
and a packager should not have to hand-edit an ini for their recordings to
travel. This is recorded as decision 11 rather than left implied.

**How a load gets its identity.** `CustomTrack_NavIdentityForLoad` answers on the
**same serve predicate** as the bytes, the arena and the character pack
(`CustomTrack_ServingLoad`), so a recording's identity cannot disagree with the
geometry actually loaded. `game/BOTS.c` asks it once per load, before the
`BOTS_InitNavPath` loop and therefore before `AP_NavRec_AfterBotsInit` opens a
file. Both branches are unconditional, so no load inherits the previous one's
answer: a cup exit, a race pad to the host slot in an armed session, and an
ordinary retail race all clear the identity.

**An identity can never refuse a track.** A missing or malformed
`custom_track_nav_uuid` is announced once, at parse time, and leaves the build
with no custom-track recording identity to stamp or match; the track is still
served and still raced. `custom_track_nav_rev` defaults to 1, and a malformed
revision falls back to 1 rather than guessing. A revision without a UUID
identifies nothing and is ignored.

**What this does to recordings made before it.** Every pre-existing recording is
NAV2 and therefore carries the legacy retail interpretation. While the custom
track is active, all of them are rejected — including one that was in fact
recorded on the custom track's geometry, because a NAV2 file has no field that
could say so. Those have to be re-recorded under NAV3. The reverse also holds: a
NAV3 recording is rejected on a retail load of the same slot.

### The field

Ruled 2026-08-29, after measuring why the field was the four bosses.

**What the measurement found.** An AI driver's model is not positional and is not
indexed by character id. `VehBirth_NonGhost` turns `data.characterIDs[i]` into a
debug **name** through `data.MetaDataCharacters[id].name_Debug` and hands it to
`VehBirth_GetModelByName`, which linear-searches `data.driverModelExtras` (3
slots, empty on the 1P path) and then `sdata->PLYROBJECTLIST` — the **one** MPK
pack the load queued. A name that is not in that pack returns NULL, and
`INSTANCE_Birth3D(m, m->name, t)` dereferences it on the spot.

So a character id outside the loaded pack is **a null-pointer crash, not a wrong
model**, and the pack — not the engine — is the constraint.

**Why "randomly any of the 16" is not reachable from this rung.**
`LOAD_DriverMPK` queues exactly one pack, and no retail pack carries all sixteen:

| Pack family | Count | Contents |
|---|---|---|
| `BI_1PARCADEPACK + playerChar` | 16 | the player plus the seven opponents `LOAD_Robots1P` names for them, always drawn from characters 0..7 |
| `BI_2PARCADEPACK + 7` | 1 | the four bosses, and only those four |
| `BI_2PARCADEPACK + 0..6` | 7 | 2P robot sets, all ids 0..7 |

Reaching characters 8..15 as AI therefore needs **asset work, not a policy
change**: sideloading models into the three `driverModelExtras` slots (the
gem-cup branch already does this for the player, so the mechanism exists) or a
purpose-built pack. Three sideloaded slots would make a pool of ten. That is a
real option and it is recorded, not taken: whether a `BI_RACERMODELHI` model
renders correctly as an AI, with the pack's VRM rather than its own, cannot be
answered without running the game, and this rung ships without a runtime run.

**What is reachable, and what shipped.** The displaced race inherited the boss
branch only because it is still `cupID == 4`. That branch now also asks whether
this load *is* the event race — `CustomTrack_ServingLoad`, the same predicate as
the byte serving and the arena sizing — and the event race falls through to the
ordinary 1P arcade branch instead. It gets the player's own arcade pack, and with
it seven candidates instead of four fixed bosses. Four AI slots out of seven
candidates is **35 distinct fields**, redrawn on every load, instead of one.

The shuffle is expressed as a **permutation of what `LOAD_Robots1P` already
produced**, never as a selection from the roster, and that is the whole safety
argument rather than a stylistic choice. `LOAD_Robots1P` writes seven distinct
ids, none of them the player's, every one of them in the pack the same branch is
about to queue — so every permutation of them is seven distinct ids, none of them
the player's, all of them in the pack, without the shuffle having to check any of
the three. It is a permutation for *any* draw values, so a bad draw can make the
field boring but cannot make it invalid.

Draws come from `sdata->advRng` through `RngDeadCoed`, the same stream and the
same masking idiom `AH_WarpPad.c`'s grid shuffle already uses, so this consumes an
RNG the race path is known to perturb rather than introducing a new one. Nothing
in the AP check flow reads it.

### How many karts

Ruled 2026-08-29 (rung 3c): race the whole field. The size is not a constant but
follows the track,

```
field = clamp(descriptor spawns, 5, 8)
```

because the descriptor's spawn count is the only measurement of how many grid
slots the packager authored. Baby T Park reports eight, so the event race grids
eight.

**Why it follows the track instead of flipping 5 to 8.** A hard eight-spawn
requirement would move a **refusal edge**: a track one slot short would refuse
the whole feature and leave the cup vanilla, which is a total-refusal failure
mode traded for a presentation improvement. Clamping instead leaves
`CustomTrackPolicy_RequiredSpawns` at five, so nothing that was served before is
refused now, and a track reporting six grids six.

**Why eight is the ceiling.** `struct Level::DriverSpawn` is a **fixed, inline
array of exactly 8** (`include/namespace_Level.h`) with no count field and no
range check anywhere. Indexing is
`level->DriverSpawn[kartSpawnOrderArray[driverID]]` (`VehBirth.c`), and both
indices are bounded by construction, so an over-large field would not crash — it
would silently seat karts on whatever bytes the packager left in unauthored
slots. That is the outcome the clamp prevents, and it is why the descriptor's
count is believed rather than the array's length. Five is the floor because it is
what the cup gridded before, so an under-reporting descriptor can never make the
race smaller than the version that shipped.

**Four engine facts were measured before raising the number, and three needed
nothing:**

| Measured | Verdict |
|---|---|
| the grid shuffle (`AH_WarpPad.c`) | already permutes seven entries, keyed on neither field size nor cup — the five-kart grid simply left three slots empty |
| `BOTS_Adv_CopySpawnOrder(purple_cup_1/_2)` | `0x03020100` / `0x07060504` decode to the **identity** mapping over all eight slots; correct at any field size, and why the vanilla Purple grid is deterministic |
| points and the gem award | `cupPointsPerPosition` is `int[8] = {9,6,3,1,0,0,0,0}` and both readers clamp to four regardless of field size; winning is `cupPositionPerPlayer[0] == drivers[0]->driverID`, which reads no kart count at all, and neither does the AP check dispatch |
| the item-set switch (`VehPhysGeneral.c`) | already has a `case 8`, so item quality by position moves to the standard eight-kart curve |

**Two did need changing, and both are the same shape as the pack swap — a
`cupID == 4` term that assumed the boss field:**

- **the standings icon layout.** The Purple fork lays five in one row and
  collapses every icon past the fifth to (0, 0). The ordinary eight-kart layout
  (two rows of four) is already present and already handles 0..7, so the fork now
  asks the field size rather than the cup.
- **the audio banks** (`HOWL_Music.c`, two sites). Purple is excluded from bank
  54, the "8 drivers" bank, and instead loads **five individual character banks,
  one per kart** — because the boss field is characters 8..11, which bank 54 does
  not carry. Once the roster became base characters that exclusion was backwards,
  and at eight karts it is a hole: drivers 5..7 would have no bank loaded at all.
  Both sites now ask the serve predicate, so the event race takes the ordinary
  arcade audio path.

The champion-pole term in `AH_WarpPad.c` was measured and needs nothing: its loop
is already `1..8` and its ordered branch produces the identity mapping.

#### Does the wider field fit the arena

Yes, and by a bound rather than by a run. The verified live run measured this
race at a worst frame of **389,484 bytes** against the 1 MiB floor, over 3,092
frames, with all three drop counters zero.

Take the most pessimistic assumption available — that *every* byte of that frame
is per-kart cost scaling linearly with the field, which is plainly false, since
terrain, sky, HUD and weather do not care how many karts are on track:

| | bytes |
|---|---|
| measured worst frame, five karts | 389,484 |
| the same frame scaled 5 → 8 karts | 623,174 |
| `CTR_CT_PRIM_ARENA_BYTES` | 1,048,576 |
| spare | 425,402 |

That is 59% of the floor, and the spare is 42x the largest bucket reserve
(`0x2700` = 9,984). Turned around: for the wider field to overflow, the
kart-proportional share **alone** would have to exceed 1,098,486 bytes — more
than the entire measured frame including terrain and sky. Not reachable, so this
needs no live run.

For scale, the retail levelID-6 budget was 105,472 bytes and the five-kart race
already spent 3.7x that. The floor is doing all the work on this track whatever
the field size.

The two related hazards are untouched by kart count: the rendered-quadblock bound
counts *terrain* quadblocks, and the 51-record BSP stack is walked before any
primitive is written. GPU tokens are unchanged at six ranges of the same sizes.

What is worth watching instead is **MEMPACK**: three more karts means three more
model instances and their allocations, and instance-pool sizing has starved
MEMPACK before. The 8 MiB pack this build selects makes it very likely fine, and
the instrument is the `[AP POOL] free=` telemetry, not the per-load render
report.

**What the shuffle means at a full field.** With seven candidates in seven slots
the permutation no longer selects who races — everyone races. It still decides
which character sits in which grid slot, because `CopySpawnOrder` maps driver `i`
to slot `i`, so it stays a live grid-order roll rather than dead code. It is kept
for the smaller fields the clamp can still produce, where it does choose the
racers.

**One visible consequence of the pack swap.** The boss branch loaded
`BI_RACERMODELHI + characterIDs[0]` into `driverModelExtras[0]`, so the player's
own kart was a high-LOD model. The arcade branch does not: the player resolves
out of the pack like every other driver, which is what a normal single race
already does.

**Recorded, not fixed.** The two soft gaps for AI ids ≥ 8 remain reasons the
sideload option is not free — AI voice banks cover characters 0..7 only, and the
speed-champion pole placement matches only ids below 8. Neither is reachable,
since every id the shuffle can produce is already 0..7.

Also recorded, and **pre-existing**: `AH_WarpPad.c`'s champion lookup indexes
`data.metaDataLEV[levelID]` with `levelID` still the pad's own 100..104 for a
gem-cup pad, against a `struct MetaDataLEV[0x41]` — 35 to 39 entries past the
end. The `champID` it gets is therefore out-of-bounds data for every gem cup,
vanilla included. The tree already knows this array needs guarding at that call
path: `AH_WarpPad.c:412` guards it with `levelID < AH_WP_ADV_CUP` and another
site repeats the guard with a comment saying `metaDataLEV` is valid for
track/trial/arena LevelIDs but **not** cup pads. The champion site is the one
that missed the idiom.

It is **not purely a read**. `champID` feeds `if ((champID < 8) && ...)`, and if
the out-of-range bytes satisfy that, the branch rewrites all eight entries of
`sdata->kartSpawnOrderArray`. What masks it is the identity mapping recorded
above: `BOTS_Adv_CopySpawnOrder` overwrites that array for cupID 4 afterwards, so
the grid comes out right regardless. Reachability is exactly the five cup pads;
every other pad has a levelID under 25.

Untouched here because it predates this work, affects vanilla identically, is
masked downstream, and behaves the same at five karts and eight — and because it
sits beside the champion-pole term, so it wants ruling on together with that
rather than separately.

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

## Absent entries in the level's ST1 table

Every level carries a `SpawnType1` pointer table (`struct Level::ptrSpawnType1`)
whose entries are indexed by the `ST1_*` enum: minimap, object spawns,
end-of-race cameras, the intro camera path, the two ghosts, credits.

Retail says "this level has no X" by making the table **short**, so every engine
consumer guards with a count threshold and then dereferences the entry
unconditionally. Measured by relocating the real files through the engine's own
`LOAD_RunPtrMap`:

| Content | `count` | `ST1_CAMERA_PATH` | `ST1_CAMERA_EOR` |
|---|---|---|---|
| all 18 retail arcade LEVs | 4 | non-NULL on every one | non-NULL on every one |
| all 7 retail battle arenas | 0 | out of bounds | out of bounds |
| Baby T Park | **7** | **NULL** | **NULL** |

A custom track need not use retail's encoding, and this one does not. It emits a
**full-width table and expresses absence as a NULL entry**: the slot is left out
of the LEV's pointer map, so `LOAD_RunPtrMap` never relocates it and it arrives
as 0. Baby T Park's `ST1_MAP`, `ST1_CAMERA_EOR`, `ST1_CAMERA_PATH` and
`ST1_CREDITS` are all NULL inside a seven-entry table. That walks straight past
every count threshold in the engine.

Two of those thresholds are reachable on the event race, and both crashed:

- **The start-line fly-in.** `CAM_FollowDriver_Normal` guards on `count < 4` and
  then hands `CAM_StartLine_FlyIn` `ptrEnd = cameraPath + 0x354`. With
  `cameraPath` NULL that dereferences 0x354 on the first frame the level
  renders. This is the crash the event candidate hit, ~107 frames after the hub
  handed off to the race.
- **The end-of-race camera.** `CAM_EndOfRace` arms
  `CAMERA_FLAG_ARCADE_END_OF_RACE_REQUESTED` on `count > 1`, and the block in
  `CAM_ThTick` guards on `count < 3` and then reads `*ptrs[ST1_CAMERA_EOR]` to
  get the camera count. Same shape, one race later: it fires the frame the first
  driver crosses the line, which on this race is the win the Gem depends on.

The fix asks the question the consumers meant to ask — "is the entry at this
index really there" — through `CustomTrackPolicy_St1EntryPresent` in
`include/platform/native_custom_tracks_policy.h`. It subsumes the count
threshold rather than replacing it (`index < count` for `ST1_CAMERA_PATH` **is**
`count >= 4`), which is why swapping one for the other cannot change what retail
content does: on retail the two agree in both directions, as the table above
shows and the harnesses assert.

Behaviour with the entry absent is the one a level without those cameras already
gets:

- no camera path — the fly-in reports itself done immediately and `x = 0x1000`,
  exactly the battle-map branch;
- no EOR table — `CAM_EndOfRace` takes its `CAM_EndOfRace_Battle` branch, so the
  race ends on the battle-map end-of-race camera.

Three call sites, all in `game/CAM.c`, all inside `#ifdef CTR_CUSTOM_TRACKS`:

| Site | Was | Now |
|---|---|---|
| `CAM_EndOfRace` | `count > 1` | + the `ST1_CAMERA_EOR` entry is present |
| `CAM_FollowDriver_Normal` fly-in | `count < 4` | the `ST1_CAMERA_PATH` entry is absent |
| `CAM_ThTick` EOR block | `count < 3` only | + skip if the entry is NULL |

The third is not redundant with the first. `CAM_EndOfRace` is the engine's only
writer of that flag, but `ap/ap_democam.c` sets
`AP_DEMOCAM_FLAG_ARCADE_EOR_REQUESTED` straight onto `cDC->flags`, and its own
`AP_DemoCamEorTableUsable` answers on `count >= 3` and a non-NULL *table header*
— never the entry — so it would arm on exactly the shape that crashes. Guarding
the dereference itself covers that path too.

**Consumers checked and deliberately left alone.** Every other read of the table
was audited against this race (adventure cup, single player, five karts, not
battle, not time trial, boxes denied, ghosts off):

- `ST1_MAP` (`UI_Map.c`, `UI_RenderFrame.c`) is reachable — the descriptor's
  `minimap` flag is inert on the native side, so the engine still draws the
  minimap — but both consumers already null-check the pointer they fetch, so
  Baby T Park's NULL means *no minimap*, not a crash.
- `ST1_SPAWN` (`RB_Plant.c`, `RB_Orca.c`, `RB_FlameJet.c`, `RB_Armadillo.c`) is
  unreachable: each needs a LEV instance carrying a specific retail model id.
  Worth recording that these guard `count > 0` and then read index 1, and that
  15 of the 18 retail arcade LEVs ship a NULL `ST1_SPAWN` — so this is a latent
  retail bug, not a custom-track one.
- `ST1_NTROPY` / `ST1_NOXIDE` (`GhostReplay.c`) is time-trial only, and Baby T
  Park's entries are non-NULL anyway.
- `ST1_CREDITS` (`CS_Credits.c`) is credits-level only, `ST1_CAMERA_PATH` in
  `MM_Title.c` is title-screen only, and `AH_Map.c` is hub only — all three
  reached only on retail LEVs.
- `CAM_Path_GetNumPoints` / `CAM_Path_Move` already null-check the entry and are
  overlay-233 menu and cutscene paths regardless.

Not fixed here, and recorded rather than discovered: `AP_DemoCamEorTableUsable`
still answers on count alone, so a dev-key democam session on a track with no
EOR table engages and then does nothing. `MM_Title.c` guards `count > 2` before
reading index 3. Neither is reachable on this race.

## The per-level primitive budget

Each frame gets a fixed primitive arena, and its size is looked up **by level
ID**: `data.primMem_SizePerLEV_1P[levelID] << 10` (`game/MAIN/MainInit.c`,
`MainInit_GetPrimMemSize`). A custom track borrows an arcade slot, so it
inherits the budget retail chose for **that slot's** geometry while bringing its
own. For host slot 6 that is `0x67 << 10` = 105,472 bytes, with
`guardEnd = end - 0x100` leaving 105,216 usable.

Most emitters already bound themselves against `PrimMem::guardEnd`, because
retail's own content could approach it — fifteen files do. Two did not, and both
take their iteration count straight from the level file, which means a custom
track sets it:

| Emitter | Count comes from |
|---|---|
| `DrawSky_Piece` (`game/DrawSky.c`) | `skybox->numFaces[segment]` |
| `RenderStars` (`game/RenderStars.c`) | `lev->stars.numStars`, via `MainInit.c` |

Past the end of the arena this is not a visual artifact. The native port maps
the draw arenas to 24-bit GPU tokens, and `NativeGpuLinks_FromHostPointer`
(`platform/native_gpu_links.c`) `abort()`s on a pointer it has no token for — so
the first primitive linked past `primMem->end` is a hard `SIGABRT`.

### Measured

Relocating the real files through the engine's own `LOAD_RunPtrMap` and reading
`lev->ptr_skybox`:

| Content | worst 4-segment frame | POLY_G3 bytes | % of its own budget |
|---|---|---|---|
| 18 retail arcade tracks | 69 – 385 faces | 1,932 – 10,780 | **1.8% – 9.9%** |
| (4 of those 18) | no skybox at all | 0 | 0% |
| Baby T Park | **11,088 faces** | **310,464** | **294%** |

The event track carries **2,772 faces in every one of its eight segments**, and
`DrawSky_Full` draws four segments per frame. So one frame demands 310,464 bytes
from a 105,472-byte arena — it overruns by 205,248 bytes **before any other draw
in the frame**. It is a 29x outlier against the worst retail track.

### Why the fix is a clamp and not a bigger buffer

The budget table is `u8[0x1c]` shifted left by 10, so the largest value it can
express is `255 << 10` = 261,120 bytes — **smaller than this one track's sky**.
There is no value that would have fitted, so growing the buffer was never an
available answer. `CustomTrackPolicy_PrimFits` therefore answers "does one more
primitive fit", and the two unbounded emitters stop drawing when it says no.

The sky clamp sits at the top of `DrawSky_Piece`'s face loop rather than at the
emit, so a segment with no room left costs one compare instead of a full GTE
projection pass over 2,772 faces every frame. It logs once per process, not once
per frame. `RenderStars` reserves a `TILE_1` **plus** its trailing draw-mode
packet, because stopping with room for only the star would leave that packet to
overrun instead; the packet is separately guarded for the case where the loop
never ran at all.

`RenderStars` is bounded pre-emptively. The event track sets `numStars` to 0 and
never enters that loop — the two retail tracks that do have a star field ask for
300, about 3.5% of their budgets — but it is the other half of the same hole, in
the same rung, and the descriptor does not measure star counts. The count is
also read through a `u16`, so a negative `s16` in a custom LEV would ask for up
to 65,535 stars.

### Sizing the arena instead of only clamping to it

The clamps above keep the client alive, but they were never the right resting
state: they draw as much as the borrowed slot's budget allows and silently drop
the rest. Under `CTR_CUSTOM_TRACKS` the retail table is not binding, and the same
argument the [MEMPACK](#mempack) expansion already made applies here.

`MainInit_PrimMem` now takes the retail figure from `MainInit_GetPrimMemSize` --
which is ASM-verified and deliberately untouched -- and raises it to
`CTR_CT_PRIM_ARENA_BYTES` (1 MiB) for a load that qualifies. Two independent
reasons qualify a load, and either one is enough:

- **the loader is serving the custom track for it**, because the borrowed slot's
  budget was chosen for someone else's geometry, and
- **it is a 1P level load** (`numPlyrCurrGame == 1`), on measured headroom.

Split-screen, the `numPlyrCurrGame == 0` attract path, and every load in a build
without `CTR_CUSTOM_TRACKS` allocate exactly the bytes retail allocates.

1 MiB is measured, not chosen:

| | bytes |
|---|---|
| sky, worst four-segment frame | 310,464 |
| level geometry, every quadblock at near LOD | 496,704 |
| both at once, which cannot actually happen | 807,168 |
| `CTR_CT_PRIM_ARENA_BYTES` | **1,048,576** |

Four ceilings were checked rather than assumed, and each is asserted in
`tools/test-custom-track-policy.c`:

- **GPU link tokens.** Ranges are handed tokens counting *down* from `0x00f00000`
  (`platform/native_gpu_links.c`), so every registered range shares a
  15,728,640-byte budget. Two 1 MiB arenas plus the OT pair and the swapchain
  pair come to 4,218,928 — 27% of it. An over-large arena would fail **loudly**:
  `NativeGpuLinks_RegisterRangeChecked` aborts at startup rather than truncating.
- **MEMPACK.** 5,418,356 bytes were free during this race on the 8 MiB arena the
  custom-track build already selects; the expansion costs 1,886,208 of that.
- **No 16-bit primitive offsets anywhere downstream.** `PrimMem`'s own fields are
  `u32`/`void *`, and the GPU tags carry 24-bit *tokens* rather than truncated
  pointers, so nothing narrows an address as the arena grows.
- **MEMPACK on *every* 1P load**, not only the one race that was measured. Any
  load that runs at all under retail pressure fits the retail window whole
  (`0x144e10` = 1,330,704 bytes), prim arenas included. A custom-tracks build
  replaces those arenas inside an 8 MiB pack (8,386,560 usable), so the worst
  conceivable 1P demand is that window plus both floors — 3,427,856 bytes, with
  4,958,704 spare. The figure over-counts, because it does not subtract the
  retail arenas the floor replaces.

The GPU-token ceiling does **not** compound across level loads.
`MainFrame_RegisterGpuLinkRanges` calls `NativeGpuLinks_Reset()` before it
registers, so exactly six ranges are ever live and each load starts the token
space over. Widening the floor to every 1P load registers the same six ranges at
the same sizes.

The expansion is a **floor, not a replacement**: a slot whose retail budget was
already larger keeps it, so turning the feature on can never give a custom track
less room than the slot it borrowed.

With the arena sized to the demand, the clamps become what they should have been
from the start — a safety net for a track that exceeds even this, not the
operative path.

#### What the hub measurement does and does not establish

The 2026-08-29 diagnostic run put the adventure hub (levelID 25, retail budget
`0x1c000` = 114,688 bytes) at a worst frame of 106,324 bytes, leaving 8,364 —
93% spent, against a constant the PS1 chose for PS1 draw pressure. 8,364 is less
than `DRAW_LEVEL_OVR1P_BUCKET_RESERVE_FULL_DYNAMIC` (`0x2700` = 9,984), the
largest reserve `DrawLevelOvr1P_HasBucketPrimReserve` asks for.

That is **headroom evidence, not exhaustion evidence**, and the difference
matters. The reserve is tested during terrain while the figure is sampled after
the sky; and more importantly a frame that *did* refuse a reserve abandons the
rest of level rendering, so it spends **less** than a frame that completed and
never becomes the maximum. The high-water mark is structurally unable to show
the failure it was built to show. That is why the report now counts refusals
directly — see [the per-load render report](#the-per-load-render-report).

So the floor is widened on headroom. Whether the hub was actually crossing its
budget is a question the next run answers, not one this rung claims to have
settled.

### Why this only appeared after the camera fix

Not a frame-count coincidence, and the frame numbers are not comparable: the two
crashes came from different sessions, so how long the player spent in the hub
first differs. The ordering is in the call graph.

`MainInit_FinalizeInit` runs one camera tick at the end of the level load —
`ThTick_RunBucket(gGT->threadBuckets[CAMERA].thread)`, `game/MAIN/MainInit.c` —
and the fly-in crash was rooted there. That is **before** the main loop reaches
`MainFrame_RenderFrame` (`game/MAIN/MainMain.c`) for the first time, so the
level had never rendered a frame and `DrawSky_Full` had never run. Fixing the
camera let initialization finish, and the sky overrun then landed on the first
rendered frames. The two are sequential failures on the same load, not a
regression introduced by the camera fix.

### Where this feature's log goes

Everything the feature prints now goes through `CustomTrack_Log`
(`platform/native_custom_tracks.c`), which writes to **stdout** — what the
harnesses capture and assert on — and, in an AP build, also hands the same text
to `AP_LogLine`, the sink that reaches `ctr-ap.log`.

Before that helper existed every `[CustomTracks]` line was a bare `printf` to
stdout, so none of it survived a real session: a run that refused a track, or
clamped a sky, left no evidence any log grep could find. Both halves matter —
dropping stdout would blind the harnesses, and the missing `AP_LogLine` half is
what made the first runtime question about the sky clamp unanswerable.

`AP_LogLine` is bound by prototype, the way every other game-side AP call site
does it, so the loader stays linkable in a clean non-AP build.

### The per-load render report

`RenderAllLevelGeometry` opens a frame's accounting before the BSP walk and
closes it after the sky, sampling `primMem->cursor` three times — before the
terrain, after it, and after the sky. One line is emitted **per level load**,
when the level changes, through `CustomTrack_Log`:

```
[CustomTracks] level L over N frames: prim arena worst frame W/CAP bytes
               (other draws A, terrain +B, sky +C, F free, P primitives across
               E leaves); reserve refused R, rendered list full U, bsp records
               dropped D
```

`other draws` is the load-bearing arena number: terrain is the 22nd of the
frame's primitive writers and the sky is the 23rd, so both live on whatever the
HUD, the weather, the karts, the crates, the tires and the shadows left behind.

The three counters at the end are what the first version of this report was
missing. The high-water mark reports the worst **completed** frame, and a frame
that ran the arena dry completes *less* work and therefore spends *less*, so
exhaustion hides from it. Each counter is recorded where it happens:

| counter | where | what it means |
|---|---|---|
| `reserve refused` | `DrawLevelOvr1P`'s six `HasBucketPrimReserve` failure sites | the prefix cut itself: the frame stopped drawing level geometry partway through |
| `rendered list full` | `DrawLevelOvr1P_AppendRenderedQuadBlock` | the rendered-quadblock bound refused an append that retail would have written past the array |
| `bsp records dropped` | `RenderLists_PushChild` | the 51-record scratch stack was full and a whole subtree was never walked |

A non-zero `reserve refused` also emits its own loud line naming the load, the
frame count, and the *closest* refusal — the reserve that was wanted and the
bytes that were left — because the smallest shortfall is the figure a later
sizing decision would be based on. A non-zero `rendered list full` emits one
too.

The first version printed on every new per-load maximum, which was 101 lines in
one session, keyed the load on the arena size (so two levels sharing a budget
merged into one report), and still could not answer the question. This version
prints once per load, keys on `levelID` **and** capacity, and counts the three
events that a completed frame cannot show. Counting is scoped to the window
between `CustomTrackDiag_BeginFrame` and `CustomTrackDiag_NoteFrameSpend`, which
split-screen never opens, so a 3P race cannot leave its refusals in the report
of the 1P load before it.

### The rendered-quadblock bound

`DrawLevelOvr1P_AppendRenderedQuadBlock` stored a pointer at the scratch cursor
and advanced it with **no end test**. The cursor is seeded to
`sdata_static.quadBlocksRendered`, which is `struct QuadBlock *[0x100]`, and the
next member of that struct is the `GamepadSystem`. Retail bounded this by
arithmetic rather than by a test: the primitive arena ran out first. The floor
above removes that accident, so the bound had to become explicit **before** the
arena was widened, not after.

The bound is the **end of the array**, not the `0x40` per-player stride. In 1P
the base is `&quadBlocksRendered[0]` and retail lets that one list use all 256
slots, so clamping at the stride would refuse work retail does on ordinary
frames. Split-screen bases at `0x40`/`0x80`/`0xC0` can still run into the next
player's region exactly as retail does — that is retail's own layout, and this
does not change it. What it stops is the write *past* the array.

Two call sites, one predicate (`CustomTrackPolicy_RenderedSlotsFit`). The append
asks for two slots, because the entry it is about to write must leave room for
the `NULL` terminator that follows the last one; the terminator asks for one.
The array's length is tied to `CTR_CT_RENDERED_QUADBLOCK_SLOTS` by a
`CTR_STATIC_ASSERT` beside the bound, so drift is a build failure.

`RenderLists_PushChild`'s silent 51-record drop was audited alongside it and is
**orthogonal to the arena**: the BSP walk runs before any primitive is written,
so the floor neither helps nor hurts it. It is counted rather than bounded,
because dropping a subtree is a competing explanation for missing geometry and
the honest next step is to find out whether it happens at all.

### Siblings checked

Every other primitive emitter reachable in a 1P arcade race frame was audited
and left alone, because each is either already guarded or bounded by something a
custom track cannot set:

- Already guarded against `guardEnd`: `Particle.c`, `RenderBucket_QueueExecute.c`
  (ten sites), `VehGroundShadow.c`, `VehGroundSkids.c`, `RB_Spider.c`,
  `PushBuffer.c`, `UI_RaceHud.c`, `UI_Meter.c`, `UI_RenderFrame.c`, `FLARE.c`,
  `CTR_Box.c`, `RECTMENU.c`, `prim.c`.
- `DrawLevelOvr1P` guards against `primMem->end` with per-bucket reserves, and
  its `HasBucketPrimReserve` deliberately tolerates `cursor > end` so it fails
  closed if an earlier emitter already overran.
- Bounded by a fixed cap rather than level data: `RenderWeather`
  (`weather_intensity` is a `u8`, so ≤1020 particles), `DrawConfetti`
  (hard-coded 250/200), `Torch_Main` (12), `CAM_SkyboxGlow` (3), `RaceFlag`
  (10×35), `DotLights` (4), and the HUD/icon writers (fixed element counts).
- Not primitive emitters at all: `RenderLists_Init1P2P`, `CTR_CycleTex_*`.

One related finding recorded rather than fixed: `AnimateWater_Common` walks
`numWaterVertices` from the LEV with no bound, but it mutates vertex colours in
place inside the level's own MEMPACK allocation and never touches `PrimMem`, so
it cannot produce this abort. Baby T Park reports 0 water vertices.

The `>2 MiB LEV` shim flagged in [MEMPACK](#mempack) was re-checked against this
crash and is **not** implicated.
`DrawLevelOvr1P_TryConvertNativeMempackPointerToPsxWord` feeds exactly one
consumer, which returns a single `s8` used only as an OT index offset. It can
perturb draw order by ±127 entries but can never produce an out-of-range
*primitive* pointer, and an unknown token makes `NativeGpuLinks_ToHostPointer`
return NULL rather than abort. It remains visual-only, as recorded.

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
armed session. It also pins the ST1 table shape: it builds a DRAM-file image
whose pointer map omits the camera slots, relocates it through the engine's own
`LOAD_RunPtrMap` (the function is compiled into the harness, so there is no
transcription to drift), and asserts that the omission is what produces the NULL
— then that both count thresholds `CAM.c` used to guard its two cameras with
**pass** on that table while the predicate that replaced them refuses, and that
on a retail-shaped table old and new agree in both directions.

`test-custom-track-policy` covers the same predicate as a truth table, against
the three measured table shapes, plus its bounds behaviour: the index check runs
before the array is indexed, which is load-bearing rather than defensive because
`CAM_EndOfRace` asks about index 2 on a table it has only proven to have
`count > 1`.

The primitive-budget clamp is pinned the same two ways. `test-custom-track-load`
builds a LEV image whose `Level::ptr_skybox` carries the event track's measured
face counts, relocates it, computes the worst four-segment frame from the same
rotation `DrawSky_Full` uses, and asserts it overruns the levelID-6 arena by
exactly 205,248 bytes — then drives the clamp over that demand and asserts the
cursor never passes `guardEnd`, that it stops short of the demand, and that it
still fills the arena exactly rather than giving up early. It repeats the walk on
a retail-shaped sky and asserts **every** face is drawn, so the clamp is shown
inert on retail rather than merely assumed to be, and on a level with no skybox
at all (four of the 18 retail arcade tracks). `test-custom-track-policy` pins
`CustomTrackPolicy_PrimFits` itself: the exact edge where a primitive would land
on the guard, a cursor already past the guard, the two different primitive sizes
the two call sites ask about, and the measured demand figures including the fact
that the budget table's own ceiling is smaller than this track's sky.

`test-custom-track-load` additionally pins the seam the arena expansion rides
on: that `CustomTrack_ServingLoad` — what `MainInit_PrimMem` asks — and
`CustomTrack_GetOverride` — what the subfile reader asks — give the **same**
answer for the same load, so the bytes a race is served and the arena it is
given can never disagree. It checks the event race and a retail pad to the very
same host slot in one armed session, and that a withdrawn descriptor expands
nothing. Those assertions all ask at a player count the 1P widening does not
cover, so the serving term stays separately load-bearing.

The 1P widening has its own case in `test-custom-track-load`, because the point
of it is that it applies to loads the loader knows nothing about. It runs the
retail sizing rule itself — the same branch structure `MainInit_GetPrimMemSize`
uses, so a change to that function's constants shows up as a changed input
rather than a stale expectation — and asserts that the hub gets the floor both
in a session armed for a completely different track *and* in a disarmed one,
while all 25 arcade slots keep retail sizing at 2P and 4P.

`test-custom-track-policy` pins the arena decision itself: split-screen and the
attract path keep their exact figure, every 1P load gets the floor, the
expansion is a floor rather than a replacement on **both** reasons, the four
ceilings above hold as arithmetic, and the hub measurement is recorded together
with what it does not establish.

The rendered-quadblock bound is pinned twice.
`test-custom-track-policy` pins `CustomTrackPolicy_RenderedSlotsFit` itself:
the 254th and 255th entries fit, the 256th is refused because its terminator
would land past the array, the last slot still takes a terminator, a cursor at
or past the end fits nothing, and both split-screen and 1P can still reach the
end of the array — the assertion that would fail if the bound had been put at
the `0x40` stride. `test-custom-track-load` drives the two call sites over an
array with a canary behind it, in the layout `sdata_static` actually has, and
asserts that 200 and 255 appends land untouched, that 400 stop at 255 with the
canary clean, and that the same loop *without* the bound overwrites the canary
from its first byte.

The displaced cup's **name** is pinned on both halves. `test-custom-track-policy`
pins the width rule against `DecalFont_GetLineWidthStrlen`'s own three constants,
including the two glyph classes that are not one character width, and derives the
238-pixel ceiling from `PURPLE GEM CUP` rather than asserting it as a bare
number; it pins the refusals (a byte the font has no width for, a name that fills
the buffer, one glyph past the ceiling, eight button glyphs that a
character-counting bound would have passed); and it sweeps every combination of
cup, adventure term, feature flag and verification flag asserting that **a cup is
renamed exactly when it is redirected** — the invariant that stops the pad saying
Baby T Park while it loads the Purple legs. `test-custom-track-load` pins the half
that only exists in engine: the key is read from a real `config.ini`, an unusable
value is dropped once with a reason **and the loader still arms**, and the name
survives a descriptor being withdrawn and replaced while never being shown for a
cup that is no longer displaced.

The **field** is pinned three ways. `test-custom-track-policy` pins the pick's
range across the whole 12-bit draw space and every remaining count, and pins that
`CustomTrackPolicy_PermuteRoster` is a permutation for adversarial draws as well
as ordinary ones — all-zero, all-ones, and a 4,096-step sweep — which is the
property the safety argument rests on. `test-custom-track-load` runs the **real**
`LOAD_Robots1P` and the **real** `RngDeadCoed` for all sixteen characters a player
can be, 64 rolls each, asserting on every roll that the four karts that reach the
grid are distinct, none of them the player, and all inside the pack, and that the
sweep produces more than 100 distinct fields rather than one. It then drives the
**real** `LOAD_DriverMPK` and asserts on the queued subfile index: the event race
queues `BI_1PARCADEPACK + player` and not `BI_2PARCADEPACK + 7`, while a
non-event leg of the same cup, a retail pad to the same host slot, and a
withdrawn descriptor all still get the boss pack and the fixed boss lineup in the
same session.

The **field size** is pinned as two guarantees, because they fail differently.
`test-custom-track-policy` sweeps the clamp over spawn counts from −4 to 20 —
well outside the 1..8 the wire validates, because the clamp is the last thing
between a descriptor and an index into a fixed 8-entry array — and asserts every
result lands in range, that under-reporting grids the floor rather than shrinking
the race, and that over-reporting is capped rather than seating a kart on an
unauthored slot. It then asserts separately that **the refusal edge did not
move**: every spawn count servable before the field grew is still servable, and
one below the floor is still refused. It also pins the two engine forks the size
drives — the driver count and the standings layout — including that the two
agree, which is what stops a seated field reaching a layout that cannot lay it
out. `test-custom-track-load` drives the same path through the **real loader**:
an 8-spawn descriptor grids eight, a 6-spawn descriptor grids six instead of
being refused, a 5-spawn descriptor grids the floor, and a 4-spawn descriptor is
refused exactly as it always was.

Every one of these is mutation-checked. Raising the clamp ceiling to 9 or
dropping the floor to 4 turns three assertions red in each harness; raising
`RequiredSpawns` to 8 alongside the field — the change the adaptive shape exists
to avoid — turns seven red in the policy harness and five in the loader harness;
keying the standings layout on the cup instead of the field size turns seven and
two red; and making the driver count ignore the event field turns three red in
each. Dropping the serve term from
`LOAD_DriverMPK`'s cup branch turns nine assertions red in the loader harness;
making the permutation a no-op turns two red in each; moving the width
comparison from `>` to `>=` turns two red, raising the pixel ceiling turns five
red, and charging a button glyph one width instead of two turns four red, all in
the policy harness; dropping the redirect gate from the name accessor turns five
red in the policy harness and four in the loader harness. Reverting the sizing
predicate to its
first form turns three assertions red in each harness; moving the floor to 2P
turns four red in the policy harness and eight in the loader harness; dropping
the append's second slot, moving the fit comparison from `>=` to `>`, or
changing the array length by one each turn assertions red in both. The array
length is additionally tied to the engine's own declaration by a
`CTR_STATIC_ASSERT`, so that last mutation is a **build failure** as well.

## Guard-off identity

With `CTR_CUSTOM_TRACKS` off, the preprocessed `main.c` translation unit (5.6 MB
without AP, 6.3 MB with it — the whole unity build either way) differs from
`main` only in the `__LINE__`-derived names of unused `CTR_STATIC_ASSERT`
typedefs, shifted by the added `#include` blocks. Those typedefs generate no
code. There are no other differences.

The count is **thirteen** typedefs in a non-AP build, measured against
`origin/main`. It was five before the camera guard added an `#include` block to
`game/CAM.c` (six), and the primitive-budget guard added two more such blocks, to
`game/DrawSky.c` and `game/RenderStars.c` (thirteen). Both of those files carry
`CTR_STATIC_ASSERT`s of their own near the top, which is why two `#include`
blocks shift seven names. The arena-sizing change added `#include` blocks to
`game/MAIN/MainInit.c` and `game/MAIN/MainFrame_RenderFrame.c` and did **not**
move the count: neither file contains a `CTR_STATIC_ASSERT`, and `__LINE__`
shifts are confined to the file they occur in. The 1P widening added `#include`
blocks to two files that **do** carry them —
`game/226/226_00_DrawLevelOvr1P.c` (five) and `game/RenderLevel/RenderLists.c`
(one) — and did not move the count either, because in both files the block is
placed **below** the asserts for exactly that reason. There is a comment saying
so at each site. The name and field work added one more `#include` block, to
`game/LOAD/LOAD_Assets.c`, and did not move the count: that file carries no
`CTR_STATIC_ASSERT`, and `__LINE__` shifts are confined to the file they occur
in. Its three other files — `AH_WarpPad.c`, `UI_RaceFlow.c` and
`UI_CupStandings.c` — already included the header before this rung. The field
work added one more, to `game/HOWL/HOWL_Music.c`, which likewise carries no
`CTR_STATIC_ASSERT`; `MainInit.c` and `UI_CupStandings.c` already had theirs.
Placement was checked before the edit rather than inferred from the result. An AP build additionally shows the `ap/ap_seedcfg.cpp`
custom-tracks parse itself, which is deliberate and documented under
[Lifecycle](#lifecycle): a guard-off AP build still parses the block so its
verifier stays correct about displacement.

The number that matters is the other one: **zero** non-static-assert lines
differ, in both the AP and non-AP configurations, at every step.

To reproduce, preprocess `main.c` with `cc -E` and the guard-off flags in this
tree and at the commit being compared against, normalising `CTR_NATIVE_BUILD_ID`
and `CTR_NATIVE_VERSION` (they embed the git hash and would otherwise differ),
then diff and filter out `# NNN "file"` line markers.

**Do not compare the linked binaries.** `CTR_SPLIT_DEBUG` strips the debug info
into a `.debug` sidecar and stamps the binary with a `.gnu_debuglink` carrying
that sidecar's CRC, so the shipped binary changes whenever a line number moves
even though no instruction did. Compare the *object* instead, compiled with
`-g0` so line numbering cannot reach it:

```
cc -c -m32 -msse -g0 -O3 -DNDEBUG -std=gnu99 -O2 -DBUILD=926 -DCTR_INTERNAL \
   -DCTR_NATIVE -DCTR_NATIVE_BUILD_ID='"pinned"' -DCTR_NATIVE_VERSION='"pinned"' \
   -I include -I <builddir>/generated -I <builddir>/externals/SDL/include-revision \
   -I externals/SDL/include main.c -o /tmp/off.o
```

Run it with and without `-DCTR_AP`, at both commits, and `sha256sum` the four
objects. For the 1P widening those objects are byte-identical to the previous
head in both configurations.

## Caveats

- The custom track keeps the RETAIL slot's identity for ghosts and saved times,
  because the slot's levelID is unchanged. Do not save ghosts or times on it.
- The displaced cup's name is a per-client string. Two players on the same seed
  with different `custom_track_name` values see different names for the same
  race, and a player with no key set sees `PURPLE GEM CUP`. A descriptor field
  is what makes every client agree, and it is not in this rung.
- The event race grids as many karts as the descriptor reports spawn slots for,
  capped at eight. A packager that over-reports seats karts on grid slots it
  never authored; nothing in the engine can catch that, because
  `struct Level::DriverSpawn` has no count to check against.
- Three more karts than the previous rung draw three more karts' worth of
  primitives per frame. This is **settled by arithmetic, not pending a live
  run** — see [does the wider field fit the arena](#does-the-wider-field-fit-the-arena).
  What is worth watching instead is MEMPACK: three more karts means three more
  model instances and their allocations. The custom-tracks build has the 8 MiB
  pack so this is very likely fine, and the instrument is the `[AP POOL] free=`
  telemetry rather than the per-load render report.
- The event race's AI are drawn from the eight base characters only. The bosses
  and the unlockables cannot appear, because no shipped MPK pack carries them
  alongside the base roster and an id outside the loaded pack is a crash rather
  than a wrong model. See [the field](#the-field).
- The track's music (`.sca`) is not loaded; the retail slot's music plays.
- A custom track has no track-preview video: the main menu reads preview sectors
  from BIGFILE directly, bypassing the loader entirely.
- A track with no `ST1_CAMERA_PATH` has no start-line fly-in: the race begins on
  the follow camera with the countdown already up. A track with no
  `ST1_CAMERA_EOR` ends on the battle-map end-of-race camera rather than a
  cinematic angle. Baby T Park has neither entry, so the event race shows both.
- The minimap is blank on a track whose `ST1_MAP` entry is absent, which Baby T
  Park's is. The descriptor's `minimap` flag does not cause this and does not
  prevent it — it is measured and logged but inert on the native side.
- No descriptor flag measures sky faces, star counts or geometry density, so a
  track whose per-frame primitive demand exceeds even the expanded arena is not
  refused and cannot be predicted from the wire. It races correctly; it clips.
  A check rung that measures primitive demand at pack time is the real fix and is
  not in this rung.
- **This track's BSP is unusually coarse: 12.4 quadblocks per leaf, against 2.5
  to 5.6 on every retail arcade track**, and none of its leaves carry a LOD
  render flag. The BSP leaf is the culling unit, so for the same visible screen
  area it submits roughly two to four times the quadblocks retail would, all on
  the expensive four-face path. Its total geometry is ordinary — 2,388
  quadblocks, where four retail tracks carry more — so this is a packing
  property, not a size problem, and no amount of arena covers a track that
  cannot cull.
