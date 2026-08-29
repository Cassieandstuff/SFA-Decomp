#include "port/gx_shim.h"
#include <stdio.h>
#include <string.h>

// Stub implementations - replace with Sokol/Vulkan backend
volatile void* GXWGFifo = NULL;

void GXFlush_(int a, int b) { (void)a; (void)b; }

void GXSetTevColorIn(int a, int b, int c, int d, int e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void GXSetTevAlphaIn(int a, int b, int c, int d, int e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void GXSetTevColorOp(int a, int b, int c, int d, int e, int f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
void GXSetTevAlphaOp(int a, int b, int c, int d, int e, int f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
void GXSetTevOrder(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; }
void GXLoadTexObj(void* a, int b) { (void)a;(void)b; }
void GXSetVtxAttrFmt(int a, int b, int c, int d, int e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void GXLoadTexMtxImm(float m[3][4], int id, int ty) { (void)m;(void)id;(void)ty; }

void gx_shim_submitFifo(void* base, uint32_t size) { (void)base; (void)size; }
void gx_shim_beginFrame(void) {}
void gx_shim_endFrame(void) {}
