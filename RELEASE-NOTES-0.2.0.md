# CTR-AP v0.2.0

CTR-AP is an Archipelago integration for the native PC port of Crash Team Racing (PlayStation, 1999). It connects to Archipelago in-process, turns races, trophies, relics, tokens, gems and boss wins into checks, and delivers progression items through the multiworld.

Start with the [CTR Archipelago setup guide](https://ap-pie.com/guides/ctr), then follow the [living SETUP.md](https://github.com/dowlle/ctr-native-ap/blob/main/SETUP.md) or the [SETUP.md shipped with v0.2.0](https://github.com/dowlle/ctr-native-ap/blob/v0.2.0/SETUP.md). This release uses the matching CTR-AP native client and apworld pair with schema 9. When generating a new game, use the matching 0.2.0 pair and fresh seeds.

With much of my attention on my partner's recovery, I've relied more than usual on AI assistance for this release. Astra coordinated implementation, reviews and release preparation around plans and priorities we had already agreed on. Automated checks cover part of that work; community testing will help us find what they miss.

## Highlights

- A community-tested stable 0.2.0 client and matching apworld, with the major randomization and native integration work from the Alpha releases brought together.
- More forgiving AP-box kart and direct-projectile contact. Explosion range, ownership and placement behavior are unchanged. Some awkward placements can still remain, especially on custom or user-authored tracks.
- Recorded AI lap collection and playback are included as experimental features and remain off by default. Playback currently shares up to three recorded lines across opponents, so contributor names can repeat. Per-racer assignment, broader contributor-pool selection, finish-line smoothing and ordinary-crate interaction remain planned corrections for 0.2.1. Recorded AI cannot collect AP check boxes.
- Setup documentation explains asset extraction, recorded-AI files and options, troubleshooting, and the local support-bundle workflow.

## Bring your own disc

This release ships no game data. Provide the game files from your own NTSC-U (North American) Crash Team Racing disc. PAL and Japanese discs are detected and refused. The supported raw formats are a `.cue` plus `.bin`, a raw `.bin`, or a `.chd`; a cooked 2048-byte `.iso` does not contain the required audio and video sectors. See the [setup guide](https://ap-pie.com/guides/ctr) for the player walkthrough and the [release SETUP.md](https://github.com/dowlle/ctr-native-ap/blob/v0.2.0/SETUP.md) for the exact versioned instructions.

## Testing and support

This is a community-tested 0.2.0 release. The release preparation baseline includes 58 native harnesses with 5 documented skips and 1,427 apworld tests with 1 documented skip. The full named apworld fuzz matrix also passed its baseline run. Remaining gameplay and verifier coverage is best extended through community testing. The 0.2.x fixes in this release have been verified at source and in their applicable automated checks.

<!-- Draft testing placeholder: update the automated-check counts above if the final release run changes them. -->

## Known limitations

- NTSC-U discs only. PAL and Japanese discs are not supported.
- `.chd` images require `chdman` on your PATH.
- Recorded AI is experimental, default-off, and currently has the shared-lane and finish-smoothing limitations described above.
- Generalized custom-track support is planned for 0.3.0. Development builds for that work are not part of this release.

## Credits

- The [CTR-tools CTR-ModSDK decompilation project](https://github.com/CTR-tools/CTR-ModSDK).
- Icebound777 and Taor for the CTR randomizer work this integration builds on.
- PsyCross for parts of the PlayStation platform layer.
- SDL3 and the Archipelago client libraries.

Crash Team Racing is a trademark of its respective rights holders. This is an unofficial fan project and is not affiliated with or endorsed by them.

Parts of this project were developed with AI assistance. See the AI disclosure in the repository for details.

## Reporting a problem

Please report crashes, freezes, connection problems, inaccessible checks, incorrect pad or box behavior, or other regressions through the [GitHub issue chooser](https://github.com/dowlle/ctr-native-ap/issues/new/choose). Include the client version, platform, seed or YAML, reproduction steps, and the local support bundle when relevant. The bundle is intended for inspection and does not upload automatically.

There is no response calendar or guaranteed response time. Community reports remain welcome and help guide 0.2.x fixes.
