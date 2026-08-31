# CTR Archipelago (ctr-native-ap)

Play **Crash Team Racing (PS1, 1999)** as an [Archipelago](https://archipelago.gg) multiworld randomizer, natively on your PC. This is a native port of the game, built on the [CTR-native](https://github.com/CTR-tools/ctr-native) decompilation, with the Archipelago client integrated directly into it: no emulator and no ROM patching. It connects to the server, receives items, changes progression and access throughout Adventure Mode, and sends your location checks and goal.

Visit the [CTR Archipelago page on AP-Pie](https://ap-pie.com/ctr) for the feature overview and player resources, follow the [CTR setup guide](https://ap-pie.com/guides/ctr), or use the [CTR YAML Builder](https://ap-pie.com/apworlds?build=ctr) to create a player file. Builds are published on the [Releases](https://github.com/dowlle/ctr-native-ap/releases) page.

## Getting started

To play, you need a release build, your own NTSC-U copy of the game and your Archipelago room details. The complete walkthrough, troubleshooting guidance and optional space-saving asset extractor are in [SETUP.md](SETUP.md). The short version:

**1. Download and extract a release.** Grab the archive for your platform from the [Releases](https://github.com/dowlle/ctr-native-ap/releases) page: `ctr-archipelago-vX.Y.Z-windows-x86.zip` on Windows, or `ctr-archipelago-vX.Y.Z-linux-x86.tar.gz` on Linux and Steam Deck. The extracted folder contains the game executable and its support files.

**2. Add your own game disc image.** This project contains no game assets. You must own a retail **NTSC-U** copy of Crash Team Racing. Dump it as a raw `.bin` disc image and place it next to the executable in a folder named `assets` (launching the game once creates the folder). The filename does not matter: the game picks the `.bin` whose boot id matches the NTSC-U disc; `ctr-u.bin` is the conventional name:

```
CTR-Archipelago/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

The image must be the common single-track raw PSX BIN layout (MODE2/2352 sectors, data track starting at byte 0). A 2048-byte `.iso` will not work: it strips the sector data needed for audio and video.

**3. Connect to your room.** Run the executable, go to **OPTIONS → Connection**, type your server address (for example `archipelago.gg:38281`), slot name, and password, and select **Connect**. Settings persist in `config.ini` next to the executable and the game dials automatically on later launches. A startup dial that cannot reach the room stops after a few automatic attempts instead of retrying forever; the Connection screen then says automatic attempts stopped, and selecting **Connect** retries. If the connection drops mid-session after you have connected once, the client keeps recovering in the background so you are not stranded away from the menu. Secure connections (`wss://`, for archipelago.gg rooms) are used automatically. Prefer a text file? Copy [`ap-config.example.txt`](ap-config.example.txt) to `ap-config.txt` instead; see [SETUP.md](SETUP.md).

## Creating or joining a multiworld

The randomization logic lives in the companion [`ctr.apworld`](https://github.com/dowlle/ctr-archipelago-apworld). The [CTR page on AP-Pie](https://ap-pie.com/ctr) provides the player-facing overview and option guidance; the [CTR world README](https://github.com/dowlle/ctr-archipelago-apworld/blob/main/worlds/ctr/README.md) documents the apworld project itself.

Like every Archipelago game, CTR needs one YAML options file per player when a room is generated. The [CTR YAML Builder on AP-Pie](https://ap-pie.com/apworlds?build=ctr) is the recommended way to choose a slot name and options and download that player file. It prepares the YAML but does not generate or host the multiworld. Give the downloaded file to the person generating the room. Only the generator needs the matching `ctr.apworld` installed. Once the room exists, each player needs the client setup above, the matching slot name and the room address.

Seeds with `box_locations` turned on place AP item boxes around the tracks. This is separate from `itemsanity`, which controls received weapons and weapon-use checks. There is nothing extra to install: the placement set ships inside the client, and which boxes actually stand is decided by your own slot's seed. See [Item boxes](SETUP.md#item-boxes) in the setup guide.

## Optional custom content

Public releases contain no custom-track files. When a release recognizes a supported package, **OPTIONS → Custom Content** shows its creator source and verifies the installed files before marking it Ready. Ready means the local content is compatible; it does not opt a generated seed into using the track. Export the package's YAML fragment and add it to the player YAML before generation. A seed that requires missing or mismatched content fails closed instead of loading a different retail race.

New to Archipelago itself? Start with the [Archipelago tutorials](https://archipelago.gg/tutorial/). For CTR-specific information, return to [ap-pie.com/ctr](https://ap-pie.com/ctr).

## Building from source

Developers and the curious: see [BUILDING.md](BUILDING.md) for prerequisites, build steps for the vanilla and AP builds, and the project architecture. For the plain native port of CTR without Archipelago, see upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native).

## AI usage

I use Claude Code while developing CTR Archipelago. It helps with implementation, debugging, and review, while I make the design decisions and test releases in game. The project does not use AI-generated art. I am disclosing this because I want people to know how the project is made. I have ADHD, and this is one of the tools that helps me turn ideas into finished projects.

More detail about how I use and verify AI-assisted work is available in [AI_USAGE.md](AI_USAGE.md).

## License

GPL-3.0, inherited from upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native) - see [LICENSE](LICENSE). The Archipelago client layer added by this fork (the code in this repository) is GPL-3.0 as well. The companion apworld is a separate codebase and is MIT-licensed, matching upstream [ArchipelagoMW/Archipelago](https://github.com/ArchipelagoMW/Archipelago). Vendored and build-time third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This repository contains no game assets. You must own a retail NTSC-U copy of Crash Team Racing to play.

## Credits

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) - the decompilation project this is built on
- [PsyCross](https://github.com/OpenDriver2/PsyCross) - original PS1 compatibility code from which parts of CTR Native's owned platform layer and PsyQ facade headers are derived
- [thecodingbob/ctr-native](https://github.com/thecodingbob/ctr-native) - the in-game config menu framework this fork's options menu is ported from, plus the graphics options this release adds: the widescreen / generalized aspect-ratio support (branch `widescreen-option`), the borderless fullscreen toggle (branch `fullscreen-option`) and the PSX-authentic dithering toggle (branch `dithering-option`)
- [SDL3](https://github.com/libsdl-org/SDL) - cross-platform multimedia
- Icebound777 and Taor - the [CTR randomizer](https://github.com/icebound777/CTR-Randomizer-Standalone) whose design this project's Archipelago integration builds on, carried forward with his blessing; the foundational work and the credit for it stay with them
- [apclientpp](https://github.com/black-sliver/apclientpp) - the Archipelago client library powering the in-process AP connection
- Crash Team Racing is a trademark of Sony Computer Entertainment / Naughty Dog
