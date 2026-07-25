/* mupen64plus-VR — Tier-1 OpenXR compositor, VULKAN path (packet S12).
 */

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>   /* usleep — frame-probe wait-for-READY pacing */

#include "vr_video_vk.h"
#include "vr_pose.h"      /* vr_pose_publish — head pose feeds the Tier-3 paths */
#include "vr_profile.h"   /* vr_settings_get — resolved screen geometry drives the quad */

#define VR_LOG(...) do { std::fprintf(stderr, "(VRvk) " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define VR_ERR(...) do { std::fprintf(stderr, "(VRvk) ERROR: " __VA_ARGS__); std::fputc('\n', stderr); } while (0)

namespace {

XrInstance      s_instance      = XR_NULL_HANDLE;
XrSystemId      s_system_id     = XR_NULL_SYSTEM_ID;
XrSession       s_session       = XR_NULL_HANDLE;
XrSpace         s_space         = XR_NULL_HANDLE;   /* LOCAL (seated) reference space */
XrSpace         s_view_space    = XR_NULL_HANDLE;   /* VIEW space — head pose = VIEW in LOCAL */
XrSessionState  s_session_state = XR_SESSION_STATE_UNKNOWN;
std::atomic_bool s_session_running{false};
bool            s_lost = false;

VkInstance       s_vk_instance = VK_NULL_HANDLE;
VkPhysicalDevice s_vk_phys     = VK_NULL_HANDLE;
VkDevice         s_vk_device   = VK_NULL_HANDLE;
VkQueue          s_vk_queue    = VK_NULL_HANDLE;
uint32_t         s_gfx_family  = UINT32_MAX;

PFN_xrGetVulkanGraphicsRequirements2KHR s_pfnGetVkReqs      = nullptr;
PFN_xrCreateVulkanInstanceKHR           s_pfnCreateVkInst   = nullptr;
PFN_xrGetVulkanGraphicsDevice2KHR       s_pfnGetVkDevice    = nullptr;
PFN_xrCreateVulkanDeviceKHR             s_pfnCreateVkDevice = nullptr;

/* Persistent compositor resources (the init/submit/shutdown API the core drives).
XrSwapchain           g_sc[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
std::vector<VkImage>  g_sc_images[2];
bool                  g_have[2] = { false, false };
VkCommandPool         g_pool = VK_NULL_HANDLE;
VkCommandBuffer       g_cb = VK_NULL_HANDLE;
VkFence               g_fence = VK_NULL_HANDLE;
VkBuffer              g_staging = VK_NULL_HANDLE;
VkDeviceMemory        g_staging_mem = VK_NULL_HANDLE;
void*                 g_staging_map = nullptr;
uint32_t              g_w = 0, g_h = 0;

bool CheckXR(XrResult res, const char* what)
{
    if (XR_SUCCEEDED(res)) return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (s_instance != XR_NULL_HANDLE) xrResultToString(s_instance, res, buf);
    VR_ERR("%s failed: %s (%d)", what, buf, static_cast<int>(res));
    return false;
}

bool CheckVk(VkResult res, const char* what)
{
    if (res == VK_SUCCESS) return true;
    VR_ERR("%s failed: VkResult %d", what, static_cast<int>(res));
    return false;
}

const char* SessionStateName(XrSessionState s)
{
    switch (s) {
        case XR_SESSION_STATE_IDLE:         return "IDLE";
        case XR_SESSION_STATE_READY:        return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE:      return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED:      return "FOCUSED";
        case XR_SESSION_STATE_STOPPING:     return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING:      return "EXITING";
        default:                            return "UNKNOWN";
    }
}

void HandleSessionStateChange(XrSessionState new_state)
{
    VR_LOG("Session state: %s -> %s", SessionStateName(s_session_state), SessionStateName(new_state));
    s_session_state = new_state;
    switch (new_state) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
            bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (CheckXR(xrBeginSession(s_session, &bi), "xrBeginSession")) {
                VR_LOG("Session began.");
                s_session_running.store(true, std::memory_order_release);
            } else s_lost = true;
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            s_session_running.store(false, std::memory_order_release);
            vr_pose_invalidate();   /* pose must not outlive the session — vr_pose.h */
            CheckXR(xrEndSession(s_session), "xrEndSession");
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
        case XR_SESSION_STATE_EXITING:
            s_session_running.store(false, std::memory_order_release);
            vr_pose_invalidate();   /* pose must not outlive the session — vr_pose.h */
            s_lost = true;
            break;
        default: break;
    }
}

