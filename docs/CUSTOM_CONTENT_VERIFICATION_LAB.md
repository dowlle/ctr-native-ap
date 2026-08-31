# CTR Custom Content Verification Lab

This development-only vertical slice inventories Project Saphi v3 media without
changing creator files or Saphi metadata. Every result belongs to an exact
`track id + semantic version + LEV SHA-256 + VRM SHA-256` identity.

The evidence vocabulary is deliberately identical to Saphi's API tags:
`time_trial`, `relic_race`, `ctr_challenge`, `arcade`, and
`crystal_challenge`. `adventure_mode` is outside this race/battle lab.

## Build and inventory

```sh
cc -std=c99 -O2 -I. -Iinclude tools/custom-content-verify.c -o custom-content-verify
python3 tools/custom_content_lab.py --root custom-content-lab sync
python3 tools/custom_content_lab.py --root custom-content-lab analyze \
  --verifier ./custom-content-verify
python3 tools/custom_content_lab.py --root custom-content-lab export \
  --json custom-content-lab/saphi-verification.json \
  --xlsx custom-content-lab/saphi-verification.xlsx
```

Downloads use `.part` files, resume with HTTP Range, are rate-limited, and are
stored by Saphi track/version/media IDs. Inventory is atomically checkpointed
after every exact media pair, so an interrupted sweep resumes safely. LEV/VRM
mode-signature mismatches are retained as inconsistent evidence and never
guessed into a pair.

## File analysis and in-game review

File analysis is static evidence, not a claim that a track was played. Each of
the five tags has two independent `Yes` / `No` / `Not tested` columns in the
exports: `File analysis` and `In-game test`.

Browse the pending queue:

```sh
python3 tools/custom_content_lab.py --root custom-content-lab queue
```

An AP + custom-tracks development build accepts an exact review launch. The
helper supplies absolute paths and both hashes, and the native loader refuses
startup on any mismatch:

```sh
python3 tools/custom_content_lab.py --root custom-content-lab launch EXACT_KEY \
  --client ./build/ctr_native_ap --host 6
```

Select the named arcade host slot in the client. Only that slot is overridden
for that process; no cup is redirected and no seed metadata is changed. For a
single browse/launch/record loop use:

```sh
python3 tools/custom_content_lab.py --root custom-content-lab review arcade \
  --client ./build/ctr_native_ap --host 6 --client-build DEV_BUILD_ID
```

Manual verdicts are written atomically to `verdicts.json` and repeat the exact
LEV/VRM hashes. A verdict therefore cannot silently follow a later upload that
reuses a semantic version.

## Current boundary

The lab does not publish or alter Saphi tags, implement randomizer placement,
or build custom cups. Review launch deliberately reuses a retail arcade slot;
battle-map launch is not yet wired into the native battle selector. Static
analysis confirms structural ingredients only. Runtime behavior, route quality,
AI driving quality, and challenge completable-ness remain in-game evidence.
