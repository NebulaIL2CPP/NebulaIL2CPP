#include "Nebula/UI/VulkanOverlay.h"

#include <vulkan/vulkan.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "Nebula/Core/Log.h"
#include "Nebula/Hook/Hook.h"
#include "Nebula/UI/Overlay.h"

namespace {
using PFN_CreateInstance = VkResult (*)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
using PFN_CreateDevice = VkResult (*)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
using PFN_GetDeviceQueue = void (*)(VkDevice, uint32_t, uint32_t, VkQueue*);
using PFN_CreateSwapchain = VkResult (*)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
using PFN_GetSwapchainImages = VkResult (*)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
using PFN_QueuePresent = VkResult (*)(VkQueue, const VkPresentInfoKHR*);

PFN_CreateInstance g_createInstance = nullptr;
PFN_CreateDevice g_createDevice = nullptr;
PFN_GetDeviceQueue g_getDeviceQueue = nullptr;
PFN_CreateSwapchain g_createSwapchain = nullptr;
PFN_GetSwapchainImages g_getSwapchainImages = nullptr;
PFN_QueuePresent g_queuePresent = nullptr;

VkInstance g_instance = VK_NULL_HANDLE;
VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
VkQueue g_queue = VK_NULL_HANDLE;
uint32_t g_queueFamily = 0;
VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
VkFormat g_format = VK_FORMAT_UNDEFINED;
VkExtent2D g_extent{};
uint32_t g_minImageCount = 2;
std::vector<VkImage> g_images;
VkRenderPass g_renderPass = VK_NULL_HANDLE;
std::vector<VkImageView> g_views;
std::vector<VkFramebuffer> g_framebuffers;
VkCommandPool g_commandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> g_commands;
std::vector<VkFence> g_fences;
std::once_flag g_imguiOnce;
std::atomic<bool> g_inPresent{false};
std::atomic<bool> g_firstPresent{false};
std::mutex g_stateMutex;

template <typename T> T Find(const char* name) {
    return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
}

bool CreateRenderResources() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_device == VK_NULL_HANDLE || g_queue == VK_NULL_HANDLE ||
        g_swapchain == VK_NULL_HANDLE || g_images.empty() ||
        g_renderPass != VK_NULL_HANDLE) return g_renderPass != VK_NULL_HANDLE;

    VkAttachmentDescription attachment{};
    attachment.format = g_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1; rp.pAttachments = &attachment;
    rp.subpassCount = 1; rp.pSubpasses = &subpass; rp.dependencyCount = 2; rp.pDependencies = deps;
    if (vkCreateRenderPass(g_device, &rp, nullptr, &g_renderPass) != VK_SUCCESS) return false;

    g_views.resize(g_images.size()); g_framebuffers.resize(g_images.size());
    for (size_t i = 0; i < g_images.size(); ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = g_images[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = g_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(g_device, &vi, nullptr, &g_views[i]) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = g_renderPass; fb.attachmentCount = 1; fb.pAttachments = &g_views[i];
        fb.width = g_extent.width; fb.height = g_extent.height; fb.layers = 1;
        if (vkCreateFramebuffer(g_device, &fb, nullptr, &g_framebuffers[i]) != VK_SUCCESS) return false;
    }
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex = g_queueFamily;
    if (vkCreateCommandPool(g_device, &cp, nullptr, &g_commandPool) != VK_SUCCESS) return false;
    g_commands.resize(g_images.size()); g_fences.resize(g_images.size());
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = g_commandPool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = static_cast<uint32_t>(g_commands.size());
    if (vkAllocateCommandBuffers(g_device, &ca, g_commands.data()) != VK_SUCCESS) return false;
    for (auto& fence : g_fences) {
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(g_device, &fi, nullptr, &fence) != VK_SUCCESS) return false;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0; info.Instance = g_instance;
    info.PhysicalDevice = g_physicalDevice; info.Device = g_device;
    info.QueueFamily = g_queueFamily; info.Queue = g_queue;
    info.DescriptorPoolSize = 1000; info.MinImageCount = std::max(2u, g_minImageCount);
    info.ImageCount = static_cast<uint32_t>(g_images.size());
    info.PipelineInfoMain.RenderPass = g_renderPass;
    info.PipelineInfoMain.Subpass = 0; info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&info)) return false;
    NEBULA_LOGI("Vulkan ImGui initialized (%ux%u, images=%u)", g_extent.width, g_extent.height,
                static_cast<unsigned>(g_images.size()));
    return true;
}

