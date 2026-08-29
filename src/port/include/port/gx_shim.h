#pragma once
// gx_shim.h - GX -> RHI dispatch for Stairfax Temperatures
// Shadows include/dolphin/gx.h on host. Include this via -I src/port/include before include/
//
// Strategy: keep GX type names (GXTexObj, GXColor, etc.) but reimplement
// the functions as RHI calls. shader_dolphin.c (5719 GX hits) writes to GXWGFifo;
// on host we capture those into a command buffer and translate to RHI.
// Backends: Vulkan/D3D12 primary, D3D11/GL best-effort (Dusklight model).
// See src/port/include/port/renderer/rhi.h for the abstraction.

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal GX types needed to compile src/main without pulling dolphin/gx headers
// Host build should still include the real GX structs from include/dolphin/gx/GXStruct.h
// where binary layout matters (e.g. GXTexObj). For now stub the high-frequency ops.

void GXFlush_(int a, int b); // src/main/gameloop.c:408

// TEV / Texture / Transform - stubbed, translate to pipeline
// Full mapping TODO: see src/main/shader_dolphin.c:2818 hits
void GXSetTevColorIn(int stage, int a, int b, int c, int d);
void GXSetTevAlphaIn(int stage, int a, int b, int c, int d);
void GXSetTevColorOp(int stage, int op, int bias, int scale, int clamp, int out);
void GXSetTevAlphaOp(int stage, int op, int bias, int scale, int clamp, int out);
void GXSetTevOrder(int stage, int coord, int map, int color);
void GXLoadTexObj(void* obj, int map);
void GXSetVtxAttrFmt(int fmt, int attr, int compCnt, int compType, int shift);
void GXLoadTexMtxImm(float mtx[3][4], int id, int type);

// Fifo: Game writes directly to GXWGFifo. On host, buffer it.
extern volatile void* GXWGFifo;
void gx_shim_submitFifo(void* base, uint32_t size);
void gx_shim_beginFrame(void);
void gx_shim_endFrame(void);

// RHI plumbing - called by vi_shim on init
struct RhiInstance;
struct RhiSwapchain;
void gx_shim_setRhi(struct RhiInstance* rhi, struct RhiSwapchain* sc);
struct RhiInstance* gx_shim_getRhi(void);

#ifdef __cplusplus
}
#endif
