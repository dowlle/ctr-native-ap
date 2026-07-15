# DRAFT: CTR-AP release notes

Status: DRAFT. This file is a working draft of the text a future GitHub release
would carry. It is not published, not linked from anywhere, and no release
exists yet. Do not treat any part of this as final copy.

---

## CTR-AP

CTR-AP is an Archipelago integration for a native PC port of Crash Team Racing
(PlayStation, 1999). It lets Crash Team Racing take part in an Archipelago
multiworld: trophies, relics, tokens, gems, and boss wins become checks, and the
items you need to progress arrive from the multiworld.

### Bring your own disc

This release ships no game data. You provide the game files from your own copy
of the NTSC-U (North American) Crash Team Racing disc. Nothing here contains
Crash Team Racing game assets, and none are distributed.

A helper script (`extract_assets.py`) reads your own disc image (`.cue`, `.bin`,
or `.chd`) and copies out just the files the game needs into an `assets` folder.
It checks that the disc is the supported region and that the extracted set is
complete. See `SETUP.md` for the full walkthrough.

### What is in this release

- `ctr_native_ap.exe`: the game executable with Archipelago support built in.
- `ctr.apworld`: the Archipelago world, for whoever generates the multiworld.
- `extract_assets.py`: the bring-your-own-disc asset extractor.
- `ap-config.example.txt`: a documented template for your server settings
  (optional; the in-game OPTIONS → Connection screen is the recommended way).
- `SETUP.md`: step-by-step setup.

### Known limitations

- NTSC-U (North American) discs only. PAL and Japanese discs are detected and
  refused. Other regions may be supported later.
- The disc image must be a raw MODE2/2352 dump (a `.cue`/`.bin`, a raw `.bin`,
  or a `.chd`). A cooked 2048-byte `.iso` does not carry the audio and video
  sector data the game needs.
- `.chd` images require the `chdman` tool to be installed and on your PATH.

### Credits

- The CTR-tools CTR-ModSDK decompilation project, which this port is built on.
- Reverse engineering and Archipelago work by Icebound777 and Taor.
- PsyCross, for PlayStation hardware-abstraction code that parts of the native
  platform layer are derived from.
- SDL3, for cross-platform windowing, input, timing, and audio.
- Crash Team Racing is a trademark of its respective rights holders. This is an
  unofficial, fan-made project and is not affiliated with or endorsed by them.

### AI disclosure

Parts of this project were developed with AI assistance. See the AI disclosure
in the repository for details.