void Render(uint32_t index) {
    if (index >= g_commands.size() || !CreateRenderResources()) return;
    std::lock_guard<std::mutex> lock(g_stateMutex);
    VkCommandBuffer cmd = g_commands[index];
    vkWaitForFences(g_device, 1, &g_fences[index], VK_TRUE, UINT64_MAX);
    vkResetFences(g_device, 1, &g_fences[index]);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.image = g_images[index]; b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = g_renderPass; rbi.framebuffer = g_framebuffers[index]; rbi.renderArea.extent = g_extent;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_NewFrame();
    Nebula::Overlay::PrepareVulkanFrame(g_extent.width, g_extent.height);
    Nebula::Overlay::DrawContents(); ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    std::swap(b.oldLayout, b.newLayout); b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) return;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(g_queue, 1, &si, g_fences[index]);
    if (!g_firstPresent.exchange(true)) NEBULA_LOGI("First Vulkan ImGui frame rendered");
}

VkResult CreateInstanceHook(const VkInstanceCreateInfo* i, const VkAllocationCallbacks* a, VkInstance* out) {
    VkResult r = g_createInstance(i, a, out); if (r == VK_SUCCESS) g_instance = *out; return r;
}
VkResult CreateDeviceHook(VkPhysicalDevice p, const VkDeviceCreateInfo* i, const VkAllocationCallbacks* a, VkDevice* out) {
    VkResult r = g_createDevice(p, i, a, out); if (r == VK_SUCCESS) {
        g_physicalDevice = p; g_device = *out;
        uint32_t n = 0; vkGetPhysicalDeviceQueueFamilyProperties(p, &n, nullptr);
        std::vector<VkQueueFamilyProperties> q(n); vkGetPhysicalDeviceQueueFamilyProperties(p, &n, q.data());
        for (uint32_t x = 0; x < n; ++x) if (q[x].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_queueFamily = x; break; }
    } return r;
}
void GetDeviceQueueHook(VkDevice d, uint32_t f, uint32_t x, VkQueue* q) {
    g_getDeviceQueue(d, f, x, q); if (d == g_device && f == g_queueFamily && *q) g_queue = *q;
}
VkResult CreateSwapchainHook(VkDevice d, const VkSwapchainCreateInfoKHR* i, const VkAllocationCallbacks* a, VkSwapchainKHR* s) {
    VkResult r = g_createSwapchain(d, i, a, s); if (r == VK_SUCCESS) {
        g_swapchain = *s; g_format = i->imageFormat; g_extent = i->imageExtent; g_minImageCount = i->minImageCount;
    } return r;
}
VkResult GetSwapchainImagesHook(VkDevice d, VkSwapchainKHR s, uint32_t* n, VkImage* images) {
    VkResult r = g_getSwapchainImages(d, s, n, images);
    if (r == VK_SUCCESS && s == g_swapchain && images != nullptr) g_images.assign(images, images + *n);
    return r;
}
VkResult QueuePresentHook(VkQueue q, const VkPresentInfoKHR* p) {
    if (Nebula::Overlay::IsExternalSurfaceActive()) {
        return g_queuePresent(q, p);
    }
    if (!g_inPresent.exchange(true) && p && p->swapchainCount && p->pSwapchains[0] == g_swapchain)
        Render(p->pImageIndices ? p->pImageIndices[0] : 0);
    g_inPresent.store(false);
    return g_queuePresent(q, p);
}
} // namespace

namespace Nebula {
VulkanOverlay& VulkanOverlay::Get() { static VulkanOverlay v; return v; }
bool VulkanOverlay::Install() {
    static std::mutex mutex; static bool installed = false;
    std::lock_guard<std::mutex> lock(mutex); if (installed) return true;
    bool ok = true;
    auto hook = [&](const char* n, void* rep, void** orig) {
        void* p = Find<void*>(n); if (!p) { ok = false; return; }
        if (!Hook::HookFunction(reinterpret_cast<uintptr_t>(p), rep, orig)) ok = false;
    };
    hook("vkCreateInstance", reinterpret_cast<void*>(&CreateInstanceHook), reinterpret_cast<void**>(&g_createInstance));
    hook("vkCreateDevice", reinterpret_cast<void*>(&CreateDeviceHook), reinterpret_cast<void**>(&g_createDevice));
    hook("vkGetDeviceQueue", reinterpret_cast<void*>(&GetDeviceQueueHook), reinterpret_cast<void**>(&g_getDeviceQueue));
    hook("vkCreateSwapchainKHR", reinterpret_cast<void*>(&CreateSwapchainHook), reinterpret_cast<void**>(&g_createSwapchain));
    hook("vkGetSwapchainImagesKHR", reinterpret_cast<void*>(&GetSwapchainImagesHook), reinterpret_cast<void**>(&g_getSwapchainImages));
    hook("vkQueuePresentKHR", reinterpret_cast<void*>(&QueuePresentHook), reinterpret_cast<void**>(&g_queuePresent));
    if (ok) { installed = true; NEBULA_LOGI("Vulkan overlay hooks installed"); }
    return installed;
}
} // namespace Nebula
