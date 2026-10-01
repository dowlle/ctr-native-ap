# How custom tracks load in this fork

This is for CTR Native modders who want to know how this fork
(`dowlle/ctr-native-ap`) loads community tracks: what a package is, where the
files live, which engine functions the loading path goes through, and what
memory it needs. References are `file: function` in this repository. The
freestanding headers named below carry the full reasoning in their comments,
and most decisions have a host harness under `tools/test-custom-*`.

There are two ways a custom track reaches the engine. Both end in the same
byte-serving hook (section 6).

- **Packages (offline).** A verified, immutable folder per track revision,
  installed from Project Saphi or by hand, and raced from extra pages on the
  normal Arcade and Time Trial track selector. This is what the authoring
  client and the Archipelago box authoring download use, and what most of this
  document describes.
- **Seed-bound tracks (Archipelago player client only).** An Archipelago seed
  names a track by its LEV and VRM SHA-256 and the Gem Cup it replaces;
  `config.ini [CustomTracks]` says where the two files are.
  `platform/native_custom_tracks.c: CustomTrack_ApplySeedDescriptor` hashes
  them and arms the loader. Without a seed this path never arms. It is
  summarised in section 12.

## 1. Builds and options

All options are in `CMakeLists.txt`.

| Option | What it adds | Needs |
|---|---|---|
| `CTR_CUSTOM_TRACKS` | The loader core: BIGFILE serving, the 8 MiB memory arena, render bounds for larger tracks | nothing |
| `CTR_CUSTOM_PACKAGES` | Package library, Track Manager, Arcade and Time Trial custom pages, offline loader, Saphi catalogue and install, custom music, custom Time Trial records, 1 MiB SPU memory | `CTR_CUSTOM_TRACKS` |
| `CTR_BOX_AUTHORING` | Box Author Mode, the in-game box placement editor | nothing |
| `CTR_AI_LAP_RECORDER` | The AI lap recorder (always present when `CTR_AP` is on) | nothing |
| `CTR_AUTHORING_CLIENT` | All four of the above, without Archipelago; exe `ctr_native_authoring`, window title `AUTHORING CLIENT (no Archipelago)` | `CTR_AP=OFF` |

With `CTR_AP=ON`, packages and Box Author Mode come only from
`CTR_AP_AUTHORING` (the box authoring download, `ctr_native_ap_authoring`).
The Archipelago player client never contains them.

Linux, from a checkout:

```
bash ap/vendor/fetch-deps.sh
cmake -S . -B build-authoring -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS='-m32 -msse' -DCMAKE_CXX_FLAGS=-m32 -DCTR_AUTHORING_CLIENT=ON
cmake --build build-authoring --parallel 2
```

The package code is C++17 and needs 32-bit OpenSSL and zlib (static
archives are preferred) and, on Linux, standalone asio. JSON is the pinned
nlohmann/json under `ap/vendor/json`, and Unicode handling is the vendored
utf8proc under `vendor/utf8proc`; `cmake/CustomPackageDeps.cmake` refuses to
configure if either differs from its pin. `fetch-deps.sh` fetches the pinned
header-only dependencies. Windows builds with MinGW32 (see
`tools/ci/build-clients.sh windows`) and uses WinHTTP for Saphi.

The engine is one unity translation unit (`main.c` includes
`game/game_unity.h` and the platform files), so every hook below is a guarded
block inside an existing function, not a separate module boundary.

## 2. Where files live

Everything is relative to the assets folder next to the executable
(`platform/native_assets.c: NativeAssets_GetAssetDir`) or to the working
directory, the same place as `config.ini`.

| Path | Written by | Contents |
|---|---|---|
| `assets/tracks/packages/<pin>/` | Saphi install, inbox import | One installed package revision. Never modified after install |
| `assets/tracks/packages/.pending-<pin>-…` | an install in progress | Staging; a failed one is kept for review and never treated as installed |
| `assets/tracks/inbox/<pin>/` | you | A package to import by hand |
| `assets/tracks/library.catalog` | an external tool, optional | Metadata-only cache (`CTRLB001`), never treated as installed or verified |
| `custom-records/` | Time Trial on a custom track | Best times and ghost per track identity (section 9) |
| `ap-box-placements-authoring.json` | Box Author Mode | Placements on the 18 retail tracks and 7 arenas |
| `ap-box-placements-custom-authoring.json` | Box Author Mode | Placements on custom tracks, keyed by package identity |
| `ap-navpaths/navpath-<level>-<nnn>.navlap` | AI lap recorder | Recorded laps |
| `ctr-authoring.log` | authoring client | Log of the authoring modules (the Archipelago builds write `ctr-ap.log`) |
| `config.ini` | Options menu | `[Authoring]` rows, and `[CustomTracks]` paths for the seed-bound path |

