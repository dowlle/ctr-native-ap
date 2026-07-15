# CTR Archipelago (ctr-native-ap)

Play **Crash Team Racing (PS1, 1999)** as an [Archipelago](https://archipelago.gg) multiworld randomizer, natively on your PC. This is a native port of the game (built on the [CTR-native](https://github.com/CTR-tools/ctr-native) decompilation) with the Archipelago client built directly into it: no emulator, no ROM patching. It connects to the server, receives items, locks and unlocks warp pads, boss garages, doors and gem cups per seed, and sends your location checks and goal.

Builds are published on the [Releases](https://github.com/dowlle/ctr-native-ap/releases) page.

## Getting started

You need three things: a release build, your own (NTSC-U) copy of the game, and your room details. The full walkthrough (including the optional space-saving asset extractor) is in [SETUP.md](SETUP.md); the short version:

**1. Download a release.** Grab the build for your platform from the [Releases](https://github.com/dowlle/ctr-native-ap/releases) page: `ctr_native_ap.exe` (Windows) or `ctr_native_ap` (Linux).

**2. Add your own game disc image.** This project contains no game assets. You must own a retail **NTSC-U** copy of Crash Team Racing. Dump it as a raw `.bin` disc image, name the file `ctr-u.bin`, and place it next to the executable in a folder named `assets`:

```
CTR-Archipelago/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

The image must be the common single-track raw PSX BIN layout (MODE2/2352 sectors, data track starting at byte 0). A 2048-byte `.iso` will not work: it strips the sector data needed for audio and video.

**3. Connect to your room.** Run the executable, go to **OPTIONS → Connection**, type your server address (for example `archipelago.gg:38281`), slot name, and password, and select **Connect**. Settings persist in `config.ini` next to the executable and the game reconnects automatically on later launches. Secure connections (`wss://`, for archipelago.gg rooms) are used automatically. Prefer a text file? Copy [`ap-config.example.txt`](ap-config.example.txt) to `ap-config.txt` instead — see [SETUP.md](SETUP.md).

## Joining a multiworld

The randomization logic lives in the companion apworld, [`dowlle/ctr-archipelago-apworld`](https://github.com/dowlle/ctr-archipelago-apworld) (see the [CTR world README](https://github.com/dowlle/ctr-archipelago-apworld/blob/main/worlds/ctr/README.md)).

Like every Archipelago game, CTR needs a YAML options file per player at generation time: install `ctr.apworld` into your Archipelago installation, use the Launcher's **Generate Template Options** to get the CTR template YAML, set your slot name and options, and hand it to whoever generates the multiworld. Only the generator needs the apworld; as a player you just need the YAML you submitted, the client setup above, and your slot name.

New to Archipelago itself? Start with the [Archipelago tutorials](https://archipelago.gg/tutorial/).

## Building from source

Developers and the curious: see [BUILDING.md](BUILDING.md) for prerequisites, build steps for the vanilla and AP builds, and the project architecture. For the plain native port of CTR without Archipelago, see upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native).

## AI Usage Disclosure

CTR Archipelago is developed with AI assistance (Anthropic's Claude, via Claude Code). You deserve to know how something is made before you decide how you feel about it, so here is the honest version.

- **AI writes code under my direction:** randomization and generation logic, the native AP integration, debugging, and review passes. The design decisions, the priorities, and the accountability are mine. I use AI as a tool that helps me finish what I start, not as a replacement for anyone's craft.
- **No AI-generated art.** Every tracker icon and in-game marker is rendered from the game's own 3D models. No generated textures, logos, or models. This project exists because of human creative work (the original game, the decompilation, the randomizer design it builds on), and it doesn't launder anyone's art through a model.
- **Nothing ships unverified.** Every apworld release passes a full run of Eijebong's Archipelago fuzzer (10/10 check categories across ~14,000 generations; nothing ships red). I playtest every native build in-game on real seeds, and gating logic is verified against the game's actual code, not guessed. The project has a human-reviewed specification and data contract; I don't merge code I haven't understood.
- **The footprint, for those weighing it:** measured production data ([Google, 2025](https://cloud.google.com/blog/products/infrastructure/measuring-the-environmental-impact-of-ai-inference); [independent measurement](https://www.sciencedirect.com/science/article/pii/S2542435126001145)) puts a typical AI request at roughly 0.3 Wh and a fraction of a milliliter of water. A heavy day of AI-assisted development on this project costs electricity on the order of one hot shower. The data center buildout at large is a real concern; I think it belongs in energy policy, not at the feet of hobby projects, but you may weigh that differently.
- **Why:** AI lets me actually finish my projects (I have ADHD). Using it is a considered choice, not a careless one.

If AI-assisted development is a dealbreaker for you, that's a fair call to make with the facts in front of you.

## License

GPL-3.0, inherited from upstream [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native) - see [LICENSE](LICENSE). The Archipelago client layer added by this fork (the code in this repository) is GPL-3.0 as well. The companion apworld is a separate codebase and is MIT-licensed, matching upstream [ArchipelagoMW/Archipelago](https://github.com/ArchipelagoMW/Archipelago). Vendored and build-time third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This repository contains no game assets. You must own a retail NTSC-U copy of Crash Team Racing to play.

## Credits

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) — the decompilation project this is built on
- [PsyCross](https://github.com/OpenDriver2/PsyCross) — original PS1 compatibility code from which parts of CTR Native's owned platform layer and PsyQ facade headers are derived
- [thecodingbob/ctr-native](https://github.com/thecodingbob/ctr-native) — the in-game config menu framework this fork's options menu is ported from
- [SDL3](https://github.com/libsdl-org/SDL) — cross-platform multimedia
- Icebound777 and Taor — the [CTR randomizer](https://github.com/icebound777/CTR-Randomizer-Standalone) whose design this project's Archipelago integration builds on, carried forward with his blessing; the foundational work and the credit for it stay with them
- [apclientpp](https://github.com/black-sliver/apclientpp) — the Archipelago client library powering the in-process AP connection
- Crash Team Racing is a trademark of Sony Computer Entertainment / Naughty Dog
