# AI lap recording container, format versions 2 and 3

Status: specification for the 0.2.0 recorder slice. This is the file the client
writes when `nav_record` is on, and the only file the client reads when
`nav_use_recorded` is on.

The container exists because the engine's own nav node struct cannot carry any
of the information a recorded lap needs. `struct NavFrame`
(`include/namespace_Bots.h`) is 20 bytes of engine data with a compile-time size
assertion on it, and `BOTS.c` walks arrays of it directly. It does not change,
now or later. Everything the recorder adds lives in the container beside the raw
`NavFrame` arrays.

Version 3 extends the header for custom-track identity. Version 2 remains the
retail writer shape and stays readable. A v3 file carries the custom package's
permanent UUID and its navigation compatibility revision. Its physical engine
slot remains diagnostic and never decides custom-track compatibility.

Version 1, the throwaway spike container (magic `NAVR`, level id, three counts,
three raw `NavFrame` arrays), is not readable by a version 2 reader and is not
convertible. It carries no driver, no timing, no integrity check and no version
field, so there is nothing to migrate and no way to tell a truncated one from a
complete one. Version 1 files use a different magic, so they are rejected at the
first four bytes rather than misparsed.

## Conventions

- Little-endian throughout. Both shipped targets are x86-32.
- Fixed-width fields only. No padding beyond what is written below, no
  compiler-dependent struct layout in the header or the directory.
- Every offset in this document is absolute from the start of the file unless it
  is introduced as "within" a record.
- Reserved fields are written as zero and must be ignored by a reader, not
  validated. That is what makes them usable later without a version bump.
- Text fields are fixed-size byte arrays, NUL padded to their full length. A
  reader must treat the field as terminated at the first NUL and must not assume
  a NUL is present at all: it must stop at the field's end.

## Layout

```
+--------------------------------------+  0x00
|  FileHeader            96/128 bytes  |
+--------------------------------------+  headerSize
|  LapDirectory[lapCount]    32 bytes  |
|  each, lapCount is 1 to 3            |
+--------------------------------------+  headerSize + 32*lapCount
|  Lap payloads, in directory order:   |
|    NavFrame[nodeCount]  20 bytes ea. |
|    timestamps[nodeCount] 4 bytes ea. |
+--------------------------------------+  totalSize - 8
|  Trailer: contentHash      8 bytes   |
+--------------------------------------+  totalSize
```

Payload offsets are not implied by the layout. Every lap payload is located
through the explicit `framesOffset` and `timestampsOffset` in its directory
entry, and a reader must validate those offsets rather than recompute them. The
writer emits them in directory order and contiguously, but a reader that assumes
so would be trusting the writer instead of checking the file.

### FileHeader, 96 or 128 bytes at 0x00

