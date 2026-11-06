#!/bin/bash
# dkr360: builds Golden Balloon (Diddy Kong Racing PC port) for the Xbox 360 with xsdk.
#   XSDK_HOME   folder with xsdk/, xsdk-gcc/, SDL-xenon/ (built) and glslang-xenon/ (default $HOME)
#   WORK        where sources and the build go (default ~/dkr360-work)
#   JOBS        parallel compiles (default nproc)
# Golden Balloon is cloned at patches/base-commits.txt and patches/goldenballoon-xsdk.patch is applied. The result is
# $WORK/build/dkr360.xex. No game data here: make-game-data.sh checks and converts the player's own ROM.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
XSDK_HOME="${XSDK_HOME:-$HOME}"
WORK="${WORK:-$HOME/dkr360-work}"
JOBS="${JOBS:-$(nproc)}"
export GIT_TERMINAL_PROMPT=0
commit() { awk -v k="$1" '$1==k{print $2}' "$ROOT/patches/base-commits.txt"; }
mkdir -p "$WORK"
GB="$WORK/goldenballoon"
if [ ! -d "$GB/.git" ]; then
	git clone -q https://github.com/akratch/goldenballoon "$GB"
	git -C "$GB" checkout -q "$(commit goldenballoon)"
	git -C "$GB" apply "$ROOT/patches/goldenballoon-xsdk.patch"
fi
# SDL2 (sdl2-compat over SDL-xenon) for controllers and sound; the deps script also builds a few libraries this
# port doesn't link, which costs nothing but time.
if [ ! -f "$WORK/sysroot/lib/libSDL2.a" ]; then
	XSDK_HOME="$XSDK_HOME" WORK="$WORK" BS="$GB" bash "$HERE/deps/build-all.sh"
fi
export HOME="$XSDK_HOME" XSDK_SYSROOT="$WORK/sysroot"
cmake -S "$HERE/project" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/dkr-xsdk.cmake" -DGB_DIR="$GB" >"$WORK/configure.log"
cmake --build "$WORK/build" -j "$JOBS" ${KEEP_GOING:+-- -k 0}
ls -la "$WORK/build/dkr360.xex"
