#!/usr/bin/env bash
# Build and run tools/controls/test_native_controls.c (the Controls page module,
# ported from CTR Native Expanded). Run from anywhere; CI runs it through the
# `cmd` line in tools/ci/harnesses.txt.
#
# The harness links SDL3 for key and gamepad names and the pref path it uses as a
# scratch directory, so a host build of the vendored externals/SDL is made first,
# once, under $HARNESS_OUT (default /tmp/ctr-harness, the same directory
# run-harnesses.py uses). Only the subsystems the harness touches are enabled, so
# the build needs no X11, Wayland or audio development packages. Both are 32-bit,
# like the game: the pad structures it replays are size-checked for -m32.
#
# Usage: bash tools/controls/run-tests.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${HARNESS_OUT:-/tmp/ctr-harness}"
sdl_build="$out/sdl3-host-m32"
jobs="${CONTROLS_TEST_JOBS:-2}"

mkdir -p "$out"

if [ ! -f "$sdl_build/libSDL3.a" ]; then
	cmake -S "$root/externals/SDL" -B "$sdl_build" -G "Unix Makefiles" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
		-DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF \
		-DSDL_VIDEO=OFF -DSDL_AUDIO=OFF -DSDL_RENDER=OFF -DSDL_GPU=OFF \
		-DSDL_CAMERA=OFF -DSDL_SENSOR=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
		-DSDL_POWER=OFF -DSDL_HAPTIC=ON -DSDL_JOYSTICK=ON -DSDL_HIDAPI=OFF \
		-DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_UNIX_CONSOLE_BUILD=ON \
		>"$out/sdl3-host-configure.log" 2>&1 \
		|| { tail -40 "$out/sdl3-host-configure.log"; exit 1; }
	cmake --build "$sdl_build" -j "$jobs" >"$out/sdl3-host-build.log" 2>&1 \
		|| { tail -40 "$out/sdl3-host-build.log"; exit 1; }
fi

cc -m32 -std=gnu17 -Wall -Wextra -Wno-unused-function -DCTR_AP \
	-I "$root" -I "$root/include" -I "$root/externals/SDL/include" \
	"$root/tools/controls/test_native_controls.c" "$sdl_build/libSDL3.a" \
	-o "$out/test_native_controls" -lm -lpthread -ldl

"$out/test_native_controls"