| Offset | Size | Type   | Field            | Value and meaning |
|--------|------|--------|------------------|-------------------|
| 0x00   | 4    | u8[4]  | magic            | `4E 41 56 32`, ASCII `NAV2`. The container family marker. Constant across format versions 2 and later. |
| 0x04   | 2    | u16    | formatVersion    | 2 for retail/legacy recordings; 3 for custom-track recordings. |
| 0x06   | 2    | u16    | headerSize       | 96 for v2; 128 for v3. The lap directory begins here. |
| 0x08   | 4    | u32    | totalSize        | Total file size in bytes, including the 8-byte trailer. |
| 0x0C   | 4    | s32    | levelId          | Engine `gGT->levelID` at record time. The reader uses it to confirm the file belongs to the level being loaded. |
| 0x10   | 32   | u8[32] | clientVersion    | `CTR_AP_VERSION` from `ap/ap_version.h` at record time, NUL padded. Informational: a reader must not reject on it. It exists so a physics or recorder change in a later build can be traced to the laps it invalidated. |
| 0x30   | 32   | u8[32] | driverName       | Sanitized driver name, NUL padded. See "Driver name" below. May be all-NUL, meaning the recorder had no name to write. |
| 0x50   | 2    | s16    | characterId      | `driver->driverID` of the recording player (`include/namespace_Vehicle.h`, offset 0x4A in `struct Driver`). -1 when unknown. |
| 0x52   | 2    | s16    | difficultyPreset | Engine AI-difficulty value in force at record time, as `g_config.aiDifficulty` stores it (0 vanilla, 0x50, 0xa0, 0xf0, 0x140, 0x280). -1 when unknown. |
| 0x54   | 1    | u8     | shortcutTier     | Declared shortcut-knowledge tier of the seed that was connected at record time. 0 unknown or no seed, 1 easy, 2 medium, 3 hard. Derived from `ctr_cfg.shortcut_knowledge` (0/1/2) plus one, and left at 0 when `ctr_cfg.schema_version` is 0, which is the engine's own "no slot data parsed" state. |
| 0x55   | 1    | u8     | lapCount         | 1 to 3. |
| 0x56   | 1    | u8     | navFrameSize     | 20. Written so a reader on a hypothetical future engine can refuse a file whose node stride it does not agree with, instead of walking it at the wrong pitch. A version 2 reader requires exactly 20. |
| 0x57   | 1    | u8     | reserved0        | 0. |
| 0x58   | 4    | u32    | trackKind        | 0 when the level shipped a retail nav table (`level1->LevNavTable` non-null with at least one non-null lane), 1 when it did not. Custom tracks are 1. This is the reason a lap's `shortcutFlag` can be "unknown" and is recorded separately so a consumer does not have to infer it. |
| 0x5C   | 4    | u32    | reserved1        | 0. |

Version 3 adds:

| Offset | Size | Type | Field | Value and meaning |
|--------|------|------|-------|-------------------|
| 0x60 | 1 | u8 | identityKind | 1, meaning a custom-track identity. |
| 0x61 | 3 | u8[3] | reserved | 0. |
| 0x64 | 16 | u8[16] | trackUuid | Permanent author-controlled package UUID. Renames, translations, textures and music do not change it. |
| 0x74 | 4 | u32 | navRevision | Navigation compatibility revision. Increment only when geometry, checkpoints or navigation compatibility invalidate old lines. |
| 0x78 | 8 | u8[8] | reserved | 0. |

A custom loader sets the active UUID and revision before bot navigation is
initialized and clears it before an ordinary retail load. Playback requires an
exact UUID and revision match. A v2 level-ID-only recording is never eligible
while a custom identity is active, even if both use the same physical slot.

### LapDirectory entry, 32 bytes, at headerSize + 32 * index

| Within | Size | Type | Field            | Value and meaning |
|--------|------|------|------------------|-------------------|
| 0x00   | 4    | u32  | nodeCount        | Decimated nodes in this lap. 8 to 1024 inclusive. |
| 0x04   | 4    | u32  | lapFrames        | Lap length in engine frames, which is the lap time. The recorder samples once per race frame, so this is also the number of raw samples the lap was driven over. |
| 0x08   | 4    | u32  | sampleCount      | Raw samples actually banked for this lap. Equal to `lapFrames` unless the per-lap sample cap was hit, in which case it is lower and the lap is marked dirty. Diagnostic only. |
| 0x0C   | 4    | u32  | framesOffset     | Absolute offset of this lap's `NavFrame` array. |
| 0x10   | 4    | u32  | timestampsOffset | Absolute offset of this lap's timestamp array. |
| 0x14   | 1    | u8   | cleanFlag        | 1 when the lap was clean, 0 otherwise. See "What makes a lap dirty". The 0.2.0 writer only writes clean laps, so this is 1 in every file it produces. It is in the format because a later submission pipeline may want to accept a dirty lap deliberately, and a consumer must never have to guess. |
| 0x15   | 1    | u8   | shortcutFlag     | 0 no, 1 yes, 2 unknown. See "Shortcut classification". |
| 0x16   | 1    | u8   | laneHint         | 0 to 2. The engine lane this lap should fill by default. The writer assigns 0, 1, 2 in the order laps are written. |
| 0x17   | 1    | u8   | reserved0        | 0. |
| 0x18   | 2    | u16  | maskFires        | How many times the Mask held item was FIRED during this lap by the recording player. See "Held-item counters". |
| 0x1A   | 2    | u16  | turboFires       | How many times the Turbo held item was fired during this lap. |
| 0x1C   | 4    | u32  | reserved1        | 0. |

