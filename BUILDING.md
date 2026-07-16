# Building from source

Developer documentation for CTR Native and its Archipelago build. For playing, see the [README](README.md).

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
  ap/                 Archipelago client layer (only compiled with CTR_AP)
  build.bat           Windows build (MinGW32)
  build.sh            Linux build
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

SDL3 is compiled from vendored source, so no separate install is needed.

### Linux (Debian/Ubuntu)

```
sudo apt install gcc-multilib
sudo apt install libx11-dev libxext-dev libgl1-mesa-dev libasound2-dev libudev-dev libdbus-1-dev
```

The AP build additionally links a static, 32-bit OpenSSL and zlib for the `wss://`
transport, so the AP build also needs the 32-bit development packages for those
(`libssl-dev` and `zlib1g-dev` for the `i386` multiarch, or your distro's
equivalent). The vanilla port does not need them.

> **Shipping Linux binaries: target glibc 2.38, not the newest available.**
> A binary built against a bleeding-edge glibc will refuse to start on
> distributions that ship an older one, and users generally cannot upgrade glibc
> themselves. CTR-AP is forked from upstream beta 6.1, whose Linux binaries
> target **glibc 2.38** (widely available); keep that as the floor for any
> published Linux/Steam Deck build and do not let the build host silently raise
> it. Build on (or in a container matching) a glibc-2.38-era toolchain when
> producing release binaries; building locally for your own machine has no such
> constraint.

## Building the vanilla port

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

## Building the AP client

All Archipelago code is isolated behind `#ifdef CTR_AP`; the vanilla build is unaffected.

The AP build needs header-only networking dependencies in `ap/vendor/` that are not tracked in this repository: [apclientpp](https://github.com/black-sliver/apclientpp), [wswrap](https://github.com/black-sliver/wswrap), [WebSocket++](https://github.com/zaphoyd/websocketpp), [Asio](https://github.com/chriskohlhoff/asio), [nlohmann/json](https://github.com/nlohmann/json), and [valijson](https://github.com/tristanpenman/valijson). Clone or copy each into `ap/vendor/<name>/` (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for versions and licenses), then configure with CMake (same toolchain as above):

```
cmake -S . -B build-ap -DCTR_AP=ON
cmake --build build-ap
```

Output: `ctr_native_ap.exe` (Windows) or `ctr_native_ap` (Linux). It reads its server connection from `ap-config.txt` next to the executable (see [`ap-config.example.txt`](ap-config.example.txt)).

## Running development builds

Development builds run from `build/` expect the disc image next to the source tree:

```
ctr-native/
  build/
    ctr_native.exe
  assets/
    ctr-u.bin
```

The disc image requirements are the same as for players (raw NTSC-U BIN, MODE2/2352; see the [README](README.md#getting-started)).

### Extracted asset override

You do not need extracted assets for normal play. Extracted files are supported for development, modding, and debugging: if present under `assets/`, they override files from `ctr-u.bin`.

```
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

## Bug replays

Internal builds can record a small bug report folder. See [docs/REPLAYS.md](docs/REPLAYS.md).

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
- The default build uses 32-bit mode while remaining PSX address-shaped data and host-pointer contracts are audited. GPU primitive links are bridged through 24-bit native tokens; see [docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md).

Further internals: [docs/DATA_SECTIONS.md](docs/DATA_SECTIONS.md), [docs/OVERLAYS.md](docs/OVERLAYS.md).

## Roadmap

- Clean up `game/` copies: strip byte-budget hacks and route platform-specific code through `CTR_NATIVE`
- Keep reducing 32-bit host-pointer assumptions in PSX-shaped data, and keep pruning inherited compatibility code now owned in `include/` and `platform/`.