bool CreateInstanceAndSystem(const char* app_name)
{
    if (s_instance != XR_NULL_HANDLE) return true;

    const char* exts[1] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(ici.applicationInfo.applicationName, app_name ? app_name : "mupen64plus-VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(ici.applicationInfo.engineName, "mupen64plus-VR", XR_MAX_ENGINE_NAME_SIZE - 1);
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);   /* rig loader is 1.0.20 */
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = exts;

    XrResult res = xrCreateInstance(&ici, &s_instance);
    if (XR_FAILED(res)) {
        VR_LOG("xrCreateInstance failed (%d) — OpenXR runtime active? Running flat.", static_cast<int>(res));
        s_instance = XR_NULL_HANDLE;
        return false;
    }

    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    if (CheckXR(xrGetInstanceProperties(s_instance, &ip), "xrGetInstanceProperties"))
        VR_LOG("OpenXR runtime: %s %u.%u.%u", ip.runtimeName,
               XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    res = xrGetSystem(s_instance, &sgi, &s_system_id);
    if (XR_FAILED(res)) {
        VR_LOG("No HMD system (%d) — headset connected/awake? Running flat.", static_cast<int>(res));
        xrDestroyInstance(s_instance); s_instance = XR_NULL_HANDLE; s_system_id = XR_NULL_SYSTEM_ID;
        return false;
    }
    XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
    if (CheckXR(xrGetSystemProperties(s_instance, s_system_id, &sp), "xrGetSystemProperties"))
        VR_LOG("HMD: %s", sp.systemName);

#define LOAD_XR_FN(pfn, name) \
    if (XR_FAILED(xrGetInstanceProcAddr(s_instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&pfn))) || pfn == nullptr) { \
        VR_ERR("could not load %s — running flat.", name); \
        xrDestroyInstance(s_instance); s_instance = XR_NULL_HANDLE; s_system_id = XR_NULL_SYSTEM_ID; return false; }
    LOAD_XR_FN(s_pfnGetVkReqs,      "xrGetVulkanGraphicsRequirements2KHR");
    LOAD_XR_FN(s_pfnCreateVkInst,   "xrCreateVulkanInstanceKHR");
    LOAD_XR_FN(s_pfnGetVkDevice,    "xrGetVulkanGraphicsDevice2KHR");
    LOAD_XR_FN(s_pfnCreateVkDevice, "xrCreateVulkanDeviceKHR");
#undef LOAD_XR_FN

    s_lost = false;
    return true;
}

bool CreateVulkanDevice()
{
    XrGraphicsRequirementsVulkan2KHR reqs = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!CheckXR(s_pfnGetVkReqs(s_instance, s_system_id, &reqs), "xrGetVulkanGraphicsRequirements2KHR")) return false;
    VR_LOG("Runtime supports Vulkan %u.%u - %u.%u",
           XR_VERSION_MAJOR(reqs.minApiVersionSupported), XR_VERSION_MINOR(reqs.minApiVersionSupported),
           XR_VERSION_MAJOR(reqs.maxApiVersionSupported), XR_VERSION_MINOR(reqs.maxApiVersionSupported));

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "mupen64plus-VR"; app.applicationVersion = 1;
    app.pEngineName = "mupen64plus-VR";      app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_1;     /* every runtime in the matrix accepts 1.1+ */
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xici = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xici.systemId = s_system_id;
    xici.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xici.vulkanCreateInfo = &ici;
    VkResult vkres = VK_SUCCESS;
    if (!CheckXR(s_pfnCreateVkInst(s_instance, &xici, &s_vk_instance, &vkres), "xrCreateVulkanInstanceKHR")) return false;
    if (!CheckVk(vkres, "vkCreateInstance(through XR)")) return false;

    XrVulkanGraphicsDeviceGetInfoKHR gdi = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gdi.systemId = s_system_id;
    gdi.vulkanInstance = s_vk_instance;
    if (!CheckXR(s_pfnGetVkDevice(s_instance, &gdi, &s_vk_phys), "xrGetVulkanGraphicsDevice2KHR")) return false;

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(s_vk_phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(s_vk_phys, &qn, qf.data());
    s_gfx_family = UINT32_MAX;
    for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { s_gfx_family = i; break; }
    if (s_gfx_family == UINT32_MAX) { VR_ERR("no graphics queue family."); return false; }

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = s_gfx_family; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    XrVulkanDeviceCreateInfoKHR xdci = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xdci.systemId = s_system_id;
    xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xdci.vulkanPhysicalDevice = s_vk_phys;
    xdci.vulkanCreateInfo = &dci;
    vkres = VK_SUCCESS;
    if (!CheckXR(s_pfnCreateVkDevice(s_instance, &xdci, &s_vk_device, &vkres), "xrCreateVulkanDeviceKHR")) return false;
    if (!CheckVk(vkres, "vkCreateDevice(through XR)")) return false;
    vkGetDeviceQueue(s_vk_device, s_gfx_family, 0, &s_vk_queue);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(s_vk_phys, &props);
    VR_LOG("Vulkan device: %s (queue family %u)", props.deviceName, s_gfx_family);
    return true;
}

