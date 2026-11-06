/* SDL2 public-header configuration for Xbox 360 titles built with xsdk (newlib C library).
   Only what SDL_stdinc.h and friends need: the SDL2 implementation is sdl2-compat over SDL3. */
#ifndef SDL_config_xsdk_h_
#define SDL_config_xsdk_h_
#define SDL_config_h_
#include "SDL_platform.h"
#define HAVE_LIBC 1
#define STDC_HEADERS 1
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_CTYPE_H 1
#define HAVE_MATH_H 1
#define HAVE_FLOAT_H 1
#define HAVE_WCHAR_H 1
#define HAVE_MALLOC 1
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#define HAVE_GCC_ATOMICS 1
#endif
