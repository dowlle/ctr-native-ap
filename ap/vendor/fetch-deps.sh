#!/bin/bash
# Fetch + verify the header-only Archipelago networking deps into ap/vendor/.
#
# These are NOT committed (large, external). This script reproduces the exact
# layout the CMake AP block expects AND pins every dependency to a commit sha,
# so a build is reproducible and a silently-swapped dep (issue #78: an RC once
# shipped apclientpp 0.6.0 instead of the tested 0.6.4) is caught before build.
#
# The pin set lives in versions.lock (committed) next to this script. Each dep
# is cloned at its pinned sha, the checkout's HEAD is checked against that sha
# (git's object model makes the sha self-verifying), .git is stripped, and a
# deterministic tree hash of the extracted files is compared to the manifest.
# The identical tree-hash algorithm is reimplemented in cmake/APVendorCheck.cmake
# so the same gate runs at configure time with no bash dependency.
#
# Layout produced (matches the include dirs in CMakeLists.txt):
#   ap/vendor/apclientpp        (apclient.hpp at root)
#   ap/vendor/wswrap/include    (wswrap.hpp)
#   ap/vendor/websocketpp       (websocketpp/ at root)
#   ap/vendor/asio/include      (asio.hpp)  <- lifted from the repo's inner asio/
#   ap/vendor/json/include      (nlohmann/json.hpp)
#
# valijson is intentionally NOT fetched: AP_NO_SCHEMA (CMakeLists.txt) compiles
# out apclientpp's valijson includes, so it never enters the build. It is listed
# optional in versions.lock; only fetch it if AP_NO_SCHEMA is ever removed.
#
# Usage:
#   ./fetch-deps.sh          fetch any missing dep at its pin, then verify all
#   ./fetch-deps.sh verify   re-hash the existing trees only (no network)
#   ./fetch-deps.sh hash     fetch if missing, print computed tree hashes
#                            (maintenance: use to (re)seed versions.lock)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
LOCK="versions.lock"

# --- deterministic tree hash -------------------------------------------------
# sha256 over the sorted list of "<relative-path> <sha256-of-content>\n" lines.
# Byte-for-byte identical to ap_tree_hash() in cmake/APVendorCheck.cmake:
#   * paths relative to the dep dir, forward slashes, leading "./" stripped
#   * .git excluded (stripped before hashing anyway)
#   * LC_ALL=C byte sort of paths (matches CMake list(SORT))
#   * one space between path and content hash, one '\n' per line (LF only)
tree_hash() {
  ( cd "$1" && find . -type f ! -path './.git/*' -print0 \
      | LC_ALL=C sort -z \
      | while IFS= read -r -d '' f; do
          printf '%s %s\n' "${f#./}" "$(sha256sum "$f" | cut -d' ' -f1)"
        done ) | sha256sum | cut -d' ' -f1
}

# --- versions.lock parser ----------------------------------------------------
# Flat "key = value" blocks, one per [dep]. Returns the value for key of the
# current dep, or empty. Values may contain spaces (e.g. "{0, 6, 4}").
lock_get() { # $1 = dep, $2 = key
  awk -v dep="$1" -v key="$2" '
    /^\[/ { cur = substr($0, 2, index($0,"]")-2); next }
    cur == dep {
      line = $0
      sub(/^[ \t]*#.*/, "", line)
      n = index(line, "=")
      if (n == 0) next
      k = line; sub(/[ \t]*=.*/, "", k); gsub(/^[ \t]+|[ \t]+$/, "", k)
      if (k == key) {
        v = substr(line, n+1); gsub(/^[ \t]+|[ \t]+$/, "", v); print v; exit
      }
    }' "$LOCK"
}

deps() { awk -F'[][]' '/^\[/{print $2}' "$LOCK"; }

# --- fetch one dep at its pinned sha, strip .git, arrange layout -------------
fetch_one() { # $1 = dep
  local dep="$1" url sha dir
  url=$(lock_get "$dep" url); sha=$(lock_get "$dep" sha); dir=$(lock_get "$dep" dir)
  [ -n "$url" ] && [ -n "$sha" ] && [ -n "$dir" ] || { echo "!! $dep: incomplete lock entry" >&2; exit 1; }
  [ -d "$dir" ] && return 0

  echo ">> fetching $dep at $sha"
  local work="_fetch_$dep"
  rm -rf "$work"
  git init -q "$work"
  git -C "$work" config core.autocrlf false
  git -C "$work" config core.eol lf
  git -C "$work" remote add origin "$url"
  # Fetch the exact commit. GitHub serves reachable shas; fall back to a full
  # fetch + checkout if the server refuses a by-sha want.
  if ! git -C "$work" -c protocol.version=2 fetch -q --depth 1 origin "$sha" 2>/dev/null; then
    echo "   (by-sha fetch refused, falling back to full fetch)" >&2
    git -C "$work" fetch -q origin
  fi
  git -C "$work" -c advice.detachedHead=false checkout -q "$sha"

  local head; head=$(git -C "$work" rev-parse HEAD)
  if [ "$head" != "$sha" ]; then
    echo "!! $dep: checked-out HEAD $head != pinned $sha" >&2; rm -rf "$work"; exit 1
  fi
  rm -rf "$work/.git"

  # Arrange to the on-disk layout. asio vendors only the repo's inner asio/.
  if [ "$dep" = "asio" ]; then
    mv "$work/asio" "$dir"
    rm -rf "$work"
  else
    mv "$work" "$dir"
  fi
}

# --- verify one dep against the manifest ------------------------------------
verify_one() { # $1 = dep ; returns 0 ok, 1 mismatch
  local dep="$1" dir want got
  dir=$(lock_get "$dep" dir)
  if [ ! -d "$dir" ]; then echo "!! $dep: missing dir '$dir'" >&2; return 1; fi
  want=$(lock_get "$dep" tree_sha256)
  got=$(tree_hash "$dir")
  if [ "$want" = "SEED" ] || [ -z "$want" ]; then
    echo "   $dep tree_sha256 = $got"
    return 0
  fi
  if [ "$got" != "$want" ]; then
    echo "!! $dep: tree hash mismatch" >&2
    echo "     expected $want" >&2
    echo "     actual   $got" >&2
    return 1
  fi
  echo "   $dep OK ($got)"
  return 0
}

mode="${1:-fetch}"
rc=0
case "$mode" in
  fetch)
    for d in $(deps); do
      [ "$(lock_get "$d" optional)" = "true" ] && continue
      fetch_one "$d"
    done
    for d in $(deps); do
      [ "$(lock_get "$d" optional)" = "true" ] && continue
      verify_one "$d" || rc=1
    done
    ;;
  verify)
    for d in $(deps); do
      [ "$(lock_get "$d" optional)" = "true" ] && continue
      verify_one "$d" || rc=1
    done
    ;;
  hash)
    for d in $(deps); do
      [ "$(lock_get "$d" optional)" = "true" ] && continue
      fetch_one "$d"
      dir=$(lock_get "$d" dir)
      printf '%s\t%s\n' "$d" "$(tree_hash "$dir")"
    done
    ;;
  *) echo "usage: $0 [fetch|verify|hash]" >&2; exit 2 ;;
esac

if [ "$rc" -ne 0 ]; then
  echo "" >&2
  echo "vendor verification FAILED -- the tree does not match versions.lock." >&2
  echo "delete the offending dir under ap/vendor/ and re-run ./fetch-deps.sh" >&2
  exit 1
fi
echo "AP vendor deps ready + verified under $(pwd)"