### NavFrame record, 20 bytes

Written raw, exactly as the engine holds it, so the reader can hand the array
straight to `sdata->NavPath_ptrNavFrameArray[]` with no per-node conversion. The
layout is `struct NavFrame` from `include/namespace_Bots.h` and is repeated here
so a non-C reader can parse the file without the engine headers.

| Within | Size | Type | Field |
|--------|------|------|-------|
| 0x00   | 2 | s16   | pos.x |
| 0x02   | 2 | s16   | pos.y |
| 0x04   | 2 | s16   | pos.z |
| 0x06   | 4 | u8[4] | rot, of which only rot[1] (yaw, the CTR angle shifted right by 4) is meaningful for a ground path |
| 0x0A   | 2 | s16   | distToNextNavXYZ |
| 0x0C   | 2 | s16   | distToNextNavXZ |
| 0x0E   | 2 | s16   | flags |
| 0x10   | 2 | s16   | pathChangeOpcode |
| 0x12   | 1 | u8    | goBackCount |
| 0x13   | 1 | u8    | specialBits |

Positions are in `NavFrame` units, which are world units shifted right by 8.
That is the space `BOTS.c` compares against, because it derives its own estimate
as `posCurr >> 8`.

`pathChangeOpcode` is written as the no-lane-change sentinel 0x7FFF in every
node, not zero. Zero decodes as "lane 0, node 0", and the engine would take it:
an overtaking bot would be teleported to the start line. Any positive value at
or above the engine's path-change cap (0xC00) fails the `changeOpcode < cap`
test and disables lane changes safely. Real cross-lane correspondence between a
driver's own three lines is a later release's problem, and until it exists the
sentinel is the only correct value.

`goBackCount` is the recording driver's own `checkpoint.currentIndex` at the
point on the track where that node sits. It is NOT a spare byte and it must NOT
be a constant, and getting this wrong hangs the game rather than merely driving a
bot badly.

The engine reads it as a checkpoint index: `BOTS.c` assigns it straight into
`botData.ai_quadblock_checkpointIndex`. `BOTS_Killplane` then rewinds a fallen
bot with

```
while (backCount == currNav || (frame->flags & 0x4000))
```

walking one node back each iteration, where `currNav` is the bot's own
`checkpoint.currentIndex` and `backCount` is the candidate node's `goBackCount`.
Nothing inside that loop changes `currNav`. Its only exit is reaching a node
whose `goBackCount` differs, so a lane on which every node carries the same value
gives it no exit at all: the moment a bot falls off while its checkpoint index
equals that value, the loop spins forever and the game stops responding.

Recording the driver's index per sample and carrying it through decimation into
each node is therefore not a refinement, it is the condition that makes a lane
safe to hand to the engine. A reader must not rewrite it.

`flags` bit 0x4000 is that loop's other term: a node which sets it is never
accepted as a rewind target, so a lane that sets it on every node hangs the same
loop. The recorder never emits it and the reader clears it. See "What a reader
must force".

### Timestamp array, 4 bytes per node

`u32[nodeCount]`, one entry per node, in node order. Each entry is the frame
index, counted from 0 at the first frame of the lap, at which the driver reached
that node. Entry 0 is 0.

The array is required to be non-decreasing. A reader must reject a file whose
timestamps decrease anywhere, because a pace controller that indexes into a
non-monotonic array produces a negative time delta and a nonsense speed
correction. Equal consecutive entries are legal: two nodes can fall inside one
frame on a fast straight.

The last entry is at most `lapFrames`.

This array is the whole reason the format is being settled before the recorder
ships. The pace-calibrated playback in a later release compares a bot's current
node against the recorded frame at that node. A lap recorded without timestamps
cannot be used for it, and the timestamps are free at decimation time because
the recorder already samples every frame.

### Trailer, 8 bytes at totalSize - 8

| Within | Size | Type | Field |
|--------|------|------|-------|
| 0x00   | 8 | u64 | contentHash |

