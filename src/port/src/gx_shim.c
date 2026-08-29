// gx_shim.c - GX immediate-mode -> RHI colored draws (first GX brick).
//
// GXBegin(prim,fmt,n) starts a batch; GXPosition* + GXColor* fill one vertex each
// (color finalizes the vertex, matching the game's pos-then-color ordering);
// GXEnd() converts the primitive to a triangle list and issues one rhi_drawColored.
// Positions are treated as clip-space for now - GX viewport/projection/model
// matrices come with the transform stage; TEV/textures with the shader stage.

#include "port/gx_shim.h"
#include "port/renderer/rhi.h"

#include <stdlib.h>
#include <string.h>

static struct RhiInstance*  gRhi;
static struct RhiSwapchain* gSc;

#define GX_MAX_BATCH 4096
static RhiColorVertex gVerts[GX_MAX_BATCH];
static int            gCount;
static uint8_t        gPrim;
static RhiColorVertex gCur;
static bool           gHavePos;

void gx_shim_setRhi(struct RhiInstance* rhi, struct RhiSwapchain* sc) { gRhi = rhi; gSc = sc; }
struct RhiInstance* gx_shim_getRhi(void) { return gRhi; }

void GXInit_host(void) { gCount = 0; gHavePos = false; }
void GXSetViewport(float x, float y, float w, float h, float n, float f) { (void)x;(void)y;(void)w;(void)h;(void)n;(void)f; }
void GXSetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x;(void)y;(void)w;(void)h; }
void GXSetCullMode(int mode) { (void)mode; }

void GXBegin(uint8_t primitive, uint8_t vtxfmt, uint16_t nverts) {
    (void)vtxfmt; (void)nverts;
    gPrim = primitive;
    gCount = 0;
    gHavePos = false;
}

void GXPosition3f32(float x, float y, float z) {
    gCur.x = x; gCur.y = y; gCur.z = z;
    gHavePos = true;
}
void GXPosition2f32(float x, float y) { GXPosition3f32(x, y, 0.0f); }

static void pushVertex(uint32_t rgba) {
    if (!gHavePos || gCount >= GX_MAX_BATCH) return;
    gCur.rgba = rgba;
    gVerts[gCount++] = gCur;
    gHavePos = false;
}
void GXColor1u32(uint32_t c) {
    uint8_t r = (uint8_t)(c >> 24), g = (uint8_t)(c >> 16), b = (uint8_t)(c >> 8), a = (uint8_t)c;
    pushVertex((uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24));
}
void GXColor4u8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    pushVertex((uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24));
}

// Expand the primitive into a triangle list, then draw.
static void flushBatch(void) {
    if (!gRhi || gCount == 0) return;

    if (gPrim == GX_TRIANGLES) {
        rhi_drawColored(gRhi, gVerts, (uint32_t)(gCount - gCount % 3));
        return;
    }
    if (gPrim == GX_TRIANGLESTRIP || gPrim == GX_TRIANGLEFAN) {
        static RhiColorVertex tri[GX_MAX_BATCH * 3];
        int n = 0;
        for (int i = 2; i < gCount && n + 3 <= GX_MAX_BATCH * 3; ++i) {
            if (gPrim == GX_TRIANGLEFAN) {
                tri[n++] = gVerts[0];
                tri[n++] = gVerts[i - 1];
                tri[n++] = gVerts[i];
            } else { // strip: alternate winding
                if (i & 1) { tri[n++] = gVerts[i - 1]; tri[n++] = gVerts[i - 2]; tri[n++] = gVerts[i]; }
                else       { tri[n++] = gVerts[i - 2]; tri[n++] = gVerts[i - 1]; tri[n++] = gVerts[i]; }
            }
        }
        rhi_drawColored(gRhi, tri, (uint32_t)n);
        return;
    }
    if (gPrim == GX_QUADS) {
        static RhiColorVertex tri[GX_MAX_BATCH * 3 / 2];
        int n = 0;
        for (int i = 0; i + 4 <= gCount && n + 6 <= (int)(sizeof(tri)/sizeof(tri[0])); i += 4) {
            tri[n++] = gVerts[i];   tri[n++] = gVerts[i+1]; tri[n++] = gVerts[i+2];
            tri[n++] = gVerts[i];   tri[n++] = gVerts[i+2]; tri[n++] = gVerts[i+3];
        }
        rhi_drawColored(gRhi, tri, (uint32_t)n);
        return;
    }
    // points/lines not yet supported by the colored-triangle path
}

void GXEnd(void) {
    flushBatch();
    gCount = 0;
    gHavePos = false;
}
