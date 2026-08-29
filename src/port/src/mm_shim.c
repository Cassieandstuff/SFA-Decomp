// mm_shim.c - host implementation of the game's memory manager (main/mm.h).
//
// The console mm carves fixed heaps out of the boot arena; the host maps mmAlloc
// onto malloc. Allocations are 32-byte aligned (the game assumes this for DVD
// reads) and carry a small header so mm_free and getHeapItemSize work. The heap
// "type"/"flag"/region knobs are inert on the host - one flat allocator.

#include "main/mm.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct MmHdr {
    void* base;   // original malloc pointer
    int   size;   // requested size
    int   magic;
} MmHdr;

#define MM_MAGIC 0x6D6D4831 /* "mmH1" */
#define MM_ALIGN 32

void* mmAlloc(int size, int type, int flag) {
    (void)type; (void)flag;
    if (size < 0) return NULL;
    size_t total = (size_t)size + sizeof(MmHdr) + MM_ALIGN;
    unsigned char* base = (unsigned char*)malloc(total);
    if (!base) return NULL;
    uintptr_t user = ((uintptr_t)base + sizeof(MmHdr) + (MM_ALIGN - 1)) & ~(uintptr_t)(MM_ALIGN - 1);
    MmHdr* h = (MmHdr*)(user - sizeof(MmHdr));
    h->base = base;
    h->size = size;
    h->magic = MM_MAGIC;
    return (void*)user;
}

void mm_free(void* ptr) {
    if (!ptr) return;
    MmHdr* h = (MmHdr*)((unsigned char*)ptr - sizeof(MmHdr));
    if (h->magic != MM_MAGIC) return; // not ours / double free guard
    h->magic = 0;
    free(h->base);
}

void  mmFree(void* p)          { mm_free(p); }
void  mmFreeDeferred(void* p)  { mm_free(p); }
void  mmFreeTick(int arg)      { (void)arg; }
void  mmInit(void)             { }

int getHeapItemSize(void* ptr) {
    if (!ptr) return 0;
    MmHdr* h = (MmHdr*)((unsigned char*)ptr - sizeof(MmHdr));
    return (h->magic == MM_MAGIC) ? h->size : 0;
}

// --- alignment helpers -----------------------------------------------------
int alignUp2(int v)    { return (v + 1) & ~1; }
int roundUpTo4(int v)  { return (v + 3) & ~3; }
int roundUpTo8(int v)  { return (v + 7) & ~7; }
int roundUpTo16(int v) { return (v + 15) & ~15; }
int roundUpTo32(int v) { return (v + 31) & ~31; }

// --- heap mode knobs (inert on host) ---------------------------------------
int mmSetFreeDelay(int v)               { (void)v; return 0; }
int testAndSet_onlyUseHeaps1and2(int v) { (void)v; return 0; }
int testAndSet_onlyUseHeap3(int v)      { (void)v; return 0; }
int mmGetRegionForPtr(u8* ptr)          { (void)ptr; return 0; }
void mmSetTextureAllocationState(int s) { (void)s; }
int printHeapStats(int mode)            { (void)mode; return 0; }

// --- cache staging (host has no separate cache memory) ---------------------
void* getCache(void)                              { return NULL; }
void  cacheQueueWait(int sync)                    { (void)sync; }
void  copyToCache(void* dst, void* src, u32 n)    { if (dst && src) memcpy(dst, src, n); }
void  memcpyToCache(void* dst, void* src, u32 n)  { if (dst && src) memcpy(dst, src, n); }

// --- atomic singly-linked list (node's first word is the next pointer) ------
void AtomicSList_Push(void** list, void* node) {
    if (!list || !node) return;
    *(void**)node = *list;
    *list = node;
}
void* AtomicSList_Pop(void** list) {
    if (!list || !*list) return NULL;
    void* n = *list;
    *list = *(void**)n;
    return n;
}

// --- pools / stores --------------------------------------------------------
void* stackCreate(int count, int size) {
    return malloc((size_t)count * (size_t)size);
}
int   mmCreateMemoryStore(int size) { (void)size; return 0; }
void* mmAllocateFromFBMemoryStore(int handle, int size) { (void)handle; return malloc((size_t)size); }
