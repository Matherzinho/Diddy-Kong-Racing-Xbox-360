# Wraps xsdk's toolchain file and adds this project's dependency sysroot (built by oot360's
# platform/xbox360/deps scripts: zlib, libzip, tinyxml2, nlohmann-json, spdlog, SDL2 = sdl2-compat over SDL-xenon).
# Paths: XSDK_ROOT_DIR (default ~/xsdk) and XSDK_SYSROOT (default ~/dkr360-work/sysroot).
if(NOT DEFINED ENV{XSDK_ROOT_DIR})
	set(ENV{XSDK_ROOT_DIR} "$ENV{HOME}/xsdk")
endif()
include("$ENV{XSDK_ROOT_DIR}/cmake/xsdk.cmake")
if(DEFINED ENV{XSDK_SYSROOT})
	set(XSDK_SYSROOT "$ENV{XSDK_SYSROOT}")
else()
	set(XSDK_SYSROOT "$ENV{HOME}/dkr360-work/sysroot")
endif()
# libultraship's xsdk dependency file (from oot360) names it OOT360_SYSROOT.
set(OOT360_SYSROOT "${XSDK_SYSROOT}")
list(APPEND CMAKE_FIND_ROOT_PATH "${XSDK_SYSROOT}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XSDK_SYSROOT OOT360_SYSROOT)
# This platform folder (the glue sources and the build-time generator BattleShip's CMakeLists.txt uses).
get_filename_component(DKR_XSDK_PLATFORM_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# POSIX headers xsdk's newlib lacks, as stubs (platform/xbox360/compat/include).
get_filename_component(DKR_COMPAT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../compat/include" ABSOLUTE)
list(APPEND CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${DKR_COMPAT_INCLUDE}")
list(APPEND CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${DKR_COMPAT_INCLUDE}")
string(APPEND CMAKE_C_FLAGS_INIT " -include ${DKR_COMPAT_INCLUDE}/dkr_xsdk_prelude.h")
string(APPEND CMAKE_CXX_FLAGS_INIT " -include ${DKR_COMPAT_INCLUDE}/dkr_xsdk_prelude.h")
# No fused multiply-add: GCC contracts a * b + c into fmadd on PowerPC by default, which rounds differently from the
# N64; BattleShip keeps the game's trig and physics bit-exact the same way on AArch64.
string(APPEND CMAKE_C_FLAGS_INIT " -ffp-contract=off")
string(APPEND CMAKE_CXX_FLAGS_INIT " -ffp-contract=off")
