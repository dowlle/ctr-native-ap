# Custom-track loader spike (Phase 0)

This is the Artemis walkthrough for the custom-track loader. Goal: get one
community custom track (extracted from an xdelta-patched CTR ROM) to drive, with
AI, in adventure mode, replacing one retail track. Default test target: Dingo
Canyon (levelID 0).

## What the spike does

With `CTR_CUSTOM_TRACKS` compiled in and a `config.ini` mapping a track's
levelID to a folder of dumped subfiles, the game serves that folder's bytes for
the track's 8-subfile BIGFILE group instead of the bytes inside BIGFILE.BIG. It
is byte-for-byte group serving: no `.lev`/`.vrm` parsing. The dumped bytes come
from a patched ROM whose track already runs on this engine, so the existing
pointer-map fixup and VRAM upload downstream of the read are unchanged.

With the flag off, or with no `[CustomTracks]` config present, behaviour is
identical to the retail build.

## Prerequisites on Artemis

- The Windows/32-bit build toolchain you already use for `ctr_native_ap`.
- `xdelta3` on PATH.
- A community custom-track xdelta patch. Example: "Figure Ate Fries" or any
  single-track patch from GameBanana. These are distributed as an xdelta against
  a specific CTR disc image.
- Your own retail NTSC-U CTR disc image (`.bin`/`.cue`/`.chd`) -- the same one
  the patch was authored against. Custom-track patches are picky about the base
  image; use the base the patch page names.

## Step 1 -- apply the xdelta patch to your disc image

```
xdelta3 -d -s clean.bin patch.xdelta patched.bin
```

`clean.bin` is your retail image, `patch.xdelta` is the community patch,
`patched.bin` is the output. (If the patch page ships a `.cue`, keep the patched
`.bin` next to a matching `.cue`.)

## Step 2 -- extract BIGFILE.BIG from BOTH images

Run the existing asset extractor on the clean and the patched image separately,
into two different output folders:

```
python tools/extract-assets/extract_assets.py clean.bin   --out clean-assets
python tools/extract-assets/extract_assets.py patched.bin --out patched-assets
```

Each writes a full `assets` tree; you only need `BIGFILE.BIG` from each:
`clean-assets/BIGFILE.BIG` and `patched-assets/BIGFILE.BIG`.

Note: `extract_assets.py` reads the disc's ISO9660 file system and pulls
`BIGFILE.BIG` directly (it is a top-level file on the disc, see the tool's
`CORE_FILES`). It only accepts NTSC-U images.

## Step 3 -- find which track slot the patch replaced

```
python tools/extract_track.py diff clean-assets/BIGFILE.BIG patched-assets/BIGFILE.BIG
```

This prints the changed subfile indices grouped by arcade track. An old-style
single-track patch shows one track with all 8 subfiles changed, for example:

```
  levelID 0   Dingo Canyon      subfiles 0,1,2,3,4,5,6,7  (indices 0,1,2,3,4,5,6,7)
             -> full group replaced; dump with: extract_track.py dump <patched> 0 tracks/<name>
```

The `levelID` reported here is the slot the patch actually replaced. Remember it.

**Reality check (verified against Figure Ate Fries v2.0 on Artemis, 2026-07-14):
modern CrashTeamEditor-era patches do NOT replace an arcade group at all.** The
FAF v2.0 diff shows only 5 real changes, none in the arcade range: the custom
`.lev` and `.vrm` are stashed in the `BI_OVERLAYSECT1` slots (indices 221/222),
plus code edits in an overlay (230), a track-name edit in a language file (235),
and one shared-VRM tweak (258); the loader redirect lives in code the xdelta
patches outside BIGFILE. If the diff shows this pattern, skip Step 4's dump and
use the **raw-file route** below instead.

### Raw-file route (CTE patches)

CTE-era zips usually ship the raw `<track>.lev` and `<track>.vrm` next to the
xdelta (FAF v2.0 does). A retail arcade group is 4 mode-pairs of `(vrm, lev)` --
even subfiles 0/2/4/6 are VRMs (each exactly 458808 bytes; a CTE `.vrm` matches
that size exactly), odd subfiles 1/3/5/7 are the per-mode LEVs. Assemble the
group directly:

