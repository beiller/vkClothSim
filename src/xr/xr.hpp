#pragma once
#include <vulkan/vulkan.h>

#include "math.hpp"
#include "xr/openxr_loader.hpp"
#include "xr_backend.hpp"
#include <vector>

class VkApp;

// this openxr.h ships the KHR_vulkan_enable type enums but not the structs; the two defined here match the runtime's layout
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
using PFN_xrGetVulkanGraphicsDevice2KHR = XrResult (*)(XrInstance, const XrVulkanGraphicsDeviceGetInfoKHR*,
                                                       VkPhysicalDevice*);
struct XrVulkanInstanceCreateInfoKHR {
    XrStructureType type;
    const void* next;
    XrSystemId systemId;
    uint32_t createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    const VkInstanceCreateInfo* vulkanCreateInfo;
    const VkAllocationCallbacks* vulkanAllocator;
};
using PFN_xrCreateVulkanInstanceKHR = XrResult (*)(XrInstance, const XrVulkanInstanceCreateInfoKHR*, VkInstance*,
                                                   VkResult*);
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

class Xr : public IXrBackend {
public:
    ~Xr() override { shutdown(); } // idempotent, so an explicit shutdown() first is safe
    bool createDevice(VkApp& app) override;
    bool init(VkApp& app) override;
    void shutdown() override;

    void waitFrame() override;
    XrFrame poll() override;
    void endFrame(bool render) override;

    void acquireImage(int eye) override;
    void releaseImage(int eye) override;
    int imageIndex(int eye) const override { return m_swaps[eye].cur; }

    int eyeCount() const override { return m_eyeCount; }
    VkDevice device() const override { return m_dev; }
    VkFormat format() const override { return m_fmt; }
    VkExtent2D extent(int eye) const override { return m_swaps[eye].extent; }
    const std::vector<VkImage>& images(int eye) const override { return m_swaps[eye].images; }

private:
    struct Swap {
        XrSwapchain sc = XR_NULL_HANDLE;
        std::vector<VkImage> images;
        VkExtent2D extent{};
        int cur = -1;
        bool acquired = false;
    };

    void pollEvents();
    void initActions();
    void pollControllers(XrFrame& fr);
    void destroyActions();

    XrLoader m_loader;
    XrInstance m_inst = XR_NULL_HANDLE;
    VkInstance m_shadowInst = VK_NULL_HANDLE; // created via OpenXR so the runtime holds our vkGetInstanceProcAddr
    VkDevice m_dev = VK_NULL_HANDLE; // created via xrCreateVulkanDeviceKHR so the compositor uses instance-proc
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_refSpace = XR_NULL_HANDLE;
    XrEnvironmentBlendMode m_blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    VkFormat m_fmt = VK_FORMAT_UNDEFINED;
    int m_eyeCount = 0;
    bool m_running = false;
    XrSessionState m_state = XR_SESSION_STATE_UNKNOWN;
    XrFrameState m_fs{};
    XrView m_views[2]{};
    XrActionSet m_actionSet = XR_NULL_HANDLE;
    XrPath m_hand[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrAction m_gripPose = XR_NULL_HANDLE, m_aimPose = XR_NULL_HANDLE, m_trigger = XR_NULL_HANDLE,
             m_stick = XR_NULL_HANDLE, m_squeeze = XR_NULL_HANDLE, m_buttonA = XR_NULL_HANDLE,
             m_buttonB = XR_NULL_HANDLE;
    XrSpace m_gripSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}, m_aimSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    int m_diagFrame = 0;
    bool createInstance();
    void createShadowInstance();
    void queryRuntimeVulkan(VkApp& app);
    bool createVulkanDevice(VkApp& app);
    XrCompositionLayerProjectionView m_proj[2]{};
    XrRect2Di m_rects[2]{};
    Swap m_swaps[2]{};
};
