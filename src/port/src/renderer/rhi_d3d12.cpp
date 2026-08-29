// rhi_d3d12.cpp - D3D12 backend (primary on Windows). Clear-to-color bring-up.
//
// C++ TU exposing a C ABI. Classic command-queue/descriptor-heap/fence setup.
// Sync is a full GPU flush per present - not the final perf model, but correct and
// simple for bring-up; the frame pipeline gets deepened when real drawing lands.

#include "port/renderer/rhi_d3d12.h"
#include "port/renderer/rhi_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

const UINT kFrameCount = 2;

struct D3d12Swapchain {
    ComPtr<IDXGISwapChain3>      swap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource>       renderTargets[kFrameCount];
    ComPtr<ID3D12CommandAllocator> allocators[kFrameCount];
    UINT   rtvDescSize = 0;
    UINT   frameIndex  = 0;
    int    w = 0, h = 0;
    bool   vsync = true;

    ComPtr<ID3D12Fence> fence;
    UINT64              fenceValue = 0;
    HANDLE              fenceEvent = nullptr;
};

struct D3d12Instance {
    RhiInstance                        base;
    ComPtr<IDXGIFactory4>              factory;
    ComPtr<ID3D12Device>               device;
    ComPtr<ID3D12CommandQueue>         queue;
    ComPtr<ID3D12GraphicsCommandList>  cmdList;
    D3d12Swapchain*                    active = nullptr;
};

D3d12Instance* self(RhiInstance* r) { return reinterpret_cast<D3d12Instance*>(r); }

D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(D3d12Swapchain* sc) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = sc->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)sc->frameIndex * sc->rtvDescSize;
    return h;
}

void barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
             D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &b);
}

void waitIdle(D3d12Instance* s) {
    if (!s->active || !s->active->fence) return;
    D3d12Swapchain* sc = s->active;
    const UINT64 v = ++sc->fenceValue;
    s->queue->Signal(sc->fence.Get(), v);
    if (sc->fence->GetCompletedValue() < v) {
        sc->fence->SetEventOnCompletion(v, sc->fenceEvent);
        WaitForSingleObjectEx(sc->fenceEvent, INFINITE, FALSE);
    }
}

bool makeTargets(D3d12Instance* s, D3d12Swapchain* sc) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = sc->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT n = 0; n < kFrameCount; ++n) {
        if (FAILED(sc->swap->GetBuffer(n, IID_PPV_ARGS(&sc->renderTargets[n])))) return false;
        s->device->CreateRenderTargetView(sc->renderTargets[n].Get(), nullptr, h);
        h.ptr += sc->rtvDescSize;
    }
    sc->frameIndex = sc->swap->GetCurrentBackBufferIndex();
    return true;
}

RhiSwapchain* d3d12_swapchainCreate(RhiInstance* r, void* windowHandle, int w, int h, bool vsync) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = new D3d12Swapchain();
    sc->w = w; sc->h = h; sc->vsync = vsync;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount = kFrameCount;
    desc.Width       = (UINT)w;
    desc.Height      = (UINT)h;
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;
    desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swap1;
    if (FAILED(s->factory->CreateSwapChainForHwnd(
            s->queue.Get(), (HWND)windowHandle, &desc, nullptr, nullptr, &swap1))) {
        delete sc; return nullptr;
    }
    s->factory->MakeWindowAssociation((HWND)windowHandle, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(swap1.As(&sc->swap))) { delete sc; return nullptr; }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = kFrameCount;
    hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(s->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sc->rtvHeap)))) {
        delete sc; return nullptr;
    }
    sc->rtvDescSize = s->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (!makeTargets(s, sc)) { delete sc; return nullptr; }

    for (UINT n = 0; n < kFrameCount; ++n) {
        if (FAILED(s->device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sc->allocators[n])))) {
            delete sc; return nullptr;
        }
    }

    if (!s->cmdList) {
        if (FAILED(s->device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, sc->allocators[0].Get(),
                nullptr, IID_PPV_ARGS(&s->cmdList)))) {
            delete sc; return nullptr;
        }
        s->cmdList->Close();
    }

    if (FAILED(s->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sc->fence)))) {
        delete sc; return nullptr;
    }
    sc->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!sc->fenceEvent) { delete sc; return nullptr; }

    s->active = sc;
    return reinterpret_cast<RhiSwapchain*>(sc);
}