## 3. The package format

A package is a directory named by its **pin**, holding `manifest.json` and the
files it lists. The parser is `platform/native_custom_package.cpp:
CustomPackage_ParseManifest`.

```json
{
  "schema_version": 1,
  "package_uuid": "8-4-4-4-12 lowercase hex, not all zeros",
  "version": "the author's version label",
  "title": "1..160 Unicode codepoints, NFC, no control characters or edge whitespace",
  "compatibility": {"native_contract": 1, "apworld_contract": 1},
  "files": [
    {"role": "lev", "path": "original/lev-123.lev", "sha256": "…", "bytes": 2470836},
    {"role": "vrm", "path": "original/vrm-124.vrm", "sha256": "…", "bytes": 1048576}
  ]
}
```

Rules the parser enforces:

- Exactly these keys, no duplicates, every integer in range.
- Roles and size limits: `lev` (16 MiB), `vrm` (4 MiB), `navigation` (16 MiB),
  `ap_boxes` (4 MiB), `ctr_letters` (1 MiB), `relic_targets` (1 MiB),
  `presentation` (4 MiB), `race_settings` (4096 bytes), `sca` (1 MiB). Each role
  at most once; `lev` and `vrm` are required; 2 to 32 files.
- Paths are relative, compared case-insensitively, and may not collide or nest
  inside each other. Symlinks, reparse points and non-regular files are refused
  when a directory is read (`platform/native_custom_package_files.cpp:
  CustomPackage_AcquireDirectory`).
- Every file's SHA-256 and byte count must match.

**The pin.** The manifest is re-serialised canonically (nlohmann/json compact
dump, keys in sorted order, `files` sorted by role then path) and hashed with
SHA-256. That 64-character lowercase hex digest is the package's pin and its
directory name. A package is only ever asked for by pin, and a pin that does
not match the manifest refuses the whole package. Changing any byte of any
file, the title or the version gives a new pin, so an installed revision is
immutable.

**Identity.** Three different identities are used for three jobs:

- The pin: which exact revision is installed and raced.
- The package UUID plus the LEV and VRM SHA-256: which track is on screen. Box
  placements, Time Trial records and savestate checks use this, so a new
  revision with different geometry never inherits old placements or ghosts.
- `CTR_CUSTOM_LEVEL_ID` (`0x80`, `include/platform/native_custom_identity.h`):
  the level id the engine logs and tests during a custom race, above all 65
  retail level ids. The track never borrows a retail track's identity.

**Sidecars.** `race_settings` is a flat JSON object
`{"schema_version": 1, "lev_sha256", "vrm_sha256", "laps"}` bound to the
package's own LEV and VRM; without it a race runs 3 laps, and more than 7 is
refused (`CustomPackage_GetRaceLaps`, `CustomIdentity_RaceLaps`).
`relic_targets` carries sapphire, gold and platinum times in 1/960 s
(`CustomPackage_GetRelicTargets`). `presentation` holds display metadata such
as `metadata/saphi-source.json`. `sca` is the track's music (section 10).

**Structural checks.** Before a package can race, the LEV and VRM are checked
structurally from the owned bytes (`platform/native_custom_content_verify.c`):
an Arcade race needs detected Arcade structures and eight measured spawns, a
Time Trial needs a lap checkpoint graph
(`platform/native_custom_offline.c: CustomOffline_CheckStructure`). A track
that cannot run in a mode is listed dimmed with the reason. Passing these
checks is not a promise that the track plays well; it only rules out what is
known to crash or misbehave.

## 4. Track Manager and the library

`Options > Custom Content` opens the Track Manager
(`game/230/MM_CustomManager.c`, included by `game/230/MM_ConfigMenu.c:
MM_ConfigProc_CustomContent`). It has a Saphi browse list and an Installed
list, with search on R2.

- **Store scan.** `CustomPackage_StartStoreScan` walks
  `assets/tracks/packages/` on a worker thread, re-acquires and re-verifies
  every directory whose name is a lowercase 64-hex pin, and ignores staging and
  unrelated names. Damaged entries stay visible with their reason. The result
  replaces the library in one step (`platform/native_custom_track_library.c:
  CustomTrackLibrary_ApplyStore`).
- **Library.** `struct CustomTrackLibrary` is a UI projection only: title,
  version, author, supported modes and the reason a mode is refused. It never
  authorises a load; the offline loader re-acquires the pinned directory
  itself.
