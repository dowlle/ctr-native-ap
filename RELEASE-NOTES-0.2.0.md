# CTR-AP v0.2.0

CTR-AP is an Archipelago integration for the native PC port of Crash Team Racing (PlayStation, 1999). It connects to Archipelago in-process, turns races, trophies, relics, tokens, gems and boss wins into checks, and delivers progression items through the multiworld.

Start with the [CTR Archipelago setup guide](https://ap-pie.com/guides/ctr), then follow the [living SETUP.md](https://github.com/dowlle/ctr-native-ap/blob/main/SETUP.md) or the [SETUP.md shipped with v0.2.0](https://github.com/dowlle/ctr-native-ap/blob/v0.2.0/SETUP.md). This release uses the matching CTR-AP native client and apworld pair with schema 9. When generating a new game, use the matching 0.2.0 pair and fresh seeds.

With much of my attention on my partner's recovery, I've relied more than usual on AI assistance for this release. Astra coordinated implementation, reviews and release preparation around plans and priorities we had already agreed on. Automated checks cover part of that work; community testing will help us find what they miss.

## Highlights

- A community-tested stable 0.2.0 client and matching apworld, with the major randomization and native integration work from the Alpha releases brought together.
- Character unlocks and hub swapping, optional progressive stats and boost, expanded traps, and the in-race Turbo filler item are part of the 0.2.0 feature set.
- More forgiving AP-box kart and direct-projectile contact. Some awkward placements can still remain; identifiable reports will help us address those individually.
- Secure AP connections now verify the server's certificate name as well as its trust chain, and datapackage-cache paths reject unsafe components.
- Recorded AI lap collection and playback are included as experimental features and remain off by default. Playback currently shares up to three recorded lines across opponents, so contributor names can repeat. Per-racer assignment, broader contributor-pool selection, finish-line smoothing and ordinary-crate interaction remain planned corrections for 0.2.1. Recorded AI cannot collect AP check boxes.
- Setup documentation explains asset extraction, recorded-AI files and options, troubleshooting, and the local support-bundle workflow.

## Bring your own disc

This release ships no game data. Provide the game files from your own NTSC-U (North American) Crash Team Racing disc. PAL and Japanese discs are detected and refused. The supported raw formats are a `.cue` plus `.bin`, a raw `.bin`, or a `.chd`; a cooked 2048-byte `.iso` does not contain the required audio and video sectors. See the [setup guide](https://ap-pie.com/guides/ctr) for the player walkthrough and the [release SETUP.md](https://github.com/dowlle/ctr-native-ap/blob/v0.2.0/SETUP.md) for the exact versioned instructions.

## Testing and support

This is a community-tested 0.2.0 release. Automated coverage includes native harnesses, apworld unit tests, the full eleven-part generation fuzz matrix, and targeted connection/cache checks. These checks do not cover every gameplay or connect-time verifier scenario. Community testing will help extend that coverage, with reproduced issues and verified fixes feeding into matching 0.2.x client/apworld releases.

## Known limitations

- NTSC-U discs only. PAL and Japanese discs are not supported.
- `.chd` images require `chdman` on your PATH.
- Recorded AI is experimental, default-off, and currently has the shared-lane and finish-smoothing limitations described above.
- Large reconnect backlogs still need gameplay coverage. Per-item console logging was reduced, but network parsing remains on the game thread; please include the support bundle if a reconnect causes a long freeze.
- Optional checks for breaking every time crate in a Relic Race remain deferred to 0.2.1.
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

Reports are welcome even when the cause is unclear. We will check the version, settings and reproduction evidence before deciding on a fix.