void d3d12_swapchainDestroy(RhiInstance* r, RhiSwapchain* h) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = reinterpret_cast<D3d12Swapchain*>(h);
    if (!sc) return;
    if (s->active == sc) { waitIdle(s); s->active = nullptr; }
    if (sc->fenceEvent) CloseHandle(sc->fenceEvent);
    delete sc;
}

void d3d12_swapchainResize(RhiInstance* r, RhiSwapchain* h, int w, int t) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = reinterpret_cast<D3d12Swapchain*>(h);
    if (!sc || !sc->swap) return;
    waitIdle(s);
    for (UINT n = 0; n < kFrameCount; ++n) sc->renderTargets[n].Reset();
    sc->swap->ResizeBuffers(kFrameCount, (UINT)w, (UINT)t, DXGI_FORMAT_UNKNOWN, 0);
    sc->w = w; sc->h = t;
    makeTargets(s, sc);
}

void d3d12_beginFrame(RhiInstance* r) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = s->active;
    if (!sc) return;
    ID3D12CommandAllocator* alloc = sc->allocators[sc->frameIndex].Get();
    alloc->Reset();
    s->cmdList->Reset(alloc, nullptr);
    barrier(s->cmdList.Get(), sc->renderTargets[sc->frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHandle(sc);
    s->cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = {}; vp.Width = (FLOAT)sc->w; vp.Height = (FLOAT)sc->h; vp.MaxDepth = 1.0f;
    D3D12_RECT     rc = {}; rc.right = sc->w; rc.bottom = sc->h;
    s->cmdList->RSSetViewports(1, &vp);
    s->cmdList->RSSetScissorRects(1, &rc);
}

void d3d12_clear(RhiInstance* r, float cr, float cg, float cb, float ca) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = s->active;
    if (!sc) return;
    const float color[4] = { cr, cg, cb, ca };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHandle(sc);
    s->cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void d3d12_endFrame(RhiInstance* r) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = s->active;
    if (!sc) return;
    barrier(s->cmdList.Get(), sc->renderTargets[sc->frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    s->cmdList->Close();
    ID3D12CommandList* lists[] = { s->cmdList.Get() };
    s->queue->ExecuteCommandLists(1, lists);
}

bool d3d12_present(RhiInstance* r, RhiSwapchain* h) {
    D3d12Instance* s = self(r);
    D3d12Swapchain* sc = reinterpret_cast<D3d12Swapchain*>(h);
    if (!sc || !sc->swap) return false;
    HRESULT hr = sc->swap->Present(sc->vsync ? 1 : 0, 0);
    if (FAILED(hr)) return false;
    waitIdle(s); // flush; simple + correct for bring-up
    sc->frameIndex = sc->swap->GetCurrentBackBufferIndex();
    return true;
}

void d3d12_destroy(RhiInstance* r) {
    D3d12Instance* s = self(r);
    if (!s) return;
    if (s->active) {
        waitIdle(s);
        if (s->active->fenceEvent) CloseHandle(s->active->fenceEvent);
        delete s->active;
        s->active = nullptr;
    }
    delete s;
}

const RhiOps kOps = {
    d3d12_destroy,
    d3d12_swapchainCreate,
    d3d12_swapchainDestroy,
    d3d12_swapchainResize,
    d3d12_present,
    d3d12_beginFrame,
    d3d12_endFrame,
    d3d12_clear,
};

bool pickAdapter(IDXGIFactory4* factory, ComPtr<IDXGIAdapter1>& out) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d; adapter->GetDesc1(&d);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            out = adapter;
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" bool rhi_d3d12_available(void) {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IDXGIAdapter1> adapter;
    return pickAdapter(factory.Get(), adapter);
}

extern "C" RhiInstance* rhi_d3d12_create(const RhiCreateInfo* info) {
    UINT factoryFlags = 0;
    if (info->debug) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    D3d12Instance* s = new D3d12Instance();
    s->base.ops     = &kOps;
    s->base.backend = RHI_BACKEND_D3D12;

    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&s->factory)))) { delete s; return nullptr; }

    ComPtr<IDXGIAdapter1> adapter;
    if (!pickAdapter(s->factory.Get(), adapter)) { delete s; return nullptr; }
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&s->device)))) {
        delete s; return nullptr;
    }

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(s->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&s->queue)))) { delete s; return nullptr; }

    (void)info;
    return &s->base;
}
