#ifdef __XSDK__
/* ssb64-360: what the Xbox 360 build needs around the game and libultraship, after oot360's soh/platform/xbox360
 * (MemTrace.c): malloc's span for the interpreter's pointer checks, memory and phase logging the Xenos backend calls,
 * 4 MB stacks for every thread, and the counters gfx_xenos.cpp prints (oot360's own interpreter fills them; here they
 * stay 0).
 *
 * The link wraps malloc, calloc, realloc, memalign and pthread_create (CMakeLists.txt, -Wl,--wrap=...). */
#include <malloc.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <xsdk/kernel.h>
#include <xsdk/log.h>

void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* ptr, size_t size);
void* __real_memalign(size_t align, size_t size);
int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);

#define SSB64_BIG_ALLOCATION (4u << 20)

static int sInTrace;

void Oot360_LogMemory(const char* where) {
	MM_QUERY_STATISTICS_RESULT st = { .size = sizeof(st) };
	struct mallinfo mi;
	if (sInTrace) {
		return;
	}
	sInTrace = 1;
	mi = mallinfo();
	if (NT_SUCCESS(MmQueryStatistics(&st))) {
		xsdk_log_write("mem: %s: free %u KB, image %u KB, heap pages %u KB, physical %u KB; malloc arena %u KB, in use "
			       "%u KB",
			       where, (unsigned)(st.title.available_pages * 4), (unsigned)(st.title.image_pages * 4),
			       (unsigned)(st.title.heap_pages * 4), (unsigned)(st.title.physical_pages * 4),
			       (unsigned)(mi.arena >> 10), (unsigned)(mi.uordblks >> 10));
	}
	sInTrace = 0;
}

static void NoteAllocation(const char* what, size_t size, void* result, void* r0, void* r1) {
	if (sInTrace) {
		return;
	}
	sInTrace = 1;
	xsdk_log_write("mem: %s(%u) = %p from %p %p", what, (unsigned)size, result, r0, r1);
	sInTrace = 0;
	if (!result) {
		Oot360_LogMemory("after a failed allocation");
	}
}

/* The heap's span as malloc has handed it out, so pointers into it can be told from N64 segment addresses
 * (libultraship's interpreter.cpp, gfxPointerInLoadedModule). The bounds only move outwards, by compare-and-swap. */
static uintptr_t sHeapLow = UINTPTR_MAX;
static uintptr_t sHeapHigh = 0;

