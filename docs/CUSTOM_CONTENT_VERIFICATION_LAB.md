# CTR Custom Content Verification Lab

This is a development-only Project Saphi v3 inventory and review workflow. It
does not alter creator files or publish Saphi metadata. Public API values are
untrusted input and static analysis never grants loader authority.

The exact Saphi mode slugs are `time_trial`, `relic_race`, `ctr_challenge`,
`arcade`, and `crystal_challenge`. Battle files are inventoried, but their
runtime support is `Unsupported` until a separate battle-selector integration
exists.

## Identities and evidence

`variant_id` identifies one Saphi upload pair using the NUL-delimited track ID,
semantic version, LEV media ID, and VRM media ID. It is the inventory, queue,
session, and verdict key. `content_id` identifies reused bytes using the two
SHA-256 values. Equal content IDs are provenance only: verdicts are never
inherited automatically. `copy-verdict` performs an explicit, audited copy
after both variants' bytes are revalidated.

File analysis reports `Detected`, `Not detected`, `Indeterminate`, or `Error`.
Those values describe recognized structures, not playability. Only a
Steam-backed human review records an in-game `Yes` or `No`.

## Build, synchronize, and analyze

```sh
cc -std=c11 -O2 -Wall -Wextra -I. -Iinclude \
  tools/custom-content-verify.c -o custom-content-verify
python3 tools/custom_content_lab.py --root custom-content-lab sync
python3 tools/custom_content_lab.py --root custom-content-lab analyze \
  --verifier ./custom-content-verify
python3 tools/custom_content_lab.py --root custom-content-lab export
python3 tools/custom_content_lab.py --root custom-content-lab \
  verify-structural-goldens
```

Downloads are confined to numeric track/media cache paths on Project Saphi's
HTTPS origin. Cached and downloaded files must match Saphi's exact size and
CRC32. Resumption accepts only a matching `206 Content-Range`; integrity
failures are quarantined. Catalogue pagination, JSON size/depth, track/media
counts, retries, and text fields are bounded.

Analysis checkpoints a full reconciled inventory after every pair. Completed
entries are skipped only while their metadata, CRC32, SHA-256, and cached files
remain unchanged. A failure remains an exact, hash-bound error record rather
than deleting the rest of the scan.

`verify-structural-goldens` pins the exact Saphi media identities, hashes,
measurements, and cautious structural results for the Matsuri 1.1.0 and Arabian
Heights Night 1.1.0 discrepancy controls. It also re-hashes their cached bytes.
These are structural regressions only; they do not replace the Steam-backed
positive and negative runtime controls required for each reviewable mode.

Exports are published beneath `custom-content-lab/exports/<generation>/` as
`report.json`, `report.xlsx`, and `manifest.json`. Only the generation named by
the atomically replaced `exports/current.json` is published. Formula-like XLSX
text is stored as text, and the manifest records whether escaping occurred.

## Queue controls

```sh
python3 tools/custom_content_lab.py --root custom-content-lab queue \
  --difference --pending --mode arcade --newest-only --page-size 50
```

Filters for author, track type, collection, exact Saphi mode, differences, and
pending runtime evidence can be combined. Text and `--json` output share the
same stable ordering and cursor pagination. Rows print the short variant ID and
both media IDs.

## User-driven Steam review

The lab never starts CTR. Prepare one exact race-mode session:

```sh
python3 tools/custom_content_lab.py --root custom-content-lab prepare-session \
  VARIANT_ID arcade --host 6 --minutes 30
```

This revalidates the cached pair and writes the only accepted production input:
`custom-content-lab/session/manifest.json`. Start the configured development
build yourself through Steam. At startup the client validates the fixed
manifest, consumes it to `armed.json`, retains the exact hashed bytes, and
writes `receipt.json` for its own executable image and compile-time commit.
Authority is offline-only, host-and-mode exact, generation-bound, and one-use.
It is revoked on AP connection, seed activity, expiry, target exit, completion,
load failure, or process teardown. A direct executable launch is not valid
runtime evidence.

After the Steam-started process exits normally and finalizes the receipt:

```sh
python3 tools/custom_content_lab.py --root custom-content-lab collect-session \
  --executable /exact/path/to/ctr_native_ap --reviewer NAME \
  --steam-attested --result Yes --notes "review notes"
```

Collection independently hashes the configured executable and requires matching
nonce, upload identity, content identity, byte counts, hashes, host, mode,
commit, feature configuration, start/end times, and explicit Steam attestation.
It consumes both manifest and receipt. Use `abort-session` to consume an
unwanted or failed attempt.

## Verification harnesses

```sh
cc -std=c11 -Wall -Wextra -Werror -I. -Iinclude \
  tools/test-custom-content-verify.c -o /tmp/test-custom-content-verify
/tmp/test-custom-content-verify

cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I. -Iinclude \
  tools/fuzz-custom-content-verify.c -o /tmp/fuzz-custom-content-verify
/tmp/fuzz-custom-content-verify [known-good.lev known-good.vrm]

python3 tools/test_custom_content_lab.py
```

The canonical 32-bit policy and loader harnesses remain the regression gate for
the existing seed-driven Alpha6 path. A completed lab review additionally
requires the matching real Steam receipt; static tests cannot replace it.