- **Inbox.** A package copied to `assets/tracks/inbox/<pin>/` is verified and
  then published to `packages/<pin>/` (`CustomPackage_InstallInbox`). An
  install never overwrites an existing revision; it re-verifies it instead.

## 5. Project Saphi catalogue and install

The Saphi code is in `ap/ap_custom_track_download.cpp` (it sits under `ap/`
for history; it has no Archipelago dependency) and
`platform/native_saphi_catalogue.cpp`.

- **Refresh.** `CustomSaphi_StartRefresh` pages through
  `https://www.projectsaphi.com/api/v3/tracks?per_page=100&include_standards=0&include_events=0&include_downloads=1&page=N`
  on a worker thread. `CustomSaphi_ParseCatalogue` builds one row per track
  version and requires exactly one LEV and one VRM per version and mode
  signature; ambiguous rows stay visible but disabled. HTTPS only: WinHTTP on
  Windows, asio with OpenSSL on Linux, which looks for the system CA file
  itself because a static OpenSSL carries the build host's default paths.
- **Install.** `CustomSaphi_StartInstall` downloads the selected revision's
  LEV, VRM and, when Saphi lists exactly one current one, its `.sca`, from
  `/api/v3/tracks/<id>/downloads/<media id>` (8 MiB cap per file). Each
  download must match the byte count and CRC32 the catalogue listed. The
  installer then builds a package: SHA-256 for every file, a
  `presentation` file `metadata/saphi-source.json` recording track id, media
  ids, version and author, a `race_settings` file when the track lists a lap
  count (current revisions only), and a manifest whose UUID is a UUIDv5 of
  `https://www.projectsaphi.com/tracks/<track id>`, so every revision of one
  Saphi track shares a UUID. The package goes through the same acquisition and
  structural checks as any other and is published under its pin.
- Older versions of a track can be listed and installed side by side; the
  manager marks a newer Saphi version as "Update available" and never replaces
  an installed revision.

## 6. Racing a package: the loading path

Custom tracks race only as a one-player Arcade single race or a one-kart Time
Trial. The package's LEV and VRM are served through the BIGFILE subfile group
of one arcade slot, the **host slot** (level 6, Roo's Tubes,
`game/230/MM_CustomTrackSelect.c: MM_CUSTOM_ARCADE_HOST_LEVEL`). The BIGFILE
index cannot grow and every race-mode rule in the engine is a range test on
`levelID`, so borrowing a slot is the loading mechanism. Everything
identity-related goes through `CTR_CUSTOM_LEVEL_ID` instead (section 3).

In order:

1. **Pick.** `game/230/MM_TrackSelect.c: MM_TrackSelect_Init,
   MM_TrackSelect_MenuProc` add custom pages after the retail page
   (`game/230/MM_CustomTrackSelect.c`). Left and right change pages.
2. **Prepare.** The first Cross starts `CustomOffline_StartPrepare`
   (`platform/native_custom_offline.c`): a worker re-acquires
   `packages/<pin>/` into memory and verifies it. The game never reads the
   package directory again after this, so a file changed on disk mid-race
   cannot reach the engine. `CustomOffline_CheckStructure` then checks the
   mode.
3. **Begin.** The second Cross calls `CustomOffline_BeginRuntime` with the host
   slot and mode, sets the lap count from the package, and starts the normal
   race load for the host slot. In the Archipelago box authoring download this
   is refused while a seed is loaded (`AP_CustomOfflineLaunchAllowed`); in the
   authoring client it is always allowed (`ap/ap_authoring_host.c`).
4. **Load request.** `game/MAIN/MainRaceTrack.c: MainRaceTrack_RequestLoad`
   tells the runtime a load was requested; a retry of the host slot keeps it.
5. **Load start.** `MainRaceTrack_StartLoad` refuses the load (back to the
   main menu, with a log line) if the mode or files do not fit, applies the
   package's laps, and ends the custom runtime if the level being loaded is
   not the host slot. The custom identity therefore lasts exactly as long as
   the custom geometry is resident.
6. **Bytes.** `game/LOAD/LOAD_File.c: LOAD_ReadFile_ex` asks
   `MainRaceTrack_OfflineRuntimeFile` for every subfile read. For the host
   slot's group it returns the VRM for even and the LEV for odd subfiles, takes
   the size from the package, and fills the sector-rounded buffer with
   `CustomOffline_ReadRuntimeFile`, zero-padding the tail and raising the same
   completion callback a CD read would. A track's single LEV and VRM pair is
   served for all four mode pairs of the group
   (`include/platform/native_custom_tracks_policy.h`, decision 1). There is no
   LEV parsing: the file's own pointer map goes through the normal
   `LOAD_RunPtrMap` fixup.
