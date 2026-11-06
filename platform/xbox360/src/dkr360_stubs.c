/* dkr360: what Golden Balloon's desktop code calls that the Xbox 360 build has no use for or xsdk's newlib lacks.
 *   - custom-character name text (HarfBuzz/SheenBidi shaping, platform/fast3d/gfx_character_text.c): not built; every
 *     name falls back to the game's own font, as a name the shaper can't render does on the PC
 *   - file locks: one title, one process: flock always succeeds
 *   - processes (opening a folder in the file manager): fail as on a system without them
 *   - fstatat (mod and character folder scans): fails, so those scans find nothing; nothing is installed there */
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gfx_character_text.h"

bool gfx_character_text_measure(const char *source, size_t source_capacity, uint32_t max_width, uint32_t height,
				GfxCharacterTextMetrics *metrics) {
	(void)source;
	(void)source_capacity;
	(void)max_width;
	(void)height;
	if (metrics) {
		memset(metrics, 0, sizeof *metrics);
		metrics->fallback_reason = GFX_CHARACTER_TEXT_FALLBACK_FONT_UNAVAILABLE;
	}
	return false;
}

bool gfx_character_text_render_rgba(const char *source, size_t source_capacity, uint32_t max_width, uint32_t height,
				    uint8_t *output, size_t output_size, size_t output_stride,
				    GfxCharacterTextMetrics *metrics) {
	(void)output;
	(void)output_size;
	(void)output_stride;
	return gfx_character_text_measure(source, source_capacity, max_width, height, metrics);
}

const char *gfx_character_text_fallback_reason_name(GfxCharacterTextFallbackReason reason) {
	return reason == GFX_CHARACTER_TEXT_FALLBACK_NONE ? "none" : "font-unavailable";
}

const char *gfx_character_text_latin_face_base85(void) {
	return "";
}
const char *gfx_character_text_arabic_face_base85(void) {
	return "";
}
const char *gfx_character_text_hebrew_face_base85(void) {
	return "";
}
void gfx_character_text_shutdown(void) {
}

int flock(int fd, int operation) {
	(void)fd;
	(void)operation;
	return 0;
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags) {
	(void)flags;
	if (dirfd == -2 /* AT_FDCWD */ || (path && path[0] && strchr(path, ':'))) {
		return stat(path, st);
	}
	errno = ENOSYS;
	return -1;
}

int rmdir(const char *path) {
	(void)path;
	errno = ENOSYS;
	return -1;
}

int posix_memalign(void **out, size_t alignment, size_t size) {
	void *p = memalign(alignment, size);
	if (!p) {
		return ENOMEM;
	}
	*out = p;
	return 0;
}

pid_t waitpid(pid_t pid, int *status, int options) {
	(void)pid;
	(void)status;
	(void)options;
	errno = ENOSYS;
	return -1;
}

int execvp(const char *file, char *const argv[]) {
	(void)file;
	(void)argv;
	errno = ENOSYS;
	return -1;
}

typedef struct posix_spawn_file_actions posix_spawn_file_actions_t_dkr;
int posix_spawnp(pid_t *pid, const char *file, const void *file_actions, const void *attrp, char *const argv[],
		 char *const envp[]) {
	(void)pid;
	(void)file;
	(void)file_actions;
	(void)attrp;
	(void)argv;
	(void)envp;
	return ENOSYS;
}
