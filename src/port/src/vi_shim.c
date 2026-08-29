#include "port/vi_shim.h"
#include <stdio.h>

void VIInit(void) {}
void VIConfigure(GXRenderModeObj* r) { (void)r; }
void VISetNextFrameBuffer(void* fb) { (void)fb; }
void VISetBlack(bool b) { (void)b; }
void VIFlush(void) {}
void VIWaitForRetrace(void) {
    // Host: SDL_GL_SwapWindow + vsync
}
int VIGetNextField(void) { return 0; }
int VIGetTvFormat(void) { return 0; }
int VIGetDTVStatus(void) { return 0; }
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) { (void)cb; return NULL; }
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) { (void)cb; return NULL; }

void* vi_shim_getWindow(void) { return NULL; }
void vi_shim_pollEvents(void) {}
int vi_shim_shouldClose(void) { return 0; }
