/* Included before every source of the Xbox 360 build (platform/xbox360/cmake/dkr-xsdk.cmake, -include).
   newlib's <sys/types.h> keeps BSD's "#define quad quad_t", which renames struct members called quad in some
   translation units and not others (oot360 found it in Ocarina of Time's collider structs). */
#ifndef DKR_XSDK_PRELUDE_H
#define DKR_XSDK_PRELUDE_H
#include <sys/types.h>
#undef quad
/* The console is big-endian like the N64: Golden Balloon's in-place swaps of ROM data are skipped
   (patches/goldenballoon-xsdk.patch keys them on this; platform/asset_swap.c checks __BYTE_ORDER__ itself). */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MDKR_HOST_BIG_ENDIAN 1
#endif
/* POSIX calls newlib declares nowhere (src/xsdk_posix_stubs.c): no links on the console, so lstat is stat; utime does
   nothing (miniz sets extracted files' times with it). */
#ifdef __cplusplus
extern "C" {
#endif
struct stat;
struct utimbuf;
int lstat(const char *path, struct stat *st);
int utime(const char *path, const struct utimbuf *times);
#ifdef __cplusplus
}
#endif
#endif
