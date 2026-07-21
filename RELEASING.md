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
- [ ] Verifier sweep: for every YAML in `tools/verify-profiles/`, generate a
      solo seed with the release apworld, connect the release client, and
      record the verifier verdict. Any wrong verdict (a warning on a beatable
      seed, or silence on an unbeatable one) blocks the tag. This sweep is
      separate from the fuzz gate: the fuzzer proves generation succeeds, the
      sweep proves the client's connect-time verifier judges the seed
      correctly. See `tools/verify-profiles/README.md`.
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
- [ ] Linux/Steam Deck binary: build on the multiarch rig at the glibc-2.38
      floor (`-DCTR_AP=ON`; see BUILDING.md). The build strips the binary and
      writes a `ctr_native_ap.debug` sidecar next to it (CTR_SPLIT_DEBUG,
      default ON). Confirm the debuglink is present with
      `readelf --string-dump=.gnu_debuglink ctr_native_ap` and confirm the
      binary reports "stripped" with `file ctr_native_ap`.
- [ ] OPERATOR NOTE, run once on the first stripped build: `readelf -S
      ctr_native_ap.debug` and check whether the sidecar actually contains
      DWARF (`.debug_*` sections) or only a `.symtab`. The Linux release config
      builds with no explicit `-g`, so the sidecar may be symtab-only. Separately,
      the Linux crash reporter (`ap/ap_crash.c`) symbolizes via `.dynsym` and is
      built without `-rdynamic`, so it likely resolves no game-frame names even
      unstripped, and the shipped binary may carry no DWARF at all. If addr2line-
      able crash addresses are wanted, adding `-g` to the Linux release build is a
      worthwhile companion decision (its cost lands entirely in the `.debug`
      sidecar after this change). That is a flagged follow-up decision, not part
      of this step.

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
- [ ] Linux tarball: assemble `ctr-archipelago-vX.Y.Z-linux-x86.tar.gz`
      containing the STRIPPED `ctr_native_ap` plus the same companion files as
      the Windows bundle (`ctr.apworld`, `extract_assets.py`, `SETUP.md`,
      `ap-config.example.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md`,
      `support-bundle.sh`). The tarball NEVER contains the `.debug` sidecar. As
      with the zip, the folder inside the tarball carries the release name. Write
      the tarball's `.sha256` sidecar.
- [ ] The `.debug` sidecar ships as its OWN public release asset:
      `ctr_native_ap.debug` plus a `ctr_native_ap.debug.sha256` sidecar
      alongside it in the GitHub release (this lets community members symbolize
      their own crash reports). It must be the `.debug` from the EXACT release
      build; a rebuilt one fails the debuglink CRC32 check and is useless.
- [ ] Record in the release evidence the sha256 of BOTH the stripped
      `ctr_native_ap` and the `ctr_native_ap.debug`: add them to the devlog
      artifact table, and archive a copy of the `.debug` and its checksums under
      `ctr-artifacts/<version>/` (the existing evidence home). Because the
      debuglink CRC rejects any rebuild, the `.debug` archived here must be the
      one from the exact published release, not a later rebuild.

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
- Cross-check every claim in "New" against the Testing section's
  not-verified-in-game list IN THE SAME DOCUMENT. A feature on that list
  carries its hedge inside its own New bullet, not three sections down.
  (v0.1.2 claimed cup-leg rungs were in logic while its own Testing section
  listed them as unverified; the claim was half wrong, see #86.)
- A "What's next" section from the roadmap keeps expectations honest.

## 7. Publish

- [ ] Privacy scan the notes before publishing: no real names, no machine or
      host names, no local paths.
- [ ] `gh release create vX.Y.Z` with the Windows assets: the zip, its
      `.sha256`, and the standalone `ctr.apworld` (multiworld hosts often want
      just the world file).
- [ ] Add the Linux assets to the same release: the `.tar.gz` and its
      `.sha256`, plus the `ctr_native_ap.debug` sidecar and its
      `ctr_native_ap.debug.sha256` (see §4; the `.debug` is a public asset so
      players can symbolize their own crashes).
- [ ] Verify with `gh release view`: title, tag, and every asset present (the
      Windows zip + sha256, `ctr.apworld`, the Linux tarball + sha256, and the
      `.debug` + sha256).

## 8. After publishing

- [ ] Repo About descriptions on both repos still accurate (no stale "in
      development" wording).
- [ ] File or update GitHub issues for the shipped known issues, labeled
      `known-issue`.
- [ ] Publish the version to the apworld indexes, so players installing from
      an index get this release instead of an older one. Two indexes carry
      CTR: `dowlle/Archipelago-index` and `ionium-ap/Archipelago-index`. In
      each, add the version under `[versions]` in `index/ctr.toml` and add a
      `"X.Y.Z" = "<sha256>"` line under `[ctr]` in `index.lock`. Compute the
      hash from the downloaded release asset, never from the release notes.
      Index CI then runs the full check matrix against the published
      `ctr.apworld`, which is an independent re-verification of the gate.
      The ionium one is cross-fork: branch from their `main` (not ours, which
      has diverged), push to our own index fork, and open the PR with
      `--repo ionium-ap/Archipelago-index --base main`. This step was missed
      for v0.1.1, which left every index a release behind for four days.
- [ ] Announce (community Discord), and route bug reports to the issue
      tracker.
- [ ] Fast-forward `master` to the upstream head: `git fetch upstream && git
      push origin upstream/master:master`. `master` is a read-only mirror of
      `CTR-tools/ctr-native` kept so "what changed upstream since we last
      looked" stays a one-command question; it carries no work of ours (our
      work lives on `main`). While there, skim the new upstream commits for
      anything worth cherry-picking, the way the v0.1.2 cutscene-matrix and
      third-turbo fixes were.
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
