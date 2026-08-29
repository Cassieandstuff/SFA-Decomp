#pragma once
// vi_shim.h - VI -> SDL_Window/swapchain
// Shadows include/dolphin/vi.h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GXRenderModeObj GXRenderModeObj; // from dolphin/gx

void VIInit(void);
void VIConfigure(GXRenderModeObj* rmode);
void VISetNextFrameBuffer(void* fb);
void VISetBlack(bool black);
void VIFlush(void);
void VIWaitForRetrace(void);
int VIGetNextField(void);
int VIGetTvFormat(void);
int VIGetDTVStatus(void);

typedef void (*VIRetraceCallback)(uint32_t count);
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb);
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb);

// Host helpers
void* vi_shim_getWindow(void);
void vi_shim_pollEvents(void);
int vi_shim_shouldClose(void);

#ifdef __cplusplus
}
#endif
