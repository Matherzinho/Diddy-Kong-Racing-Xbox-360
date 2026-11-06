/* ssb64-360: POSIX calls BattleShip's desktop code makes that xsdk's newlib doesn't have. A title can't start another
 * process or open a shell pipe on the Xbox 360, so those fail as on a system without them (ENOSYS); usleep sleeps. */
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>

int usleep(unsigned int usec) {
	struct timespec ts;
	ts.tv_sec = usec / 1000000u;
	ts.tv_nsec = (long)(usec % 1000000u) * 1000L;
	return nanosleep(&ts, NULL);
}

pid_t fork(void) {
	errno = ENOSYS;
	return -1;
}

int execve(const char* path, char* const argv[], char* const envp[]) {
	(void)path;
	(void)argv;
	(void)envp;
	errno = ENOSYS;
	return -1;
}

pid_t wait(int* status) {
	(void)status;
	errno = ENOSYS;
	return -1;
}

FILE* popen(const char* command, const char* mode) {
	(void)command;
	(void)mode;
	errno = ENOSYS;
	return NULL;
}

int pclose(FILE* stream) {
	(void)stream;
	errno = ENOSYS;
	return -1;
}

/* dkr360: declared in compat/include/dkr_xsdk_prelude.h. */
#include <sys/stat.h>
struct utimbuf;
int lstat(const char* path, struct stat* st) {
	return stat(path, st);
}
int utime(const char* path, const struct utimbuf* times) {
	(void)path;
	(void)times;
	return 0;
}
