// gx_smoketest.c - render a triangle through the GX shim -> RHI draw path.
//
// Proves GX immediate-mode calls (GXBegin/GXPosition/GXColor/GXEnd) become real
// RHI draws. Sets up a window + RHI + swapchain, hands them to gx_shim, then each
// frame clears and submits one RGB triangle via GX. --gfx picks the backend
// (default d3d11 - the first backend with the draw path). --capture writes a BMP.

#include "port/gx_shim.h"
#include "port/renderer/rhi.h"
#include "port/plat_window.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#endif

static RhiBackend parse_backend(const char* s) {
    if (!s) return RHI_BACKEND_D3D11;
    if (!strcmp(s,"vk")||!strcmp(s,"vulkan")) return RHI_BACKEND_VULKAN;
    if (!strcmp(s,"d3d12"))                   return RHI_BACKEND_D3D12;
    if (!strcmp(s,"d3d11"))                   return RHI_BACKEND_D3D11;
    if (!strcmp(s,"auto"))                    return RHI_BACKEND_AUTO;
    return RHI_BACKEND_D3D11;
}

int main(int argc, char** argv) {
    const char* gfx = "d3d11";
    const char* capturePath = NULL;
    int frames = 240, captureFrame = 45;
    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--gfx") && i+1 < argc)     gfx = argv[++i];
        else if (!strcmp(argv[i], "--capture") && i+1 < argc) capturePath = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i+1 < argc)  frames = atoi(argv[++i]);
    }

    const int W = 1280, H = 720;
    char title[128];
    snprintf(title, sizeof(title), "Stairfax Temperatures - GX triangle [%s]", gfx);
    PlatWindow* win = plat_window_create(title, W, H);
    if (!win) { fprintf(stderr, "[gx] window failed\n"); return 2; }

    RhiCreateInfo ci = {0};
    ci.backend = parse_backend(gfx);
    ci.windowHandle = plat_window_native_handle(win);
    ci.width = W; ci.height = H; ci.vsync = true; ci.appName = "Stairfax Temperatures";
    RhiInstance* rhi = rhi_create(&ci);
    if (!rhi) { fprintf(stderr, "[gx] rhi_create failed\n"); return 3; }
    RhiSwapchain* sc = rhi_swapchainCreate(rhi, ci.windowHandle, W, H, ci.vsync);

    gx_shim_setRhi(rhi, sc);
    GXInit_host();
    GXSetViewport(0, 0, (float)W, (float)H, 0, 1);
    printf("[gx] backend=%s\n", rhi_backendName(rhi_getBackend(rhi)));

    int captured = 0;
    for (int f = 0; f < frames; ++f) {
        if (!plat_window_pump(win)) break;

        rhi_beginFrame(rhi);
        rhi_clear(rhi, 0.08f, 0.10f, 0.14f, 1.0f);

        // One RGB triangle, submitted the way the game does: begin, per-vertex
        // position then color, end. Clip-space coords for this first brick.
        GXBegin(GX_TRIANGLES, 0, 3);
        GXPosition3f32( 0.0f,  0.6f, 0.5f); GXColor1u32(0xFF0000FF); // red   top
        GXPosition3f32( 0.6f, -0.6f, 0.5f); GXColor1u32(0x00FF00FF); // green bottom-right
        GXPosition3f32(-0.6f, -0.6f, 0.5f); GXColor1u32(0x0000FFFF); // blue  bottom-left
        GXEnd();

        rhi_endFrame(rhi);
        rhi_present(rhi, sc);

        if (capturePath && !captured && f >= captureFrame) {
            sleep_ms(60);
            if (plat_window_capture_bmp(win, capturePath))
                printf("[gx] captured frame %d -> %s\n", f, capturePath);
            captured = 1;
            frames = f + 3;
        }
    }

    rhi_swapchainDestroy(rhi, sc);
    rhi_destroy(rhi);
    plat_window_destroy(win);
    printf("[gx] done\n");
    return 0;
}