```
tracks/<name>/0.bin = <track>.vrm     tracks/<name>/1.bin = <track>.lev
tracks/<name>/2.bin = <track>.vrm     tracks/<name>/3.bin = <track>.lev
tracks/<name>/4.bin = <track>.vrm     tracks/<name>/5.bin = <track>.lev
tracks/<name>/6.bin = <track>.vrm     tracks/<name>/7.bin = <track>.lev
python tools/extract_track.py check tracks/<name>
```

(Using the same pair for all 4 modes is spike-grade: 1P adventure/arcade uses
pair 0/1. The per-mode retail LEVs differ, so 2P/4P/relic on a custom track are
untested territory.) Then continue at Step 5 with the levelID of whichever
retail slot you want the track to replace.

## Step 4 -- dump the patched group

Dump the replaced track's 8 subfiles from the PATCHED BIGFILE:

```
python tools/extract_track.py dump patched-assets/BIGFILE.BIG <levelID> tracks/<name>
python tools/extract_track.py check tracks/<name>
```

e.g. `dump patched-assets/BIGFILE.BIG 0 tracks/figure-ate-fries`. `check`
confirms all 8 `.bin` files are present and nonzero. `tracks/<name>` is
cwd-relative to where you run the game.

## Step 5 -- map it in config.ini

In the `config.ini` next to the executable, add:

```
[CustomTracks]
custom_track_<levelID> = tracks/<name>
```

**Primary (safest) mapping: map the group onto the SAME levelID the patch
replaced.** The subfile group bytes are slot-relative -- a track's models and
textures are self-contained, but any baked levelID references only line up when
the group sits in its original slot. So if the diff said levelID 0, use
`custom_track_0 = tracks/<name>` and drive Dingo Canyon. This is the byte-faithful
case and the one to try first.

Stretch goal (note, not the default): cross-slot mapping, e.g. dumping a group
the patch put in some other slot but mapping it onto `custom_track_0` to drive it
as Dingo Canyon. This may or may not work depending on baked slot references;
treat it as an experiment and try both ways if the same-slot case is not the slot
you want.

Keys are `custom_track_0` .. `custom_track_17` (arcade tracks only; levelID 18+
are battle arenas and are not handled). Missing keys, an empty value, or no
`[CustomTracks]` section all mean "no override". A mapped folder whose `.bin`
file is missing logs a loud warning and falls back to the BIGFILE.

## Step 6 -- build with the flag on

```
cmake -S . -B build-ct -DCTR_AP=ON -DCTR_CUSTOM_TRACKS=ON
cmake --build build-ct
```

(`-DCTR_AP=ON` keeps the AP build; the custom-track flag is independent and can
be combined with a clean build too.)

## Step 7 -- run and verify

Run the resulting executable from the folder that holds `config.ini`, `assets/`,
and `tracks/`. On startup the log prints the active overrides, e.g.:

```
[CustomTracks] levelID 0 -> "tracks/figure-ate-fries"
```

Warp to the mapped track's pad in adventure mode. When the track loads, the log
prints one `[CustomTracks] serving levelID 0 subfile N from ...` line per subfile
read.

**Success looks like:** the custom track geometry, textures, and layout load; you
can drive it; AI racers are present and drive it too.

**Failure looks like:** a crash or hang during load (a size/offset or pointer-map
mismatch), garbled geometry or textures (wrong bytes served), or the retail track
loading instead (check the log for a `WARNING: ... missing, falling back` line,
which means a `.bin` was not found at the mapped path).

## Caveats for the spike

- The overridden track keeps the RETAIL slot's identity for ghosts and saves
  (the slot's levelID is unchanged). This is harmless for the spike -- but do NOT
  save ghosts or times on the custom track, since they would be written under the
  retail track's identity.
- A custom track whose subfiles are much larger than retail could exhaust the
  MEMPACK arena at load. If a same-slot mapping crashes on load with an
  allocation failure, that is the likely cause.
