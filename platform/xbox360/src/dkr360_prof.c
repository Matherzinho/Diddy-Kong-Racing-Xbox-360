/* dkr360: where a second of the game's thread goes, in the log every 5 s (dkr360_prof.h has the slots). The CPU's
 * time base (49.875 MHz) read around the frame's parts in patches/goldenballoon-xsdk.patch and gfx_xenos.cpp. */
#include "dkr360_prof.h"

#include <stdio.h>
#include <xsdk/log.h>

static unsigned long long sTicks[DKR360_PROF_COUNT];
static unsigned sHits[DKR360_PROF_COUNT];
static unsigned long long sWindowStart, sLastFrame, sWorstFrame;
static unsigned sFrames, sSlowFrames;

static const char *const kNames[DKR360_PROF_COUNT] = { "walk", "replay", "endframe", "audio", "pace",
						       "texture", "gpuwait", "menu", "tick",
						       "flush", "bind", "vtx", "poly", "scan", "copy", "present" };

void dkr360_prof_add(int slot, unsigned long long ticks) {
	if (slot >= 0 && slot < DKR360_PROF_COUNT) {
		sTicks[slot] += ticks;
		sHits[slot]++;
	}
}

/* Each presented frame (gfx_xenos.cpp's end_frame). */
void dkr360_prof_frame(void) {
	const unsigned long long now = dkr360_tb();
	enum { TB_PER_MS = 49875, WINDOW = 5 * 1000 * TB_PER_MS };
	if (sLastFrame != 0) {
		const unsigned long long frame = now - sLastFrame;
		if (frame > sWorstFrame) {
			sWorstFrame = frame;
		}
		if (frame > 20u * TB_PER_MS) {
			sSlowFrames++;
		}
	}
	sLastFrame = now;
	sFrames++;
	if (sWindowStart == 0) {
		sWindowStart = now;
		return;
	}
	if (now - sWindowStart < WINDOW) {
		return;
	}
	const double ms = (double)(now - sWindowStart) / TB_PER_MS;
	char line[512];
	int n = snprintf(line, sizeof line, "prof: %u frames in %.0f ms (worst %.1f ms, %u over 20 ms); ms/s:", sFrames,
			 ms, (double)sWorstFrame / TB_PER_MS, sSlowFrames);
	double accounted = 0.0;
	for (int i = 0; i < DKR360_PROF_COUNT; i++) {
		const double share = (double)sTicks[i] / TB_PER_MS * 1000.0 / ms;
		if (i == DKR360_PROF_TICK) {
			continue;
		}
		n += snprintf(line + n, sizeof line - n, " %s %.1f/%u", kNames[i], share, sHits[i]);
		// the top-level parts of the loop; the rest is the game's own simulation and bookkeeping
		if (i == DKR360_PROF_WALK || i == DKR360_PROF_REPLAY || i == DKR360_PROF_ENDFRAME ||
		    i == DKR360_PROF_AUDIO || i == DKR360_PROF_PACE) {
			accounted += share;
		}
		sTicks[i] = 0;
		sHits[i] = 0;
	}
	n += snprintf(line + n, sizeof line - n, " | game+other %.1f", 1000.0 - accounted);
	xsdk_log_write("%s", line);
	sWindowStart = now;
	sFrames = 0;
	sSlowFrames = 0;
	sWorstFrame = 0;
}
