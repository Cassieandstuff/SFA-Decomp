// os_shim.c - host implementation of the Dolphin OS API (see dolphin/os.h shadow).
//
// The console's OS handles interrupts, caches, threads, and a boot-time arena.
// On a coherent multitasking host most of this collapses: cache ops are no-ops,
// interrupt enable/disable is a bookkeeping flag, and time comes from the host
// clock. Threads/messages/mutexes are stubbed for now (the game boots effectively
// single-threaded); they get real host backing when a subsystem needs concurrency.

#include "dolphin/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// --- error / report --------------------------------------------------------
void OSReport(const char* msg, ...) {
    va_list ap; va_start(ap, msg); vprintf(msg, ap); va_end(ap);
}
void OSVReport(const char* msg, va_list list) { vprintf(msg, list); }

void OSPanic(const char* file, int line, const char* msg, ...) {
    fprintf(stderr, "OSPanic %s:%d: ", file, line);
    va_list ap; va_start(ap, msg); vfprintf(stderr, msg, ap); va_end(ap);
    fputc('\n', stderr);
    abort();
}
void OSHalt(const char* msg) { fprintf(stderr, "OSHalt: %s\n", msg ? msg : ""); abort(); }

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    (void)error; (void)handler; return NULL;
}

// --- interrupts (single flag; no real ISRs on the host) --------------------
static BOOL gInterruptsEnabled = TRUE;
BOOL OSDisableInterrupts(void) { BOOL prev = gInterruptsEnabled; gInterruptsEnabled = FALSE; return prev; }
BOOL OSEnableInterrupts(void)  { BOOL prev = gInterruptsEnabled; gInterruptsEnabled = TRUE;  return prev; }
BOOL OSRestoreInterrupts(BOOL level) { BOOL prev = gInterruptsEnabled; gInterruptsEnabled = level; return prev; }

// --- cache (host is coherent) ----------------------------------------------
void DCInvalidateRange(void* addr, u32 n)   { (void)addr; (void)n; }
void DCFlushRange(void* addr, u32 n)        { (void)addr; (void)n; }
void DCStoreRange(void* addr, u32 n)        { (void)addr; (void)n; }
void DCFlushRangeNoSync(void* addr, u32 n)  { (void)addr; (void)n; }
void DCStoreRangeNoSync(void* addr, u32 n)  { (void)addr; (void)n; }
void DCZeroRange(void* addr, u32 n)         { if (addr && n) memset(addr, 0, n); }
void DCTouchRange(void* addr, u32 n)        { (void)addr; (void)n; }
void ICInvalidateRange(void* addr, u32 n)   { (void)addr; (void)n; }
void ICFlashInvalidate(void)                { }
void DCEnable(void)                         { }

// --- time ------------------------------------------------------------------
u32 OSGetTickRate(void) { return 162000000u; } // GC bus 243MHz -> TB = bus/2? use 162MHz TB-like base

OSTime OSGetTime(void) {
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (OSTime)((c.QuadPart * (long long)OSGetTickRate()) / f.QuadPart);
#else
    return 0;
#endif
}
OSTick OSGetTick(void) { return (OSTick)OSGetTime(); }

// --- init / system ---------------------------------------------------------
void OSInit(void) { }
u32  OSGetConsoleType(void) { return 0x10000006u; } // retail
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) { (void)reset; (void)resetCode; (void)forceMenu; }
u32  OSGetResetCode(void) { return 0; }
void OSSetSoundMode(u32 mode) { (void)mode; }
u32  OSGetSoundMode(void) { return 1; } // stereo
void OSSetProgressiveMode(u32 on) { (void)on; }
u32  OSGetProgressiveMode(void) { return 0; }

// --- arena / heap (backed by the C heap) -----------------------------------
void* OSGetArenaHi(void) { return NULL; }
void* OSGetArenaLo(void) { return NULL; }
void  OSSetArenaHi(void* addr) { (void)addr; }
void  OSSetArenaLo(void* addr) { (void)addr; }
void* OSAllocFromArenaLo(u32 size, u32 align) { (void)align; return malloc(size); }
void* OSAllocFromArenaHi(u32 size, u32 align) { (void)align; return malloc(size); }

void*        OSInitAlloc(void* lo, void* hi, int maxHeaps) { (void)lo; (void)hi; (void)maxHeaps; return NULL; }
OSHeapHandle OSCreateHeap(void* start, void* end) { (void)start; (void)end; return 0; }
OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap) { (void)heap; return 0; }
void*        OSAllocFromHeap(OSHeapHandle heap, u32 size) { (void)heap; return malloc(size); }
void*        OSAllocFixed(void** rstart, void** rend) { (void)rend; return rstart ? *rstart : NULL; }
void         OSFreeToHeap(OSHeapHandle heap, void* ptr) { (void)heap; free(ptr); }

// --- threads (stubbed; game boots effectively single-threaded) -------------
void OSInitThreadQueue(OSThreadQueue* q) { if (q) { q->head = NULL; q->tail = NULL; } }
BOOL OSCreateThread(OSThread* t, void* (*f)(void*), void* p, void* s, u32 sz, OSPriority pr, u16 a) {
    (void)t; (void)f; (void)p; (void)s; (void)sz; (void)pr; (void)a; return TRUE;
}
void OSExitThread(void* v) { (void)v; }
void OSCancelThread(OSThread* t) { (void)t; }
BOOL OSJoinThread(OSThread* t, void** v) { (void)t; if (v) *v = NULL; return TRUE; }
s32  OSResumeThread(OSThread* t) { (void)t; return 0; }
s32  OSSuspendThread(OSThread* t) { (void)t; return 0; }
BOOL OSIsThreadTerminated(OSThread* t) { (void)t; return TRUE; }
OSThread* OSGetCurrentThread(void) { return NULL; }
void OSSleepThread(OSThreadQueue* q) { (void)q; }
void OSWakeupThread(OSThreadQueue* q) { (void)q; }
void OSYieldThread(void) { }
OSPriority OSGetThreadPriority(OSThread* t) { (void)t; return 16; }
BOOL OSSetThreadPriority(OSThread* t, OSPriority p) { (void)t; (void)p; return TRUE; }

// --- message queue (stub) --------------------------------------------------
void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* arr, s32 n) { (void)mq; (void)arr; (void)n; }
BOOL OSSendMessage(OSMessageQueue* mq, OSMessage m, s32 f) { (void)mq; (void)m; (void)f; return TRUE; }
BOOL OSJamMessage(OSMessageQueue* mq, OSMessage m, s32 f) { (void)mq; (void)m; (void)f; return TRUE; }
BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* m, s32 f) { (void)mq; (void)f; if (m) *m = NULL; return FALSE; }

// --- mutex (single-thread: trivially satisfied) ----------------------------
void OSInitMutex(OSMutex* m) { (void)m; }
void OSLockMutex(OSMutex* m) { (void)m; }
void OSUnlockMutex(OSMutex* m) { (void)m; }
BOOL OSTryLockMutex(OSMutex* m) { (void)m; return TRUE; }

// --- alarm (stub) ----------------------------------------------------------
void OSCreateAlarm(OSAlarm* a) { (void)a; }
void OSSetAlarm(OSAlarm* a, OSTime t, OSAlarmHandler h) { (void)a; (void)t; (void)h; }
void OSSetPeriodicAlarm(OSAlarm* a, OSTime s, OSTime p, OSAlarmHandler h) { (void)a; (void)s; (void)p; (void)h; }
void OSCancelAlarm(OSAlarm* a) { (void)a; }