7. **Level setup.** `game/LOAD/LOAD_TenStages.c: LOAD_TenStages` names the
   level `custom` instead of the host slot's debug name.
   `game/MAIN/MainInit.c: MainInit_PrimMem` raises the per-frame primitive
   arena for a custom load, and `MainInit_JitPoolsNew` reserves the LEV's own
   instance count on top of the retail instance pool
   (`ap/ap_instance_pool_logic.h`), because authored instances leave the free
   list without entering the taken list.
8. **Race.** Identity-keyed reads use the custom identity: AI difficulty is the
   median of the 18 retail rows (`CTR_CUSTOM_DIFFICULTY_PARAMS1/2`), the start
   banner shows the package title (`game/UI/UI_RaceFlow.c`), Roo's Tubes
   bubbles and ambience are off (`game/231/RB_Bubbles.c: RB_Bubbles_RoosTubes`,
   `game/HOWL/HOWL_LevelAudio.c: Level_AmbientSound`), and the high-score
   pointer is a private table (`game/GAMEPROG.c: GAMEPROG_GetPtrHighScoreTrack`).
9. **Finish.** `game/PlayLevel.c: PlayLevel_UpdateLapStats` reports the finish
   (`MainRaceTrack_OfflineRaceFinished`). Time Trial results go to
   `custom-records/` (`game/MAIN/MainFrame.c: MainFrame_GameLogic`,
   `game/MAIN/MainGameEnd.c`), never to the memory card.

## 7. Memory: what a custom track needs

Retail CTR fits a track into a PlayStation memory budget that community
tracks often exceed. `CTR_CUSTOM_TRACKS` changes these, and only these, for
every load in that build:

- **8 MiB MEMPACK arena.** The build sets
  `CTR_NATIVE_MEMPACK_RETAIL_PRESSURE=0`, which selects an 8 MiB arena in
  `platform/native_memory.c` instead of the retail-pressure window of
  `0x144e10` bytes (about 1.27 MiB) inside a 2 MiB store. The first event
  track's resident payload alone was 2,470,836 bytes. The allocator, the
  sector arithmetic in `LOAD_ReadFile_ex` and the pointer-map fixup were
  audited for 32-bit headroom (`tools/CUSTOM-TRACK-SPIKE.md`). Upstream
  `CTR-tools/ctr-native` removed this expanded arena in commit `56326417f`
  ("refactor(memory): enforce retail mempack budget"), so this loader does not
  work on upstream as it stands; a port would have to bring the arena back.
- **Primitive arena.** Retail sizes each frame's primitive arena by level id,
  as `u8 << 10`, at most 261,120 bytes. A custom load gets a floor above that
  (`game/MAIN/MainInit.c`, decision 8 in the policy header).
- **Bounded emitters.** `game/DrawSky.c: DrawSky_Piece, DrawSky_Full` and
  `game/RenderStars.c: RenderStars` take their counts from the level file and
  had no bound, so they now stop when the arena is full instead of writing
  past it (decision 7). `game/226/226_00_DrawLevelOvr1P.c:
  DrawLevelOvr1P_AppendRenderedQuadBlock` stops at the end of the 256-entry
  rendered-quadblock array (decision 9), and `game/RenderLevel/RenderLists.c:
  RenderLists_PushChild` counts dropped BSP records.
- **SPU memory** (packages only). `include/platform/native_spu_memory.h`
  doubles the emulated SPU RAM to 1 MiB so a track's own sound bank fits next
  to the retail banks. The engine's u16 SPU addresses switch from 8-byte to
  16-byte units; every retail bank lands at the same byte address.

Known side effects of the larger arena, both recorded in
`tools/CUSTOM-TRACK-SPIKE.md`: quads whose retail PSX address cannot be
reconstructed get a fallback z-sort index (visual only), and savestate pointer
registration slows down on very large LEVs.

## 8. Savestates

A savestate stores memory, VRAM and engine state, but not the offline
runtime. Each checkpoint therefore records which package was loaded (its UUID,
LEV and VRM SHA-256 and mode, `include/platform/native_custom_state_identity.h`)
and a restore is refused unless it matches the live one
(`platform/native_checkpoint.c: NativeCheckpoint_Capture,
NativeCheckpoint_Restore`). A refused restore shows a message in the race HUD.

## 9. Time Trial records

