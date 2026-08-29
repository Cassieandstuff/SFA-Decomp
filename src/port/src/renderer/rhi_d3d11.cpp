// rhi_d3d11.cpp - D3D11 backend (best-effort). Clear-to-color bring-up.
//
// C++ TU exposing a C ABI (rhi_d3d11_available / rhi_d3d11_create). COM via ComPtr.
// Feature level 11_0/11_1, flip-model swapchain, immediate-context clear.

#include "port/renderer/rhi_d3d11.h"
#include "port/renderer/rhi_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

struct D3d11Swapchain {
    ComPtr<IDXGISwapChain1>        swap;
    ComPtr<ID3D11RenderTargetView> rtv;
    int  w = 0, h = 0;
    bool vsync = true;
};

struct D3d11Instance {
    RhiInstance                 base;
    ComPtr<ID3D11Device>        device;
    ComPtr<ID3D11DeviceContext> ctx;
    D3d11Swapchain*             active = nullptr;
};

D3d11Instance* self(RhiInstance* r) { return reinterpret_cast<D3d11Instance*>(r); }

bool makeRTV(D3d11Instance* s, D3d11Swapchain* sc) {
    ComPtr<ID3D11Texture2D> backbuf;
    if (FAILED(sc->swap->GetBuffer(0, IID_PPV_ARGS(&backbuf)))) return false;
    return SUCCEEDED(s->device->CreateRenderTargetView(backbuf.Get(), nullptr, &sc->rtv));
}

RhiSwapchain* d3d11_swapchainCreate(RhiInstance* r, void* windowHandle, int w, int h, bool vsync) {
    D3d11Instance* s = self(r);
    D3d11Swapchain* sc = new D3d11Swapchain();
    sc->w = w; sc->h = h; sc->vsync = vsync;

    ComPtr<IDXGIDevice>  dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(s->device.As(&dxgiDev)))              { delete sc; return nullptr; }
    if (FAILED(dxgiDev->GetAdapter(&adapter)))       { delete sc; return nullptr; }
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) { delete sc; return nullptr; }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width  = (UINT)w;
    desc.Height = (UINT)h;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    if (FAILED(factory->CreateSwapChainForHwnd(
            s->device.Get(), (HWND)windowHandle, &desc, nullptr, nullptr, &sc->swap))) {
        delete sc; return nullptr;
    }
    if (!makeRTV(s, sc)) { delete sc; return nullptr; }

    s->active = sc;
    return reinterpret_cast<RhiSwapchain*>(sc);
}

void d3d11_swapchainDestroy(RhiInstance* r, RhiSwapchain* h) {
    D3d11Instance* s = self(r);
    D3d11Swapchain* sc = reinterpret_cast<D3d11Swapchain*>(h);
    if (s->active == sc) s->active = nullptr;
    delete sc;
}

void d3d11_swapchainResize(RhiInstance* r, RhiSwapchain* h, int w, int t) {
    D3d11Instance* s = self(r);
    D3d11Swapchain* sc = reinterpret_cast<D3d11Swapchain*>(h);
    if (!sc || !sc->swap) return;
    s->ctx->OMSetRenderTargets(0, nullptr, nullptr);
    sc->rtv.Reset();
    sc->swap->ResizeBuffers(0, (UINT)w, (UINT)t, DXGI_FORMAT_UNKNOWN, 0);
    sc->w = w; sc->h = t;
    makeRTV(s, sc);
}

bool d3d11_present(RhiInstance* r, RhiSwapchain* h) {
    (void)r;
    D3d11Swapchain* sc = reinterpret_cast<D3d11Swapchain*>(h);
    if (!sc || !sc->swap) return false;
    return SUCCEEDED(sc->swap->Present(sc->vsync ? 1 : 0, 0));
}

void d3d11_beginFrame(RhiInstance* r) {
    D3d11Instance* s = self(r);
    if (!s->active || !s->active->rtv) return;
    ID3D11RenderTargetView* rtv = s->active->rtv.Get();
    s->ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {};
    vp.Width  = (FLOAT)s->active->w;
    vp.Height = (FLOAT)s->active->h;
    vp.MaxDepth = 1.0f;
    s->ctx->RSSetViewports(1, &vp);
}

void d3d11_endFrame(RhiInstance* r) { (void)r; }

void d3d11_clear(RhiInstance* r, float cr, float cg, float cb, float ca) {
    D3d11Instance* s = self(r);
    if (!s->active || !s->active->rtv) return;
    const float color[4] = { cr, cg, cb, ca };
    s->ctx->ClearRenderTargetView(s->active->rtv.Get(), color);
}

void d3d11_destroy(RhiInstance* r) {
    D3d11Instance* s = self(r);
    if (!s) return;
    if (s->ctx) s->ctx->ClearState();
    delete s->active;
    delete s;
}

const RhiOps kOps = {
    d3d11_destroy,
    d3d11_swapchainCreate,
    d3d11_swapchainDestroy,
    d3d11_swapchainResize,
    d3d11_present,
    d3d11_beginFrame,
    d3d11_endFrame,
    d3d11_clear,
};

} // namespace

extern "C" bool rhi_d3d11_available(void) {
    return true; // D3D11 ships with the OS; device creation is the real gate.
}

extern "C" RhiInstance* rhi_d3d11_create(const RhiCreateInfo* info) {
    D3d11Instance* s = new D3d11Instance();
    s->base.ops     = &kOps;
    s->base.backend = RHI_BACKEND_D3D11;

    UINT flags = 0;
    if (info->debug) flags |= D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
        D3D11_SDK_VERSION, &s->device, nullptr, &s->ctx);

    // Retry without the debug layer if it isn't installed.
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
            D3D11_SDK_VERSION, &s->device, nullptr, &s->ctx);
    }
    if (FAILED(hr)) { delete s; return nullptr; }

    // Swapchain is created explicitly via rhi_swapchainCreate (VI owns it);
    // an HWND can host only one flip-model swapchain at a time.
    (void)info;
    return &s->base;
}
