// rhi_vulkan.cpp - Vulkan backend (primary). Clear + colored-triangle draw path.
//
// C ABI over C++. VK_NO_PROTOTYPES + dynamically-loaded vulkan-1.dll (reports
// unavailable rather than failing process load if no loader/ICD). The frame uses a
// render pass (loadOp CLEAR) rather than vkCmdClearColorImage, so the same pass both
// clears and draws: beginFrame acquires; clear() begins the pass with the clear
// color; drawColored binds the built-in pos+color pipeline (embedded SPIR-V) and
// draws; endFrame ends the pass and submits. GX matrices / TEV / textures come next.
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

#include "rhi_vk_shaders.h"

#include <vector>
#include <cstdio>
#include <cstring>

namespace {

const uint32_t kFramesInFlight = 2;

struct VkApi {
    HMODULE dll = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    // instance
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
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
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkDestroyImageView destroyImageView = nullptr;
    PFN_vkCreateRenderPass createRenderPass = nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
    PFN_vkCreateShaderModule createShaderModule = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass cmdEndRenderPass = nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
    PFN_vkCmdBindVertexBuffers cmdBindVertexBuffers = nullptr;
    PFN_vkCmdSetViewport cmdSetViewport = nullptr;
    PFN_vkCmdSetScissor cmdSetScissor = nullptr;
    PFN_vkCmdDraw cmdDraw = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
};

struct VkSwap {
    VkSurfaceKHR   surface   = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat       format    = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent    = {0, 0};
    std::vector<VkImage>       images;
    std::vector<VkImageView>   views;
    std::vector<VkFramebuffer> framebuffers;
    bool vsync = true;
};

struct VkInst {
    RhiInstance      base;
    VkApi            api;
    VkInstance       instance   = VK_NULL_HANDLE;
    VkPhysicalDevice phys       = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps = {};
    uint32_t         queueFamily = 0;
    VkDevice         device     = VK_NULL_HANDLE;
    VkQueue          queue      = VK_NULL_HANDLE;
    VkCommandPool    cmdPool    = VK_NULL_HANDLE;

    VkCommandBuffer  cmd[kFramesInFlight]        = {};
    VkSemaphore      acquireSem[kFramesInFlight] = {};
    VkSemaphore      renderSem[kFramesInFlight]  = {};
    VkFence          inFlight[kFramesInFlight]    = {};

    VkRenderPass     renderPass     = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;

    // per-frame dynamic vertex buffers
    VkBuffer         vbo[kFramesInFlight]       = {};
    VkDeviceMemory   vboMem[kFramesInFlight]    = {};
    void*            vboMapped[kFramesInFlight] = {};
    uint32_t         vboCap[kFramesInFlight]    = {};

