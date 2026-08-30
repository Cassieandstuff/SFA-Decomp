// tex_decode.c - GX texture formats -> linear RGBA8. See tex_decode.h.

#include "port/tex_decode.h"
#include <stdlib.h>
#include <string.h>

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint8_t  x5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }  // 5->8 bit
static inline uint8_t  x4(uint32_t v) { return (uint8_t)((v << 4) | v); }         // 4->8 bit
static inline uint8_t  x3(uint32_t v) { return (uint8_t)((v << 5) | (v << 2) | (v >> 1)); } // 3->8
static inline uint8_t  x6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }  // 6->8 bit

static void put(uint8_t* dst, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t* p = dst + ((size_t)y * w + x) * 4;
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

uint8_t* gxTexDecode(int fmt, int w, int h, const uint8_t* src) {
    if (w <= 0 || h <= 0 || !src) return NULL;
    uint8_t* dst = (uint8_t*)malloc((size_t)w * h * 4);
    if (!dst) return NULL;

    const uint8_t* s = src;
    switch (fmt) {
    case GX_TF_I8: // 8x4 tiles, 1 byte intensity
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 8)
                for (int iy = 0; iy < 4; ++iy)
                    for (int ix = 0; ix < 8; ++ix) {
                        uint8_t i = *s++;
                        put(dst, w, h, tx + ix, ty + iy, i, i, i, 255);
                    }
        return dst;

    case GX_TF_IA8: // 4x4 tiles, 16-bit: intensity high byte, alpha low byte
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4)
                for (int iy = 0; iy < 4; ++iy)
                    for (int ix = 0; ix < 4; ++ix) {
                        uint16_t v = be16(s); s += 2;
                        uint8_t i = (uint8_t)(v >> 8), a = (uint8_t)(v & 0xff);
                        put(dst, w, h, tx + ix, ty + iy, i, i, i, a);
                    }
        return dst;

    case GX_TF_RGB565: // 4x4 tiles, 16-bit
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4)
                for (int iy = 0; iy < 4; ++iy)
                    for (int ix = 0; ix < 4; ++ix) {
                        uint16_t v = be16(s); s += 2;
                        put(dst, w, h, tx + ix, ty + iy,
                            x5(v >> 11), x6((v >> 5) & 0x3f), x5(v & 0x1f), 255);
                    }
        return dst;

    case GX_TF_RGB5A3: // 4x4 tiles, 16-bit: top bit selects RGB555 (opaque) vs RGB4A3
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4)
                for (int iy = 0; iy < 4; ++iy)
                    for (int ix = 0; ix < 4; ++ix) {
                        uint16_t v = be16(s); s += 2;
                        uint8_t r, g, b, a;
                        if (v & 0x8000) { r = x5((v >> 10) & 0x1f); g = x5((v >> 5) & 0x1f); b = x5(v & 0x1f); a = 255; }
                        else            { a = x3((v >> 12) & 0x7); r = x4((v >> 8) & 0xf); g = x4((v >> 4) & 0xf); b = x4(v & 0xf); }
                        put(dst, w, h, tx + ix, ty + iy, r, g, b, a);
                    }
        return dst;

    case GX_TF_RGBA8: // 4x4 tiles, 64 bytes: 32 bytes AR pairs, then 32 bytes GB pairs
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4) {
                const uint8_t* ar = s;       // A,R for 16 texels
                const uint8_t* gb = s + 32;  // G,B for 16 texels
                for (int t = 0; t < 16; ++t) {
                    int ix = t & 3, iy = t >> 2;
                    put(dst, w, h, tx + ix, ty + iy, ar[t * 2 + 1], gb[t * 2 + 0], gb[t * 2 + 1], ar[t * 2 + 0]);
                }
                s += 64;
            }
        return dst;

    default:
        free(dst);
        return NULL; // unsupported (I4/IA4/CMPR/palettized) - added later
    }
}

int gxTexDecodeSelfTest(void) {
    // RGBA8: one 4x4 tile. texel 0 = (R,G,B,A)=(10,20,30,40), texel 5 = (1,2,3,4).
    uint8_t rgba8[64];
    memset(rgba8, 0, sizeof(rgba8));
    // AR pairs (bytes 0..31): [A,R] per texel; GB pairs (32..63): [G,B].
    rgba8[0] = 40; rgba8[1] = 10;   rgba8[32] = 20; rgba8[33] = 30;   // texel 0
    rgba8[10] = 4; rgba8[11] = 1;   rgba8[42] = 2;  rgba8[43] = 3;    // texel 5
    uint8_t* d = gxTexDecode(GX_TF_RGBA8, 4, 4, rgba8);
    if (!d) return 1;
    int ok = d[0] == 10 && d[1] == 20 && d[2] == 30 && d[3] == 40 &&
             d[5 * 4 + 0] == 1 && d[5 * 4 + 1] == 2 && d[5 * 4 + 2] == 3 && d[5 * 4 + 3] == 4;
    free(d);
    if (!ok) return 2;

    // RGB5A3 opaque white (0xFFFF -> 555 all ones -> 255,255,255,255).
    uint8_t rgb5a3[32];
    memset(rgb5a3, 0, sizeof(rgb5a3));
    rgb5a3[0] = 0xFF; rgb5a3[1] = 0xFF; // texel 0 = 0xFFFF
    d = gxTexDecode(GX_TF_RGB5A3, 4, 4, rgb5a3);
    if (!d) return 3;
    ok = d[0] == 255 && d[1] == 255 && d[2] == 255 && d[3] == 255;
    free(d);
    if (!ok) return 4;

    return 0;
}
