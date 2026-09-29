#pragma once
#include <vulkan/vulkan.h>

#include "math.hpp"
#include "xr/openxr_loader.hpp"

#include <vector>

class VkApp;

// this copy of openxr.h ships the KHR_vulkan_enable type enums but not the structs,
// so define the two we need (layout matches the runtime's expectation)
struct XrGraphicsBindingVulkanKHR {
    XrStructureType type;
    const void* next;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t queueFamilyIndex;
    uint32_t queueIndex;
};
struct XrSwapchainImageVulkanKHR {
    XrStructureType type;
    const void* next;
    VkImage image;
};
struct XrGraphicsRequirementsVulkan2KHR {
    XrStructureType type;
    const void* next;
    uint32_t minApiVersionSupported;
    uint32_t maxApiVersionSupported;
};
using PFN_xrGetVulkanGraphicsRequirements2KHR = XrResult (*)(XrInstance, XrSystemId, XrGraphicsRequirementsVulkan2KHR*);
struct XrVulkanGraphicsDeviceGetInfoKHR {
    XrStructureType type;
    const void* next;
    XrSystemId systemId;
    VkInstance vulkanInstance;
};
using PFN_xrGetVulkanGraphicsDevice2KHR = XrResult (*)(XrInstance, const XrVulkanGraphicsDeviceGetInfoKHR*, VkPhysicalDevice*);
struct XrVulkanInstanceCreateInfoKHR {
    XrStructureType type;
    const void* next;
    XrSystemId systemId;
    uint32_t createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    const VkInstanceCreateInfo* vulkanCreateInfo;
    const VkAllocationCallbacks* vulkanAllocator;
};
using PFN_xrCreateVulkanInstanceKHR = XrResult (*)(XrInstance, const XrVulkanInstanceCreateInfoKHR*, VkInstance*, VkResult*);
struct XrVulkanDeviceCreateInfoKHR {
    XrStructureType type;
    const void* next;
    XrSystemId systemId;
    uint32_t createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    VkPhysicalDevice vulkanPhysicalDevice;
    const VkDeviceCreateInfo* vulkanCreateInfo;
    const VkAllocationCallbacks* vulkanAllocator;
};
using PFN_xrCreateVulkanDeviceKHR = XrResult (*)(XrInstance, const XrVulkanDeviceCreateInfoKHR*, VkDevice*, VkResult*);

// per-frame XR output: head + per-eye poses in the local-floor reference space
struct XrFrameData {
    bool running = false;
    bool focused = false;
    bool shouldRender = false;
    bool havePose = false;
    int nEyes = 0;
    XrEyeData eye[2]{};
};

class Xr {
public:
    // phase 1: load loader, create OpenXR instance + Vulkan device (via xrCreateVulkanDeviceKHR)
    // so the compositor holds our pfn+device; must run before anything allocates on the device
    bool createDevice(VkApp& app);
    // phase 2: create session/swapchains (assumes createDevice succeeded)
    bool init(VkApp& app);
    void shutdown();

    // per-frame: events + wait/begin frame + locate views
    XrFrameData poll();
    // must run once per poll() while the session is running
    void endFrame(bool render);

    // swapchain image lifecycle for direct headset rendering
    void acquireImage(int eye);
    void releaseImage(int eye);
    int imageIndex(int eye) const;

    bool running() const { return m_running; }
    bool focused() const { return m_focused; }
    bool ready() const { return m_ready; }
    int eyeCount() const { return m_eyeCount; }
    VkDevice device() const { return m_dev; }
    VkFormat format() const { return m_fmt; }
    VkExtent2D extent(int eye) const { return m_swaps[eye].extent; }
    const std::vector<VkImage>& images(int eye) const { return m_swaps[eye].images; }

private:
    struct Swap {
        XrSwapchain sc = XR_NULL_HANDLE;
        std::vector<VkImage> images;
        VkExtent2D extent{};
        int cur = -1;
        bool acquired = false;
    };

    void pollEvents();

    XrLoader m_loader;
    XrInstance m_inst = XR_NULL_HANDLE;
    VkInstance m_shadowInst = VK_NULL_HANDLE; // created via OpenXR so the runtime holds our vkGetInstanceProcAddr
    VkDevice m_dev = VK_NULL_HANDLE;          // created via xrCreateVulkanDeviceKHR so the compositor uses instance-proc
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_refSpace = XR_NULL_HANDLE;
    XrEnvironmentBlendMode m_blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    VkFormat m_fmt = VK_FORMAT_UNDEFINED;
    int m_eyeCount = 0;
    bool m_ready = false;
    bool m_running = false;
    bool m_focused = false;
    XrSessionState m_state = XR_SESSION_STATE_UNKNOWN;
    XrFrameState m_fs{};
    XrView m_views[2]{};
    XrCompositionLayerProjectionView m_proj[2]{};
    XrRect2Di m_rects[2]{};
    Swap m_swaps[2]{};
};
