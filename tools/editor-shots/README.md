# Editor shots

`shot.py` renders headless PNG screenshots of retail CTR tracks using the
box-placement editor, with AP item boxes drawn in place. No display, no GPU
and no sudo are required: it runs the editor binary through Mesa llvmpipe
and SDL's offscreen video driver.

## Prerequisites

- A built headless editor binary. See "Building the binary" below.
- The retail disc image (`ctr-u.bin`), placed or linked where the binary's
  assets directory expects it (`$CTR_EDITOR_ASSETS/ctr-u.bin`, default
  beside the binary).
- Python 3 with Pillow (`pip install pillow`) for image handling.
- This source tree, so `tools/run_editor_project.py` and the `levtool`
  module can extract and inspect retail LEV/VRM data (cached after first
  use per track).

Never commit extracted retail assets (LEV/VRM/BIN files or their derived
JSON caches) to this repository or to any vault. They are retail-derived
and stay local-only.

## Building the binary

Configure a 32-bit editor build with headless support enabled, then copy
the binary next to where you plan to run `shot.py` from (default
`$CTR_EDITOR_ROOT/bin/ctr_native_editor`, see "Paths" below):

```
cmake -S . -B build-editor-headless -DCMAKE_BUILD_TYPE=Release \
  -DCTR_EDITOR=ON -DCTR_EDITOR_HEADLESS=ON -DCTR_AP=OFF -DCTR_CUSTOM_TRACKS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_C_FLAGS="-m32 -msse" -DCMAKE_CXX_FLAGS="-m32" \
  -DCMAKE_LIBRARY_ARCHITECTURE=i386-linux-gnu -DCMAKE_PREFIX_PATH=/usr/lib/i386-linux-gnu
nice -n 15 cmake --build build-editor-headless -j2
cp build-editor-headless/ctr_native_editor "$CTR_EDITOR_ROOT/bin/"
```

On memory-constrained hosts, keep parallelism low (`-j2` or less) and run
the build under `nice`.

`CTR_EDITOR_HEADLESS` only takes effect when `CTR_EDITOR=ON` is also set;
it stops CMake forcing SDL's offscreen video driver off. It is off by
default, so a normal (non-headless) editor build is unaffected.

## Paths

All paths are overridable by environment variable; every default works
without editing the script:

| Variable | Default | Meaning |
|---|---|---|
| `CTR_EDITOR_SOURCE` | this script's own repository | editor source tree, for `tools/run_editor_project.py` and `levtool` |
| `CTR_EDITOR_ROOT` | `~/ctr-editor-shots` | working directory for the binary, the extracted-track cache and output |
| `CTR_EDITOR_BINARY` | `$CTR_EDITOR_ROOT/bin/ctr_native_editor` | the built editor binary |
| `CTR_EDITOR_ASSETS` | beside the binary (`bin/assets/`) | retail assets directory containing `ctr-u.bin` |
| `CTR_EDITOR_PLACEMENTS` | `$CTR_EDITOR_SOURCE/ap/ap_placements_data.h` | compiled-in AP item box placement table, for `--box N` |

The extracted-track cache lives at `$CTR_EDITOR_ROOT/projects/` and is
created on first use of each track; delete it to force re-extraction.
Output PNGs go to `$CTR_EDITOR_ROOT/out/` unless `--out` is given.

You can keep a stable entry point outside the repo (for example
`~/ctr-editor-shots/shot.py`) as a symlink to this file, so agent commands
and shell history do not need to change across repo updates:

```
ln -s /path/to/this/checkout/tools/editor-shots/shot.py ~/ctr-editor-shots/shot.py
```

## Usage

```
shot.py --track "Mystery Caves" --box 13
shot.py --track 9 --eye -8200,60,4200 --look -7900,250,5900 --name my-view
shot.py --track crash-cove --eye -3450,949,5020 --look -3542,599,3423 --marker -3542,599,3423
```

Run `shot.py --help` for the full flag reference. See the vault reference
note "CTR Archipelago — Headless Editor Guide (reference)" (if you have
access to the project vault) for prerequisites, recipes, how to read the
pictures, and known limits.