bool CreateSessionVk()
{
    XrGraphicsBindingVulkan2KHR bind = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    bind.instance = s_vk_instance;
    bind.physicalDevice = s_vk_phys;
    bind.device = s_vk_device;
    bind.queueFamilyIndex = s_gfx_family;
    bind.queueIndex = 0;
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &bind;
    sci.systemId = s_system_id;
    if (!CheckXR(xrCreateSession(s_instance, &sci, &s_session), "xrCreateSession")) { s_session = XR_NULL_HANDLE; return false; }

    XrReferenceSpaceCreateInfo rsci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (!CheckXR(xrCreateReferenceSpace(s_session, &rsci, &s_space), "xrCreateReferenceSpace")) return false;

    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;   /* for the head pose */
    if (!CheckXR(xrCreateReferenceSpace(s_session, &rsci, &s_view_space), "xrCreateReferenceSpace(VIEW)"))
        s_view_space = XR_NULL_HANDLE;   /* non-fatal: no pose publish, everything else works */

    s_session_state = XR_SESSION_STATE_UNKNOWN;
    VR_LOG("XR session created (Vulkan).");
    return true;
}

void PumpEvents()
{
    if (s_instance == XR_NULL_HANDLE) return;
    for (;;) {
        XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
        const XrResult res = xrPollEvent(s_instance, &ev);
        if (res == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(res)) { CheckXR(res, "xrPollEvent"); s_lost = true; break; }
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& e = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
            if (e.session == s_session) HandleSessionStateChange(e.state);
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            s_session_running.store(false, std::memory_order_release);
            vr_pose_invalidate();   /* pose must not outlive the session — vr_pose.h */
            s_lost = true;
        }
    }
}

void DestroyAll()
{
    if (s_session != XR_NULL_HANDLE) {
        if (s_session_running.load(std::memory_order_acquire)) {
            /* xrEndSession is only legal from STOPPING — calling it on a running
            if (XR_SUCCEEDED(xrRequestExitSession(s_session))) {
                for (int i = 0; i < 100 && s_session_running.load(std::memory_order_acquire) && !s_lost; ++i) {
                    PumpEvents();
                    if (!s_session_running.load(std::memory_order_acquire)) break;
                    usleep(10000);
                }
            }
            if (s_session_running.load(std::memory_order_acquire)) {   /* no STOPPING delivered */
                s_session_running.store(false, std::memory_order_release);
                xrEndSession(s_session);   /* best effort on a wedged runtime */
            }
        }
        vr_pose_invalidate();   /* disarm every pose consumer the moment the session goes */
        if (s_view_space != XR_NULL_HANDLE) { xrDestroySpace(s_view_space); s_view_space = XR_NULL_HANDLE; }
        if (s_space != XR_NULL_HANDLE) { xrDestroySpace(s_space); s_space = XR_NULL_HANDLE; }
        xrDestroySession(s_session); s_session = XR_NULL_HANDLE;
    }
    if (s_instance != XR_NULL_HANDLE) { xrDestroyInstance(s_instance); s_instance = XR_NULL_HANDLE; }
    if (s_vk_device != VK_NULL_HANDLE)   { vkDestroyDevice(s_vk_device, nullptr);     s_vk_device = VK_NULL_HANDLE; }
    if (s_vk_instance != VK_NULL_HANDLE) { vkDestroyInstance(s_vk_instance, nullptr); s_vk_instance = VK_NULL_HANDLE; }
    s_system_id = XR_NULL_SYSTEM_ID; s_vk_phys = VK_NULL_HANDLE; s_vk_queue = VK_NULL_HANDLE; s_gfx_family = UINT32_MAX;
    s_pfnGetVkReqs = nullptr; s_pfnCreateVkInst = nullptr; s_pfnGetVkDevice = nullptr; s_pfnCreateVkDevice = nullptr;
    s_lost = false;
}

void ImgBarrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to,
                VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS)
{
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = srcA; b.dstAccessMask = dstA;
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

/* Full Vulkan Tier-1 pipeline spike: session + swapchain + a real frame loop that clears
bool RunFrameProbeVk(int max_frames)
{
    bool ok = false;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    do {
        if (!CreateInstanceAndSystem("mupen64plus-VR-vk-frame")) break;
        if (!CreateVulkanDevice()) break;
        if (!CreateSessionVk()) break;

        uint32_t vc = 0;
        if (!CheckXR(xrEnumerateViewConfigurationViews(s_instance, s_system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vc, nullptr), "xrEnumerateViewConfigurationViews")) break;
        std::vector<XrViewConfigurationView> views(vc, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(s_instance, s_system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, vc, &vc, views.data());
        const uint32_t W = views[0].recommendedImageRectWidth, H = views[0].recommendedImageRectHeight;

        uint32_t fc = 0;
        xrEnumerateSwapchainFormats(s_session, 0, &fc, nullptr);
        std::vector<int64_t> formats(fc);
        xrEnumerateSwapchainFormats(s_session, fc, &fc, formats.data());
        int64_t chosen = formats.empty() ? VK_FORMAT_R8G8B8A8_SRGB : formats[0];
        for (int64_t f : formats) { if (f == VK_FORMAT_R8G8B8A8_SRGB) { chosen = f; break; } if (f == VK_FORMAT_B8G8R8A8_SRGB) chosen = f; }
        VR_LOG("Swapchain: %ux%u, format %lld (%u offered)", W, H, static_cast<long long>(chosen), fc);

        XrSwapchainCreateInfo sci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        sci.format = chosen; sci.sampleCount = 1; sci.width = W; sci.height = H;
        sci.faceCount = 1; sci.arraySize = 1; sci.mipCount = 1;
        if (!CheckXR(xrCreateSwapchain(s_session, &sci, &swapchain), "xrCreateSwapchain")) break;

        uint32_t ic = 0;
        xrEnumerateSwapchainImages(swapchain, 0, &ic, nullptr);
        std::vector<XrSwapchainImageVulkanKHR> images(ic, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrEnumerateSwapchainImages(swapchain, ic, &ic, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = s_gfx_family;
        if (!CheckVk(vkCreateCommandPool(s_vk_device, &pci, nullptr, &pool), "vkCreateCommandPool")) break;
        VkCommandBufferAllocateInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbi.commandPool = pool; cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbi.commandBufferCount = 1;
        if (!CheckVk(vkAllocateCommandBuffers(s_vk_device, &cbi, &cb), "vkAllocateCommandBuffers")) break;
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;   /* pre-signaled so frame 1's wait passes (no deadlock) */
        if (!CheckVk(vkCreateFence(s_vk_device, &fci, nullptr, &fence), "vkCreateFence")) break;

        VR_LOG("frame loop: %u images; rendering an animated quad for up to %d frames — LOOK IN THE HEADSET.", ic, max_frames);
        int rendered = 0, wait_ready = 0;
        while (rendered < max_frames && !s_lost) {
            PumpEvents();
            if (!s_session_running.load(std::memory_order_acquire)) {
                if (++wait_ready > 3000) { VR_ERR("frame: session never reached READY."); break; }
                usleep(2000); continue;
            }
            XrFrameWaitInfo fwi = {XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState fs = {XR_TYPE_FRAME_STATE};
            if (!CheckXR(xrWaitFrame(s_session, &fwi, &fs), "xrWaitFrame")) break;
            XrFrameBeginInfo fbi = {XR_TYPE_FRAME_BEGIN_INFO};
            xrBeginFrame(s_session, &fbi);

            XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            std::vector<XrCompositionLayerBaseHeader*> layers;
            if (fs.shouldRender) {
                uint32_t idx = 0;
                XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                xrAcquireSwapchainImage(swapchain, &ai, &idx);
                XrSwapchainImageWaitInfo swi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                swi.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(swapchain, &swi);

                vkWaitForFences(s_vk_device, 1, &fence, VK_TRUE, UINT64_MAX);
                vkResetFences(s_vk_device, 1, &fence);
                vkResetCommandBuffer(cb, 0);
                VkCommandBufferBeginInfo bbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer(cb, &bbi);
                VkImage img = images[idx].image;
                ImgBarrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                const float c = static_cast<float>(rendered % 180) / 180.0f;
                VkClearColorValue col; col.float32[0] = c; col.float32[1] = 0.25f; col.float32[2] = 1.0f - c; col.float32[3] = 1.0f;
                VkImageSubresourceRange rng = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &rng);
                ImgBarrier(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                vkEndCommandBuffer(cb);
                VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
                si.commandBufferCount = 1; si.pCommandBuffers = &cb;
                vkQueueSubmit(s_vk_queue, 1, &si, fence);
                vkWaitForFences(s_vk_device, 1, &fence, VK_TRUE, UINT64_MAX);   /* ensure done before release */

                XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(swapchain, &ri);

                quad.space = s_space;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = swapchain;
                quad.subImage.imageRect.extent = {static_cast<int32_t>(W), static_cast<int32_t>(H)};
                quad.pose.orientation.w = 1.0f;
                quad.pose.position.z = -1.5f;
                quad.size.width = 1.6f;
                quad.size.height = 1.6f * static_cast<float>(H) / static_cast<float>(W);
                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
            }
            XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO};
            fei.displayTime = fs.predictedDisplayTime;
            fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            fei.layerCount = static_cast<uint32_t>(layers.size());
            fei.layers = layers.data();
            if (!CheckXR(xrEndFrame(s_session, &fei), "xrEndFrame")) break;
            if (++rendered % 90 == 0) VR_LOG("frame loop: %d frames submitted", rendered);
        }
        VR_LOG("frame loop: done, %d frames submitted.", rendered);
        ok = (rendered > 0);
    } while (0);

    if (s_vk_device != VK_NULL_HANDLE) vkDeviceWaitIdle(s_vk_device);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(s_vk_device, fence, nullptr);
    if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(s_vk_device, pool, nullptr);
    if (swapchain != XR_NULL_HANDLE) xrDestroySwapchain(swapchain);
    DestroyAll();
    return ok;
}

