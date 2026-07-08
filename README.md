# CTR Archipelago (ctr-native-ap)

Play **Crash Team Racing (PS1, 1999)** as an [Archipelago](https://archipelago.gg) multiworld randomizer, natively on your PC. No emulator, no ROM patching: this is a native port of the game (built on the [CTR-tools](https://github.com/CTR-tools/CTR-ModSDK) decompilation) with the Archipelago client built directly into it. It connects to the server, receives items, locks and unlocks warp pads, boss garages, doors and gem cups per seed, and sends your location checks and goal.

**Status: in development, not yet released.** The code is public for collaboration and transparency. Once released, Windows and Linux builds will be available on the [Releases](https://github.com/dowlle/ctr-native-ap/releases) page.

## Getting started

You need three things: a release build, your own copy of the game, and a small config file.

**1. Download a release.** Grab the build for your platform from the [Releases](https://github.com/dowlle/ctr-native-ap/releases) page: `ctr_native_ap.exe` (Windows) or `ctr_native_ap` (Linux).

**2. Add your own game disc image.** This project contains no game assets. You must own a retail **NTSC-U** copy of Crash Team Racing. Dump it as a raw `.bin` disc image and place it next to the executable in a folder named `assets`:

```
CTR-Archipelago/
  ctr_native_ap.exe
  ap-config.txt
  assets/
    ctr-u.bin
```

The image must be the common single-track raw PSX BIN layout (MODE2/2352 sectors, data track starting at byte 0). A 2048-byte `.iso` will not work: it strips the sector data needed for audio and video.

**3. Configure your connection.** Copy [`ap-config.example.txt`](ap-config.example.txt) to `ap-config.txt` next to the executable and fill in your room details:

```
uri=ws://localhost:38281
slot=Player1
password=
```

The client currently connects over a plain, unencrypted WebSocket (`ws://`), aimed at a local or self-hosted server. Then run the executable: it connects at startup and you play from there.

## Joining a multiworld

The randomization logic lives in the companion apworld, [`dowlle/ctr-archipelago-apworld`](https://github.com/dowlle/ctr-archipelago-apworld) (see the [CTR world README](https://github.com/dowlle/ctr-archipelago-apworld/blob/main/worlds/ctr/README.md)). Whoever generates the multiworld needs `ctr.apworld`; as a player you only need the client setup above and your slot name. New to Archipelago itself? Start with the [Archipelago tutorials](https://archipelago.gg/tutorial/).

## Building from source

Developers and the curious: see [BUILDING.md](BUILDING.md) for prerequisites, build steps for the vanilla and AP builds, and the project architecture. For the plain native port of CTR without Archipelago, see upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native).

## AI Usage Disclosure

CTR Archipelago is developed with AI assistance (Anthropic's Claude, via Claude Code). The short version:

- **AI writes much of the code**, under my direction: randomization and generation logic, the native AP integration, debugging, and review passes.
- **No AI-generated art.** Every tracker icon and in-game marker is rendered from the game's own 3D models. No generated textures, logos, or models.
- **Nothing ships unverified.** Every apworld release passes a full run of Eijebong's Archipelago fuzzer (10/10 check categories across ~14,000 generations; nothing ships red). I playtest every native build in-game on real seeds, and gating logic is verified against the game's actual code, not guessed. The project has a human-reviewed specification and data contract; I don't merge code I haven't understood.
- **Why:** AI lets me actually finish my projects (I have ADHD). Using it is a considered choice, not a careless one.

If AI-assisted development is a dealbreaker for you, that's a fair call to make with the facts in front of you.

## License

GPL-3.0, inherited from upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native) — see [LICENSE](LICENSE). The Archipelago layer added by this fork is GPL-3.0 as well. Vendored and build-time third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This repository contains no game assets. You must own a retail NTSC-U copy of Crash Team Racing to play.

## Credits

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) — the decompilation project this is built on
- [PsyCross](https://github.com/OpenDriver2/PsyCross) — original PS1 compatibility code from which parts of CTR Native's owned platform layer and PsyQ facade headers are derived
- [SDL3](https://github.com/libsdl-org/SDL) — cross-platform multimedia
- Icebound777 and Taor — the CTR randomizer whose design this project's Archipelago integration builds on, carried forward with his blessing; the foundational work and the credit for it stay with them
- [apclientpp](https://github.com/black-sliver/apclientpp) — the Archipelago client library powering the in-process AP connection
- Crash Team Racing is a trademark of Sony Computer Entertainment / Naughty Dog