`platform/native_custom_records.c` keeps a custom track's best lap, five best
times and one ghost in `custom-records/<uuid>-<lev 16 hex>-<vrm 16 hex>-<laps>l.times`
and `.ghost`. Both files repeat the full identity and are refused if it
differs, so a new revision or lap count starts a fresh table.
The custom Time Trial page loads the ghost when the race starts
(`MainRaceTrack_CustomLoadGhost`), and `game/MAIN/MainGameEnd.c:
MainGameEnd_CheckTimeTrialGhost` saves a new one (`MainRaceTrack_CustomSaveGhost`).

## 10. Custom music

Saphi ships a track's audio as a separate `.sca` file: a container with a
retail level sound bank (`BANK`), a retail CSEQ song pack (`CSEQ`), per-sample
SPU sizes (`SIZE`) and display metadata (`META`). The layout was derived from
Saphi's files; there is no public specification
(`include/platform/native_custom_music.h`). During a custom race
`game/LOAD/LOAD_Howl.c: LOAD_HowlSectorChainStart` serves the level bank
sectors from the package (`game/HOWL/HOWL_CustomMusic.c:
HOWL_CustomMusic_ServeSectors`), and `game/HOWL/HOWL_Music.c:
Music_AsyncParseBanks` takes the level bank and song from the `.sca`
(`HOWL_CustomMusic_LoadBank`, `HOWL_CustomMusic_SetSong`). A missing or refused `.sca` keeps the
default: Crash Cove's song and sound bank.

## 11. Box Author Mode and the AI lap recorder

**Box Author Mode** (`ap/ap_author.c`) places Archipelago item box locations.
Turn it on at `Options > Authoring > Box Author Mode`, then race:

- Numpad 9 or Select drops a placement at the kart, Numpad 0 or Select + L1
  removes the newest one on this track, Numpad . or Select + R1 saves and lists
  them in the log.
- Retail tracks and arenas write `ap-box-placements-authoring.json`: one line
  per placement with `level_id`, `level`, `pos` and `rot_y`, in LEV InstDef
  units (signed 16-bit world units; `0x1000` is a full turn). It starts from the
  241 placements built into the client (`ap/ap_placements_data.h`).
- Custom tracks write `ap-box-placements-custom-authoring.json`
  (`ap/ap_author_custom.h`), keyed by package UUID plus LEV and VRM SHA-256.
- Markers use the level's weapon crate model, or the Archipelago logo mesh on
  levels without one (`ap/ap_marker_model.c`). Their height comes from the same
  crate measurement the Archipelago client uses (`ap/ap_box_measure.c`,
  `ap/ap_box_spawn_pos.c`), so a marker stands where the real box will.

Nothing in the authoring client reads these files back for gameplay. The
Archipelago client turns placement N on a track into that track's Nth item box
location; a fork can use the same files for its own purposes.
`docs/BOX_AUTHORING.md` covers the format and the editor in more detail.

**AI lap recorder** (`ap/ap_navrec.c`). `Save AI Lap Recordings` banks the
laps you drive and writes the best ones to `ap-navpaths/` when the race ends; `Use
Recorded AI Laps` hands recorded laps to the bots in place of the level's own
paths. The file format is in `tools/navrec/FORMAT.md`. Recording and playback
are blocked on custom-page races, because those have no recording identity
yet.

In the authoring client these modules run from
`ap/ap_authoring_host.c: Authoring_OnFrame`, called from
`game/MAIN/MainMain.c: main` in the same order the Archipelago client uses.

## 12. The seed-bound path (Archipelago only)

For completeness: in the Archipelago player client a seed's slot_data carries
a custom track descriptor (laps, host slot, the cup it replaces, the LEV and
VRM SHA-256). `ap/ap_hooks.c` passes it to `CustomTrack_ApplySeedDescriptor`,
which hashes the two files named in `config.ini [CustomTracks]` and arms
`CustomTrack_GetOverride`, the older sibling of the offline hook in
`LOAD_ReadFile_ex`. A refusal is total: no custom bytes and the cup keeps its
four retail legs. That path is not in the authoring client.

## 13. The authoring-client branch

`tools/authoring-branch/make-branch.py <release tag>` produces a branch that
is that release plus one commit: the Archipelago-only sources under `ap/` are
removed, the authoring client is the default build, and harnesses that need
the removed files are skipped. It is regenerated per release and never edited
by hand. `#ifdef CTR_AP` blocks remain in shared engine files; they compile to
nothing.

## 14. License

The code is GPL-3.0, the same as upstream CTR Native. If you copy this loader
into your fork, your fork's distribution must also follow GPL-3.0. The
Archipelago logo used as a fallback marker is CC BY-NC 4.0; see
`THIRD_PARTY_NOTICES.md` for the attributions that must stay with it.