FNV-1a 64 over every byte from offset 0 to `totalSize - 8`, exclusive. Offset
basis 0xCBF29CE484222325, prime 0x100000001B3, one byte at a time, multiply
after xor.

The hash detects a truncated, corrupted or hand-edited file. It is not a
signature and is not a defence against a deliberate forgery: anyone who edits a
lap can recompute it. It exists so a file that was cut short by a full disk, or
mangled by a transfer, fails loudly at load instead of driving bots through a
wall. A submission pipeline that needs to trust a lap has to replay it, which is
a server-side problem and outside this format.

## Reader rules

A reader must perform all of the following before using any lap, and must reject
the whole file, not a single lap, if any of them fails. Rejection is a log line
and a return to vanilla nav data. It is never a fallback to partial content.

1. The file is at least 96 + 32 + 8 bytes.
2. `magic` equals `NAV2`. Anything else, including the version 1 magic `NAVR`,
   is rejected.
3. `formatVersion` is 2 or 3. Every other value is rejected. A reader must never
   attempt a best-effort parse of a version it does not know.
4. `headerSize` equals 96 for v2 or 128 for v3, and `navFrameSize` equals 20.
5. `totalSize` equals the actual file size and does not exceed a sanity cap of
   1 MiB. Three laps of 1024 nodes is about 74 KiB, so the cap is roughly
   fourteen times the largest legitimate file.
6. `lapCount` is 1 to 3.
7. `contentHash` matches FNV-1a 64 recomputed over `[0, totalSize - 8)`.
8. For every lap: `nodeCount` is 8 to 1024; `framesOffset` and
   `timestampsOffset` are at or after the end of the directory; the frames span
   `[framesOffset, framesOffset + 20 * nodeCount)` and the timestamp span
   `[timestampsOffset, timestampsOffset + 4 * nodeCount)` both lie inside
   `[0, totalSize - 8)`; and neither span overlaps the header or the directory.

   The bounds test must reject a start beyond the ceiling BEFORE it computes the
   remaining allowance. Writing the test as `len > (ceiling - start)` alone is
   wrong: for a start past the ceiling that subtraction wraps and yields an
   enormous allowance, so a declared offset such as 0xFFFFFF00 passes the bound
   and the reader walks off the end of the buffer. That is a real out-of-bounds
   read, not a theoretical one.

   No two payload spans may overlap: not between two laps, and not between a
   lap's own node array and its own timestamp array. Overlapping spans are not a
   memory-safety problem once the bounds hold, but they let one lap's nodes
   masquerade as another's and no recorder produces them.
9. For every lap: `timestamps[0]` is 0 and the array is non-decreasing.
10. `levelId` matches the level being loaded. This one is the caller's check
    rather than the parser's, because the parser does not know what level is
    loading, but no lap may be injected without it.
11. For every lap, `goBackCount` is not identical across all of its nodes. See
    "What a reader must force": this is the one killplane field a reader cannot
    repair, so it is a rejection.

Order matters for one pair: the hash check must run before any offset is
followed, so a corrupted offset is never dereferenced.

## What a reader must force

Validation is not enough for three fields. A container can arrive from anyone:
handed over chat, downloaded, or edited by hand. Most of a node is geometry, and
bad geometry drives a bot badly, which is a quality problem. These three are
different, because a hostile or careless value does not degrade the game, it
stops it. A reader must therefore FORCE them rather than reject on them, so that
a merely clumsy file still plays:

- `pathChangeOpcode` is set to the 0x7FFF sentinel on every node, whatever the
  file said. A file carrying 0 would otherwise reinstate the start-line teleport
  on the first overtake.
- `flags` bit 0x4000 is cleared on every node. A file setting it everywhere would
  otherwise leave `BOTS_Killplane`'s rewind loop no exit.
- `driverName` is put through the same sanitizer the writer uses (printable
  ASCII, trimmed, capped) before anything draws or logs it.

`goBackCount` is the fourth case, and it is a REJECTION rather than a force.

