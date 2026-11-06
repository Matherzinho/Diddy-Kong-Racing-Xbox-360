/* Xbox 360 (xsdk) has no dynamic loader: every title is one static XEX. This header lets code that probes for
   shared libraries compile; every call fails, as a missing library would. */
#ifndef SSB64_COMPAT_DLFCN_H
#define SSB64_COMPAT_DLFCN_H
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;
static inline int dladdr(const void *address, Dl_info *info) { (void)address; (void)info; return 0; }
static inline void *dlopen(const char *file, int mode) { (void)file; (void)mode; return 0; }
static inline void *dlsym(void *handle, const char *name) { (void)handle; (void)name; return 0; }
static inline int dlclose(void *handle) { (void)handle; return 0; }
static inline char *dlerror(void) { return (char *)"dynamic loading is not available on Xbox 360"; }
#ifdef __cplusplus
}
#endif
#endif
