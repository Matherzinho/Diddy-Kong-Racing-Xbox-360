# Diddy Kong Racing for Xbox 360

Work in progress, by **Matherzin**: [Golden Balloon](https://github.com/akratch/goldenballoon) (akratch's native PC port of
Diddy Kong Racing, on the [DavidSM64/Diddy-Kong-Racing](https://github.com/DavidSM64/Diddy-Kong-Racing) decompilation)
built for the Xbox 360 (RGH/JTAG) with [xsdk](https://github.com/xenosworks/xsdk), drawing through the Xenos renderer
of the Star Fox 64, Super Smash Bros. 64 and Zelda ports, reshaped for Golden Balloon's fast3d API.

**This repository contains no game: no ROM, no data and no executable.** It holds a patch over Golden Balloon, the
Xbox 360 platform layer and build scripts. You build the `.xex` yourself and use your own cartridge dump.

## Status

Runs in Xenia Canary: title, menus, Time Trial and four-player split-screen races on Ancient Lake, 60 fps
(interpolated), widescreen, sound, saves, the console menu. **Not yet run on a console.** See [CONTINUAR.md](CONTINUAR.md)
(Portuguese) for the details, what was checked where, and what's next.

## What you need

- A console that runs unsigned code (RGH or JTAG) with a dashboard such as Aurora.
- Your own dump of Diddy Kong Racing: USA 1.0 (SHA-1 `0cb115d8716dbbc2922fda38e533b9fe63bb9670`, what this port was
  tested with) or 1.1, or Europe 1.0/1.1, as `.z64`, `.n64` or `.v64`.
- WSL 2 (or Linux) with xsdk, its GCC (`~/xsdk`, `~/xsdk-gcc`), SDL-xenon (`~/SDL-xenon`, built) and glslang-xenon, as
  in the [Super Smash Bros. 64 port's README](https://github.com/Matherzinho/Super-Smash-Bros-64---Xbox-360-port).

## Build

```bash
platform/xbox360/build.sh                       # -> ~/dkr360-work/build/dkr360.xex
platform/xbox360/make-game-data.sh "Diddy Kong Racing (USA).n64" ~/dkr360-game   # -> dkr.z64
cp ~/dkr360-work/build/dkr360.xex ~/dkr360-game/
```

Copy `dkr360.xex` and `dkr.z64` into one folder on the console and open `dkr360.xex`. Settings and saves go into a
`dkr360` folder beside it; the log is `dkr360.log`. **Please don't share `dkr.z64` or the `.xex`.**

## Layout

- `patches/goldenballoon-xsdk.patch`: Golden Balloon at `patches/base-commits.txt`, made to build and run on the
  console (big-endian gates on its in-place swaps of ROM data, the Xbox 360's address map in the display-list
  resolver, Xenos in place of OpenGL, the 1.0 cartridge's magic-code table, XAM controllers as a fallback, ...).
- `platform/xbox360/`: `build.sh`, the CMake project (`project/`), the toolchain file, the dependency scripts (`deps/`,
  from the Star Fox port), and the sources: `gfx_xenos.cpp` (renderer), `dkr360_main.c`, `dkr360_menu.cpp` (the
  console menu, on Golden Balloon's own settings), `xsdk_glue.c`, stubs, Xenos shaders.
- `tools/xenia/`: running and checking it in Xenia without a display (frame captures, audio dump, input scripts).