It cannot be forced, because the rewind loop's requirement is that the value
VARY along the lane, and every constant a reader might substitute is precisely
the failure being guarded against. It cannot be waved through either: a shared
file carrying a constant `goBackCount` hangs the game exactly as an all-0x4000
lane does, and the previous revision of this recorder emitted one, so files like
that can exist in the wild.

So a reader must reject any lap whose `goBackCount` is identical across all of
its nodes. One differing node is enough to accept, because one differing node is
enough for the loop to terminate; the test is the loop's precondition, not a
judgement about how good the data is. A lap whose `goBackCount` varies but is
otherwise nonsense produces bad rewinds rather than a hang, and that is a quality
problem, which is where a reader's responsibility stops.

## Driver name

`driverName` is 32 bytes. At most 24 characters are ever written into it, so
there are always at least 8 NUL bytes. The remaining bytes are reserved for a
later cap increase without a format change.

Sanitizing, applied by the writer before the name reaches the file:

- Only printable ASCII, 0x20 to 0x7E, is kept. Every other byte is dropped, not
  substituted, so a name cannot smuggle control characters, a newline or a NUL
  into a file that will later be shown next to a kart.
- Leading and trailing spaces are dropped.
- The result is truncated to 24 characters.
- If nothing survives, the field is all-NUL and the file records an anonymous
  lap. That is a valid file.

The cap is 24 because Archipelago slot names are at most 16 characters, which is
the default source of this value, and 24 leaves headroom for a recorder who is
not connected to a seed while still fitting the options row and the future
overhead kart label at the small font without overlapping the row's own label.

The name comes from the `nav_driver_name` option. When that option is empty the
recorder falls back to the configured Archipelago slot name (`g_config.slot`),
sanitized identically. Empty option plus empty slot means an anonymous file.

The option is `CFG_STRING`. The generic options-section renderer had no
`CFG_STRING` case at all, which is why `native_config.h` notes that the
last-seen-version string is config-file-only. Adding a read-only display case to
`Config_DrawValue` turned out to be five lines and no new input handling,
because the menu's edit paths are already keyed on type: cross toggles only
`CFG_BOOL`, left and right step only `CFG_ENUM`, and the slider loop only
touches `CFG_INT`. So for 0.2.0 the row is drawn on the Authoring section and
shows the effective name, and the value is edited in `config.ini` under
`[Authoring] nav_driver_name`. An in-game text-entry widget is separate UI work
and does not gate collection.

## Shortcut classification

`shortcutFlag` is decided at record time, per lap, from the level's own retail
nav table. The recording client is the only place with both the driven line and
the retail line in memory at once, so deciding it later would mean shipping the
retail line to whoever validates the lap.

The test:

- The recorder snapshots the XZ position of every node of every lane of
  `level1->LevNavTable` when it first arms on a level. That table is the LEV's
  own data and is never written by the injection path, which only replaces the
  pointers in `sdata`, so the snapshot is the retail line even in a session that
  is also playing recorded laps back.
- After decimation, each of the lap's nodes is tested against the snapshot. The
  node is inside the corridor when its XZ distance to the nearest retail node of
  any lane is at most `AP_NAVREC_SHORTCUT_CORRIDOR_UNITS`.
- A run of consecutive nodes outside the corridor that reaches
  `AP_NAVREC_SHORTCUT_MIN_RUN_NODES` marks the lap `shortcut` = 1. Otherwise the
  lap is `shortcut` = 0.
- If the level has no retail nav table, the lap is `shortcut` = 2, unknown. It
  is never 0. A custom track has no reference line, so "we did not detect a
  shortcut" and "there is nothing to detect against" are different facts and the
  format keeps them different.

Both thresholds are compile-time constants in `ap/ap_navrec_format.h`.

`AP_NAVREC_SHORTCUT_CORRIDOR_UNITS` is 1200 `NavFrame` units. Retail lanes sit
about 500 units apart, and retail node spacing on Crash Cove reaches 499 units,
so a driver sitting exactly on a retail line but halfway between two of its
nodes already measures up to about 250 units from the nearest node. 1200 is
therefore roughly two lane widths outside the outermost lane plus that sampling
slack. A wide line through a corner does not reach it; leaving the road does.

