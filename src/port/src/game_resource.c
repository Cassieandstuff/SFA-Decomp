// game_resource.c - decomp-native resource/DLL registry for the port.
//
// On GameCube the ~705 game "DLLs" are relocatable PPC modules loaded from disc,
// relocated, and registered in gResourceDescriptors[]; Resource_Acquire(id) then
// hands back the module's interface (a vtable). We can't run PPC modules on x86,
// so decomp-native loading means compiling each DLL's SOURCE (src/dlls/**) to x86
// and registering its descriptor statically. This file is the faithful engine for
// that: it reimplements resource.h exactly (Resource_Acquire/Release/ResetRefCounts
// over gResourceDescriptors/gResourceLoadedHandles/gResourceRefCounts) and defaults
// every id to a generated no-op stub descriptor, so the game boots today. Real DLLs
// slot in one at a time via the REGISTER list below - each replaces its stub with
// the compiled `*_funcs` descriptor, and the running engine calls its real code.
//
// Descriptor layout (resource.h / object_descriptor.h are identical):
//   0x00 u32 metadata[4]      (reserved0..2, slotCountAndFlags)
//   0x10 acquire/initialise   (NULL for most engine DLLs - no disc load)
//   0x14 release
//   0x18 data[] = the interface methods (what the game calls)
// Resource_Acquire returns &gResourceLoadedHandles[id], a void** whose target is
// &descriptor->data; both `(*gXInterface)->m()` and `gXInterface->vtable->m()`
// access patterns resolve through it.

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RESOURCE_DESCRIPTOR_COUNT 0x2c1
#define DESC_DATA_OFFSET 0x18

typedef struct RD {
    unsigned int metadata[4];
    void (*acquire)(struct RD* d);
    void (*release)(void);
    /* data (interface methods) follow at offset 0x18 */
} RD;

RD*   gResourceDescriptors[RESOURCE_DESCRIPTOR_COUNT];
void* gResourceLoadedHandles[RESOURCE_DESCRIPTOR_COUNT];
unsigned short gResourceRefCounts[RESOURCE_DESCRIPTOR_COUNT];

// One shared stub descriptor: NULL acquire/release + a wide table of no-op methods
// (wider than the largest real interface). Serves every id not yet brought up.
static void resource_noop(void) {}
static struct StubDesc {
    unsigned int metadata[4];
    void (*acquire)(RD*);
    void (*release)(void);
    void* methods[64];
} gStubDesc;

// ---- real DLL registration -------------------------------------------------
// Each brought-up DLL declares its descriptor here and registers it by id. The
// symbol names match src/main/modelEngine.c's gResourceDescriptors[] table.
// (none yet - add entries as DLL sources are compiled into game_engine)
typedef struct { int id; RD* desc; } DllReg;

// Real engine DLLs compiled into game_engine and registered by id (folder number).
extern char sky_funcs[];   // src/dlls/engine/5/5.c - SkyDllInterface (id 5)

static const DllReg* dllRegList(int* count) {
    static DllReg regs[] = {
        { 5, (RD*)sky_funcs },
    };
    *count = (int)(sizeof(regs) / sizeof(regs[0]));
    return regs;
}

static int gInited;
static void resource_init(void) {
    int i, n;
    const DllReg* regs;
    if (gInited) return;
    gInited = 1;

    for (i = 0; i < 4; ++i) gStubDesc.metadata[i] = 0;
    gStubDesc.acquire = NULL;
    gStubDesc.release = NULL;
    for (i = 0; i < 64; ++i) gStubDesc.methods[i] = (void*)resource_noop;

    for (i = 0; i < RESOURCE_DESCRIPTOR_COUNT; ++i) {
        gResourceDescriptors[i] = (RD*)&gStubDesc;
        gResourceLoadedHandles[i] = NULL;
        gResourceRefCounts[i] = 0;
    }

    regs = dllRegList(&n);
    for (i = 0; i < n; ++i) {
        if (regs[i].id >= 0 && regs[i].id < RESOURCE_DESCRIPTOR_COUNT)
            gResourceDescriptors[regs[i].id] = regs[i].desc;
    }

    // Minimal SaveGame/MapEvent descriptor (id 0x17): supplies getLast() -> the
    // save-state buffer that gGameBitSaveData points at. See game_savestate.c.
    {
        extern void* stairfax_mapevent_descriptor(void);
        void* me = stairfax_mapevent_descriptor();
        if (me) { gResourceDescriptors[0x17] = (RD*)me; n++; }
    }
    if (getenv("STAIRFAX_TRACE"))
        fprintf(stderr, "[resource] registry init: %d ids, %d real DLL(s)\n",
                RESOURCE_DESCRIPTOR_COUNT, n);
}

void* Resource_Acquire(unsigned short id, int unused) {
    RD* d;
    (void)unused;
    resource_init();
    if (id >= RESOURCE_DESCRIPTOR_COUNT) return &gResourceLoadedHandles[0];
    d = gResourceDescriptors[id];
    if (gResourceRefCounts[id] == 0 && d->acquire != NULL) d->acquire(d);
    gResourceRefCounts[id]++;
    gResourceLoadedHandles[id] = (char*)d + DESC_DATA_OFFSET;
    return &gResourceLoadedHandles[id];
}

int Resource_Release(void* handleSlot) {
    int i = 0;
    RD* descriptor = (RD*)handleSlot;
    resource_init();
    while (i < RESOURCE_DESCRIPTOR_COUNT) {
        if ((void*)&gResourceLoadedHandles[i] == handleSlot) {
            descriptor = gResourceDescriptors[i];
            break;
        }
        i++;
    }
    if (i >= RESOURCE_DESCRIPTOR_COUNT) return 0;
    gResourceRefCounts[i]--;
    if (gResourceRefCounts[i] == 0) {
        if (descriptor->release != NULL) descriptor->release();
        return 1;
    }
    return 0;
}

void Resource_ResetRefCounts(void) {
    int i;
    resource_init();
    for (i = 0; i < RESOURCE_DESCRIPTOR_COUNT; ++i) gResourceRefCounts[i] = 0;
}
