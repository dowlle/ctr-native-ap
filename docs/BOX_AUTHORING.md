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

1. the Archipelago-logo marker model (`STATIC_AP`, from #124). Preferred, but it
   is only built once `STATIC_GEM` has been seen, which is a hub model -- an
   arcade-only session may never have it;
2. the weapon box (`PU_RANDOM_CRATE`), which every arcade track's LEV carries.

Neither is the final AP box model. This is a stand-in so the spot can be judged.

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
