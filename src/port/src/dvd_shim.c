// dvd_shim.c - DVD / fileio backed by the host filesystem.
//
// Mirrors the console file layer's observable behavior:
//   DVDOpen(path) resolves <discRoot>/<path> and reports the size (info.length).
//   DVDRead(info,buf,size,offset) reads bytes; reads past EOF zero-pad to size
//     (matching aligned console reads); returns size, or -1 on error.
//   fileLoad(id) maps id -> name (from filetable.txt) -> DVDOpen/Read into a
//     malloc'd buffer, cached by id so a repeat fileLoad returns the same pointer
//     (the game relies on gResourceFileBuffers caching this way).

#include "port/dvd_shim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DVD_MAX_FILE_ID 2048

static char   gDiscRoot[512] = ".";
static char*  gNameTable[DVD_MAX_FILE_ID];
static void*  gCache[DVD_MAX_FILE_ID];
static int    gCacheSize[DVD_MAX_FILE_ID];
static int    gAutoInvalidate = 0;

static void resolvePath(char* out, size_t outSize, const char* rel) {
    // Accept both separators; the host takes '/' on Windows too.
    while (*rel == '/' || *rel == '\\') ++rel;
    snprintf(out, outSize, "%s/%s", gDiscRoot, rel);
    for (char* p = out; *p; ++p) if (*p == '\\') *p = '/';
}

int dvd_shim_setName(int id, const char* relPath) {
    if (id < 0 || id >= DVD_MAX_FILE_ID || !relPath) return 0;
    free(gNameTable[id]);
    size_t n = strlen(relPath) + 1;
    gNameTable[id] = (char*)malloc(n);
    if (!gNameTable[id]) return 0;
    memcpy(gNameTable[id], relPath, n);
    return 1;
}

static void loadManifest(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/filetable.txt", gDiscRoot);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[600];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        int id = -1;
        char rel[512];
        if (sscanf(line, "%d %511s", &id, rel) == 2) {
            if (dvd_shim_setName(id, rel)) ++count;
        }
    }
    fclose(f);
    printf("[dvd] loaded %d entries from filetable.txt\n", count);
}

int dvd_shim_init(const char* discRoot) {
    if (discRoot && discRoot[0]) {
        snprintf(gDiscRoot, sizeof(gDiscRoot), "%s", discRoot);
        for (char* p = gDiscRoot; *p; ++p) if (*p == '\\') *p = '/';
    }
    loadManifest();
    printf("[dvd] disc root: %s\n", gDiscRoot);
    return 1;
}

void dvd_shim_shutdown(void) {
    for (int i = 0; i < DVD_MAX_FILE_ID; ++i) {
        free(gNameTable[i]); gNameTable[i] = NULL;
        free(gCache[i]);     gCache[i] = NULL;
        gCacheSize[i] = 0;
    }
}

void DVDInit(void) {}
void DVDSetAutoInvalidation(int32_t enable) { gAutoInvalidate = enable; }
int32_t DVDGetDriveStatus(void) { return 0; }
int32_t DVDGetCommandBlockStatus(DVDCommandBlock* block) { (void)block; return 0; }

int32_t DVDOpen(const char* path, DVDFileInfo* info) {
    if (!path || !info) return 0;
    resolvePath(info->hostPath, sizeof(info->hostPath), path);
    FILE* f = fopen(info->hostPath, "rb");
    if (!f) { info->hostFile = NULL; info->length = 0; return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    info->hostFile = f;
    info->length = (int32_t)sz;
    info->startAddr = 0;
    return 1;
}

int32_t DVDClose(DVDFileInfo* info) {
    if (!info || !info->hostFile) return 0;
    fclose((FILE*)info->hostFile);
    info->hostFile = NULL;
    return 1;
}

int32_t DVDRead(DVDFileInfo* info, void* buf, int32_t size, int32_t offset) {
    if (!info || !info->hostFile || !buf || size < 0) return -1;
    FILE* f = (FILE*)info->hostFile;
    if (fseek(f, offset, SEEK_SET) != 0) return -1;
    size_t got = fread(buf, 1, (size_t)size, f);
    // Console aligned reads past EOF return the requested (aligned) size, padded.
    if ((int32_t)got < size) memset((char*)buf + got, 0, (size_t)size - got);
    return size;
}

int32_t DVDReadAsyncPrio(DVDFileInfo* info, void* buf, int32_t size, int32_t offset, DVDCallback cb, int32_t prio) {
    (void)prio;
    int32_t r = DVDRead(info, buf, size, offset);
    if (cb) cb(r, info);          // synchronous completion; thread pool is a later refinement
    return r < 0 ? 0 : 1;
}

void* loadFileByPath(char* path, int* outSize, int unused) {
    (void)unused;
    if (outSize) *outSize = 0;
    DVDFileInfo info;
    if (!DVDOpen(path, &info)) return NULL;
    int size = info.length;
    int32_t aligned = (size + 0x1f) & ~0x1f;      // match the game's aligned alloc
    void* buf = malloc((size_t)aligned);
    if (!buf) { DVDClose(&info); return NULL; }
    if (DVDRead(&info, buf, aligned, 0) < 0) { free(buf); DVDClose(&info); return NULL; }
    DVDClose(&info);
    if (outSize) *outSize = size;
    return buf;
}

void* fileLoad(int id, int heap) {
    (void)heap;
    if (id < 0 || id >= DVD_MAX_FILE_ID) return NULL;
    if (gCache[id]) return gCache[id];               // cached: same pointer as before
    if (!gNameTable[id]) {
        fprintf(stderr, "[dvd] fileLoad(%d): no name mapping\n", id);
        return NULL;
    }
    int size = 0;
    void* buf = loadFileByPath(gNameTable[id], &size, 0);
    if (!buf) return NULL;
    gCache[id] = buf;
    gCacheSize[id] = size;
    return buf;
}

int fileLoadToBuffer(int id, void* buffer) {
    if (id < 0 || id >= DVD_MAX_FILE_ID || !buffer) return 0;
    if (gCache[id]) { memcpy(buffer, gCache[id], (size_t)gCacheSize[id]); return gCacheSize[id]; }
    if (!gNameTable[id]) return 0;
    DVDFileInfo info;
    if (!DVDOpen(gNameTable[id], &info)) return 0;
    int32_t r = DVDRead(&info, buffer, info.length, 0);
    int len = info.length;
    DVDClose(&info);
    return r < 0 ? 0 : len;
}

int fileLoadToBufferOffset(int id, void* dst, int offset, int size) {
    if (size == 0 || id < 0 || id >= DVD_MAX_FILE_ID || !dst) return 0;
    if (gCache[id]) {
        if (offset + size > gCacheSize[id]) return 0;
        memcpy(dst, (char*)gCache[id] + offset, (size_t)size);
        return size;
    }
    if (!gNameTable[id]) return 0;
    DVDFileInfo info;
    if (!DVDOpen(gNameTable[id], &info)) return 0;
    int32_t r = DVDRead(&info, dst, size, offset);
    DVDClose(&info);
    return r < 0 ? 0 : size;
}

int32_t fileGetSize(int id) {
    if (id < 0 || id >= DVD_MAX_FILE_ID) return 0;
    if (gCache[id]) return gCacheSize[id];
    if (!gNameTable[id]) return 0;
    DVDFileInfo info;
    if (!DVDOpen(gNameTable[id], &info)) return 0;
    int32_t len = info.length;
    DVDClose(&info);
    return len;
}
