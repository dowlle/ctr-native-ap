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
client, apworld, versions file, setup/license/notices, extraction helper, and
platform support helpers. Debug sidecars are standalone assets and game assets
are never read or included.

`.github/workflows/release-prepare.yml` is a manual, read-only preparation
workflow. It requires exact successful native and companion run IDs plus their
full source commits, downloads both hosted build outputs with `GH_TOKEN`,
generates the template from Archipelago 0.6.7 and the downloaded apworld, and
uploads the eleven files as one workflow artifact. It deliberately stops
before tagging or creating a GitHub release; a human can review that artifact
and attach it to the player-facing native release.
