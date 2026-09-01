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
2. The release archive for your platform from the
   [releases page](https://github.com/dowlle/ctr-native-ap/releases): the Windows
   `.zip`, or the Linux and Steam Deck `.tar.gz`.

That is everything you need on the game side when you are joining an existing
room. With a raw `.bin` image you do not need Python and you do not need to
extract the disc. A `.chd` image uses the extractor in the appendix. If you are
generating the multiworld yourself, you also need the matching `ctr.apworld`
and a CTR YAML file; see [Generating or hosting a multiworld](#generating-or-hosting-a-multiworld).

For a guided first setup, use the
[CTR setup guide on AP-Pie](https://ap-pie.com/guides/ctr). To create a player
file for this exact prerelease, open the
[Alpha7 CTR YAML Builder](https://ap-pie.com/apworlds?build=ctr&version=0.2.0-alpha7).

## Step 1: get the game executable

Download and extract the archive for your platform from the
[releases page](https://github.com/dowlle/ctr-native-ap/releases):
`ctr-archipelago-vX.Y.Z-windows-x86.zip` on Windows, or
`ctr-archipelago-vX.Y.Z-linux-x86.tar.gz` on Linux and Steam Deck. Keep the
extracted files together in their own folder. The rest of these steps add the
game data and your server settings there.

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
  Slot names are case-sensitive, so copy the capitalization exactly. The in-game
  font marks uppercase letters with a short outlined white line underneath.
- **Password**: the room password, or leave it blank if there is none.

Select **Connect**. The status line on the same screen shows the connection
state (Connecting… / Connected / an error message). Your settings are saved to
`config.ini` next to the executable, and the game dials automatically on later
launches. A startup dial that cannot reach the room stops after a few automatic
attempts instead of retrying forever: the status line then says that automatic
attempts stopped, and selecting **Connect** retries with the same settings.

## Your first five minutes

Once the status says **Connected**, return to the main menu and start Adventure
mode. Use the normal save station in the hub to save your local Adventure
progress. The Archipelago server remains authoritative for received items and
completed locations, and rebuilds your received-item counts when you reconnect.

The locations in your seed depend on its YAML options. They can include Trophy
Races, CTR Token Challenges, relic Time Trials, boss races, Gem Cups, Crystal
Bonus Rounds, Oxide races, and additional finishing-position checks. Complete
an event and continue past its results or award screen to send its check. Items
you receive, and the ones your checks send to other players, appear in the feed
during the race as well as in the Adventure hub. Progression items update your
available gates; traps arm silently and fire during a later race.

If the connection drops, checks completed offline are retained and sent after
you reconnect to the same seed and slot. The Connection screen shows the
current state and any error message. A drop after you have connected once keeps
the client recovering in the background, so a player mid-seed is not stranded
away from the main-menu Connection screen; a startup or manual dial that never
reached a room stops after a few automatic attempts and waits for you to select
Connect.

The client verifies the seed after connecting. If the hub shows a red **SEED NOT
COMPLETABLE** warning, stop and report it with your YAML and spoiler log. For a
crash or a seed that appears stuck, follow
[Reporting a crash or a stuck seed](#reporting-a-crash-or-a-stuck-seed). Setup
questions and reproducible bugs can go to the
[GitHub issue tracker](https://github.com/dowlle/ctr-native-ap/issues); if you
are unsure which repository is responsible, file it there anyway.

### Alternative: config file

If you prefer a text file, copy `ap-config.example.txt` to `ap-config.txt` in
the same folder as `ctr_native_ap.exe` and set `uri`, `slot`, and `password`
there; the game reads it at startup. Values saved from the in-game Connection
screen (`config.ini`) take precedence when both exist. The example file also
documents a few optional quality of life toggles (`skip_hints`, `map_flash`),
which can equally be changed in the in-game options menu.

## Item boxes

If your YAML turned on `box_locations`, the tracks carry AP item boxes: crates
placed around the course that send a location check when you break them. This
is separate from `itemsanity`, which controls received weapon items and weapon-
use checks. A broken AP box stays broken for the rest of the seed, across
reconnects and relaunches, because the client asks the server what has already
been checked rather than keeping its own tally.

You do not have to install anything for this. The placement set, meaning where
every box stands on every track, is compiled into the client, so the boxes are
simply there once your seed includes them.

Two things decide whether a box appears:

- **Your seed.** A box stands only if your slot actually has that location. If
  your options created fewer box locations than the placement set covers, or none
  at all, you get exactly the ones your seed created and no more. That is
  correct, not a missing download.
- **Whether you already broke it.** Checked boxes do not come back.

If a track looks empty and you expected boxes, open `ctr-ap.log`. Every level
load writes one line saying how many boxes are standing out of how many
placements the track holds and which placement set is live, plus, when nothing
stands, which reason applies.

## Generating or hosting a multiworld

Create your player file with the
[Alpha7 CTR YAML Builder](https://ap-pie.com/apworlds?build=ctr&version=0.2.0-alpha7).
Download the resulting YAML and give it to the person generating the room. The
Builder prepares and validates player configuration; it does not generate the
seed or host the playable server.

Only the person generating the room needs the `ctr.apworld`. Download it from
the same release as the client and install it by double-clicking it or placing
it in Archipelago's `custom_worlds` folder. Add every player's YAML to the
generator's `Players` folder, then generate and host the resulting multiworld
through Archipelago as usual.

As an offline alternative to AP-Pie, use **Generate Template Options** in the
Archipelago Launcher after installing the matching `ctr.apworld`, then edit the
generated Crash Team Racing YAML locally.

The released client and apworld are a pair. Update both together, even when a
release appears to change only one side.

## Experimental custom content in Alpha7

The public archive contains no custom-track files. Alpha7 recognizes one
experimental Baby T Park package supplied by its creator. Open **OPTIONS →
Custom Content** to inspect it, follow the creator link and verify the installed
files. **Ready** means the files are compatible; it does not make a generated
seed use the track.

To use the preview, export its YAML fragment from the manager and add that block
to the player YAML before generating the room. Every player whose slot requires
the track must install the same verified package. A missing or mismatched package
fails closed instead of loading the displaced retail race.

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
one, and your connection settings with the password removed. No game data is
included. Attach that archive to a [GitHub issue](https://github.com/dowlle/ctr-native-ap/issues/new/choose),
or bring it to the [Crash Team Racing channel](https://discord.com/channels/731205301247803413/1222304293751750777)
on the Archipelago Discord, together with a line about what you were doing; if the seed itself seems broken,
include your YAML and the spoiler log too.

The client also checks every seed on connect: if a solo seed cannot be
completed, a red "SEED NOT COMPLETABLE" warning appears on the hub and the AP
log records which locations are unreachable. Mention that verdict in your
report if you see it.

## Troubleshooting

- `InvalidSlot` or `Slot not found`: copy the slot name from the room page
  exactly, including uppercase and lowercase letters. You can also correct the
  saved `slot` value under `[Connection]` in `config.ini`.
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
