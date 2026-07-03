# CTR-AP Setup

This is a quickstart for playing CTR-AP, the Archipelago integration for the
native PC port of Crash Team Racing. Follow the steps in order.

## What you need first

CTR-AP ships no game data. You supply the game files from your own copy of the
disc. You will need:

1. A disc image of your own NTSC-U (North American) Crash Team Racing
   PlayStation disc. A `.cue` plus `.bin`, a single `.bin`, or a `.chd` all
   work. Only the North American release is supported right now. PAL (European)
   and Japanese discs are detected and refused.
2. Python 3.8 or newer, to run the asset extractor. (Only needed for the
   recommended setup below. If your image is a `.chd`, you also need the
   `chdman` tool on your PATH.)
3. The prebuilt game executable, `ctr_native_ap.exe`. Download it from the
   releases page.

## Step 1: get the game executable

Download `ctr_native_ap.exe` from the releases page and put it in a folder of
its own, for example a folder called `CTR-AP`. The rest of these steps add the
game data and your server settings next to it.

## Step 2: extract the game assets (recommended)

The extractor reads your disc image and copies out only the files the game
needs, into an `assets` folder. It also checks that your disc is the supported
region and that nothing is missing.

From the folder that holds `ctr_native_ap.exe`, run:

```
python extract_assets.py "path/to/your/CTR.cue"
```

You can also point it at a `.bin` or a `.chd`. By default it writes an `assets`
folder in the current directory. To send it somewhere else, add `--out`:

```
python extract_assets.py "path/to/your/CTR.bin" --out "path/to/CTR-AP/assets"
```

When it finishes you should have this layout:

```
CTR-AP/
  ctr_native_ap.exe
  assets/
    BIGFILE.BIG
    SOUNDS/KART.HWL
    TEST.STR
    XA/ENG.XNF
    XA/ENG/EXTRA/S00.XA ... S05.XA
    XA/ENG/GAME/S00.XA ... S20.XA
    XA/MUSIC/S00.XA ... S01.XA
```

### Alternative: one file, no extractor

If you would rather not run the extractor, the game can also read the raw disc
image directly. Rename your NTSC-U `.bin` to `ctr-u.bin` and place it in the
`assets` folder:

```
CTR-AP/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

This must be the common single-track raw PlayStation BIN layout (MODE2/2352
sectors). A cooked 2048-byte `.iso` does not carry the audio and video sector
data the game needs, so it will not work. This path skips the region check and
uses more disk space than the extracted folder, which is why the extractor is
recommended.

## Step 3: create your config file

Copy `ap-config.example.txt` to `ap-config.txt` in the same folder as
`ctr_native_ap.exe`, and open it in a text editor. Set at least:

- `uri`: the Archipelago server address (for example `archipelago.gg:38281`, or
  `ws://localhost:38281` for a server on your own machine).
- `slot`: your player name in the room, spelled exactly as it appears there.
- `password`: the room password, or leave it blank if there is none.

The other options (`skip_hints`, `map_flash`) are optional quality of life
toggles and can be left at their defaults. Every option is documented inline in
the example file.

## Step 4: run

Launch `ctr_native_ap.exe`. It connects to the server at startup using your
`ap-config.txt`, then boots the game.

## Troubleshooting

- "Missing or incomplete assets" at startup: the `assets` folder is not next to
  the executable, or a file did not extract. Re-run the extractor, and make sure
  the `assets` folder sits in the same directory as `ctr_native_ap.exe`.
- "PAL is not supported yet" from the extractor: your disc is the European
  release. You need the North American (NTSC-U) disc, whose boot id starts with
  SCUS.
- "This is a .chd image, which needs the chdman tool": install `chdman` (it
  ships with the MAME tools) and make sure it is on your PATH, or convert the
  `.chd` to `.bin`/`.cue` yourself and run the extractor on the `.cue`.
- "does not look like a PlayStation disc image": you likely pointed the tool at
  the wrong file (a zip, a folder, or a cooked `.iso`). Use the raw `.bin`,
  `.cue`, or `.chd` of the disc.
- The game window opens but there is no music or the intro video is black: your
  image was probably a cooked 2048-byte `.iso`, which drops the XA and STR
  sector data. Re-dump or re-obtain the disc as a raw MODE2/2352 image.
- Cannot connect to the server: check the `uri`, `slot`, and `password` in
  `ap-config.txt`. This build uses a plain (unencrypted) WebSocket connection,
  so it is best suited to a local or self-hosted server.
