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
