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
2. Python 3.8 or newer, only if you use the optional asset extractor below
   (it saves disk space and is the route for `.chd` images). If your image is
   a `.chd`, you also need the `chdman` tool on your PATH.
3. The prebuilt game executable, `ctr_native_ap.exe`. Download it from the
   releases page.

## Step 1: get the game executable

Download `ctr_native_ap.exe` from the releases page and put it in a folder of
its own, for example a folder called `CTR-AP`. The rest of these steps add the
game data and your server settings next to it.

## Step 2: drop in your disc image (recommended)

The game reads the raw disc image directly. Copy your NTSC-U `.bin`, rename
the copy to `ctr-u.bin`, and place it in an `assets` folder next to
`ctr_native_ap.exe`:

```
CTR-AP/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

That's it — no Python, no extraction step. The image must be the common
single-track raw PlayStation BIN layout (MODE2/2352 sectors). A cooked
2048-byte `.iso` does not carry the audio and video sector data the game
needs, so it will not work. This path does not check the disc region, so make
sure the image really is the North American (NTSC-U) release.

### Alternative: extract the assets

The extractor saves disk space (it copies out only the files the game needs)
and is the route for `.chd` images. It also checks that your disc is the
supported region and that nothing is missing. It needs Python 3.8+.

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

## Step 3: connect to your room

Launch `ctr_native_ap.exe`. On a first start with no saved connection the game
boots to the main menu and shows that it is not connected. Go to **OPTIONS →
Connection** and type your room details with your keyboard:

- **Server**: the address of your room, for example `archipelago.gg:38281`
  (the port is on your room page). Secure connections (`wss://`) are used
  automatically for hosted rooms; for a server on your own machine use
  `ws://localhost:38281` or just `localhost:38281`.
- **Slot**: your player name in the room, spelled exactly as it appears there.
- **Password**: the room password, or leave it blank if there is none.

Select **Connect**. The status line on the same screen shows the connection
state (Connecting… / Connected / an error message). Your settings are saved to
`config.ini` next to the executable, and the game reconnects automatically on
later launches.

### Alternative: config file

If you prefer a text file, copy `ap-config.example.txt` to `ap-config.txt` in
the same folder as `ctr_native_ap.exe` and set `uri`, `slot`, and `password`
there; the game reads it at startup. Values saved from the in-game Connection
screen (`config.ini`) take precedence when both exist. The example file also
documents a few optional quality of life toggles (`skip_hints`, `map_flash`),
which can equally be changed in the in-game options menu.

## Troubleshooting

- "Missing or incomplete assets" at startup: the `assets` folder is not next to
  the executable, the disc image inside it is not named `ctr-u.bin`, or a file
  did not extract. Make sure the `assets` folder sits in the same directory as
  `ctr_native_ap.exe` and holds either `ctr-u.bin` or a complete extracted set.
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
- Cannot connect to the server: check the server address, slot name, and
  password in **OPTIONS → Connection** (the status line there shows the error
  reason) — or in `ap-config.txt` if you use the config file instead. The slot
  name must match the room exactly. Settings saved from the in-game screen
  (`config.ini`) override `ap-config.txt`.
