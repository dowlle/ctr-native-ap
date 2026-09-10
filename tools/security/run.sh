#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
flags=(-m32 -std=c++17 -O0 -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STL_=1
       -Iap/vendor/wswrap/include -Iap/vendor/websocketpp -Iap/vendor/asio/include
       -Iap/vendor/apclientpp -Iap/vendor/json/include)
g++ "${flags[@]}" -DWSWRAP_WITH_SSL tools/security/transport-probe.cpp -lssl -lcrypto -lz -pthread -o "$work/transport"
python3 tools/security/test-transport.py "$work/transport"
g++ "${flags[@]}" tools/security/cache-probe.cpp -lssl -lcrypto -lz -pthread -o "$work/cache"
XDG_CACHE_HOME="$work/cache-root" "$work/cache"
