// rhi_vulkan.cpp - Vulkan backend (primary). Clear-to-color bring-up.
//
// C ABI over a C++ TU. Uses VK_NO_PROTOTYPES and loads vulkan-1.dll dynamically:
// the backend reports unavailable (rather than failing process load) when no
// loader/ICD is present. Clears with vkCmdClearColorImage, which needs no render
// pass, framebuffer, or pipeline - the minimal path to a colored window. The real
// draw path (render pass + TEV ubershader pipelines) is built on top later.
//
// Compiled only when the Vulkan SDK headers are found (STAIRFAX_HAVE_VULKAN);
// otherwise rhi_vulkan.c provides link-satisfying stubs.

#include "port/renderer/rhi_vulkan.h"
#include "port/renderer/rhi_internal.h"

#ifdef STAIRFAX_HAVE_VULKAN

#define VK_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <vector>
#include <cstdio>
#include <cstring>

namespace {

const uint32_t kFramesInFlight = 2;

struct VkApi {
    HMODULE dll = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;

    // global
    PFN_vkCreateInstance createInstance = nullptr;
    // instance
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkCreateWin32SurfaceKHR createWin32Surface = nullptr;
    PFN_vkDestroySurfaceKHR destroySurface = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getSurfaceSupport = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getSurfaceCaps = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getSurfaceFormats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getSurfacePresentModes = nullptr;
    // device
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImages = nullptr;
    PFN_vkAcquireNextImageKHR acquireNextImage = nullptr;
    PFN_vkQueuePresentKHR queuePresent = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkCmdClearColorImage cmdClearColorImage = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
};

struct VkSwap {
    VkSurfaceKHR         surface   = VK_NULL_HANDLE;
    VkSwapchainKHR       swapchain = VK_NULL_HANDLE;
    VkFormat             format    = VK_FORMAT_UNDEFINED;
    VkExtent2D           extent    = {0, 0};
    std::vector<VkImage> images;
    bool                 vsync = true;
};

struct VkInst {
    RhiInstance      base;
    VkApi            api;
    VkInstance       instance   = VK_NULL_HANDLE;
    VkPhysicalDevice phys       = VK_NULL_HANDLE;
    uint32_t         queueFamily = 0;
    VkDevice         device     = VK_NULL_HANDLE;
    VkQueue          queue      = VK_NULL_HANDLE;
    VkCommandPool    cmdPool    = VK_NULL_HANDLE;

    VkCommandBuffer  cmd[kFramesInFlight]        = {};
    VkSemaphore      acquireSem[kFramesInFlight] = {};
    VkSemaphore      renderSem[kFramesInFlight]  = {};
    VkFence          inFlight[kFramesInFlight]    = {};

