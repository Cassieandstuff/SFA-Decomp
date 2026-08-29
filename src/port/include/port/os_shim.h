#pragma once
// os_shim.h - OS -> host STL/SDL
// Shadows include/dolphin/os.h + os/OS* headers

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int OSInterruptMask;
OSInterruptMask OSDisableInterrupts(void);
OSInterruptMask OSRestoreInterrupts(OSInterruptMask mask);
void OSReport(const char* fmt, ...);
void OSVReport(const char* fmt, va_list args);
void OSPanic(const char* file, int line, const char* msg, ...);

// Thread / Message stubs (508 hits, mostly pi_dolphin.c)
typedef struct OSThread OSThread;
typedef struct OSMessageQueue { int dummy; } OSMessageQueue;
typedef void* OSMessage;
#define OS_MESSAGE_BLOCK 1
#define OS_MESSAGE_NOBLOCK 0
void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* buf, int32_t n);
int  OSSendMessage(OSMessageQueue* mq, OSMessage msg, int32_t flags);
int  OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, int32_t flags);
void OSCreateThread(OSThread* t, void* (*func)(void*), void* param, void* stack, uint32_t sz, int prio, uint16_t flags);
void OSSuspendThread(OSThread* t);
void OSResumeThread(OSThread* t);

// Cache (no-op on x86/ARM host except for JIT coherency)
void DCInvalidateRange(void* addr, uint32_t size);
void DCFlushRange(void* addr, uint32_t size);
void ICInvalidateRange(void* addr, uint32_t size);

// Arena / Alloc
void* OSAllocFromHeap(void* heap, uint32_t size);
void  OSFreeToHeap(void* heap, void* ptr);

// Time
typedef int64_t OSTime;
OSTime OSGetTime(void);
OSTime OSGetTick(void);
uint32_t OSGetTickRate(void);

// Host helpers
void os_shim_init(void);

#ifdef __cplusplus
}
#endif