uint32_t FindMemType(uint32_t bits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(s_vk_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

/* Persistent bring-up: session + a width×height swapchain + command buffer + a mapped
bool InitPersistent(uint32_t w, uint32_t h)
{
    if (!CreateInstanceAndSystem("mupen64plus-VR")) return false;
    if (!CreateVulkanDevice()) return false;
    if (!CreateSessionVk()) return false;

    /* Diagnostic: what per-eye resolution does the runtime recommend/allow? A quad
    {
        uint32_t vc = 0;
        xrEnumerateViewConfigurationViews(s_instance, s_system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vc, nullptr);
        std::vector<XrViewConfigurationView> views(vc, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        if (vc > 0 &&
            XR_SUCCEEDED(xrEnumerateViewConfigurationViews(s_instance, s_system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, vc, &vc, views.data())))
            VR_LOG("[VRres] our swapchain=%ux%u | runtime recommends %ux%u max %ux%u per eye",
                   w, h, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight,
                   views[0].maxImageRectWidth, views[0].maxImageRectHeight);
    }

    uint32_t fc = 0;
    xrEnumerateSwapchainFormats(s_session, 0, &fc, nullptr);
    std::vector<int64_t> formats(fc);
    xrEnumerateSwapchainFormats(s_session, fc, &fc, formats.data());
    int64_t chosen = formats.empty() ? VK_FORMAT_R8G8B8A8_SRGB : formats[0];
    for (int64_t f : formats) { if (f == VK_FORMAT_R8G8B8A8_SRGB) { chosen = f; break; } if (f == VK_FORMAT_B8G8R8A8_SRGB) chosen = f; }

    for (int eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo sci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        sci.format = chosen; sci.sampleCount = 1; sci.width = w; sci.height = h;
        sci.faceCount = 1; sci.arraySize = 1; sci.mipCount = 1;
        if (!CheckXR(xrCreateSwapchain(s_session, &sci, &g_sc[eye]), "xrCreateSwapchain")) return false;
        uint32_t ic = 0;
        xrEnumerateSwapchainImages(g_sc[eye], 0, &ic, nullptr);
        std::vector<XrSwapchainImageVulkanKHR> imgs(ic, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrEnumerateSwapchainImages(g_sc[eye], ic, &ic, reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
        g_sc_images[eye].clear();
        for (const auto& i : imgs) g_sc_images[eye].push_back(i.image);
    }
    g_have[0] = g_have[1] = false;

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = s_gfx_family;
    if (!CheckVk(vkCreateCommandPool(s_vk_device, &pci, nullptr, &g_pool), "vkCreateCommandPool")) return false;
    VkCommandBufferAllocateInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbi.commandPool = g_pool; cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbi.commandBufferCount = 1;
    vkAllocateCommandBuffers(s_vk_device, &cbi, &g_cb);
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(s_vk_device, &fci, nullptr, &g_fence);

    const VkDeviceSize sz = static_cast<VkDeviceSize>(w) * h * 4;
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sz; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVk(vkCreateBuffer(s_vk_device, &bci, nullptr, &g_staging), "vkCreateBuffer")) return false;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(s_vk_device, g_staging, &mr);
    const uint32_t mt = FindMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) { VR_ERR("no host-visible+coherent memory type."); return false; }
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size; mai.memoryTypeIndex = mt;
    if (!CheckVk(vkAllocateMemory(s_vk_device, &mai, nullptr, &g_staging_mem), "vkAllocateMemory")) return false;
    vkBindBufferMemory(s_vk_device, g_staging, g_staging_mem, 0);
    vkMapMemory(s_vk_device, g_staging_mem, 0, sz, 0, &g_staging_map);

    g_w = w; g_h = h;
    VR_LOG("compositor init: %ux%u x2 eyes (%zu images each), staging %llu bytes.", w, h, g_sc_images[0].size(), static_cast<unsigned long long>(sz));
    return true;
}

/* One XR frame from a CPU RGBA8 game frame (bottom-up, GL order). Pumps events; before
/* Upload one eye's pixels into its swapchain image. Swapchain acquire/release is
bool StageEye(const void* pixels, int eye)
{
    PumpEvents();
    if (!s_session_running.load(std::memory_order_acquire)) return true;   /* not begun yet */
    if (pixels != nullptr && (eye == 0 || eye == 1)) {
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        xrAcquireSwapchainImage(g_sc[eye], &ai, &idx);
        XrSwapchainImageWaitInfo swi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        swi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(g_sc[eye], &swi);

        const uint32_t rowbytes = g_w * 4;
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        uint8_t* dst = static_cast<uint8_t*>(g_staging_map);
        for (uint32_t y = 0; y < g_h; ++y)
            std::memcpy(dst + y * rowbytes, src + (g_h - 1 - y) * rowbytes, rowbytes);

        vkWaitForFences(s_vk_device, 1, &g_fence, VK_TRUE, UINT64_MAX);
        vkResetFences(s_vk_device, 1, &g_fence);
        vkResetCommandBuffer(g_cb, 0);
        VkCommandBufferBeginInfo bbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(g_cb, &bbi);
        VkImage img = g_sc_images[eye][idx];
        ImgBarrier(g_cb, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy rgn = {};
        rgn.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; rgn.imageSubresource.layerCount = 1;
        rgn.imageExtent = {g_w, g_h, 1};
        vkCmdCopyBufferToImage(g_cb, g_staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rgn);
        ImgBarrier(g_cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vkEndCommandBuffer(g_cb);
        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &g_cb;
        vkQueueSubmit(s_vk_queue, 1, &si, g_fence);
        vkWaitForFences(s_vk_device, 1, &g_fence, VK_TRUE, UINT64_MAX);

        XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(g_sc[eye], &ri);
        g_have[eye] = true;
    }
    return true;
}

/* Screen recenter anchor — the pose the virtual screen is world-locked to. Set by
static std::atomic<bool> s_recenter_req{false};
static float s_anchor_px = 0.0f, s_anchor_py = 0.0f, s_anchor_pz = 0.0f;
static float s_anchor_yaw = 0.0f;

/* Screen FOLLOW mode. 0 = ANCHORED (world-locked to the recenter anchor — the
static std::atomic<int> s_follow_mode{-1};
static float s_head_yaw = 0.0f;                 /* frame-updated flattened head yaw */
static float s_head_px = 0.0f, s_head_py = 0.0f, s_head_pz = 0.0f;
static bool  s_head_pos_valid = false;

static void follow_init_from_env(void)
{
    if (s_follow_mode.load(std::memory_order_relaxed) < 0) {
        const char* e = getenv("MUPEN_VR_FOLLOW");
        s_follow_mode.store((e != nullptr && *e != '\0' && *e != '0') ? 1 : 0,
                            std::memory_order_relaxed);
    }
}

/* Drive one XR frame: publish the head pose at the predicted display time and present
bool PresentBoth()
{
    PumpEvents();
    if (!s_session_running.load(std::memory_order_acquire)) { vr_pose_invalidate(); return true; }   /* not begun yet/stopped; pose must not outlive the session — vr_pose.h */

    XrFrameWaitInfo fwi = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    if (!CheckXR(xrWaitFrame(s_session, &fwi, &fs), "xrWaitFrame")) return false;
    XrFrameBeginInfo fbi = {XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(s_session, &fbi);

    if (s_view_space != XR_NULL_HANDLE) {
        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(s_view_space, s_space, fs.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
            m64p_vr_pose p;
            p.orient_x = loc.pose.orientation.x;
            p.orient_y = loc.pose.orientation.y;
            p.orient_z = loc.pose.orientation.z;
            p.orient_w = loc.pose.orientation.w;
            p.pos_x = loc.pose.position.x;
            p.pos_y = loc.pose.position.y;
            p.pos_z = loc.pose.position.z;
            p.valid = 1;
            p.frame = 0;                       /* publisher stamps the counter */
            vr_pose_publish(&p);

            /* Frame-update the current head basis for FOLLOW mode (same locate, so no
            {
                float cyaw;
                if (vr_pose_flatten_yaw(p.orient_x, p.orient_y, p.orient_z, p.orient_w, &cyaw))
                    s_head_yaw = cyaw;
                if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
                    s_head_px = p.pos_x; s_head_py = p.pos_y; s_head_pz = p.pos_z;
                    s_head_pos_valid = true;
                }
            }

            /* Consume a pending screen recenter with this same locate. Needs BOTH bits:
            if (s_recenter_req.load(std::memory_order_relaxed) &&
                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
                float yaw;
                if (vr_pose_flatten_yaw(p.orient_x, p.orient_y, p.orient_z, p.orient_w, &yaw))
                    s_anchor_yaw = yaw;        /* degenerate (looking straight up/down):
                                                  keep the previous heading, still re-anchor
                                                  the position */
                s_anchor_px = p.pos_x;
                s_anchor_py = p.pos_y;
                s_anchor_pz = p.pos_z;
                s_recenter_req.store(false, std::memory_order_relaxed);
                VR_LOG("screen recentered: anchor pos {%.2f %.2f %.2f} yaw %.1f deg.",
                       s_anchor_px, s_anchor_py, s_anchor_pz, s_anchor_yaw * 57.29578f);
            }
        } else {
            vr_pose_invalidate();
        }
    }

    /* Screen geometry from the resolved VR settings (profile/precedence): distance, physical
    const vr_settings* vs = vr_settings_get();
    const float scr_dist   = vs->screen_distance;
    const float scr_height = vs->screen_height;
    const float scr_width  = scr_height * static_cast<float>(g_w) / static_cast<float>(g_h);
    const float scr_voff   = vs->screen_vertical_offset;
    if (vs->screen_arc > 0.0f) {
        static bool warned_arc = false;
        if (!warned_arc) { warned_arc = true; VR_LOG("screenArc=%.1f requested but the cylinder layer is not yet implemented — presenting a flat quad.", vs->screen_arc); }
    }

    /* Screen basis: ANCHORED uses the frozen recenter anchor (world-locked); FOLLOW
    follow_init_from_env();
    const bool follow = (s_follow_mode.load(std::memory_order_relaxed) == 1) && s_head_pos_valid;
    const float base_yaw = follow ? s_head_yaw : s_anchor_yaw;
    const float base_px  = follow ? s_head_px  : s_anchor_px;
    const float base_py  = follow ? s_head_py  : s_anchor_py;
    const float base_pz  = follow ? s_head_pz  : s_anchor_pz;

    /* Compose the screen pose: yaw-only orientation, centre = basis + yaw-rotated
    const float a_sin = sinf(base_yaw), a_cos = cosf(base_yaw);
    const float a_hs  = sinf(base_yaw * 0.5f), a_hc = cosf(base_yaw * 0.5f);
    const float scr_px = base_px - scr_dist * a_sin;
    const float scr_py = base_py + scr_voff;
    const float scr_pz = base_pz - scr_dist * a_cos;

    /* [VRscreen] one diagnostic per session (and re-logged after a recenter): the exact
    {
        static int logged_at = -1000;
        static int frame_n = 0;
        ++frame_n;
        if (frame_n - logged_at > 300 || logged_at == -1000) {
            const float dx = s_head_px - scr_px, dy = s_head_py - scr_py, dz = s_head_pz - scr_pz;
            const float dh = sqrtf(dx*dx + dy*dy + dz*dz);
            const float hfov = 2.0f * atanf((scr_width * 0.5f) / (dh > 0.01f ? dh : scr_dist)) * 57.29578f;
            VR_LOG("[VRscreen] mode=%s screen(d=%.2f w=%.2f h=%.2f) quad{%.2f %.2f %.2f} "
                   "head{%.2f %.2f %.2f} head->quad=%.2fm hfov=%.0fdeg",
                   follow ? "FOLLOW" : "ANCHORED", scr_dist, scr_width, scr_height,
                   scr_px, scr_py, scr_pz, s_head_px, s_head_py, s_head_pz, dh, hfov);
            logged_at = frame_n;
        }
    }

    XrCompositionLayerQuad quad[2] = { {XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD} };
    std::vector<XrCompositionLayerBaseHeader*> layers;
    if (fs.shouldRender) {
        for (int e = 0; e < 2; ++e) {
            if (!g_have[e]) continue;
            quad[e].space = s_space;
            /* Mono-safe fallback: until a right-eye frame ever arrives (stock plugin /
            quad[e].eyeVisibility = (e == 0) ? (g_have[1] ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_BOTH)
                                             : XR_EYE_VISIBILITY_RIGHT;
            quad[e].subImage.swapchain = g_sc[e];
            quad[e].subImage.imageRect.extent = {static_cast<int32_t>(g_w), static_cast<int32_t>(g_h)};
            quad[e].pose.orientation.y = a_hs;   /* yaw-only: {0, sin(yaw/2), 0, cos(yaw/2)} */
            quad[e].pose.orientation.w = a_hc;
            quad[e].pose.position.x = scr_px;
            quad[e].pose.position.y = scr_py;
            quad[e].pose.position.z = scr_pz;
            quad[e].size.width  = scr_width;
            quad[e].size.height = scr_height;
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad[e]));
        }
    }
    XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO};
    fei.displayTime = fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fei.layerCount = static_cast<uint32_t>(layers.size());
    fei.layers = layers.data();
    if (!CheckXR(xrEndFrame(s_session, &fei), "xrEndFrame")) return false;
    return true;
}