`AP_NAVREC_SHORTCUT_MIN_RUN_NODES` is 8. Nodes are spaced by arc length, so 8 of
about 230 nodes is about 3.5 percent of the lap's driven distance, which is
something between one and two seconds. A single node outside the corridor is a
wall bump or a wide entry; eight in a row is a route the retail line does not
contain.

### Cost, and the bound on it

The naive test is quadratic: every decimated node against every retail node. A
three-lap write on a track with three retail lanes of about 230 nodes each is
3 x 230 x 690, about 476 thousand distance evaluations, and the whole write
happens inside one frame (see below). That is large enough to be worth bounding
rather than hoping about.

The corridor is therefore indexed with a uniform grid before any lap is tested.
Cell size is the larger of the corridor threshold and whatever makes the track's
own bounding box fit in 64 by 64 cells, and points are placed by a counting sort
so each cell's points are contiguous. Because a cell is never smaller than the
threshold, every point within the threshold of a query lies in the 3 by 3
neighbourhood of the query's own cell, so the query only ever reads those nine
cells.

The bound is: at most the number of retail nodes in nine cells per query node,
against at most 3072 retail nodes indexed in total. On a track whose nodes are
spread over more than a couple of cells this is a small constant; the worst case,
every retail node inside one cell, degenerates to the naive scan and is bounded
by it. The grid is an optimisation and nothing else, so the harness asserts it
returns the same answer as brute force at 4000 query points spread across and
well beyond the corridor's extent.

This is detection at record time only. Serving `shortcut` laps by seed tier is a
later release's work and is not implemented in the 0.2.0 slice.

## Lap selection, and what fewer than three laps means

A three-lap race yields three laps, which is the whole reason the container
holds up to three. The writer:

- considers only clean laps, in the order they were driven;
- sorts them by `lapFrames` ascending and keeps the fastest three;
- assigns `laneHint` 0, 1, 2 in that order;
- writes nothing at all, and logs why, when no clean lap is available.

Dirty laps are never written. A lap with a respawn or a blast is not a line
worth copying, and a container full of them would make the future submission
pipeline's job harder rather than easier.

On the playback side, when a file carries fewer than three laps the remaining
engine lanes are filled by the synthetic lane-offset generation the spike branch
already used: each missing lane is lane 0 displaced perpendicular to its own
direction of travel by a fixed lateral offset, alternating side. One recorded
lap therefore still yields three usable lanes, with two of them offset copies.

What the 0.2.0 reader does not keep from the spike branch is the cross-lap
median line and the clamped lateral envelope built from it. That machinery
averaged a corpus of laps into one line and then spread lanes across the width
humans had actually driven. It was the right answer when every lap was raw
material for one synthetic line. It is the wrong answer now: the ruling is that
each named bot drives one real person's real line, and a median destroys exactly
the thing the format exists to preserve. The perpendicular offset generation is
kept because it is the honest degradation for a one-lap or two-lap file; the
median is dropped because it would silently replace real lines with an average
of them.

## What makes a lap dirty

A dirty lap is never written by the 0.2.0 writer. The recorder marks the lap in
progress dirty when, on any sampled frame, the driver's `kartState` is one of:

- `KS_MASK_GRABBED`, which covers both a respawn and a mask reset;
- `KS_BLASTED`;
- `KS_SPINNING`;
- `KS_CRASHING`, a wall crash. This one zeroes the driver's speed, so the line
  through it is one no bot should be asked to follow.

It is also marked dirty if the lap runs past the recorder's per-lap sample cap,
because a truncated line is not a line. In that case frames keep being counted so
the recorded lap time stays true.

Firing the Mask item is deliberately NOT a dirty condition. It is a legitimate
way to drive a lap, and it is counted instead.

## Held-item counters

Each lap records two counts, in what were reserved directory bytes:
`maskFires` at directory offset 0x18 and `turboFires` at 0x1A, both u16 and both
saturating rather than wrapping.

They count the item being FIRED, taken from the one place a held item actually
fires (`VehPickupItem_ShootNow`), and only for the local player, since bots and
bosses fire through the same function. They are not counted from pickups: an item
picked up and never used says nothing about how the lap was driven.

