#include "port/os_shim.h"
#include <stdio.h>
#include <stdarg.h>

OSInterruptMask OSDisableInterrupts(void) { return 0; }
OSInterruptMask OSRestoreInterrupts(OSInterruptMask m) { (void)m; return 0; }

void OSReport(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}
void OSVReport(const char* fmt, va_list ap) { vprintf(fmt, ap); }
void OSPanic(const char* f, int l, const char* m, ...) {
    fprintf(stderr, "OS Panic %s:%d: %s\n", f,l,m);
    va_list ap; va_start(ap,m); vfprintf(stderr,m,ap); va_end(ap);
    abort();
}

void OSInitMessageQueue(OSMessageQueue* q, OSMessage* b, int32_t n) { (void)q;(void)b;(void)n; }
int OSSendMessage(OSMessageQueue* q, OSMessage m, int32_t f){ (void)q;(void)m;(void)f; return 1; }
int OSReceiveMessage(OSMessageQueue* q, OSMessage* m, int32_t f){ (void)q;(void)m;(void)f; return 0; }
void OSCreateThread(OSThread* t, void*(*fn)(void*), void* p, void* s, uint32_t sz, int pr, uint16_t fl){ (void)t;(void)fn;(void)p;(void)s;(void)sz;(void)pr;(void)fl; }
void OSSuspendThread(OSThread* t){ (void)t; }
void OSResumeThread(OSThread* t){ (void)t; }

void DCInvalidateRange(void* a, uint32_t s){ (void)a;(void)s; }
void DCFlushRange(void* a, uint32_t s){ (void)a;(void)s; }
void ICInvalidateRange(void* a, uint32_t s){ (void)a;(void)s; }

void* OSAllocFromHeap(void* h, uint32_t sz){ (void)h; return malloc(sz); }
void OSFreeToHeap(void* h, void* p){ (void)h; free(p); }

OSTime OSGetTime(void){ return 0; }
OSTime OSGetTick(void){ return 0; }
uint32_t OSGetTickRate(void){ return 1000; }

void os_shim_init(void){}
