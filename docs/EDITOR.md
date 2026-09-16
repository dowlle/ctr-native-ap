# CTR Native Editor development slice

This repository is the standalone CTR box-placement editor. It contains no Archipelago networking or seed logic. The editor uses your own retail game assets and community track files; neither is redistributed here.

## Current end-to-end workflow

Create a project after choosing the retail arcade host slot whose lifecycle the custom pair should use:

```sh
python3 -m levtool project-init \
  --lev /path/to/track.lev \
  --vrm /path/to/track.vrm \
  --host-slot 3 \
  --output /path/to/track.editor.json
```

Edit the new project's `metadata.creator`, title, content ID and version before publishing a manifest. Validate and launch it with:

```sh
python3 tools/run_editor_project.py /path/to/track.editor.json \
  --binary build-editor/ctr_native_editor \
  --hot-reload-output /path/to/track-authored.lev
```

The launcher verifies both source hashes before starting. With `--hot-reload-output`, `H` saves the project, exports through `levtool`, validates the result, writes a generation-stamped derived LEV, and relaunches the editor on it. Shift `H` rolls back to the preceding validated generation, eventually returning to the immutable source. A failed save, export, hash check, or validation is never loaded. `--export-derived` remains available for one export after an ordinary clean exit. Every output must differ from the source LEV. Agents do not launch Steam-backed runtime tests; the mapper launches the staged executable.

Use `--print-command` to validate and inspect the launch arguments without starting the client.

Inspect the complete project state and its source LEV reference graph without launching the client:

```sh
python3 -m levtool project-inspect /path/to/track.editor.json
```

The JSON report is versioned as `ctr-native-project-inspection` version 1. It includes pinned and actual source hashes, project validation, dirty and clean generations, authored-object counts, export state, and a nested `ctr-native-lev-inspection` report. The LEV report includes validation diagnostics, source identity, layout counts, model census, geometry, quadblock metadata, checkpoints, spawn groups, PVS and BSP list membership, and per-instance reference counts. `levtool diff` emits the versioned `ctr-native-lev-diff` semantic and byte-level comparison. Consumers should check both schema names and versions before reading fields. The contracts are documented under `schemas/`.

## Editor controls

- Mouse: look; left click places the preview; right click selects the hovered authored object. Shift right click toggles it in the multi-selection. `L` and `R` provide the same keyboard actions.
- `W/A/S/D`, `Q/E`: fly horizontally and vertically.
- Shift and Control: fast and slow movement. `[` and `]` adjust base camera speed.
- `1` through `4`: item crate, Wumpa crate, loose Wumpa fruit and AP candidate palettes.
- Tab: cycle selection. Arrow keys and Page Up or Page Down move the selection. `T` rotates it.
- `M`: switch the middle-mouse transform between translate and rotate. `X`, `Y` and `Z` choose its axis. Middle-mouse drag transforms every selected object.
- `V`: cycle the translation snap among 1, 16 and 64 world units. `N`: type an exact position or rotation value for the active axis, then Enter to apply or Escape to cancel.
- `G`: snap the primary selection to the current surface hit while preserving offsets within a multi-selection.
- `I`: cycle the controls, project-inspector and geometry-diagnostics HUD pages.
- `O`: cycle overlays off, diagnostic markers and wireframe. Green markers show the picked surface normal; magenta markers show the containing BSP leaf bounds. The geometry page reports picked quadblock, terrain, checkpoint, PVS instance count and predicted BSP hitbox group.
- Control `D`: duplicate. Delete: remove. Control `Z` and Control `Y`: undo and redo.
- Control `S`: atomically save the project sidecar.
- `H`: validated export and hot reload when launched with `--hot-reload-output`. Shift `H`: roll back one loaded generation.
- `F1`: switch between the editor camera and test drive, restoring the saved camera and HUD state on return.

The cyan marker is the placement preview. Hovered objects are orange and selected objects are yellow. AP candidates are visible working markers but are never written into the vanilla LEV instance graph.

