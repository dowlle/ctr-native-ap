# Box placement author mode

Tooling for [#182](https://github.com/dowlle/ctr-native-ap/issues/182), which is
tooling for [#109](https://github.com/dowlle/ctr-native-ap/issues/109). Drive to
a spot, press a key, and the client records that spot as a candidate AP-box
placement and spawns a marker there, so authoring is place-look-adjust rather
than place-quit-rebuild-look.

Placements are recorded and drawn. They are **not** pickups: nothing here gives a
marker collision or fires a location check. That is the unsolved half of #109 and
is untouched.

## Turning it on

`OPTIONS -> Authoring -> Box Author Mode`. Off by default, and that toggle is the
gate: the keys below do nothing while it is off, so a release player who never
opens the menu can never reach them. The setting persists to `config.ini` under
`[Authoring] box_author`.

No Archipelago connection is needed. The mode is entirely local.

## Keys

| Key | What it does |
|---|---|
| Numpad 9 | Drop a placement at the kart's current position and facing |
| Numpad 0 | Delete the last placement on the current track |
| Numpad . | Write the file now and list this track's placements to the log |

The numpad continues where the trap keys (Numpad 1-6) and the Shortcutless dev
keys (Numpad 7-8) stop. None of these are in the gameplay input map, so pressing
one cannot disturb driving.

The file is rewritten on **every** change, not only on Numpad `.`, so an
authoring session that ends in a crash or an alt-F4 keeps its work. Numpad `.` is
there for the listing and for a deliberate flush.

Placements can be dropped on the 18 race tracks and the 7 battle arenas. Anywhere
else (hub, garage, menus) the drop is refused with a log line.

## Two tables, and which one wins

The shipped client carries the FINAL authored placement set compiled in, as
`ap/ap_placements_data.h`. That table is generated, never hand-edited, and it is
what a player gets with no extra files: install one executable and the boxes are
there.

`ap-box-placements.json` next to the executable overrides it wholesale.
Precedence is by existence, not by content: if the file opens, the file is the
table, even when it parses to zero placements. An operator who empties the file
means zero boxes, and silently resurrecting the compiled-in set behind them would
be the worse failure, so the zero case is logged loudly instead.

The override is never a merge. Slot assignment is positional, so a table that was
part file and part default would re-point names against both. For the same reason
the loader logs a NOTE whenever an override's total differs from the compiled-in
total: it cannot distinguish a deliberate custom layout from a truncated file, so
saying so is the only guard available.

Author mode is file-only. Dropping, deleting, listing and saving all act on the
external file and never on the embedded table, which is not editable by
construction. Switching the mode on with no file present exports the compiled-in
set into the file once and then edits that, so authoring still starts from the
shipped placements. Writing the file also flips the live source mid-session, so
the runtime half follows what is being authored without a restart.

The precedence rule itself lives in `ap/ap_placement_table.h`, freestanding and
exercised out of engine by `tools/test-box-map.c`, so the tested rule and the
shipped rule are the same code.

## The file

`ap-box-placements.json`, written next to the executable, alongside `ctr-ap.log`
and `ap-state.json`.

```json
{
  "format": "ctr-ap-box-placements",
  "version": 1,
  "client": "v0.1.5",
  "units": "pos is LEV InstDef world units (signed 16-bit); rot_y is an engine angle, 0x1000 = one full turn",
  "placements": [
    {"level_id": 3, "level": "CRASH_COVE", "pos": [123, -456, 7890], "rot_y": 2048}
  ]
}
```

`pos` is three signed 16-bit world coordinates -- exactly what a track's LEV
holds for a pickup, at offset 0x30 of a `struct InstDef`, and exactly what
`INSTANCE_LevInitAll` copies into an instance matrix. The kart's live position is
wider than that (`Driver.posCurr` is three s32), so the value is narrowed at the
moment of capture and the marker is spawned at the narrowed value. What is on
screen is what is in the file; there is no second number that changes when it is
saved.

`rot_y` is the kart's facing, in the same angle space `InstDef.rot` uses.

### The row is a ground anchor, and the crate is lifted off it

`pos` is where the KART was, and a kart's origin is its ground contact point, not
its centre: `VehBirth_TeleportSelf` spawns a racer at the quadblock hit height
with no vertical offset, and the swept driver collision is a sphere whose radius
(`COLL_MOVED_PLAYER_HIT_RADIUS`, 25) equals how far its centre sits above the
origin (`originToCenter`, 25), so the sphere's lowest point is the origin itself.
A crate model's origin, by contrast, is its centre. Copying the row straight into
the spawn matrix therefore buried the bottom half of every AP crate.

So the row stays exactly as authored, and the SPAWN lifts the model by its own
measured base offset -- the distance from its origin down to its lowest face, at
the header scale the level derives -- which puts the crate's bottom face on the
authored anchor. For the shipped cube at the retail-derived scale that lift is 54
world units, half its 108-unit rendered height.

`AP_BoxModel_SpawnPos` (`ap/ap_box_model.c`) is the only place this happens, and
runtime boxes and author-mode markers both call it, so **a box dropped in author
mode previews the height the runtime box will stand at**. X and Z are carried
through untouched. The collision side follows the same rule from the other
direction: every proximity test reads the spawned instance's own matrix, never
the row it was lifted off, so the visible crate and the earnable check cannot
diverge. The arithmetic is freestanding in `ap/ap_box_offset_logic.h`.

`level` is the `enum LevelID` name, for the benefit of whoever reads the file.
`level_id` is the authoritative field.

The reader is a line scanner, not a JSON parser: it looks for `"level_id"`,
`"pos"` and `"rot_y"` on each line. Reordering fields, reflowing whitespace and
editing values by hand all survive a reload. Restructuring the file into
something the writer would not have produced does not -- it will be read as
empty rather than misread.

## What the marker looks like

The first available of:

1. the weapon box (`PU_RANDOM_CRATE`), which every arcade track's LEV carries.
   This is the ruled #109 look, so the authoring preview and the real box are the
   same shape;
2. the Archipelago-logo marker model (`STATIC_AP`, from #124), as a fallback.

**The order matters and is not a style choice.** `STATIC_AP` used to be first,
and under author mode's many-instance usage it makes track floors disappear
(reported 2026-08-09, resolved 2026-08-10 by exactly this swap and confirmed
live). If it is ever promoted back to first, that bug is the reason not to.

## How the marker gets there

`ap/ap_spawn.c`, the additive model loader. The engine draws instances from two
lists every frame: the level's baked `InstDef` array, and the instance pool's
*taken* list, walked by `RenderBucket_QueueNonLevInstances`. `INSTANCE_LevInitAll`
takes its instances off the *free* list without linking them into *taken*, so
*taken* holds exactly the runtime-born instances -- the hub's warp-pad prizes,
pause gems and garage tops, and now these. Birthing through `INSTANCE_Birth3D`
and writing the instance matrix is the whole mechanism. No level asset is
touched.

Every level load and every race restart re-inits the instance pool, which voids
every spawned instance. The loader is told at that point (`MainInit_JitPools`)
and rebuilds the marker set, which is why a restart does not leave you with an
empty track or with markers pointing at freed memory.

---

# The runtime half (#109)

Author mode records placements and draws markers. `ap/ap_boxes.c` turns those
same placements into real, breakable AP boxes during a race. The two never run at
once: while author mode is on, the runtime boxes stand down and the markers are
the set, which is how "author mode always shows the full authored set regardless
of seed state" is implemented.

## What a box does

| | |
|---|---|
| Appears | on the 18 box tracks, in ADVENTURE races only (trophy, relic, token, crystal, boss). Arcade / VS / battle carry no boxes: a box reached without its track's warp pad would be outside its own logic. |
| Breaks for | the local player only. AI drive through with no reaction. |
| On break | the vanilla crate shatter plays, the location check is sent, and the box is gone. |
| Gives | nothing else. No weapon roll, no wumpa, no relic time. The check and the item-feed line are the whole effect. |
| Stays gone | for the rest of the seed, including across a reconnect or a relaunch, because the spawn set is rebuilt from AP checked-location state rather than from anything the client remembers. |
| Blocks | nothing. You drive through a box, you do not bounce off it. |

## How collision works without a BSP hitbox

Vanilla pickups are collided through the level's baked BSP: the player sweep
reaches a hitbox node's `InstDef` and only then the model's `LInC` callback
(`CollMoved_PlayerSearch_RunHitboxLInC`, `game/COLL.c`). A runtime-born instance
has neither, which is what made "give an authored box collision" look like BSP
surgery.

The engine has a second path. Runtime objects that are not in the BSP collide by
a per-frame proximity walk over a thread bucket: `LinkedCollide_Radius`
(`game/LinkedCollide.c`) is how the seal, the spider and the minecart find
drivers. Boxes use that same call, against `threadBuckets[PLAYER]` only. Human
drivers are born into `PLAYER` and AI drivers into `ROBOT`, so the AI
pass-through rule costs no code at all: bots are simply never tested.

## Relic races

Boxes appear in relic races with no engine change. The weapon-crate strip in
`INSTANCE_LevInitAll` walks the level's `InstDef` array and clears
`DRAW_COLLISION_MASK` on weapon and fruit crates in relic and time-trial modes;
boxes are born through `INSTANCE_Birth3D` and are never in that array, so the
strip cannot reach them. That same loop is the only place `timeCratesInLEV` is
incremented, and nothing in the box path touches it, so the relic clock economy
is untouched.

## Names and codes

The location class is `item_boxes`: 270 names, `"<Track>: Item Box N"` with N
from 1 to 15, over 18 tracks. Slot assignment is positional and unconditional --
the Nth placement listed for a track is slot N whether or not it spawns -- so a
box that is absent from the seed or already checked never re-points the ones
after it. Placements past the 15th on a track are dropped and logged; they have
no name to send.

The engine `LevelID` to apworld track mapping is derived from the shipped
Sapphire Time Trial location block rather than restated, so it cannot drift from
the apworld's canonical 18-track order.

## Testing it without a disc

`tools/test-box-map.c` compiles the real bookkeeping out of engine:

    cc -Wall -Wextra -DCTR_AP -o /tmp/test-box-map tools/test-box-map.c && /tmp/test-box-map

Exit 0 means every assertion held. It links nothing from the game, so it needs no
disc, no display and no seed. What it pins:

- the `LevelID` to apworld-track derivation and the frozen 270-code block,
- slot assignment: positional, and never shifted by an absent or checked box,
- the compiled-in table: its 241 rows, its per-track counts against the
  provenance block in `ap_placements_data.h`, that no track exceeds the 15-slot
  ceiling, and that the row order is not silently sorted,
- precedence: the file wins by existence, an empty file still wins, and the
  override is wholesale,
- the spawn rule: a slot stands exactly the box locations its own seed created,
  and a seed with none stands zero,
- the pad predicate: how many boxes are still standing behind a track, which is
  what stops a pad locking over uncollected boxes,
- the scout-list filter: only codes this world created may go on the wire, which
  is the precondition a peer-bound box's feed line depends on.

Two more harnesses cover the spawn height:

    cc -Wall -Wextra -DCTR_AP -o /tmp/test-box-offset tools/test-box-offset.c && /tmp/test-box-offset
    python3 tools/test-box-anchor-premise.py

`test-box-offset` measures the SHIPPED cube out of `ap_box_model_data.h` with the
shipped walk and pins the numbers the correction rests on: 36 vertices, the
per-axis byte ranges, the retail-derived scale `0x910`, the 108-unit rendered
height, and the 54-unit lift -- including that a retail crate centred the same
way needs the identical lift, and that the pre-correction behaviour (lift 0)
fails the centred-origin invariant.

`test-box-anchor-premise` checks the two ENGINE claims no C harness can reach,
against the decompiled sources themselves: that `Driver.posCurr` is still the
kart's ground contact point, and that one model unit is still `headerScale/0x1000`
world units. A decomp correction that moves either one fails this check instead
of silently making every AP crate float or sink.
