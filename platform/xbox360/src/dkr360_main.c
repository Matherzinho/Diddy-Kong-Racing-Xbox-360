/* dkr360: the Xbox 360's main(). Golden Balloon's own main (platform/main_pc.c, mdkr64_headless_main under __XSDK__)
 * runs on a thread with 16 MB of stack on hardware thread 2: xsdk's main thread has 1 MB, and a stack overflow locks the
 * console up without a word in the log (oot360 test 4). Its settings and saves live in game:\dkr360\ beside the .xex;
 * the ROM is game:\dkr.z64 (make-game-data.sh). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xsdk/log.h>
#include <xsdk/thread.h>

int mdkr64_headless_main(int argc, char **argv);
void Ssb64_StartHeartbeat(void);
void Oot360_LogMemory(const char *where);
void dkr360_settings_load(void); /* dkr360_menu.cpp */

static int sExitCode;

static int FileExists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static void *GameThread(void *arg) {
	(void)arg;
	xsdk_thread_move(2);
	static char *argv[8] = { "dkr360", "--rom", "dkr.z64" };
	int argc = 3;
	/* Test runs where nobody holds a pad (Xenia): Golden Balloon's own input script, a line "frame BUTTONS [hold]
	 * [P1-P4]" each (tests/input_scripts/), and its autopilot, which drives the racing line. */
	if (FileExists("dkr360_autopad.txt")) {
		argv[argc++] = "--input-script";
		argv[argc++] = "dkr360_autopad.txt";
		xsdk_log_write("dkr360: input script dkr360_autopad.txt");
	}
	if (FileExists("dkr360_autopilot.txt")) {
		setenv("MDKR_AUTOPILOT", "1", 1);
		xsdk_log_write("dkr360: autopilot on");
	}
	argv[argc] = NULL;
	sExitCode = mdkr64_headless_main(argc, argv);
	xsdk_log_write("dkr360: the game returned %d", sExitCode);
	return NULL;
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	xsdk_log_write("dkr360: Diddy Kong Racing (Golden Balloon) for Xbox 360, port by Matherzin");
	Oot360_LogMemory("start");
	mkdir("dkr360", 0777);
	mkdir("dkr360/saves", 0777);
	mkdir("dkr360/mods", 0777);
	mkdir("dkr360/characters", 0777);
	/* Golden Balloon's paths (platform/user_paths.c reads these before anything of SDL's). */
	setenv("MDKR_SAVE_DIR", "dkr360/saves", 1);
	setenv("MDKR_VIDEO_CONFIG_PATH", "dkr360/video.ini", 1);
	setenv("MDKR_APP_PREFS_DIR", "dkr360", 1);
	setenv("MDKR_MODS_DIR", "dkr360/mods", 1);
	setenv("MDKR_CUSTOM_CHARACTER_DIRECTORY", "dkr360/characters", 1);
	/* The US 1.0 cartridge (us.v77) as well as 1.1: Golden Balloon re-points the asset table per revision
	 * (platform/rom_io.c, docs/ROM_REVISIONS.md: all five finish a Time Trial) but only takes 1.1 unless told. */
	setenv("MDKR_ROM_ANY_REVISION", "1", 1);
	setenv("MDKR_NO_CRASH_HANDLER", "1", 1);
	/* The renderer draws straight into the 1280x720 screen: no supersampling, MSAA or mip chains. */
	setenv("MDKR_RENDER_SCALE", "1", 1);
	setenv("MDKR_MSAA", "0", 1);
	setenv("MDKR_MIPMAPS", "0", 1);
	setenv("MDKR_ANISOTROPY", "1", 1);
	dkr360_settings_load(); /* the console menu's settings: 60/30 fps, 16:9 or 4:3, volumes (MDKR_* variables) */
	Ssb64_StartHeartbeat();

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 16u << 20);
	pthread_t game;
	if (pthread_create(&game, &attr, GameThread, NULL) != 0) {
		xsdk_log_write("dkr360: could not start the game's thread");
		return 1;
	}
	pthread_attr_destroy(&attr);
	pthread_join(game, NULL);
	return sExitCode;
}