Only these two items are recorded. The Mask makes a driver fast and briefly
untouchable and the Turbo makes them fast, so both change what a lap time means;
no other item is recorded, and the format has no room reserved for one.

## What the recorder writes to disk, and when

Nothing is written unless `nav_record` is on. That option defaults to off, is
re-read every tick rather than latched, and turning it off discards the banked
laps rather than holding them for a later write. With it off, the recorder does
not sample, does not allocate its sample bank, and never creates a directory or
a file.

### The trigger, and its cost

The write happens once, at `END_OF_RACE`, from inside the ordinary per-frame
tick. There is no key to press: a player who ticked the option and drove a race
has already said what they want, and a hidden key would be an authoring
affordance in a player-facing feature. A guard makes it fire once per race, and a
new race on the same track starts a fresh set of laps rather than blending two
races into one file.

Doing it inside the tick means one frame carries the whole cost:

- decimating up to three laps, each a walk over up to 4000 samples;
- the shortcut test for each, bounded by the grid described above;
- building the grid itself, if the level's corridor has not been indexed yet;
- one `malloc` of the serialized size, about 74 KiB at the largest;
- probing up to 999 filenames to find the next free number;
- one `fopen`, one `fwrite` of that buffer, one `fclose`.

This is a visible hitch, not a hang, and it lands at the moment the race ends and
the results presentation begins, which is the cheapest place in a race to spend
it. It has not been measured on hardware. Moving the write off the tick would
mean either a thread or a deferred queue, and both are worse trades for a
once-per-race cost at a moment when nothing is being driven.

### Where the files go

- Everything goes in one directory, `ap-navpaths/`, created on demand. A player
  who changes their mind deletes one folder rather than hunting loose files out
  of the game directory. If the directory cannot be created, the failure is
  logged naming the directory, and nothing is written.
- The path is relative to the WORKING DIRECTORY, which is where this client
  already keeps `config.ini` and `ap-config.txt`. It is deliberately the same
  base as those rather than "beside the executable": the two differ when the game
  is launched from elsewhere, and a player looking for their recordings should
  find them next to the config file they already know about.
- Files are NUMBERED and never overwritten:
  `ap-navpaths/navpath-<levelId>-<NNN>.navlap`, NNN zero-padded to three digits.
  The writer starts at one past the highest number it knows about and creates the
  file EXCLUSIVELY, stepping to the next number and retrying if that one is
  already taken.

  The exclusive create is what actually keeps the promise, and it has to be the
  create rather than a check. Opening for write truncates whatever is there, and
  a separate existence probe leaves a window between the two. It also closes the
  probe's own blind spot: a file that exists but cannot be opened for reading,
  because it is locked or unreadable, looks absent to a probe, and its number
  would then be handed to the next recording. Here the filesystem decides.
- The cap is 999 recordings per level. With every number taken the writer refuses
  and says so rather than overwriting anything.
- One client has one driver name, so per track is also per driver.
- The reader, under `nav_use_recorded`, loads the NEWEST recording for the level,
  meaning the highest number present. The whole range is probed rather than
  stopping at the first gap, so deleting a file out of the middle does not hide
  everything above it. The result is cached per level, so a savestate restore
  does not repeat the scan; the cache is refreshed when the level changes or the
  recorder writes a file, and can be stale only for a file dropped into the
  folder mid-session.
- If the newest recording is REJECTED, the reader falls back to the next lower
  number, and keeps descending until one loads or it has tried eight. Each
  rejection is logged with its reason. This does not weaken the whole-file
  rejection rule: a rejected file is still rejected entirely, and falling back is
  choosing a different file rather than salvaging a bad one. Without it, one
  corrupt newest recording would hide every good older one, and under a scheme
  that never overwrites, those older ones are exactly what the player still has.
- The extension is `.navlap`, so a leftover version 1 file can never be handed to
  a version 2 reader by name.

Every write is announced in the log with the exact path, the lap count, the byte
count, the driver name, and each lap's node count, frame count, item counts and
shortcut verdict.

Collection in 0.2.0 is manual hand-in. The client does not upload anything, and
nothing in this format assumes it will.
