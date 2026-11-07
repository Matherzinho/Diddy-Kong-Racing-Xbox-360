/* dkr360: a cheap profiler for the game's thread (dkr360_prof.c logs it every 5 s). Slots nest where their code
 * does: endframe holds gpuwait and menu; replay holds its own endframe. */
#ifndef DKR360_PROF_H
#define DKR360_PROF_H

enum {
	DKR360_PROF_WALK,     /* gfx_run: the authored frame's display list */
	DKR360_PROF_REPLAY,   /* interpolated frames' walks */
	DKR360_PROF_ENDFRAME, /* gfx_end_frame */
	DKR360_PROF_AUDIO,    /* the audio service: synthesis */
	DKR360_PROF_PACE,     /* the frame pacer's sleep */
	DKR360_PROF_TEXTURE,  /* texture uploads to Xenos */
	DKR360_PROF_GPUWAIT,  /* waiting for the GPU or a front buffer */
	DKR360_PROF_MENU,     /* the console menu */
	DKR360_PROF_TICK,     /* (unused: the game's simulation is what the top-level slots leave) */
	DKR360_PROF_FLUSH,    /* inside walks: draw_triangles */
	DKR360_PROF_BIND,     /* inside walks: texture binding (cache lookups, decodes, uploads) */
	DKR360_PROF_VTX,      /* inside walks: G_VTX, vertex transform and lighting */
	DKR360_PROF_POLY,     /* inside walks: G_TRIN, triangle setup (holds bind and flush) */
	DKR360_PROF_SCAN,     /* inside walks: the display list prescans */
	DKR360_PROF_COPY,     /* inside endframe: the arena copy for the replays */
	DKR360_PROF_PRESENT,  /* inside endframe: front buffer, resolve, present */
	DKR360_PROF_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif
static inline unsigned long long dkr360_tb(void) {
	unsigned int hi, lo, hi2;
	do {
		__asm__ volatile("mftbu %0" : "=r"(hi));
		__asm__ volatile("mftb %0" : "=r"(lo));
		__asm__ volatile("mftbu %0" : "=r"(hi2));
	} while (hi != hi2);
	return ((unsigned long long)hi << 32) | lo;
}
void dkr360_prof_add(int slot, unsigned long long ticks);
void dkr360_prof_frame(void);
#ifdef __cplusplus
}
#endif

#define DKR360_PROF_BEGIN(name) const unsigned long long dkr360_prof_##name = dkr360_tb()
#define DKR360_PROF_END(name, slot) dkr360_prof_add((slot), dkr360_tb() - dkr360_prof_##name)
#endif
