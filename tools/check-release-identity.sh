#!/bin/bash
# Verify that a packaged native client and apworld carry one matched release
# identity. Run this before constructing either platform archive.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <ctr_native_ap binary> <ctr.apworld>" >&2
  exit 2
fi

native="$1"
apworld="$2"
here="$(dirname "$(readlink -f "$0")")"
root="$(cd "$here/.." && pwd)"

for file in "$native" "$apworld"; do
  if [ ! -f "$file" ]; then
    echo "missing release artifact: $file" >&2
    exit 2
  fi
done

compat_v=$(awk '$2 == "CTR_AP_COMPAT_VERSION" { gsub(/"/, "", $3); print $3 }' \
  "$root/ap/ap_version.h")
build_v=$(awk '$2 == "CTR_AP_VERSION" { gsub(/"/, "", $3); print $3 }' \
  "$root/ap/ap_version.h")
compat="${compat_v#v}"
build="${build_v#v}"

manifest=$(unzip -p "$apworld" ctr/archipelago.json)
version_py=$(unzip -p "$apworld" ctr/version.py)

manifest_compat=$(printf '%s\n' "$manifest" | sed -n \
  's/.*"world_version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')
source_compat=$(printf '%s\n' "$version_py" | sed -n \
  's/^COMPAT_VERSION[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p')
source_build=$(printf '%s\n' "$version_py" | sed -n \
  's/^BUILD_VERSION[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p')

check_equal() {
  label="$1"
  actual="$2"
  expected="$3"
  if [ "$actual" != "$expected" ]; then
    echo "identity mismatch: $label is '$actual', expected '$expected'" >&2
    exit 1
  fi
  echo "ok: $label = $actual"
}

check_binary_string() {
  label="$1"
  needle="$2"
  # Do not use grep -q under pipefail: on a large Windows executable it exits
  # as soon as it finds the string, `strings` receives SIGPIPE, and the matched
  # pipeline is incorrectly treated as a failure.
  if ! strings "$native" | grep -Fx "$needle" >/dev/null; then
    echo "identity missing from native $label: $needle" >&2
    exit 1
  fi
  echo "ok: native $label carries $build_v"
}

check_equal "apworld manifest compatibility" "$manifest_compat" "$compat"
check_equal "apworld source compatibility" "$source_compat" "$compat"
check_equal "apworld source build" "$source_build" "$build"
check_binary_string "window title" "Crash Team Racing - CTR-AP $build_v"
check_binary_string "boot log" "[AP BOOT] ===== client run start ===== ($build_v)"
check_binary_string "exported diagnostics" "  \"client\": \"$build_v\","

echo "matched release pair: compatibility $compat, build $build"
sha256sum "$native" "$apworld"
