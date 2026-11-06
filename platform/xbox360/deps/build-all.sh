#!/bin/bash
# Fetches and cross-builds everything BattleShip needs beyond the xsdk toolchain, with no tarball from anyone:
#   $XSDK_SYSROOT   zlib, libzip, tinyxml2, nlohmann-json, spdlog, ogg, vorbis, opus, opusfile, SDL2 over SDL-xenon
#   $SSB64_DEPS     ImGui (patched as libultraship patches it), thread-pool, stb
# Called by ../build.sh. Needs XSDK_HOME (xsdk/, xsdk-gcc/, SDL-xenon/ built), WORK, BS (BattleShip's checkout).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${XSDK_HOME:?}" "${WORK:?}" "${BS:?}"
export HOME="$XSDK_HOME"
export XSDK_SYSROOT="${XSDK_SYSROOT:-$WORK/sysroot}"
SSB64_DEPS="${SSB64_DEPS:-$WORK/deps}"
SRC="$WORK/deps-src"
BUILD="$WORK/deps-build"
TC="$HERE/../cmake/ssb64-xsdk.cmake"
P="$XSDK_HOME/xsdk-gcc/bin/powerpc-xsdk-elf-"
mkdir -p "$XSDK_SYSROOT" "$SSB64_DEPS" "$SRC" "$BUILD"

SDL3_LIB="$XSDK_HOME/SDL-xenon/build-xsdk/libSDL3.a"
[ -f "$SDL3_LIB" ] || { echo "SDL-xenon isn't built: $SDL3_LIB is missing (see the README)" >&2; exit 1; }

fetch() { # name url tag-or-commit (the versions in VERSIONS)
	[ -e "$SRC/$1/.fetched" ] && return
	rm -rf "$SRC/$1"
	git clone -q "$2" "$SRC/$1"
	git -C "$SRC/$1" checkout -q "$3"
	git -C "$SRC/$1" submodule update -q --init 2>/dev/null || true
	touch "$SRC/$1/.fetched"
}
while read -r name url tag _; do
	[ -n "${name:-}" ] && fetch "$name" "$url" "$tag"
done <"$HERE/VERSIONS"

cm() { # name, extra cmake args...
	local name=$1; shift
	echo "== $name"
	cmake -S "$SRC/$name" -B "$BUILD/$name" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$XSDK_SYSROOT" -DBUILD_SHARED_LIBS=OFF "$@" \
		>"$BUILD/$name.configure.log" 2>&1 || { tail -30 "$BUILD/$name.configure.log"; exit 1; }
	cmake --build "$BUILD/$name" >"$BUILD/$name.build.log" 2>&1 || { tail -40 "$BUILD/$name.build.log"; exit 1; }
	cmake --install "$BUILD/$name" >/dev/null
}

cm zlib -DZLIB_BUILD_EXAMPLES=OFF
cm libzip -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DENABLE_OPENSSL=OFF -DENABLE_GNUTLS=OFF \
	-DENABLE_MBEDTLS=OFF -DENABLE_COMMONCRYPTO=OFF -DENABLE_WINDOWS_CRYPTO=OFF -DBUILD_TOOLS=OFF \
	-DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
	-DZLIB_ROOT="$XSDK_SYSROOT" -DZLIB_USE_STATIC_LIBS=ON
cm tinyxml2 -Dtinyxml2_BUILD_TESTING=OFF
cm json -DJSON_BuildTests=OFF
cm spdlog -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_BUILD_TESTS=OFF -DSPDLOG_NO_TLS=ON -DSPDLOG_NO_THREAD_ID=ON
cm ogg -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF
cm vorbis -DOGG_ROOT="$XSDK_SYSROOT" -DBUILD_TESTING=OFF
cm opus -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_DISABLE_INTRINSICS=ON -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF

echo "== opusfile"
mkdir -p "$BUILD/opusfile" "$XSDK_SYSROOT/include/opus"
for f in info internal opusfile stream; do
	"${P}gcc" -O2 -mcpu=cell -fno-pic -I"$XSDK_HOME/xsdk/include" -I"$XSDK_SYSROOT/include" -I"$XSDK_SYSROOT/include/opus" \
		-I"$SRC/opusfile/include" -DOP_DISABLE_HTTP -DOP_DISABLE_FLOAT_API=0 -w \
		-c "$SRC/opusfile/src/$f.c" -o "$BUILD/opusfile/$f.o"
