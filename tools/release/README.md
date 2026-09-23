# CTR-AP release preparation

`assemble-release.py` combines the exact AP artifacts from the native `Build
clients` workflow with a packed `ctr.apworld` and a generated player template.
It accepts a native checkout, the two downloaded artifact directories, the
apworld, template, pair version, and output directory explicitly:

```sh
python3 tools/release/assemble-release.py \
  --native-source /path/to/native-checkout \
  --windows-artifacts /path/to/windows-artifacts \
  --linux-artifacts /path/to/linux-artifacts \
  --apworld /path/to/ctr.apworld \
  --template /path/to/Crash.Team.Racing.yaml \
  --version 0.2.0-rc-prep1 \
  --output-dir /path/to/release
```

The command refuses a source/version mismatch, a mismatched `BUILD.json`,
missing or tampered sidecars, unsafe or unexpected archive members, an
apworld/template identity mismatch, and an existing output asset. It repeats
the platform/debuglink checks from `tools/ci/package-client.py` and emits the
eleven names required by `RELEASING.md`. The archives contain the stripped
client, apworld, versions file, setup/license/notices, extraction helper,
platform support helpers, and the `assets/` tree that `package-client.py`
placed in the build archive. Debug sidecars are standalone assets and
extracted retail game assets are never read or included.

With `--authoring`, the assembler also repackages the separate box authoring
archives (`ctr-ap-authoring-<platform>-x86-<commit>`) as
`ctr-archipelago-vX.Y.Z-box-authoring-<platform>-x86` downloads with their
`.sha256` sidecars. They are optional release assets: not one of the eleven, not
in the manifest, and the verifier accepts a release with or without them but
checks each archive against its sidecar when present.

After the eleven standard assets pass their completeness check, the assembler
writes `manifest.json` from the final archive bytes. It covers exactly the two
client archives and refuses to overwrite an existing manifest. The shared
policy in `release_policy.py` owns the standard asset list, the signed manifest
contract, strict duplicate-rejecting JSON loading and the checksum-sidecar
checks, and both the assembler and `tools/verify-release.py` import it.

`tools/verify-release.py` verifies a release against the signed manifest.
Default mode fails closed: it requires the exact published file set (eleven
standard assets plus `manifest.json` and `manifest.json.minisig`), a
non-placeholder committed public key and minisign, verifies the signature over
the exact manifest bytes before parsing, binds the manifest version to a
required `--version`, checks the two archive hashes and validates every
standard `.sha256` sidecar:

```sh
python3 tools/verify-release.py /path/to/downloaded-release --version vX.Y.Z
```

`--pre-sign` is a distinct mode for a complete twelve-file pre-sign assembly
(the eleven standard assets plus `manifest.json`). It omits signature
verification and states unmistakably that the result is not a signed release:

```sh
python3 tools/verify-release.py /path/to/assembly --version vX.Y.Z --pre-sign
```

`.github/workflows/release-prepare.yml` is a manual, read-only preparation
workflow. It requires exact successful native and companion run IDs plus their
full source commits, downloads both hosted build outputs with `GH_TOKEN`,
generates the template from Archipelago 0.6.7 and the downloaded apworld,
assembles the eleven standard assets and the unsigned manifest, verifies that
assembly with `--pre-sign`, and uploads the twelve files as one workflow
artifact. It deliberately stops before signing, tagging or creating a GitHub
release. The owner signs the exact downloaded `manifest.json` outside CI, then
a clean download of the published release must pass default-mode verification
with the committed public key.

Build archive members may be nested. Anything directly under the archive root
must be one of the known package-client outputs, and the only permitted
subtree is `assets/`, which is copied into both bundles with its relative
paths intact. The build-only `BUILD-NOTICE.txt` is dropped and never ships.
The two platform archives must agree on the asset tree byte for byte. In the
tarball the Linux client and `support-bundle.sh` keep mode 755 and every other
regular file is 644.