    VkSwap*   active     = nullptr;
    uint32_t  frame      = 0;
    uint32_t  imageIndex = 0;
    float     clearColor[4] = {0, 0, 0, 1};
};

VkInst* self(RhiInstance* r) { return reinterpret_cast<VkInst*>(r); }

bool loadInstanceProcs(VkApi* api, VkInstance inst) {
#define GIPA(fld, fn) api->fld = (PFN_##fn)api->getInstanceProcAddr(inst, #fn); if (!api->fld) return false
    GIPA(destroyInstance, vkDestroyInstance);
    GIPA(enumeratePhysicalDevices, vkEnumeratePhysicalDevices);
    GIPA(getPhysicalDeviceProperties, vkGetPhysicalDeviceProperties);
    GIPA(getPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties);
    GIPA(createDevice, vkCreateDevice);
    GIPA(getDeviceProcAddr, vkGetDeviceProcAddr);
    GIPA(createWin32Surface, vkCreateWin32SurfaceKHR);
    GIPA(destroySurface, vkDestroySurfaceKHR);
    GIPA(getSurfaceSupport, vkGetPhysicalDeviceSurfaceSupportKHR);
    GIPA(getSurfaceCaps, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    GIPA(getSurfaceFormats, vkGetPhysicalDeviceSurfaceFormatsKHR);
    GIPA(getSurfacePresentModes, vkGetPhysicalDeviceSurfacePresentModesKHR);
#undef GIPA
    return true;
}

bool loadDeviceProcs(VkApi* api, VkDevice dev) {
#define GDPA(fld, fn) api->fld = (PFN_##fn)api->getDeviceProcAddr(dev, #fn); if (!api->fld) return false
    GDPA(getDeviceQueue, vkGetDeviceQueue);
    GDPA(destroyDevice, vkDestroyDevice);
    GDPA(deviceWaitIdle, vkDeviceWaitIdle);
    GDPA(createSwapchain, vkCreateSwapchainKHR);
    GDPA(destroySwapchain, vkDestroySwapchainKHR);
    GDPA(getSwapchainImages, vkGetSwapchainImagesKHR);
    GDPA(acquireNextImage, vkAcquireNextImageKHR);
    GDPA(queuePresent, vkQueuePresentKHR);
    GDPA(createCommandPool, vkCreateCommandPool);
    GDPA(destroyCommandPool, vkDestroyCommandPool);
    GDPA(allocateCommandBuffers, vkAllocateCommandBuffers);
    GDPA(beginCommandBuffer, vkBeginCommandBuffer);
    GDPA(endCommandBuffer, vkEndCommandBuffer);
    GDPA(resetCommandBuffer, vkResetCommandBuffer);
    GDPA(cmdClearColorImage, vkCmdClearColorImage);
    GDPA(cmdPipelineBarrier, vkCmdPipelineBarrier);
    GDPA(createSemaphore, vkCreateSemaphore);
    GDPA(destroySemaphore, vkDestroySemaphore);
    GDPA(createFence, vkCreateFence);
    GDPA(destroyFence, vkDestroyFence);
    GDPA(waitForFences, vkWaitForFences);
    GDPA(resetFences, vkResetFences);
    GDPA(queueSubmit, vkQueueSubmit);
#undef GDPA
    return true;
}

void imageBarrier(VkApi* api, VkCommandBuffer cmd, VkImage image,
                  VkImageLayout oldL, VkImageLayout newL,
                  VkAccessFlags srcA, VkAccessFlags dstA,
                  VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = srcA;
    b.dstAccessMask = dstA;
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    api->cmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// --- ops -------------------------------------------------------------------

RhiSwapchain* vulkan_swapchainCreate(RhiInstance* r, void* windowHandle, int w, int h, bool vsync) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    VkSwap* sc = new VkSwap();
    sc->vsync = vsync;

    VkWin32SurfaceCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd = (HWND)windowHandle;
    if (api->createWin32Surface(s->instance, &sci, nullptr, &sc->surface) != VK_SUCCESS) {
        delete sc; return nullptr;
    }

    VkBool32 present = VK_FALSE;
    api->getSurfaceSupport(s->phys, s->queueFamily, sc->surface, &present);
    if (!present) {
        fprintf(stderr, "[vulkan] queue family %u does not support present\n", s->queueFamily);
        api->destroySurface(s->instance, sc->surface, nullptr);
        delete sc; return nullptr;
    }

    VkSurfaceCapabilitiesKHR caps = {};
    api->getSurfaceCaps(s->phys, sc->surface, &caps);

    uint32_t fmtCount = 0;
    api->getSurfaceFormats(s->phys, sc->surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    api->getSurfaceFormats(s->phys, sc->surface, &fmtCount, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    }
    sc->format = chosen.format;

    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        sc->extent = caps.currentExtent;
    } else {
        sc->extent.width  = (uint32_t)w;
        sc->extent.height = (uint32_t)h;
        if (sc->extent.width  < caps.minImageExtent.width)  sc->extent.width  = caps.minImageExtent.width;
        if (sc->extent.height < caps.minImageExtent.height) sc->extent.height = caps.minImageExtent.height;
        if (sc->extent.width  > caps.maxImageExtent.width)  sc->extent.width  = caps.maxImageExtent.width;
        if (sc->extent.height > caps.maxImageExtent.height) sc->extent.height = caps.maxImageExtent.height;
    }

    // FIFO is always supported (vsync). Non-vsync would pick MAILBOX/IMMEDIATE.
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // needed for vkCmdClearColorImage

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = sc->surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = sc->extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = usage;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (api->createSwapchain(s->device, &ci, nullptr, &sc->swapchain) != VK_SUCCESS) {
        api->destroySurface(s->instance, sc->surface, nullptr);
        delete sc; return nullptr;
    }

    uint32_t imgCount = 0;
    api->getSwapchainImages(s->device, sc->swapchain, &imgCount, nullptr);
    sc->images.resize(imgCount);
    api->getSwapchainImages(s->device, sc->swapchain, &imgCount, sc->images.data());

    s->active = sc;
    return reinterpret_cast<RhiSwapchain*>(sc);
}

void vulkan_swapchainDestroy(RhiInstance* r, RhiSwapchain* h) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    VkSwap* sc = reinterpret_cast<VkSwap*>(h);
    if (!sc) return;
    api->deviceWaitIdle(s->device);
    if (sc->swapchain) api->destroySwapchain(s->device, sc->swapchain, nullptr);
    if (sc->surface)   api->destroySurface(s->instance, sc->surface, nullptr);
    if (s->active == sc) s->active = nullptr;
    delete sc;
}

void vulkan_swapchainResize(RhiInstance* r, RhiSwapchain* h, int w, int t) {
    VkInst* s = self(r);
    VkSwap* sc = reinterpret_cast<VkSwap*>(h);
    if (!sc) return;
    void* hwnd = nullptr; (void)hwnd;
    // Recreate: capture window from the existing surface is not exposed, so the
    // caller (VI) re-creates the swapchain on resize. Kept minimal for bring-up.
    (void)w; (void)t;
}

void vulkan_beginFrame(RhiInstance* r) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    if (!s->active) return;
    uint32_t fr = s->frame;

