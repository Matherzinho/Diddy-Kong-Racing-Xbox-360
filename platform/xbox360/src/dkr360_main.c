/* dkr360: the Xbox 360's main(). Golden Balloon's own main (platform/main_pc.c, mdkr64_headless_main under __XSDK__)
 * runs on a thread with 16 MB of stack on hardware thread 2: xsdk's main thread has 1 MB, and a stack overflow locks the
 * console up without a word in the log (oot360 test 4). Its settings and saves live in game:\dkr360\ beside the .xex;
 * the ROM is game:\dkr.z64 (make-game-data.sh). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xsdk/kernel.h>
#include <xsdk/log.h>
#include <xsdk/system.h>
#include <xsdk/thread.h>
#include <xsdk/xam.h>

int mdkr64_headless_main(int argc, char **argv);
void Ssb64_StartHeartbeat(void);
void Oot360_LogMemory(const char *where);
void dkr360_settings_load(void); /* dkr360_menu.cpp */

static int sExitCode;

static int FileExists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

/* ---- the language: the console menu's setting (dkr360\console.ini, Language=), the ROM it means ---------------- */

/* Each language's ROM beside the .xex: the cartridge's (English), and translated ones the player puts there. */
static const char *const kRoms[3] = { "dkr.z64", "dkr_pt.z64", "dkr_es.z64" };
static int sRomLanguage; /* the one loaded: 0 English, 1 Portuguese (Brazil), 2 Spanish */

/* 0 automatic, 1 English, 2 Portuguese, 3 Spanish -> 0 English, 1 Portuguese, 2 Spanish; automatic follows the
 * console's language. */
int dkr360_language_resolve(int setting) {
	if (setting >= 1 && setting <= 3) {
		return setting - 1;
	}
	switch (xsdk_system_language()) {
	case XSDK_LANGUAGE_PORTUGUESE:
		return 1;
	case XSDK_LANGUAGE_SPANISH:
		return 2;
	default:
		return 0;
	}
}

int dkr360_rom_language(void) {
	return sRomLanguage;
}

int dkr360_rom_available(int language) {
	struct stat st;
	return language >= 0 && language < 3 && stat(kRoms[language], &st) == 0;
}

static int LanguageSetting(void) {
	int setting = 0;
	FILE *f = fopen("dkr360/console.ini", "r");
	if (f) {
		char line[128];
		while (fgets(line, sizeof line, f)) {
			int v;
			if (sscanf(line, "Language=%d", &v) == 1 && v >= 0 && v <= 3) {
				setting = v;
			}
		}
		fclose(f);
	}
	return setting;
}

/* Starts this .xex again (the menu, to load another language's ROM). ExLoadedImageName is the kernel's path, e.g.
 * \Device\Harddisk0\Partition1\Games\DKR\dkr360.xex; game:\ is its folder. */
void dkr360_restart(void) {
	const char *name = strrchr(ExLoadedImageName, '\\');
	char path[256];
	snprintf(path, sizeof path, "game:\\%s", name ? name + 1 : "dkr360.xex");
	xsdk_log_write("dkr360: restarting %s for the new language", path);
	if (xsdk_launch(path, NULL, 0) != 0) {
		xsdk_log_write("dkr360: restart failed");
	}
}

static void *GameThread(void *arg) {
	(void)arg;
	xsdk_thread_move(2);
	static char *argv[8] = { "dkr360", "--rom", "dkr.z64" };
	int argc = 3;
	{
		const int want = dkr360_language_resolve(LanguageSetting());
		sRomLanguage = dkr360_rom_available(want) ? want : 0;
		argv[2] = (char *)kRoms[sRomLanguage];
		if (sRomLanguage != 0) {
			/* A translation is a modified ROM: Golden Balloon checks the whole image against its reference's hash
			 * and refuses it unless told (platform/rom_validation.c, a developer override). Only for these ROMs. */
			setenv("MDKR_ROM_ALLOW_MODIFIED", "1", 1);
		}
		xsdk_log_write("dkr360: language %d, ROM %s%s", want, kRoms[sRomLanguage],
			       sRomLanguage != want ? " (the translated ROM isn't there)" : "");
	}
	/* Test runs where nobody holds a pad (Xenia): Golden Balloon's own input script, a line "frame BUTTONS [hold]
	 * [P1-P4]" each (tests/input_scripts/), and its autopilot, which drives the racing line. */
	/* A file dkr360_lead.txt (a number of microseconds, 0-12000; 0 turns it off) overrides how early the tick that is due is
	 * computed before its display slot (the present pacing's endpoint lead, platform_sdl_min.c; the default is 7000). */
	if (FileExists("dkr360_lead.txt")) {
		FILE *f = fopen("dkr360_lead.txt", "r");
		char buf[16] = { 0 };
		if (f != NULL) {
			if (fgets(buf, sizeof buf, f) != NULL) {
				unsigned long v = strtoul(buf, NULL, 10);
				if (v <= 12000ul) {
					snprintf(buf, sizeof buf, "%lu", v);
					setenv("MDKR_PRESENT_ENDPOINT_LEAD_US", buf, 1);
					xsdk_log_write("dkr360: endpoint lead %lu us (dkr360_lead.txt)", v);
				}
			}
			fclose(f);
		}
	}
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
