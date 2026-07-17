#!/bin/bash
# Fetch the header-only Archipelago networking deps into ap/vendor/.
# These are NOT committed (large, external); this script reproduces the layout
# the CMake AP block expects. Run once from anywhere; targets this dir.
#
#   ap/vendor/apclientpp        (apclient.hpp at root)
#   ap/vendor/wswrap/include    (wswrap.hpp)
#   ap/vendor/websocketpp       (websocketpp/ at root)
#   ap/vendor/asio/include      (asio.hpp)  <- lifted from repo's inner asio/
#   ap/vendor/json/include      (nlohmann/json.hpp)
set -e
cd "$(dirname "$(readlink -f "$0")")"

clone() { [ -d "$2" ] || git clone --depth 1 ${3:+--branch "$3"} "$1" "$2"; }

clone https://github.com/black-sliver/apclientpp.git apclientpp
clone https://github.com/black-sliver/wswrap.git     wswrap
clone https://github.com/zaphoyd/websocketpp.git     websocketpp 0.8.2
clone https://github.com/nlohmann/json.git           json        v3.11.3

if [ ! -d asio ]; then
  git clone --depth 1 --branch asio-1-30-2 https://github.com/chriskohlhoff/asio.git asio_repo
  mv asio_repo/asio asio && rm -rf asio_repo   # -> ap/vendor/asio/include/asio.hpp
fi

echo "AP vendor deps ready under $(pwd)"
