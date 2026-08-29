#pragma once
// dolphin/os.h - HOST SHADOW of the Dolphin OS header.
//
// This file lives in src/port/include, which the host build searches before
// include/, so a game TU's #include "dolphin/os.h" resolves here instead of the
// real header. The real one pulls in PPC inline asm (OSFastCast, OSException,
// __os) that MSVC/Clang cannot compile; this shadow declares the same OS API in
// portable C. os_shim.c provides the implementations. Only the host build sees
// this - the MWCC matching build uses include/dolphin/os.h unchanged.
//
// Types are declared to match the game's usage; opaque OS objects are sized
// generously so value declarations (OSThread t;) work. Expand as TUs demand.

#include "dolphin/types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- error / report --------------------------------------------------------
typedef u16 OSError;
typedef struct OSContext OSContext;
typedef void (*OSErrorHandler)(OSError error, OSContext* context, ...);

void OSReport(const char* msg, ...);
void OSVReport(const char* msg, va_list list);
void OSPanic(const char* file, int line, const char* msg, ...);
void OSHalt(const char* msg);
OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler);

// --- interrupts ------------------------------------------------------------
BOOL OSDisableInterrupts(void);
BOOL OSEnableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL level);

// --- cache (no-ops on a coherent host) -------------------------------------
void DCInvalidateRange(void* addr, u32 nBytes);
void DCFlushRange(void* addr, u32 nBytes);
void DCStoreRange(void* addr, u32 nBytes);
void DCFlushRangeNoSync(void* addr, u32 nBytes);
void DCStoreRangeNoSync(void* addr, u32 nBytes);
void DCZeroRange(void* addr, u32 nBytes);
void DCTouchRange(void* addr, u32 nBytes);
void ICInvalidateRange(void* addr, u32 nBytes);
void ICFlashInvalidate(void);
void DCEnable(void);

// --- time ------------------------------------------------------------------
typedef s64 OSTime;
typedef u32 OSTick;
OSTime OSGetTime(void);
OSTick OSGetTick(void);
u32    OSGetTickRate(void); // bus clock / 4

// SFA uses a 486MHz-class TB; keep the macros consistent with OSGetTickRate().
#define OS_TIMER_CLOCK (OSGetTickRate())
#define OSTicksToSeconds(t)       ((t) / OS_TIMER_CLOCK)
#define OSTicksToMilliseconds(t)  ((t) / (OS_TIMER_CLOCK / 1000))
#define OSTicksToMicroseconds(t)  (((t) * 8) / (OS_TIMER_CLOCK / 125000))
#define OSMillisecondsToTicks(ms) ((ms) * (OS_TIMER_CLOCK / 1000))
#define OSSecondsToTicks(s)       ((s) * OS_TIMER_CLOCK)

// --- stopwatch (fields match the real struct; game declares it by value) ---
typedef struct OSStopwatch {
    char*  name;
    OSTime total;
    u32    hits;
    OSTime min;
    OSTime max;
    OSTime last;
} OSStopwatch;
void   OSInitStopwatch(OSStopwatch* sw, char* name);
void   OSStartStopwatch(OSStopwatch* sw);
void   OSStopStopwatch(OSStopwatch* sw);
OSTime OSCheckStopwatch(OSStopwatch* sw);
void   OSResetStopwatch(OSStopwatch* sw);
void   OSDumpStopwatch(OSStopwatch* sw);

// --- init / system ---------------------------------------------------------
void OSInit(void);
u32  OSGetConsoleType(void);
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu);
u32  OSGetResetCode(void);
void OSSetSoundMode(u32 mode);
u32  OSGetSoundMode(void);
void OSSetProgressiveMode(u32 on);
u32  OSGetProgressiveMode(void);

// --- arena -----------------------------------------------------------------
void* OSGetArenaHi(void);
void* OSGetArenaLo(void);
void  OSSetArenaHi(void* addr);
void  OSSetArenaLo(void* addr);
void* OSAllocFromArenaLo(u32 size, u32 align);
void* OSAllocFromArenaHi(u32 size, u32 align);

// --- heap ------------------------------------------------------------------
typedef int OSHeapHandle;
void*        OSInitAlloc(void* arenaLo, void* arenaHi, int maxHeaps);
OSHeapHandle OSCreateHeap(void* start, void* end);
OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap);
void*        OSAllocFromHeap(OSHeapHandle heap, u32 size);
void*        OSAllocFixed(void** rstart, void** rend);
void         OSFreeToHeap(OSHeapHandle heap, void* ptr);
#define      OSAlloc(size)     OSAllocFromHeap(-1, (size))
#define      OSFree(ptr)       OSFreeToHeap(-1, (ptr))

// --- thread (opaque; stubbed single-thread scheduler) ----------------------
typedef struct OSThread { u8 opaque[0x340]; } OSThread;
typedef struct OSThreadQueue { OSThread* head; OSThread* tail; } OSThreadQueue;
typedef s32 OSPriority;
void OSInitThreadQueue(OSThreadQueue* queue);
BOOL OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
                    void* stack, u32 stackSize, OSPriority prio, u16 attr);
void OSExitThread(void* val);
void OSCancelThread(OSThread* thread);
BOOL OSJoinThread(OSThread* thread, void** val);
s32  OSResumeThread(OSThread* thread);
s32  OSSuspendThread(OSThread* thread);
BOOL OSIsThreadTerminated(OSThread* thread);
OSThread* OSGetCurrentThread(void);
void OSSleepThread(OSThreadQueue* queue);
void OSWakeupThread(OSThreadQueue* queue);
void OSYieldThread(void);
OSPriority OSGetThreadPriority(OSThread* thread);
BOOL OSSetThreadPriority(OSThread* thread, OSPriority prio);

// --- message queue ---------------------------------------------------------
typedef void* OSMessage;
typedef struct OSMessageQueue { u8 opaque[0x40]; } OSMessageQueue;
#define OS_MESSAGE_NOBLOCK 0
#define OS_MESSAGE_BLOCK   1
void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* msgArray, s32 msgCount);
BOOL OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags);
BOOL OSJamMessage(OSMessageQueue* mq, OSMessage msg, s32 flags);
BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, s32 flags);

// --- mutex -----------------------------------------------------------------
typedef struct OSMutex { u8 opaque[0x18]; } OSMutex;
void OSInitMutex(OSMutex* mutex);
void OSLockMutex(OSMutex* mutex);
void OSUnlockMutex(OSMutex* mutex);
BOOL OSTryLockMutex(OSMutex* mutex);

// --- alarm -----------------------------------------------------------------
typedef struct OSAlarm OSAlarm;
typedef void (*OSAlarmHandler)(OSAlarm* alarm, OSContext* context);
struct OSAlarm { u8 opaque[0x28]; };
void OSCreateAlarm(OSAlarm* alarm);
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler);
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler);
void OSCancelAlarm(OSAlarm* alarm);

#ifdef __cplusplus
}
#endif
