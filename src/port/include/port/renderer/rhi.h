#pragma once
// rhi.h - Abstract RHI for Stairfax Temperatures
// Dusklight-inspired: Vulkan/D3D12 primary, D3D11/GL best-effort (Android needs GL/GLES).
// gx_shim.h translates GX TEV fixed-function into RHI pipelines; VI owns the swapchain.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RhiBackend {
    RHI_BACKEND_AUTO = 0, // vk > d3d12 on Win, vk/gles on Android
    RHI_BACKEND_VULKAN,
    RHI_BACKEND_D3D12,
    RHI_BACKEND_D3D11, // best-effort fallback
    RHI_BACKEND_GL,    // OpenGL 4.6 / GLES 3.1, best-effort, Android
    RHI_BACKEND_COUNT
} RhiBackend;

typedef struct RhiInstance RhiInstance;
typedef struct RhiSwapchain RhiSwapchain;
typedef struct RhiTexture RhiTexture;
typedef struct RhiPipeline RhiPipeline;
typedef struct RhiBuffer RhiBuffer;

typedef struct RhiCreateInfo {
    RhiBackend backend;
    void* windowHandle; // SDL_Window* or HWND, set by vi_shim
    int width, height;
    bool vsync;
    bool debug; // validation layers
    const char* appName; // "Stairfax Temperatures"
} RhiCreateInfo;

// Lifetime
RhiInstance* rhi_create(const RhiCreateInfo* info);
void         rhi_destroy(RhiInstance* rhi);
RhiBackend   rhi_getBackend(RhiInstance* rhi);
const char*  rhi_backendName(RhiBackend b);
bool         rhi_isBestEffort(RhiBackend b); // true for D3D11/GL

// Swapchain (VI)
RhiSwapchain* rhi_swapchainCreate(RhiInstance* rhi, void* windowHandle, int w, int h, bool vsync);
void          rhi_swapchainDestroy(RhiInstance* rhi, RhiSwapchain* sc);
void          rhi_swapchainResize(RhiInstance* rhi, RhiSwapchain* sc, int w, int h);
bool          rhi_present(RhiInstance* rhi, RhiSwapchain* sc); // VIWaitForRetrace -> this

// Frame
void rhi_beginFrame(RhiInstance* rhi);
void rhi_endFrame(RhiInstance* rhi);

// GX translation - called by gx_shim.c
// TEV state hash -> pipeline. GX has 16 TEV stages; we bake the active stages
// (GXSetTevColorIn/AlphaIn/Order/Op etc. from shader_dolphin.c) into a pipeline key.
typedef struct RhiTevKey {
    uint64_t hash; // hash of GX TEV + texgen + blend state
    // expanded fields for debugging / pipeline cache
    uint32_t stageCount;
    uint32_t tevHashLo, tevHashHi;
} RhiTevKey;

RhiTevKey    rhi_buildTevKey(void); // reads current GX state shadow in gx_shim
RhiPipeline* rhi_getOrCreatePipeline(RhiInstance* rhi, RhiTevKey key);

// Resources
RhiTexture* rhi_createTexture(RhiInstance* rhi, int w, int h, int mipLevels, uint32_t gxFmt, const void* data);
void        rhi_destroyTexture(RhiInstance* rhi, RhiTexture* tex);
RhiBuffer*  rhi_createBuffer(RhiInstance* rhi, size_t size, const void* data, bool dynamic);
void        rhi_destroyBuffer(RhiInstance* rhi, RhiBuffer* buf);

// Draw - called from gx_shim_submitFifo path (GXWGFifo emulation)
void rhi_setPipeline(RhiInstance* rhi, RhiPipeline* pipe);
void rhi_setTexture(RhiInstance* rhi, int slot, RhiTexture* tex);
void rhi_setVertexBuffer(RhiInstance* rhi, RhiBuffer* vb, size_t stride);
void rhi_setIndexBuffer(RhiInstance* rhi, RhiBuffer* ib);
void rhi_drawIndexed(RhiInstance* rhi, uint32_t indexCount, uint32_t firstIndex);
void rhi_draw(RhiInstance* rhi, uint32_t vertexCount, uint32_t firstVertex);
void rhi_setViewport(RhiInstance* rhi, float x, float y, float w, float h);
void rhi_clear(RhiInstance* rhi, float r, float g, float b, float a);

// Backend capability query (for D3D11/GL disabling features)
bool rhi_supports(RhiInstance* rhi, const char* feature); // e.g. "tev_stage_16", "aniso_16x"

#ifdef __cplusplus
}
#endif
