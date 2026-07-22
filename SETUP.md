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
2. The prebuilt game executable, `ctr_native_ap.exe`. Download it from the
   [releases page](https://github.com/dowlle/ctr-native-ap/releases).

That is the whole list. You do not need Python, and you do not need to extract
anything.

## Step 1: get the game executable

Download `ctr_native_ap.exe` from the
[releases page](https://github.com/dowlle/ctr-native-ap/releases) and put it
in a folder of its own, for example a folder called `CTR-AP`. The rest of
these steps add the game data and your server settings next to it.

## Step 2: drop in your disc image

The game reads the raw disc image directly. Copy your NTSC-U `.bin` into an
`assets` folder next to `ctr_native_ap.exe` (launching the game once creates
that folder for you):

```
CTR-AP/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

The filename does not matter: the game scans the `.bin` files in `assets` and
uses the one whose boot id matches the NTSC-U disc (`SCUS_944.26`).
`ctr-u.bin` is just the conventional name. That's it: no Python, no extraction
step. The image must be the common single-track raw PlayStation BIN layout
(MODE2/2352 sectors). A cooked 2048-byte `.iso` does not carry the audio and
video sector data the game needs, so it will not work. PAL (European) and
Japanese images are detected and refused with a message naming their boot id.

Go straight to Step 3. If your image is a `.chd`, or you specifically want to
save disk space, see the appendix at the end of this guide.

## Step 3: connect to your room

Launch `ctr_native_ap.exe`. On a first start with no saved connection the game
boots to the main menu and shows that it is not connected. Go to **OPTIONS →
Connection** and fill in your room details:

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

## Optional: controllers and Steam Input

Controllers work out of the box: the game uses SDL, so common gamepads (Xbox
controllers, DualShock 4 / DualSense, and most others) are picked up
automatically with a PlayStation-style layout, whether connected before or
after launch.

If you would rather run your controller through **Steam Input** (a Steam
Controller, custom button remapping, or per-game controller configurations),
add the game to Steam as a non-Steam game: in Steam, choose **Games → Add a
Non-Steam Game to My Library…**, browse to `ctr_native_ap.exe`, and from then
on launch it from your Steam library. Steam Input then treats it like any
other Steam game.

**On a Steam Deck** you do not need a keyboard at all. Launch the game from
Steam in Gaming Mode, and focusing any field on the Connection screen brings up
the Steam on-screen keyboard. The whole edit works from the pad: X or START
saves the field, TRIANGLE cancels it.

## Reporting a crash or a stuck seed

If the game crashes, or a seed seems impossible to progress, run
`support-bundle.bat` (Windows) or `./support-bundle.sh` (Linux/Steam Deck) from
the game folder. It creates one small archive (`ctr-ap-support-<date>.zip` /
`.tar.gz`) containing the game log, the AP log, the crash report if there was
one, and your connection settings with the password removed -- no game data is
included. Attach that archive to your report on Discord or GitHub together
with a line about what you were doing; if the seed itself seems broken,
include your YAML and the spoiler log too.

The client also checks every seed on connect: if a solo seed cannot be
completed, a red "SEED NOT COMPLETABLE" warning appears on the hub and the AP
log records which locations are unreachable. Mention that verdict in your
report if you see it.

## Troubleshooting

- "Missing or incomplete assets" at startup: the `assets` folder holds no
  usable disc image, or a file did not extract. Make sure the `assets` folder
  sits in the same directory as `ctr_native_ap.exe` and holds either a raw
  NTSC-U `.bin` (any filename) or a complete extracted set. The startup log
  prints a reason line for every `.bin` it looked at and rejected.
- "ignoring ...: not a raw MODE2/2352 PlayStation disc image" at startup: that
  `.bin` is not a raw dump (it may be a renamed cooked `.iso`, an archive, or a
  corrupt file). Re-dump the disc as a raw MODE2/2352 image.
- "ignoring ...: boot id ... is the PAL (European) release" (or NTSC-J) at
  startup: your image is not the North American release. You need the NTSC-U
  disc, boot id `SCUS_944.26`.
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
  reason), or in `ap-config.txt` if you use the config file instead. The slot
  name must match the room exactly. Settings saved from the in-game screen
  (`config.ini`) override `ap-config.txt`.
- Linux, no sound when launching outside Steam (often with
  `Cannot open shared library libasound_module_pcm_pipewire.so` in the log):
  the game is a 32-bit binary, so its audio goes through the 32-bit build of
  the ALSA-to-PipeWire bridge, and most 64-bit distros no longer install that
  by default. Install the i386/32-bit build of the PipeWire ALSA plugin from
  your distro's repos (confirmed working on LMDE). On immutable distros like
  Bazzite, adding the game to Steam and using the Steam Linux Runtime is the
  practical route: Valve's runtime ships a complete 32-bit library stack.

## Appendix: extracting the assets (most people should skip this)

**You almost certainly do not need this.** Step 2 is the normal way to set the
game up, and it needs no extra tools. Extract only if one of these applies:

- your disc image is a `.chd`, or
- you want to save disk space, because extraction copies out only the files the
  game actually uses instead of keeping the whole image.

It needs Python 3.8 or newer, and `.chd` images additionally need the `chdman`
tool on your PATH. From the folder that holds `ctr_native_ap.exe`, run:

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

If you extracted with a version older than v0.1.2, run the extractor again with
the current one. Earlier versions truncated part of the audio data, which makes
music and the race announcer sound wrong or go missing.
