/* dkr360: what platform/platform_sdl_min.c names of OpenGL, for the Xbox 360 build, which draws with Xenos
 * (gfx_xenos.cpp) and has no GL context. The present fences become no-ops: Xenos paces its own frames. */
#ifndef DKR360_GL_COMPAT_H
#define DKR360_GL_COMPAT_H
#include <stdint.h>
typedef void *GLsync;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned int GLbitfield;
typedef uint64_t GLuint64;
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x1
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#define GL_WAIT_FAILED 0x911D
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
static inline GLsync glFenceSync(GLenum c, GLbitfield f) { (void)c; (void)f; return (GLsync)0; }
static inline GLenum glClientWaitSync(GLsync s, GLbitfield f, GLuint64 t) { (void)s; (void)f; (void)t; return GL_ALREADY_SIGNALED; }
static inline void glDeleteSync(GLsync s) { (void)s; }
static inline void glFinish(void) {}
static inline void glFlush(void) {}
static inline const GLubyte *glGetString(GLenum n) { (void)n; return (const GLubyte *)"Xenos"; }
typedef void *(*GLADloadproc)(const char *name);
static inline int gladLoadGLLoader(GLADloadproc p) { (void)p; return 1; }

/* gfx_xenos.cpp */
#ifdef __cplusplus
extern "C" {
#endif
int dkr360_video_init(int *width, int *height);
void dkr360_video_present(void);
#ifdef __cplusplus
}
#endif
#endif