static inline void Span(void* p, size_t size) {
	if (p) {
		const uintptr_t a = (uintptr_t)p;
		const uintptr_t end = a + size;
		uintptr_t low = __atomic_load_n(&sHeapLow, __ATOMIC_RELAXED);
		while (a < low && !__atomic_compare_exchange_n(&sHeapLow, &low, a, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
		}
		uintptr_t high = __atomic_load_n(&sHeapHigh, __ATOMIC_RELAXED);
		while (end > high &&
		       !__atomic_compare_exchange_n(&sHeapHigh, &high, end, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
		}
	}
}

int Oot360_IsHeapAddress(uintptr_t address) {
	return address >= __atomic_load_n(&sHeapLow, __ATOMIC_RELAXED) &&
	       address < __atomic_load_n(&sHeapHigh, __ATOMIC_RELAXED);
}

#define CALLERS __builtin_return_address(0), __builtin_return_address(1)

void* __wrap_malloc(size_t size) {
	void* p = __real_malloc(size);
	Span(p, size);
	if (size >= SSB64_BIG_ALLOCATION || !p) {
		NoteAllocation("malloc", size, p, CALLERS);
	}
	return p;
}

void* __wrap_calloc(size_t count, size_t size) {
	void* p = __real_calloc(count, size);
	Span(p, count * size);
	if (count * size >= SSB64_BIG_ALLOCATION || !p) {
		NoteAllocation("calloc", count * size, p, CALLERS);
	}
	return p;
}

void* __wrap_realloc(void* ptr, size_t size) {
	void* p = __real_realloc(ptr, size);
	Span(p, size);
	if (size >= SSB64_BIG_ALLOCATION || (!p && size)) {
		NoteAllocation("realloc", size, p, CALLERS);
	}
	return p;
}

void* __wrap_memalign(size_t align, size_t size) {
	void* p = __real_memalign(align, size);
	Span(p, size);
	if (size >= SSB64_BIG_ALLOCATION || !p) {
		NoteAllocation("memalign", size, p, CALLERS);
	}
	return p;
}

/* Threads get at least 4 MB of stack: xsdk's default is 1 MB, and a stack overflow locks the console up without a word
 * in the log (oot360 test 4). */
int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg) {
	enum { MIN_STACK = 4u << 20 };
	if (attr) {
		const int r = __real_pthread_create(thread, attr, start, arg);
		xsdk_log_write("thread: %p with its own attributes: %d", (void*)start, r);
		return r;
	}
	pthread_attr_t mine;
	pthread_attr_init(&mine);
	pthread_attr_setstacksize(&mine, MIN_STACK);
	const int r = __real_pthread_create(thread, &mine, start, arg);
	pthread_attr_destroy(&mine);
	xsdk_log_write("thread: %p, %u KB of stack: %d", (void*)start, (unsigned)(MIN_STACK >> 10), r);
	return r;
}

/* What the game's thread is doing, for the heartbeat below (gfx_xenos.cpp marks its present and GPU waits). */
static const char* volatile sPhase = "start";
static volatile unsigned sPhaseCount;

void Oot360_Phase(const char* phase) {
	sPhase = phase;
	sPhaseCount++;
}

void Ssb64_Phase(const char* phase) {
	Oot360_Phase(phase);
}

/* dkr360: the loudest output sample since the last beat (Golden Balloon's audio callback, audi_port_dkr.c). */
volatile int gDkr360AudioPeak;
float dkr360_video_fps(void);

static void* Heartbeat(void* arg) {
	unsigned last = 0;
	unsigned beats = 0;
	(void)arg;
	for (;;) {
		struct timespec ts = { 5, 0 };
		nanosleep(&ts, NULL);
		const unsigned now = sPhaseCount;
		const int peak = gDkr360AudioPeak;
		gDkr360AudioPeak = 0;
		xsdk_log_write("alive: %u renderer steps since the last beat, %.1f fps, audio peak %d", now - last,
			       (double)dkr360_video_fps(), peak);
		// starfox360: memory every few beats, so a crash with no message shows whether it ran out.
		if ((++beats % 4) == 0) {
			Oot360_LogMemory("beat");
		}
		last = now;
	}
	return NULL;
}

void Ssb64_StartHeartbeat(void) {
	pthread_t t;
	if (pthread_create(&t, NULL, Heartbeat, NULL) == 0) {
		pthread_detach(t);
	}
}

/* The start-up guard of oot360 test 23 isn't used here: always a normal start. */
int Oot360_SafeLevel(void) {
	return 0;
}

/* The profile counters oot360's interpreter and resource manager fill; gfx_xenos.cpp prints them every 10 s. */
uint32_t gOot360ResourceMisses;
uint32_t Oot360_ResourceGeneration(void) {
	return 0;
}
uint64_t gOot360VtxSetupTicks, gOot360VtxLoopTicks, gOot360VtxCount, gOot360VtxLightSum;
uint64_t gOot360TriHits, gOot360TriMisses;
uint32_t gOot360TriWhy[8], gOot360TriLostTo[260], gOot360TriNoPlan[2];
uint64_t gOot360LoadTicks;
uint32_t gZeldaMMHdImports, gZeldaMMHdHeldBack;
uint64_t gZeldaMMHdImportTicks, gZeldaMMHdImportFrameMost;

// oot360 test 24: the last start's log, kept. xsdk's start-up opens the log (start.c) before anything of the game's
// runs, and opening it empties it, so a start that closed the game left nothing to read once it was opened again: test
// 23's safe mode came back with the log of the start after the crash, not of the crash. The linker sends start.c's call
// here (CMakeLists.txt, -Wl,--wrap=xsdk_log_open), which copies the log to dkr360-anterior.log first, where it opens
// (game:\, else cache:\ as in Xenia). Kernel calls only, as log.c's own: this runs before the log's lock exists, and
// the C library's open() logs its failures (file.c), which took the lock uninitialised and left test 24's first build
// on a black screen. Nothing is logged here.
NTSTATUS __real_xsdk_log_open(void);

static NTSTATUS KeepLastLogOpen(HANDLE* handle, const char* path, int write) {
    const ANSI_STRING name = {
        .length = (uint16_t)strlen(path),
        .maximum_length = (uint16_t)(strlen(path) + 1),
        .pointer = path,
    };
    const OBJECT_ATTRIBUTES attributes = { .root_directory = OB_DOS_DEVICES, .name = &name };
    IO_STATUS_BLOCK io_status;
    return NtCreateFile(handle, (write ? GENERIC_WRITE : GENERIC_READ) | SYNCHRONIZE, &attributes, &io_status, NULL,
                        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, write ? FILE_OVERWRITE_IF : FILE_OPEN,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
}

static void KeepLastLog(void) {
    static const char* const kPairs[][2] = {
        { "game:\\dkr360.log", "game:\\dkr360-anterior.log" },
        { "cache:\\dkr360.log", "cache:\\dkr360-anterior.log" },
    };
    static char buffer[64 << 10];
    for (int i = 0; i < 2; i++) {
        HANDLE in;
        if (!NT_SUCCESS(KeepLastLogOpen(&in, kPairs[i][0], 0))) {
            continue;
        }
        HANDLE out;
        if (NT_SUCCESS(KeepLastLogOpen(&out, kPairs[i][1], 1))) {
            // at most 64 MB, so a runaway log can't hold the start up
            for (int chunk = 0; chunk < 1024; chunk++) {
                IO_STATUS_BLOCK io_status;
                if (!NT_SUCCESS(NtReadFile(in, 0, NULL, NULL, &io_status, buffer, sizeof buffer, NULL)) ||
                    io_status.information == 0) {
                    break;
                }
                const uint32_t n = io_status.information;
                if (!NT_SUCCESS(NtWriteFile(out, 0, NULL, NULL, &io_status, buffer, n, NULL))) {
                    break;
                }
            }
            NtClose(out);
        }
        NtClose(in);
        return;
    }
}

NTSTATUS __wrap_xsdk_log_open(void) {
    KeepLastLog();
    return __real_xsdk_log_open();
}

#endif
