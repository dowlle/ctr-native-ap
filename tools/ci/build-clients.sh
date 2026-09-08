#!/usr/bin/env bash
# Same recipe locally and in Actions. Never run the game or fetch disc assets.
set -euo pipefail
cd "$(dirname "$0")/../.."
platform=${1:?usage: build-clients.sh linux|windows}
case "$platform" in
  linux) suffix= ;;
  windows) suffix=.exe ;;
  *) echo 'Expected linux or windows' >&2; exit 2 ;;
esac
if [ -e client-artifacts ] || [ -e build-ci-ap ] || [ -e build-ci-vanilla ]; then
  echo 'Use a fresh checkout/build directory; preserve prior artifacts.' >&2
  exit 1
fi
source_status=$(git status --porcelain --untracked-files=normal)
if [ -n "$source_status" ]; then
  echo 'Build provenance requires a clean checkout, including untracked files.' >&2
  printf '%s\n' "$source_status" >&2
  exit 1
fi
mkdir client-artifacts
bash ap/vendor/fetch-deps.sh
for variant in ap vanilla; do
  ap=OFF
  custom=OFF
  binary=ctr_native
  if [ "$variant" = ap ]; then
    ap=ON
    custom=ON
    binary=ctr_native_ap
  fi
  build=build-ci-$variant
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_FLAGS='-m32 -msse' -DCMAKE_CXX_FLAGS=-m32 \
    -DCTR_AP="$ap" -DCTR_CUSTOM_TRACKS="$custom" -DCTR_AP_AUTHORING=OFF \
    -DCTR_AP_VERIFY_VENDOR=ON
  cmake --build "$build" --parallel "${CTR_BUILD_JOBS:-2}"
  exe="$build/$binary$suffix"
  if [ "$platform" = windows ]; then
    objcopy --only-keep-debug "$exe" "$exe.debug"
    objcopy --strip-all "$exe"
    objcopy --add-gnu-debuglink="$exe.debug" "$exe"
  fi
  python3 tools/ci/package-client.py "$platform" "$variant" "$exe" client-artifacts
done
