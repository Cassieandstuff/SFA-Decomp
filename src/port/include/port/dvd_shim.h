#pragma once
// dvd_shim.h - DVD/fileio -> host filesystem
// Shadows include/dolphin/dvd.h + include/main/fileio.h
// Host implements DVDFileInfo as host file handle + bulk data

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DVDFileInfo {
    // Host extension: not binary-compatible with GC, but layout matches what fileLoad() uses
    void*  hostData;
    uint32_t hostSize;
    int    fileId; // MLDF_FILEID_* or raw path hash
    int    offset;
} DVDFileInfo;

typedef struct DVDCommandBlock DVDCommandBlock;
typedef void (*DVDCallback)(int result, DVDFileInfo* info);

// Keep original signatures from include/dolphin/dvd.h
int DVDOpen(const char* path, DVDFileInfo* info);
int DVDRead(DVDFileInfo* info, void* buf, int32_t size, int32_t offset);
int DVDClose(DVDFileInfo* info);
int DVDReadAsyncPrio(DVDFileInfo* info, void* buf, int32_t size, int32_t offset, DVDCallback cb, int prio);
int DVDGetDriveStatus(void);
int DVDGetCommandBlockStatus(DVDCommandBlock* block);
void DVDInit(void);

#define DVD_FI_LENGTH(info) ((info)->hostSize)

// Game's high-level wrappers (src/main/pi_dolphin.c:323)
void* fileLoad(int id, int heap);
int   fileLoadToBuffer(int id, void* buffer);
int   fileLoadToBufferOffset(int id, void* dst, int offset, int size);
void* textureLoad(int id, int heap);
void* loadModelInstance(int id, int heap, void* tmp);
void* loadAnimation(void* modelHeader, int animId, int moveIndex, uint8_t* cache);

// Compatibility shims for old fileio.h
void setFileInfo(DVDFileInfo* info);
void* loadFileByPath(char* path, int* outSize, int unused);
int   DVDReadAsyncPrio_Stub(DVDFileInfo* f, void* b, int32_t s, int32_t o, DVDCallback cb, int p);

// Host init
int dvd_shim_init(const char* discRootOrIsoPath);
void dvd_shim_shutdown(void);

#ifdef __cplusplus
}
#endif
