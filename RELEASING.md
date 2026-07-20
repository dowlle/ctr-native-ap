# Releasing CTR-AP

The repeatable checklist for shipping a release, written down from the v0.1.0
release (2026-07-15). CTR-AP ships as a versioned PAIR: this client and the
companion `ctr.apworld` carry the same version and are tested together.

## 1. Gates (nothing ships red)

- [ ] Full fuzz clean on the apworld release commit: ~14,000 generations, zero
      failures (the fuzzer randomizes all options, so option-combo corners are
      covered). Smaller smoke runs are for iteration only.
- [ ] In-game playtest of a real generated seed on the exact release
      executable. Every surface changed since the last playtest gets looked at
      in-game, not just compiled.
- [ ] Known issues written down honestly for the release notes, each with
      impact and workaround.

## 2. Version bump (TWO places, same number)

- [ ] `ap/ap_version.h` -> `CTR_AP_VERSION` (surfaces in the window title and
      the `[CTR-AP] Release:` startup log line).
- [ ] apworld `worlds/ctr/archipelago.json` -> `world_version`.
- WARNING: never LOWER `world_version`. Archipelago's requires-check hard-fails
  any YAML generated from a template with a higher version, which strands every
  existing player YAML.

## 3. Build

- [ ] Build the AP executable from the tagged commit (MinGW32, `-DCTR_AP=ON`);
      the version string is compiled in, so the commit must be final first.
- [ ] Verify the version string is embedded in the binary and both build
      targets (vanilla + AP) compile.
- [ ] Build `ctr.apworld` from the apworld release commit and verify it is
      content-identical to the build the fuzz gate ran on.

## 4. Package

- [ ] Bundle folder named `ctr-archipelago-vX.Y.Z/` containing:
      `ctr_native_ap.exe`, `ctr.apworld`, `extract_assets.py`, `SETUP.md`,
      `ap-config.example.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md`,
      `support-bundle.bat`, `support-bundle.ps1` (Linux tarball instead ships
      `support-bundle.sh`).
- [ ] Zip it as `ctr-archipelago-vX.Y.Z-windows-x86.zip`. The folder INSIDE
      the zip must carry the release name too (players see it when unzipping).
- [ ] Write the `.sha256` file for the zip; quote the hash in the release
      notes.

## 5. Tag

- [ ] Same tag `vX.Y.Z` on BOTH repos at the exact release commits, pushed.
      The player-facing GitHub release lives on this repo only; the apworld
      tag exists for traceability.

## 6. Release notes

Start from `tools/extract-assets/RELEASE-NOTES-DRAFT.md` (the template) and
the previous release's published notes. House style:

- No em dashes anywhere in player-facing text.
- Paragraphs on single lines: GitHub renders newlines in release notes as hard
  line breaks, so wrapped source text renders ragged.
- Credits carry links; the setup guide is linked twice: the living copy
  (`blob/main/SETUP.md`) for the banner and walkthrough mentions, the
  tag-pinned copy (`blob/vX.Y.Z/SETUP.md`) labeled "as shipped".
- Known limitations section includes every known issue, with workarounds.
- A "What's next" section from the roadmap keeps expectations honest.

## 7. Publish

- [ ] Privacy scan the notes before publishing: no real names, no machine or
      host names, no local paths.
- [ ] `gh release create vX.Y.Z` with three assets: the zip, its `.sha256`,
      and the standalone `ctr.apworld` (multiworld hosts often want just the
      world file).
- [ ] Verify with `gh release view`: title, tag, all three assets present.

## 8. After publishing

- [ ] Repo About descriptions on both repos still accurate (no stale "in
      development" wording).
- [ ] File or update GitHub issues for the shipped known issues, labeled
      `known-issue`.
- [ ] Announce (community Discord), and route bug reports to the issue
      tracker.
- [ ] Update the roadmap: shipped items out, new plans in.
- [ ] **Documentation sweep. Do this every release, not only when it feels
      needed.** Decision sessions keep docs current; release events never do,
      which is how a public roadmap block ended up advertising four
      already-shipped features as "planned next", and how a fixed-two-releases-
      ago blocker stayed at the top of the backlog. Sweep for the issue numbers
      that just shipped and clear every stale status:
      - [ ] Public roadmap block: REGENERATE it from the internal ledger. Never
            hand-edit it; that is how it drifts.
      - [ ] Backlog and feature overview: anything shipped moves out of
            "planned" or "backlog".
      - [ ] Triage register: build-status cells frozen at "PR open" or
            "in build" are now shipped.
      - [ ] Any "decide BEFORE tagging" item still present: it either got
            decided or it did not matter. Resolve it, do not carry it.
- [ ] **Close cross-repo issues by hand.** GitHub only auto-closes from PR
      keywords within the same repository, so an apworld PR never closes a
      native issue. This is why issues have sat open for days after their work
      merged.
- [ ] Decide what "closed" means and be consistent: closing on merge means the
      tracker claims fixes that no download contains. Prefer closing on
      release.
