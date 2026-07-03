# CTR Native

A native PC port of Crash Team Racing (PS1, 1999), built on top of the [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) decompilation project.

## This Fork: Archipelago Integration

This repository is a fork of [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native) that adds an [Archipelago](https://archipelago.gg) Multiworld client directly into the game. The AP build, `ctr_native_ap`, connects to an Archipelago server in-process — no emulator, no ROM patching — receives items, enforces randomized gating (warp pads, boss garages, doors, gem cups), and sends location checks and goal completion.

- The randomization brain is the companion apworld: [`dowlle/ctr-archipelago-apworld`](https://github.com/dowlle/ctr-archipelago-apworld) — see its [CTR world README](https://github.com/dowlle/ctr-archipelago-apworld/blob/feat/native-randomization/worlds/ctr/README.md) for the project introduction.
- The randomization design builds on Icebound777's CTR randomizer (MIT), and this project carries the native path forward with his blessing. The foundational work, and the credit for it, stays with him and Taor.
- All Archipelago code is isolated behind `#ifdef CTR_AP`; the vanilla engine build is unaffected.

**Status: in development, not yet released.** The code is public for collaboration and transparency; there is no supported player-facing release yet.

### Building the AP client

The AP build needs header-only networking dependencies in `ap/vendor/` that are not tracked in this repository: [apclientpp](https://github.com/black-sliver/apclientpp), [wswrap](https://github.com/black-sliver/wswrap), [WebSocket++](https://github.com/zaphoyd/websocketpp), [Asio](https://github.com/chriskohlhoff/asio), [nlohmann/json](https://github.com/nlohmann/json), and [valijson](https://github.com/tristanpenman/valijson). Clone or copy each into `ap/vendor/<name>/` (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for versions and licenses), then configure with CMake (same toolchain as the Prerequisites below):

```
cmake -S . -B build-ap -DCTR_AP=ON
cmake --build build-ap
```

Output: `ctr_native_ap.exe` (Windows) or `ctr_native_ap` (Linux). It reads its server connection from `ap-config.txt` next to the executable.

## Philosophy

- **No byte budget.** Game source lives in `game/` as our own copies. Edit freely.
- **No PSX toolchain.** Targets Windows and Linux with SDL3. No MIPS compiler needed.
- **Clean platform layer.** `main.c` owns process startup; host details stay in `platform/native_*`.
- **No build system nonsense.** Just `build.bat` / `build.sh`.
- **Fully static build.** Single executable, zero dependencies. SDL3 is compiled from vendored source and linked statically.

## Directory Layout

```
ctr_native/
  main.c              Entrypoint and native platform boundary
  platform/           Native-owned audio, input, memcard, CD, and PSX facade glue
  build.bat           Windows build (MinGW32)
  build.sh            Linux build
  README.md           This file
  game/               Our copies of all decompiled game source (943 files)
    game_unity.h      Ordered unity include chain for all game source files
  include/            Project headers (structs, globals, declarations, platform facade)
  externals/
    SDL/              SDL3 source (static build)
```

## Prerequisites

### Windows

1. Install [MSYS2](https://www.msys2.org/)
2. In an MSYS2 terminal:
   ```
   pacman -Syu
   pacman -S --needed git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
   ```
   If the update asks you to close the terminal, reopen MSYS2 and run the install command.
3. Add `C:\msys64\mingw32\bin` to your system PATH
4. Open a new Command Prompt or PowerShell and run `build.bat`

That's it. SDL3 is compiled from vendored source -- no separate install needed.

### Linux (Debian/Ubuntu)

```
sudo apt install gcc-multilib
sudo apt install libx11-dev libxext-dev libgl1-mesa-dev libasound2-dev libudev-dev libdbus-1-dev
```

## Building

```
build.bat            # Windows
chmod +x build.sh
./build.sh           # Linux
```

First build compiles SDL3 from source. This is cached as a static library in `build/` -- subsequent builds only recompile touched native sources.

Output: `build/ctr_native.exe` (Windows) or `build/ctr_native` (Linux)

### Clean build

```
rmdir /s /q build    # Windows: delete cached libraries
build.bat            # Windows: rebuild everything

rm -rf build/        # Linux: delete cached libraries
./build.sh           # Linux: rebuild everything
```

## Running

### Normal Setup

If you downloaded a release build, you only need two things for normal play:

1. The game executable:
   - `ctr_native.exe` on Windows
   - `ctr_native` on Linux
2. Your own NTSC-U retail CTR disc image, named (put in directory called `assets`):
   - `assets/ctr-u.bin`

Example:

```
CTR-Native/
  ctr_native.exe
  assets/
    ctr-u.bin
```

Then run `ctr_native.exe`.

The disc image must be the common single-track raw PSX BIN layout: MODE2/2352 sectors, with the data track starting at byte 0. A cooked 2048-byte `.iso` does not preserve the XA/STR sector data needed for audio and video playback.

For development builds run from `build/`, put the same `assets/ctr-u.bin` next to the source tree:

```
ctr-native/
  build/
    ctr_native.exe
  assets/
    ctr-u.bin
```

### Extracted Asset Override

You do not need extracted assets for normal play.

Extracted files are still supported for development, modding, and debugging. If present, they override files from `ctr-u.bin`.

Extracted-asset override structure:

```
CTR-Native/
  ctr_native.exe
  assets/
    BIGFILE.BIG
    SOUNDS/KART.HWL
    TEST.STR
    XA/
      ENG.XNF
      ENG/EXTRA/S00.XA ... S05.XA
      ENG/GAME/S00.XA ... S20.XA
      MUSIC/S00.XA ... S01.XA
```

The full extracted asset list is:

- `BIGFILE.BIG`
- `SOUNDS/KART.HWL`
- `TEST.STR`
- `XA/ENG.XNF`
- `XA/ENG/EXTRA/S00.XA` through `S05.XA`
- `XA/ENG/GAME/S00.XA` through `S20.XA`
- `XA/MUSIC/S00.XA` through `S01.XA`

## Bug Replays

Internal builds can record a small bug report folder. See `docs/REPLAYS.md`.

## Architecture

```
main.c (entrypoint)
  |
  +-- platform/native_* (platform shell, audio, input, memcard, CD, renderer, PSX facade glue)
  |
  +-- game/game_unity.h
        |
        +-- game/ (all decompiled game source)
              |
              +-- include/ (headers: structs, globals, declarations)
```

- `CTR_NATIVE` is defined for native host/platform-specific code
- The default build uses 32-bit mode while remaining PSX address-shaped data and host-pointer contracts are audited. GPU primitive links are bridged through 24-bit native tokens; see `docs/MEMORY_MODEL.md`.

## Roadmap

- Clean up `game/` copies strip byte budget hacks and route platform-specific code through `CTR_NATIVE`
- Keep reducing 32-bit host-pointer assumptions in PSX-shaped data, and keep pruning inherited compatibility code now owned in `include/` and `platform/`.

## AI Usage Disclosure

I want to be upfront about how this project is built. CTR Archipelago is developed with AI assistance, specifically Anthropic's Claude, through Claude Code.

**What AI does.** A substantial part of the code is written or co-written with AI under my direction: the apworld's randomization and generation logic, the native AP-client integration in the ctr-native decomp, and debugging and code-review passes. AI also helps me keep the project backlog organised.

**What AI does not do.** There is no AI-generated art anywhere in this project. Every tracker icon and in-game marker is rendered from the game's own 3D models. No generated textures, no generated logos, no generated models.

**How I make sure it's actually correct.** AI writes a lot of the code, but nothing ships unreviewed or unverified:

- Every apworld release passes a full run of Eijebong's Archipelago fuzzer: 10 out of 10 check categories clean across roughly 14,000 randomized generations. Nothing ships red. This is the same bar I hold my other apworlds to, and I'd recommend it to any apworld developer, AI-assisted or not.
- I test every native build in-game myself, on real generated seeds, before it goes out. Automated checks catch generation bugs; I catch the "looks fine, plays broken" ones by actually playing them.
- I verify instead of guessing. Gating and solvability behaviour is checked against the game's actual code, against generation with accessibility set to full, and in-game, and reverted if it doesn't hold up.
- The architecture is written down and human-reviewed. The project has a formal specification and an apworld-to-client data contract, which I went through section by section on 2026-07-01. I don't merge code I haven't understood from a principled perspective.

**Why I use AI, honestly.** AI lets me actually finish the ideas I have. I have ADHD, and left to my own devices most of my projects would never reach a working state. I'm not thrilled that I can't do all of this unaided, but I'd rather ship something complete and playable than ship nothing at all. I don't pretend AI has no downsides, and I don't want to wave those away. Using it is a considered choice, not a careless one.

**Credit.** The randomization design is built on Icebound777's CTR randomizer (MIT), and CTR-AP carries the ctr-native path forward with his blessing. The foundational work, and the credit for it, stays with him.

Now you know what you're getting. If AI-assisted development is a dealbreaker for you, that's a fair call to make with the facts in front of you.

## License

GPL-3.0, inherited from upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native) — see [LICENSE](LICENSE). The Archipelago layer added by this fork is GPL-3.0 as well. Vendored and build-time third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This repository contains no game assets. You must own a retail NTSC-U copy of Crash Team Racing to play.

## Credits

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) — the decompilation project this is built on
- [PsyCross](https://github.com/OpenDriver2/PsyCross) — original PS1 compatibility code from which parts of CTR Native's owned platform layer and PsyQ facade headers are derived
- [SDL3](https://github.com/libsdl-org/SDL) — cross-platform multimedia
- Icebound777 and Taor — the CTR randomizer whose design this project's Archipelago integration builds on
- [apclientpp](https://github.com/black-sliver/apclientpp) — the Archipelago client library powering the in-process AP connection
- Crash Team Racing is a trademark of Sony Computer Entertainment / Naughty Dog