bool SubmitFrame(const void* pixels, int eye)
{
    if (!StageEye(pixels, eye))
        return false;
    return PresentBoth();
}

void ShutdownPersistent()
{
    if (s_vk_device != VK_NULL_HANDLE) vkDeviceWaitIdle(s_vk_device);
    if (g_staging_map != nullptr) { vkUnmapMemory(s_vk_device, g_staging_mem); g_staging_map = nullptr; }
    if (g_staging != VK_NULL_HANDLE) { vkDestroyBuffer(s_vk_device, g_staging, nullptr); g_staging = VK_NULL_HANDLE; }
    if (g_staging_mem != VK_NULL_HANDLE) { vkFreeMemory(s_vk_device, g_staging_mem, nullptr); g_staging_mem = VK_NULL_HANDLE; }
    if (g_fence != VK_NULL_HANDLE) { vkDestroyFence(s_vk_device, g_fence, nullptr); g_fence = VK_NULL_HANDLE; }
    if (g_pool != VK_NULL_HANDLE) { vkDestroyCommandPool(s_vk_device, g_pool, nullptr); g_pool = VK_NULL_HANDLE; }
    for (int e = 0; e < 2; ++e) {
        if (g_sc[e] != XR_NULL_HANDLE) { xrDestroySwapchain(g_sc[e]); g_sc[e] = XR_NULL_HANDLE; }
        g_sc_images[e].clear();
    }
    g_have[0] = g_have[1] = false; g_w = g_h = 0;
    DestroyAll();
}

} // namespace