    api->waitForFences(s->device, 1, &s->inFlight[fr], VK_TRUE, UINT64_MAX);
    api->resetFences(s->device, 1, &s->inFlight[fr]);

    api->acquireNextImage(s->device, s->active->swapchain, UINT64_MAX,
                          s->acquireSem[fr], VK_NULL_HANDLE, &s->imageIndex);

    api->resetCommandBuffer(s->cmd[fr], 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    api->beginCommandBuffer(s->cmd[fr], &bi);

    imageBarrier(api, s->cmd[fr], s->active->images[s->imageIndex],
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void vulkan_clear(RhiInstance* r, float cr, float cg, float cb, float ca) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    if (!s->active) return;
    VkClearColorValue color = {};
    color.float32[0] = cr; color.float32[1] = cg; color.float32[2] = cb; color.float32[3] = ca;
    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    api->cmdClearColorImage(s->cmd[s->frame], s->active->images[s->imageIndex],
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
}

void vulkan_endFrame(RhiInstance* r) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    if (!s->active) return;
    uint32_t fr = s->frame;

    imageBarrier(api, s->cmd[fr], s->active->images[s->imageIndex],
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    api->endCommandBuffer(s->cmd[fr]);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &s->acquireSem[fr];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s->cmd[fr];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &s->renderSem[fr];
    api->queueSubmit(s->queue, 1, &si, s->inFlight[fr]);
}

bool vulkan_present(RhiInstance* r, RhiSwapchain* h) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    VkSwap* sc = reinterpret_cast<VkSwap*>(h);
    if (!sc) return false;
    uint32_t fr = s->frame;

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &s->renderSem[fr];
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc->swapchain;
    pi.pImageIndices = &s->imageIndex;
    VkResult res = api->queuePresent(s->queue, &pi);

    s->frame = (s->frame + 1) % kFramesInFlight;
    return res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR;
}

void vulkan_destroy(RhiInstance* r) {
    VkInst* s = self(r);
    if (!s) return;
    VkApi* api = &s->api;
    if (s->device) api->deviceWaitIdle(s->device);

    if (s->active) {
        if (s->active->swapchain) api->destroySwapchain(s->device, s->active->swapchain, nullptr);
        if (s->active->surface)   api->destroySurface(s->instance, s->active->surface, nullptr);
        delete s->active;
    }
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (s->acquireSem[i]) api->destroySemaphore(s->device, s->acquireSem[i], nullptr);
        if (s->renderSem[i])  api->destroySemaphore(s->device, s->renderSem[i], nullptr);
        if (s->inFlight[i])   api->destroyFence(s->device, s->inFlight[i], nullptr);
    }
    if (s->cmdPool) api->destroyCommandPool(s->device, s->cmdPool, nullptr);
    if (s->device)  api->destroyDevice(s->device, nullptr);
    if (s->instance) api->destroyInstance(s->instance, nullptr);
    if (api->dll)   FreeLibrary(api->dll);
    delete s;
}

const RhiOps kOps = {
    vulkan_destroy,
    vulkan_swapchainCreate,
    vulkan_swapchainDestroy,
    vulkan_swapchainResize,
    vulkan_present,
    vulkan_beginFrame,
    vulkan_endFrame,
    vulkan_clear,
};

bool loadLoader(VkApi* api) {
    api->dll = LoadLibraryW(L"vulkan-1.dll");
    if (!api->dll) return false;
    api->getInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)GetProcAddress(api->dll, "vkGetInstanceProcAddr");
    if (!api->getInstanceProcAddr) return false;
    api->createInstance =
        (PFN_vkCreateInstance)api->getInstanceProcAddr(nullptr, "vkCreateInstance");
    return api->createInstance != nullptr;
}

} // namespace