    VkSwap*   active     = nullptr;
    uint32_t  frame      = 0;
    uint32_t  imageIndex = 0;
    bool      rpActive   = false;
    float     clearColor[4] = {0, 0, 0, 1};
};

VkInst* self(RhiInstance* r) { return reinterpret_cast<VkInst*>(r); }

bool loadInstanceProcs(VkApi* api, VkInstance inst) {
#define GIPA(fld, fn) api->fld = (PFN_##fn)api->getInstanceProcAddr(inst, #fn); if (!api->fld) return false
    GIPA(destroyInstance, vkDestroyInstance);
    GIPA(enumeratePhysicalDevices, vkEnumeratePhysicalDevices);
    GIPA(getPhysicalDeviceProperties, vkGetPhysicalDeviceProperties);
    GIPA(getPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceMemoryProperties);
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
    GDPA(createSemaphore, vkCreateSemaphore);
    GDPA(destroySemaphore, vkDestroySemaphore);
    GDPA(createFence, vkCreateFence);
    GDPA(destroyFence, vkDestroyFence);
    GDPA(waitForFences, vkWaitForFences);
    GDPA(resetFences, vkResetFences);
    GDPA(queueSubmit, vkQueueSubmit);
    GDPA(createImageView, vkCreateImageView);
    GDPA(destroyImageView, vkDestroyImageView);
    GDPA(createRenderPass, vkCreateRenderPass);
    GDPA(destroyRenderPass, vkDestroyRenderPass);
    GDPA(createFramebuffer, vkCreateFramebuffer);
    GDPA(destroyFramebuffer, vkDestroyFramebuffer);
    GDPA(createShaderModule, vkCreateShaderModule);
    GDPA(destroyShaderModule, vkDestroyShaderModule);
    GDPA(createPipelineLayout, vkCreatePipelineLayout);
    GDPA(destroyPipelineLayout, vkDestroyPipelineLayout);
    GDPA(createGraphicsPipelines, vkCreateGraphicsPipelines);
    GDPA(destroyPipeline, vkDestroyPipeline);
    GDPA(cmdBeginRenderPass, vkCmdBeginRenderPass);
    GDPA(cmdEndRenderPass, vkCmdEndRenderPass);
    GDPA(cmdBindPipeline, vkCmdBindPipeline);
    GDPA(cmdBindVertexBuffers, vkCmdBindVertexBuffers);
    GDPA(cmdSetViewport, vkCmdSetViewport);
    GDPA(cmdSetScissor, vkCmdSetScissor);
    GDPA(cmdDraw, vkCmdDraw);
    GDPA(createBuffer, vkCreateBuffer);
    GDPA(destroyBuffer, vkDestroyBuffer);
    GDPA(getBufferMemoryRequirements, vkGetBufferMemoryRequirements);
    GDPA(allocateMemory, vkAllocateMemory);
    GDPA(freeMemory, vkFreeMemory);
    GDPA(bindBufferMemory, vkBindBufferMemory);
    GDPA(mapMemory, vkMapMemory);
    GDPA(unmapMemory, vkUnmapMemory);
#undef GDPA
    return true;
}

uint32_t findMemoryType(VkInst* s, uint32_t typeBits, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < s->memProps.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (s->memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0;
}

VkShaderModule makeModule(VkInst* s, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    s->api.createShaderModule(s->device, &ci, nullptr, &m);
    return m;
}

bool buildRenderPass(VkInst* s, VkFormat fmt) {
    VkAttachmentDescription color = {};
    color.format = fmt;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref = {};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &color;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    return s->api.createRenderPass(s->device, &ci, nullptr, &s->renderPass) == VK_SUCCESS;
}

bool buildPipeline(VkInst* s) {
    VkApi* api = &s->api;

    VkPipelineLayoutCreateInfo lci = {};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (api->createPipelineLayout(s->device, &lci, nullptr, &s->pipelineLayout) != VK_SUCCESS) return false;

    VkShaderModule vs = makeModule(s, kTriVertSpv, sizeof(kTriVertSpv));
    VkShaderModule fs = makeModule(s, kTriFragSpv, sizeof(kTriFragSpv));
    if (!vs || !fs) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    VkVertexInputBindingDescription bind = {};
    bind.binding = 0; bind.stride = sizeof(RhiColorVertex); bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;  attrs[1].offset = 12;

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &ds;
    pci.layout = s->pipelineLayout;
    pci.renderPass = s->renderPass;
    pci.subpass = 0;

    VkResult res = api->createGraphicsPipelines(s->device, VK_NULL_HANDLE, 1, &pci, nullptr, &s->pipeline);
    api->destroyShaderModule(s->device, vs, nullptr);
    api->destroyShaderModule(s->device, fs, nullptr);
    return res == VK_SUCCESS;
}

void ensureVbo(VkInst* s, uint32_t frame, uint32_t needed) {
    if (needed <= s->vboCap[frame]) return;
    VkApi* api = &s->api;
    if (s->vbo[frame]) {
        api->unmapMemory(s->device, s->vboMem[frame]);
        api->destroyBuffer(s->device, s->vbo[frame], nullptr);
        api->freeMemory(s->device, s->vboMem[frame], nullptr);
        s->vbo[frame] = VK_NULL_HANDLE; s->vboMem[frame] = VK_NULL_HANDLE; s->vboMapped[frame] = nullptr;
    }
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = needed;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (api->createBuffer(s->device, &bci, nullptr, &s->vbo[frame]) != VK_SUCCESS) return;

    VkMemoryRequirements mr = {};
    api->getBufferMemoryRequirements(s->device, s->vbo[frame], &mr);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(s, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (api->allocateMemory(s->device, &mai, nullptr, &s->vboMem[frame]) != VK_SUCCESS) return;
    api->bindBufferMemory(s->device, s->vbo[frame], s->vboMem[frame], 0);
    api->mapMemory(s->device, s->vboMem[frame], 0, VK_WHOLE_SIZE, 0, &s->vboMapped[frame]);
    s->vboCap[frame] = needed;
}

void beginRenderPassIfNeeded(VkInst* s) {
    if (s->rpActive || !s->active) return;
    VkClearValue clear = {};
    clear.color.float32[0] = s->clearColor[0];
    clear.color.float32[1] = s->clearColor[1];
    clear.color.float32[2] = s->clearColor[2];
    clear.color.float32[3] = s->clearColor[3];

    VkRenderPassBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = s->renderPass;
    bi.framebuffer = s->active->framebuffers[s->imageIndex];
    bi.renderArea.extent = s->active->extent;
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    s->api.cmdBeginRenderPass(s->cmd[s->frame], &bi, VK_SUBPASS_CONTENTS_INLINE);

    // Negative-height viewport flips Vulkan's +Y-down NDC to match D3D/GL/GX (+Y up),
    // so geometry has the same orientation across all backends. (Core since VK 1.1.)
    VkViewport vp = {};
    vp.x = 0.0f;
    vp.y = (float)s->active->extent.height;
    vp.width = (float)s->active->extent.width;
    vp.height = -(float)s->active->extent.height;
    vp.maxDepth = 1.0f;
    VkRect2D sc = {}; sc.extent = s->active->extent;
    s->api.cmdSetViewport(s->cmd[s->frame], 0, 1, &vp);
    s->api.cmdSetScissor(s->cmd[s->frame], 0, 1, &sc);
    s->rpActive = true;
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
    if (api->createWin32Surface(s->instance, &sci, nullptr, &sc->surface) != VK_SUCCESS) { delete sc; return nullptr; }

    VkBool32 present = VK_FALSE;
    api->getSurfaceSupport(s->phys, s->queueFamily, sc->surface, &present);
    if (!present) { api->destroySurface(s->instance, sc->surface, nullptr); delete sc; return nullptr; }

    VkSurfaceCapabilitiesKHR caps = {};
    api->getSurfaceCaps(s->phys, sc->surface, &caps);

    uint32_t fmtCount = 0;
    api->getSurfaceFormats(s->phys, sc->surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    api->getSurfaceFormats(s->phys, sc->surface, &fmtCount, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    sc->format = chosen.format;

    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        sc->extent = caps.currentExtent;
    } else {
        sc->extent.width = (uint32_t)w; sc->extent.height = (uint32_t)h;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = sc->surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = sc->extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    if (api->createSwapchain(s->device, &ci, nullptr, &sc->swapchain) != VK_SUCCESS) {
        api->destroySurface(s->instance, sc->surface, nullptr); delete sc; return nullptr;
    }

    uint32_t imgCount = 0;
    api->getSwapchainImages(s->device, sc->swapchain, &imgCount, nullptr);
    sc->images.resize(imgCount);
    api->getSwapchainImages(s->device, sc->swapchain, &imgCount, sc->images.data());

    if (!s->renderPass && !buildRenderPass(s, sc->format)) { delete sc; return nullptr; }

    sc->views.resize(imgCount);
    sc->framebuffers.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = sc->images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = sc->format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        api->createImageView(s->device, &vci, nullptr, &sc->views[i]);

        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = s->renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &sc->views[i];
        fci.width = sc->extent.width;
        fci.height = sc->extent.height;
        fci.layers = 1;
        api->createFramebuffer(s->device, &fci, nullptr, &sc->framebuffers[i]);
    }

    if (!s->pipeline && !buildPipeline(s)) { delete sc; return nullptr; }

    s->active = sc;
    return reinterpret_cast<RhiSwapchain*>(sc);
}

void destroySwapObjects(VkInst* s, VkSwap* sc) {
    VkApi* api = &s->api;
    for (auto fb : sc->framebuffers) if (fb) api->destroyFramebuffer(s->device, fb, nullptr);
    for (auto v : sc->views) if (v) api->destroyImageView(s->device, v, nullptr);
    sc->framebuffers.clear(); sc->views.clear();
    if (sc->swapchain) api->destroySwapchain(s->device, sc->swapchain, nullptr);
    if (sc->surface)   api->destroySurface(s->instance, sc->surface, nullptr);
}

void vulkan_swapchainDestroy(RhiInstance* r, RhiSwapchain* h) {
    VkInst* s = self(r);
    VkSwap* sc = reinterpret_cast<VkSwap*>(h);
    if (!sc) return;
    s->api.deviceWaitIdle(s->device);
    destroySwapObjects(s, sc);
    if (s->active == sc) s->active = nullptr;
    delete sc;
}

void vulkan_swapchainResize(RhiInstance* r, RhiSwapchain* h, int w, int t) {
    (void)r; (void)h; (void)w; (void)t; // VI recreates the swapchain on resize
}

void vulkan_beginFrame(RhiInstance* r) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    if (!s->active) return;
    uint32_t fr = s->frame;
    api->waitForFences(s->device, 1, &s->inFlight[fr], VK_TRUE, UINT64_MAX);
    api->resetFences(s->device, 1, &s->inFlight[fr]);
    api->acquireNextImage(s->device, s->active->swapchain, UINT64_MAX, s->acquireSem[fr], VK_NULL_HANDLE, &s->imageIndex);
    api->resetCommandBuffer(s->cmd[fr], 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    api->beginCommandBuffer(s->cmd[fr], &bi);
    s->rpActive = false;
}

void vulkan_clear(RhiInstance* r, float cr, float cg, float cb, float ca) {
    VkInst* s = self(r);
    s->clearColor[0] = cr; s->clearColor[1] = cg; s->clearColor[2] = cb; s->clearColor[3] = ca;
    beginRenderPassIfNeeded(s); // render pass loadOp CLEAR applies the color
}

void vulkan_drawColored(RhiInstance* r, const RhiColorVertex* verts, uint32_t count) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    if (!verts || count == 0 || !s->active || !s->pipeline) return;
    uint32_t fr = s->frame;
    uint32_t needed = (uint32_t)sizeof(RhiColorVertex) * count;
    ensureVbo(s, fr, needed);
    if (!s->vboMapped[fr]) return;
    memcpy(s->vboMapped[fr], verts, needed);

    beginRenderPassIfNeeded(s);
    api->cmdBindPipeline(s->cmd[fr], VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline);
    VkDeviceSize offset = 0;
    api->cmdBindVertexBuffers(s->cmd[fr], 0, 1, &s->vbo[fr], &offset);
    api->cmdDraw(s->cmd[fr], count, 1, 0, 0);
}

void vulkan_endFrame(RhiInstance* r) {
    VkInst* s = self(r);
    VkApi* api = &s->api;
    if (!s->active) return;
    uint32_t fr = s->frame;
    if (!s->rpActive) beginRenderPassIfNeeded(s); // ensure the image is cleared/presentable
    api->cmdEndRenderPass(s->cmd[fr]);
    s->rpActive = false;
    api->endCommandBuffer(s->cmd[fr]);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (s->vbo[i])    { api->unmapMemory(s->device, s->vboMem[i]); api->destroyBuffer(s->device, s->vbo[i], nullptr); }
        if (s->vboMem[i]) api->freeMemory(s->device, s->vboMem[i], nullptr);
    }
    if (s->pipeline)       api->destroyPipeline(s->device, s->pipeline, nullptr);
    if (s->pipelineLayout) api->destroyPipelineLayout(s->device, s->pipelineLayout, nullptr);
    if (s->renderPass)     api->destroyRenderPass(s->device, s->renderPass, nullptr);
    if (s->active)         { destroySwapObjects(s, s->active); delete s->active; }
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (s->acquireSem[i]) api->destroySemaphore(s->device, s->acquireSem[i], nullptr);
        if (s->renderSem[i])  api->destroySemaphore(s->device, s->renderSem[i], nullptr);
        if (s->inFlight[i])   api->destroyFence(s->device, s->inFlight[i], nullptr);
    }
    if (s->cmdPool)  api->destroyCommandPool(s->device, s->cmdPool, nullptr);
    if (s->device)   api->destroyDevice(s->device, nullptr);
    if (s->instance) api->destroyInstance(s->instance, nullptr);
    if (api->dll)    FreeLibrary(api->dll);
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
    vulkan_drawColored,
};

bool loadLoader(VkApi* api) {
    api->dll = LoadLibraryW(L"vulkan-1.dll");
    if (!api->dll) return false;
    api->getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(api->dll, "vkGetInstanceProcAddr");
    if (!api->getInstanceProcAddr) return false;
    api->createInstance = (PFN_vkCreateInstance)api->getInstanceProcAddr(nullptr, "vkCreateInstance");
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
    s->base.ops = &kOps;
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
    api->getPhysicalDeviceMemoryProperties(s->phys, &s->memProps);

    uint32_t qfCount = 0;
    api->getPhysicalDeviceQueueFamilyProperties(s->phys, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    api->getPhysicalDeviceQueueFamilyProperties(s->phys, &qfCount, qfs.data());
    bool found = false;
    for (uint32_t i = 0; i < qfCount; ++i)
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { s->queueFamily = i; found = true; break; }
    if (!found) { vulkan_destroy(&s->base); return nullptr; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = s->queueFamily; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExts;
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
    cbi.commandPool = s->cmdPool; cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbi.commandBufferCount = kFramesInFlight;
    if (api->allocateCommandBuffers(s->device, &cbi, s->cmd) != VK_SUCCESS) { vulkan_destroy(&s->base); return nullptr; }

    VkSemaphoreCreateInfo semci = {}; semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci = {}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        api->createSemaphore(s->device, &semci, nullptr, &s->acquireSem[i]);
        api->createSemaphore(s->device, &semci, nullptr, &s->renderSem[i]);
        api->createFence(s->device, &fci, nullptr, &s->inFlight[i]);
    }

    (void)info;
    return &s->base;
}

#endif // STAIRFAX_HAVE_VULKAN
