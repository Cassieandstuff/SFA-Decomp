#include "port/dvd_shim.h"
#include <stdio.h>
#include <stdlib.h>

int dvd_shim_init(const char* path) { (void)path; return 0; }
void dvd_shim_shutdown(void) {}

int DVDOpen(const char* path, DVDFileInfo* info) { (void)path; (void)info; return 0; }
int DVDRead(DVDFileInfo* info, void* buf, int32_t size, int32_t offset) { (void)info;(void)buf;(void)size;(void)offset; return 0; }
int DVDClose(DVDFileInfo* info) { (void)info; return 0; }
int DVDReadAsyncPrio(DVDFileInfo* info, void* buf, int32_t size, int32_t offset, DVDCallback cb, int prio) {
    (void)prio;
    int r = DVDRead(info, buf, size, offset);
    if (cb) cb(r, info);
    return r;
}
int DVDGetDriveStatus(void) { return 0; }
int DVDGetCommandBlockStatus(DVDCommandBlock* b) { (void)b; return 0; }
void DVDInit(void) {}

void* fileLoad(int id, int heap) { (void)id;(void)heap; return NULL; }
int fileLoadToBuffer(int id, void* buf) { (void)id;(void)buf; return 0; }
int fileLoadToBufferOffset(int id, void* dst, int off, int sz) { (void)id;(void)dst;(void)off;(void)sz; return 0; }
void* textureLoad(int id, int heap) { (void)id;(void)heap; return NULL; }
void* loadModelInstance(int id, int heap, void* tmp) { (void)id;(void)heap;(void)tmp; return NULL; }
void* loadAnimation(void* h, int id, int idx, uint8_t* c) { (void)h;(void)id;(void)idx;(void)c; return NULL; }

void setFileInfo(DVDFileInfo* f) { (void)f; }
void* loadFileByPath(char* p, int* s, int u) { (void)p;(void)s;(void)u; return NULL; }
int DVDReadAsyncPrio_Stub(DVDFileInfo* f, void* b, int32_t s, int32_t o, DVDCallback cb, int p) { return DVDReadAsyncPrio(f,b,s,o,cb,p); }