## Headless tools

`python3 -m levtool --help` lists the commands. The important gates are:

- `inspect`, `validate`, `roundtrip`, `diff` and `fixup-check` for graph evidence;
- `project-inspect` for one stable JSON snapshot suitable for a project-inspector UI or automated evidence capture;
- `geometry-query LEV --point X Y Z` for conservative quadblock, PVS, BSP-hitbox, checkpoint and instance proximity evidence;
- `move-local` for a size-preserving supported-instance move that refuses a BSP region change;
- `project-export` for deterministic project-to-derived-LEV export;
- `manifest` for the AP/content feeder descriptor.

Portable additions clone a same-type in-track model and BSP hitbox exemplar. Export refuses a type that the source track cannot prove. The development writer currently adds each authored vanilla object to every unique PVS instance list, and to every overlapping BSP leaf-list group. That policy is conservative and deterministic, but it remains a measured development fallback until its runtime cost and visibility behavior pass mapper-launched tests.

## Current limits and safety state

- Linux i386 and Windows MinGW32 builds and headless validation pass. Steam-backed direct loading, detached-camera movement and sustained no-crash rendering passed on the preceding candidate. The current inspector, overlays, multi-selection, mouse transforms, numeric editing and hot-reload controls still await their mapper-launched runtime gate.
- Runtime editor markers are visual previews. Retail collection and regrowth are proven only after loading the derived LEV, not by the transient marker itself.
- Imported-object editing, cross-region moves and removal from an existing LEV are not yet exposed by the project UI.
- The source LEV and VRM are hash-pinned and never overwritten. Exports use a temporary sibling, flush it and atomically replace only the selected derived path.
- A project is autosaved after mutations. AP candidate IDs and ordinals remain explicit in the sidecar and manifest.

These limits are deliberate refusal boundaries, not silent compatibility claims.
# Retail track authoring

Launch `ctr_native_editor` from its dedicated Steam entry with **empty Launch
Options**. The executable finds `tools/run_editor_project.py` beside itself and
opens a track chooser. Select Slide Coliseum or Turbo Track and click Open track.
Python with Tk must be installed. The native executable stays Steam's target.

The chooser also lists the other sixteen race tracks. The initial acceptance
scope is Slide Coliseum and Turbo Track; other tracks still need their own
extraction and gameplay checks.

The editor reads its adjacent `assets/BIGFILE.BIG` or `assets/ctr-u.bin`, validates
the retail 1P pair, and creates an independent empty project beside the executable.
Existing projects reopen without rewriting their camera, candidates or ordinals.
Cached files are compared against a fresh extraction and SHA-256 hashes on each
selection. Changed sources, damaged caches and conflicting project identities
are refused. Original assets and existing projects are never overwritten by
the chooser. Retail objects remain visible in the track but are not imported
into the editable object layers.

The HUD shows the track name, LevelID and candidate count. Press **4** for AP
candidates, aim at the surface, then use **L / left mouse** to place and **Ctrl+S**
to save. These are candidate markers only. They have no letter assignment and
do not import any existing AP placement table. Closing the editor exports a
content-addressed coordinate receipt beside the sidecar, including track,
LevelID, stable zero-based ordinal, position, rotation and source hashes.

Headless preparation (never launches the editor):

```sh
python tools/run_editor_project.py --retail-track 16 --binary build-editor/ctr_native_editor --assets /path/to/assets --projects /path/to/projects --print-command
python tools/run_editor_project.py --retail-track 17 --binary build-editor/ctr_native_editor --assets /path/to/assets --projects /path/to/projects --print-command
python tools/run_editor_project.py /path/to/projects/slide-coliseum-ctr-letters.editor.json --coordinates-only
```

Preserve the `.editor.json` files, their `.log` files, and the `retail-cache`
directory together. Receipt filenames include the project hash so previous
exports survive later edits. A cancelled chooser does not start the game.