extern "C" bool rhi_vulkan_available(void) {
    VkApi api;
    if (!loadLoader(&api)) { if (api.dll) FreeLibrary(api.dll); return false; }
    FreeLibrary(api.dll);
    return true;
}

extern "C" RhiInstance* rhi_vulkan_create(const RhiCreateInfo* info) {
    VkInst* s = new VkInst();
    s->base.ops     = &kOps;
    s->base.backend = RHI_BACKEND_VULKAN;

    if (!loadLoader(&s->api)) { delete s; return nullptr; }
    VkApi* api = &s->api;

    const char* instExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = info->appName ? info->appName : "Stairfax Temperatures";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExts;
    if (api->createInstance(&ici, nullptr, &s->instance) != VK_SUCCESS) { delete s; return nullptr; }
    if (!loadInstanceProcs(api, s->instance)) { delete s; return nullptr; }

    uint32_t devCount = 0;
    api->enumeratePhysicalDevices(s->instance, &devCount, nullptr);
    if (!devCount) { vulkan_destroy(&s->base); return nullptr; }
    std::vector<VkPhysicalDevice> devs(devCount);
    api->enumeratePhysicalDevices(s->instance, &devCount, devs.data());
    s->phys = devs[0];
    for (auto d : devs) {
        VkPhysicalDeviceProperties p = {};
        api->getPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { s->phys = d; break; }
    }

    uint32_t qfCount = 0;
    api->getPhysicalDeviceQueueFamilyProperties(s->phys, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    api->getPhysicalDeviceQueueFamilyProperties(s->phys, &qfCount, qfs.data());
    bool found = false;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { s->queueFamily = i; found = true; break; }
    }
    if (!found) { vulkan_destroy(&s->base); return nullptr; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = s->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    if (api->createDevice(s->phys, &dci, nullptr, &s->device) != VK_SUCCESS) { vulkan_destroy(&s->base); return nullptr; }
    if (!loadDeviceProcs(api, s->device)) { vulkan_destroy(&s->base); return nullptr; }

    api->getDeviceQueue(s->device, s->queueFamily, 0, &s->queue);

    VkCommandPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = s->queueFamily;
    if (api->createCommandPool(s->device, &pci, nullptr, &s->cmdPool) != VK_SUCCESS) { vulkan_destroy(&s->base); return nullptr; }

    VkCommandBufferAllocateInfo cbi = {};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbi.commandPool = s->cmdPool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = kFramesInFlight;
    if (api->allocateCommandBuffers(s->device, &cbi, s->cmd) != VK_SUCCESS) { vulkan_destroy(&s->base); return nullptr; }

    VkSemaphoreCreateInfo semci = {}; semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci = {}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        api->createSemaphore(s->device, &semci, nullptr, &s->acquireSem[i]);
        api->createSemaphore(s->device, &semci, nullptr, &s->renderSem[i]);
        api->createFence(s->device, &fci, nullptr, &s->inFlight[i]);
    }

    (void)info;
    return &s->base;
}

#endif // STAIRFAX_HAVE_VULKAN