done
rm -f "$XSDK_SYSROOT/lib/libopusfile.a"
"${P}ar" rcs "$XSDK_SYSROOT/lib/libopusfile.a" "$BUILD"/opusfile/*.o
cp "$SRC/opusfile/include/opusfile.h" "$XSDK_SYSROOT/include/opus/"

# SDL2's API on the console: SDL-xenon (SDL3) with its SDL_* symbols renamed to SDL3X_*, plus sdl2-compat (patched to
# look SDL3 up in a static table) as a static libSDL2.a.
echo "== SDL2 over SDL-xenon"
SDL2C="$SRC/sdl2-compat"
mkdir -p "$BUILD/sdl"
"${P}nm" -g --defined-only "$SDL3_LIB" 2>/dev/null | awk '$3 ~ /^SDL_/ {print $3" SDL3X_"substr($3,5)}' |
	sort -u >"$BUILD/sdl/sdl3-rename.map"
"${P}objcopy" --redefine-syms="$BUILD/sdl/sdl3-rename.map" "$SDL3_LIB" "$XSDK_SYSROOT/lib/libSDL3X.a"
git -C "$SDL2C" apply --check -R "$HERE/patches/sdl2-compat-xsdk.patch" 2>/dev/null ||
	git -C "$SDL2C" apply "$HERE/patches/sdl2-compat-xsdk.patch"
for f in src/sdl2_compat.c src/dynapi/SDL_dynapi.c; do
	"${P}gcc" -O2 -mcpu=cell -fno-pic -I"$XSDK_HOME/xsdk/include" -I"$SDL2C/include" -I"$SDL2C/include/SDL2" \
		-I"$XSDK_HOME/SDL-xenon/include" -DNDEBUG -w -c "$SDL2C/$f" -o "$BUILD/sdl/$(basename "$f" .c).o"
done
rm -f "$XSDK_SYSROOT/lib/libSDL2.a"
"${P}ar" rcs "$XSDK_SYSROOT/lib/libSDL2.a" "$BUILD"/sdl/sdl2_compat.o "$BUILD"/sdl/SDL_dynapi.o
mkdir -p "$XSDK_SYSROOT/include/SDL2"
cp -r "$SDL2C/include/SDL2/." "$XSDK_SYSROOT/include/SDL2/"
cp "$HERE/sdl2-headers/SDL_config_xsdk.h" "$XSDK_SYSROOT/include/SDL2/"
# Select it before the __unix__ branch (xsdk's GCC defines __unix__).
sed -i 's|^#if defined(__WIN32__)$|#if defined(__XSDK__)\n#include "SDL_config_xsdk.h"\n#elif defined(__WIN32__)|' \
	"$XSDK_SYSROOT/include/SDL2/SDL_config.h"

# Sources libultraship would fetch itself on a PC.
echo "== ImGui, thread-pool, stb"
if [ ! -d "$SSB64_DEPS/imgui" ]; then
	git clone -q --depth 1 --branch v1.91.9b-docking https://github.com/ocornut/imgui.git "$SSB64_DEPS/imgui"
	git -C "$SSB64_DEPS/imgui" apply "$BS/libultraship/cmake/dependencies/patches/imgui-fixes-and-config.patch"
fi
if [ ! -d "$SSB64_DEPS/thread-pool" ]; then
	git clone -q --depth 1 --branch v4.1.0 https://github.com/bshoshany/thread-pool.git "$SSB64_DEPS/thread-pool"
fi
mkdir -p "$SSB64_DEPS/stb"
[ -f "$SSB64_DEPS/stb/stb_image.h" ] || curl -fsSL -o "$SSB64_DEPS/stb/stb_image.h" \
	https://github.com/nothings/stb/raw/0bc88af4de5fb022db643c2d8e549a0927749354/stb_image.h
echo "dependencies installed: $XSDK_SYSROOT, $SSB64_DEPS"