extern "C" {

int vr_video_vk_info(void)
{
    VR_LOG("--- mupen64plus-VR Vulkan OpenXR probe ---");
    bool ok = false;
    do {
        if (!CreateInstanceAndSystem("mupen64plus-VR-vk-info")) break;
        if (!CreateVulkanDevice()) break;
        if (!CreateSessionVk()) break;
        ok = true;
    } while (0);
    VR_LOG("--- vk probe: %s ---",
           ok ? "OK (Vulkan instance + device + session created in-headset)" : "FAILED (see messages above)");
    DestroyAll();
    return ok ? 0 : 1;
}

int vr_video_vk_frame_probe(int max_frames)
{
    VR_LOG("--- mupen64plus-VR Vulkan frame-loop probe ---");
    const bool ok = RunFrameProbeVk(max_frames > 0 ? max_frames : 600);
    VR_LOG("--- vk frame probe: %s ---", ok ? "OK (rendered an animated quad in the headset)"
                                            : "FAILED (see messages above)");
    return ok ? 0 : 1;
}


int vr_video_vk_init(int width, int height)
{
    if (width <= 0 || height <= 0) return 1;
    const bool ok = InitPersistent(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    if (!ok) ShutdownPersistent();
    return ok ? 0 : 1;
}

int vr_video_vk_submit(const void* rgba_pixels, int width, int height, int eye)
{
    if (static_cast<uint32_t>(width) != g_w || static_cast<uint32_t>(height) != g_h)
        return 1;   /* resolution changed — caller should re-init (S13 handles this) */
    return SubmitFrame(rgba_pixels, eye) ? 0 : 1;
}

int vr_video_vk_stage(const void* rgba_pixels, int width, int height, int eye)
{
    if (static_cast<uint32_t>(width) != g_w || static_cast<uint32_t>(height) != g_h)
        return 1;
    return StageEye(rgba_pixels, eye) ? 0 : 1;
}

void vr_video_vk_recenter(void)
{
    /* Any thread. Consumed inside PresentBoth's frame loop (valid predictedDisplayTime);
    s_recenter_req.store(true, std::memory_order_relaxed);
}

int vr_video_vk_toggle_follow(void)
{
    /* Any thread. Live A/B for the screen-follow experiment (F6). Returns the new
    follow_init_from_env();
    int v = s_follow_mode.load(std::memory_order_relaxed) == 1 ? 0 : 1;
    s_follow_mode.store(v, std::memory_order_relaxed);
    VR_LOG("screen follow: %s", v ? "FOLLOW (window tracks head yaw)" : "ANCHORED (world-locked)");
    return v;
}

int vr_video_vk_present(void)
{
    return PresentBoth() ? 0 : 1;
}

void vr_video_vk_shutdown(void)
{
    ShutdownPersistent();
}

} // extern "C"
