// gx_tex_smoketest.c - render a textured quad through the GX shim -> RHI.
//
// Builds a procedural texture in GC RGBA8 tiled format, feeds it through
// GXInitTexObj/GXLoadTexObj (which decodes it via tex_decode and uploads to the
// RHI), then draws a textured quad with GXTexCoord2f32. Proves the whole texture
// path: GC-format decode -> RHI texture -> sampled draw. --gfx picks the backend.

#include "port/gx_shim.h"
#include "port/tex_decode.h"
#include "port/renderer/rhi.h"
#include "port/plat_window.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#endif

static void perspectiveLH(float m[4][4], float fovyRad, float aspect, float zn, float zf) {
    float ys = 1.0f / tanf(fovyRad * 0.5f);
    memset(m, 0, sizeof(float) * 16);
    m[0][0] = ys / aspect; m[1][1] = ys;
    m[2][2] = zf / (zf - zn); m[2][3] = -zn * zf / (zf - zn); m[3][2] = 1.0f;
}
static void modelviewYT(float m[3][4], float angle, float dist) {
    float c = cosf(angle), s = sinf(angle);
    m[0][0] = c;  m[0][1] = 0; m[0][2] = s; m[0][3] = 0;
    m[1][0] = 0;  m[1][1] = 1; m[1][2] = 0; m[1][3] = 0;
    m[2][0] = -s; m[2][1] = 0; m[2][2] = c; m[2][3] = dist;
}

// Encode a procedural WxH image into GC RGBA8 tiled format (4x4 tiles, 64 bytes:
// 32 bytes A,R pairs then 32 bytes G,B pairs).
static void makeGcRgba8(uint8_t* out, int W, int H) {
    int o = 0;
    for (int ty = 0; ty < H; ty += 4)
        for (int tx = 0; tx < W; tx += 4) {
            uint8_t ar[32], gb[32];
            for (int t = 0; t < 16; ++t) {
                int x = tx + (t & 3), y = ty + (t >> 2);
                int checker = ((x / 8) + (y / 8)) & 1;
                uint8_t R = (uint8_t)(x * 255 / (W - 1));
                uint8_t G = (uint8_t)(y * 255 / (H - 1));
                uint8_t B = checker ? 235 : 40;
                uint8_t A = 255;
                ar[t*2] = A; ar[t*2+1] = R; gb[t*2] = G; gb[t*2+1] = B;
            }
            memcpy(out + o, ar, 32); memcpy(out + o + 32, gb, 32); o += 64;
        }
}

static RhiBackend parse_backend(const char* s) {
    if (!s) return RHI_BACKEND_D3D11;
    if (!strcmp(s,"vk")||!strcmp(s,"vulkan")) return RHI_BACKEND_VULKAN;
    if (!strcmp(s,"d3d12")) return RHI_BACKEND_D3D12;
    if (!strcmp(s,"d3d11")) return RHI_BACKEND_D3D11;
    return RHI_BACKEND_D3D11;
}

int main(int argc, char** argv) {
    const char* gfx = "d3d11"; const char* capturePath = NULL;
    int frames = 240, captureFrame = 30;
    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--gfx") && i+1 < argc)     gfx = argv[++i];
        else if (!strcmp(argv[i], "--capture") && i+1 < argc) capturePath = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i+1 < argc)  frames = atoi(argv[++i]);
    }

    printf("[gxtex] decode self-test: %s\n", gxTexDecodeSelfTest() == 0 ? "PASS" : "FAIL");

    const int W = 1280, H = 720;
    char title[128];
    snprintf(title, sizeof(title), "Stairfax Temperatures - GX textured quad [%s]", gfx);
    PlatWindow* win = plat_window_create(title, W, H);
    if (!win) return 2;

    RhiCreateInfo ci = {0};
    ci.backend = parse_backend(gfx);
    ci.windowHandle = plat_window_native_handle(win);
    ci.width = W; ci.height = H; ci.vsync = true; ci.appName = "Stairfax Temperatures";
    RhiInstance* rhi = rhi_create(&ci);
    if (!rhi) { fprintf(stderr, "[gxtex] rhi_create failed\n"); return 3; }
    RhiSwapchain* sc = rhi_swapchainCreate(rhi, ci.windowHandle, W, H, ci.vsync);
    gx_shim_setRhi(rhi, sc);
    GXInit_host();
    printf("[gxtex] backend=%s\n", rhi_backendName(rhi_getBackend(rhi)));

    // Procedural GC RGBA8 texture (64x64) + GX texture object.
    const int TW = 64, TH = 64;
    static uint8_t gcTex[64 * 64 * 4];
    makeGcRgba8(gcTex, TW, TH);
    GXTexObj tex;
    GXInitTexObj(&tex, gcTex, (uint16_t)TW, (uint16_t)TH, GX_TF_RGBA8, 0, 0, 0);

    int captured = 0;
    for (int f = 0; f < frames; ++f) {
        if (!plat_window_pump(win)) break;

        rhi_beginFrame(rhi);
        rhi_clear(rhi, 0.08f, 0.10f, 0.14f, 1.0f);

        float proj[4][4], mv[3][4];
        perspectiveLH(proj, 60.0f * 3.14159265f / 180.0f, (float)W / (float)H, 0.1f, 100.0f);
        modelviewYT(mv, (float)f * 0.02f, 3.0f);
        GXSetProjection(proj, GX_PERSPECTIVE);
        GXLoadPosMtxImm(mv, GX_PNMTX0);
        GXSetCurrentMtx(GX_PNMTX0);

        GXLoadTexObj(&tex, GX_TEXMAP0);

        // Textured quad in model space, white vertex color so the texture shows as-is.
        GXBegin(GX_QUADS, 0, 4);
        GXPosition3f32(-1.0f,  1.0f, 0.0f); GXColor1u32(0xFFFFFFFF); GXTexCoord2f32(0.0f, 0.0f); // top-left
        GXPosition3f32( 1.0f,  1.0f, 0.0f); GXColor1u32(0xFFFFFFFF); GXTexCoord2f32(1.0f, 0.0f); // top-right
        GXPosition3f32( 1.0f, -1.0f, 0.0f); GXColor1u32(0xFFFFFFFF); GXTexCoord2f32(1.0f, 1.0f); // bottom-right
        GXPosition3f32(-1.0f, -1.0f, 0.0f); GXColor1u32(0xFFFFFFFF); GXTexCoord2f32(0.0f, 1.0f); // bottom-left
        GXEnd();

        rhi_endFrame(rhi);
        rhi_present(rhi, sc);

        if (capturePath && !captured && f >= captureFrame) {
            sleep_ms(60);
            if (plat_window_capture_bmp(win, capturePath))
                printf("[gxtex] captured frame %d -> %s\n", f, capturePath);
            captured = 1; frames = f + 3;
        }
    }

    rhi_swapchainDestroy(rhi, sc);
    rhi_destroy(rhi);
    plat_window_destroy(win);
    printf("[gxtex] done\n");
    return 0;
}
