#!/bin/bash
# Emit versions.txt for a release bundle: the CTR-AP release id plus the pinned
# Archipelago vendor set (name, version, commit sha) read from
# ap/vendor/versions.lock. Bundle the output next to ctr_native_ap.exe so a
# downloaded build is self-describing and a bug report names its exact deps
# (issue #78). Run from anywhere.
#
#   tools/release-versions.sh > dist/ctr-archipelago-vX.Y.Z/versions.txt
set -euo pipefail
here="$(dirname "$(readlink -f "$0")")"
root="$(cd "$here/.." && pwd)"

ver=$(grep -oE '"v[^"]+"' "$root/ap/ap_version.h" | head -1 | tr -d '"')
lock="$root/ap/vendor/versions.lock"

echo "CTR-AP release: ${ver}"
echo "Vendored Archipelago networking dependencies (ap/vendor/versions.lock):"
awk '
  /^\[/          { d = substr($0, 2, index($0,"]")-2) }
  /^version *=/  { v=$0; sub(/^version *= */,"",v); ver[d]=v }
  /^sha *=/      { s=$0; sub(/^sha *= */,"",s);     sha[d]=s }
  END {
    split("apclientpp wswrap websocketpp asio json", a, " ")
    for (i=1; i<=5; i++) printf "  %-12s %-9s %s\n", a[i], ver[a[i]], sha[a[i]]
  }' "$lock"
