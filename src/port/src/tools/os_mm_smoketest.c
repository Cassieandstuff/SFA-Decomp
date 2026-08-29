// os_mm_smoketest.c - prove the OS/mm header shadowing + shims compile and work.
//
// Includes the game's real headers the way a game TU does: "dolphin/os.h" resolves
// to the host shadow (src/port/include/dolphin/os.h), "main/mm.h" to the real
// header (implemented by mm_shim). Built with the game include layout + TARGET_PC.

#include "dolphin/os.h"
#include "main/mm.h"

#include <stdio.h>
#include <string.h>

static int gPass = 0, gFail = 0;
static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}

int main(void) {
    printf("[os_mm_smoketest] start\n");
    OSInit();

    // OSReport goes to stdout.
    OSReport("  OSReport works: %d + %d = %d\n", 2, 3, 2 + 3);

    // Interrupt enable/disable bookkeeping returns previous state.
    BOOL was = OSDisableInterrupts();
    check("OSDisableInterrupts returns prior-enabled TRUE", was == TRUE);
    BOOL nowDisabled = OSRestoreInterrupts(was);
    check("OSRestoreInterrupts returns the disabled state", nowDisabled == FALSE);

    // mm allocator: alignment, size tracking, round-trip.
    void* p = mmAlloc(100, 0x7d7d7d7d, 0);
    check("mmAlloc returns non-NULL", p != NULL);
    check("mmAlloc is 32-byte aligned", ((uintptr_t)p & 31) == 0);
    check("getHeapItemSize == requested 100", getHeapItemSize(p) == 100);
    memset(p, 0xAB, 100);
    check("allocation is writable/readable", ((unsigned char*)p)[99] == 0xAB);
    mm_free(p);

    check("roundUpTo32(100) == 128", roundUpTo32(100) == 128);
    check("roundUpTo16(1) == 16", roundUpTo16(1) == 16);

    // Cache ops are callable no-ops (must not crash).
    int buf[8];
    DCFlushRange(buf, sizeof(buf));
    DCInvalidateRange(buf, sizeof(buf));
    DCZeroRange(buf, sizeof(buf));
    check("DCZeroRange zeroes memory", buf[0] == 0 && buf[7] == 0);

    // Time advances.
    check("OSGetTickRate nonzero", OSGetTickRate() != 0);
    OSTime t0 = OSGetTime();
    for (volatile int i = 0; i < 1000000; ++i) {}
    OSTime t1 = OSGetTime();
    check("OSGetTime advances", t1 >= t0);

    // Atomic list from mm.h.
    void* list = NULL;
    void* n1 = mmAlloc(32, 0, 0);
    void* n2 = mmAlloc(32, 0, 0);
    AtomicSList_Push(&list, n1);
    AtomicSList_Push(&list, n2);
    check("AtomicSList pop LIFO order", AtomicSList_Pop(&list) == n2 && AtomicSList_Pop(&list) == n1);
    mm_free(n1); mm_free(n2);

    printf("[os_mm_smoketest] %d passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
