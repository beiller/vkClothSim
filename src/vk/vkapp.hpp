#pragma once
#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include <vector>

class VkApp {
public:
    // phase 1: window + instance + surface + physical device (no VkDevice yet)
    bool initInstance(int width, int height, const char* title);
    // phase 2: device-dependent resources (swapchain, render passes, pool, fence)
    bool initDevice(VkDevice dev);
    // create the VkDevice with our queue + extensions (used when no OpenXR device)
    VkDevice makeDevice();
    // queue + extensions for a VkDeviceCreateInfo; OpenXR uses this for xrCreateVulkanDeviceKHR
    const VkDeviceCreateInfo& deviceCreateInfo() const { return m_dci; }
    void shutdown();

    bool windowShouldClose() const;
    void pollEvents();
    bool keyIsDown(int key) const;

    GLFWwindow* glfwWindow() const { return m_win; }
    VkInstance instance() const { return m_inst; }
    VkPhysicalDevice pdev() const { return m_pdev; }
    VkDevice device() const { return m_dev; }
    VkQueue queue() const { return m_queue; }
    uint32_t queueFamily() const { return m_qf; }
    VkRenderPass renderPass() const { return m_rp; }
    VkRenderPass sceneRenderPass() const { return m_rpScene; }
    VkSwapchainKHR swapchain() const { return m_sc; }
    VkFramebuffer frameBuffer(uint32_t i) const { return m_fbs[i]; }
    VkImageView view(uint32_t i) const { return m_views[i]; }
    VkFramebuffer sceneFramebuffer() const { return m_sceneFb; }
    VkImageView hdrView() const { return m_hdrView; }
    VkSampler hdrSampler() const { return m_hdrSampler; }
    VkImage hdrImage() const { return m_hdr; }
    VkFormat hdrFormat() const { return m_hdrFmt; }
    VkFormat swapchainFormat() const { return m_scfmt; }
    VkExtent2D extent() const { return m_extent; }
    uint32_t imageCount() const { return (uint32_t)m_images.size(); }

    uint32_t acquireNextImage();
    VkCommandBuffer beginCommands();
    void submit(VkCommandBuffer cmd);
    void present(uint32_t idx);

private:
    void createDepth(VkExtent2D ext);
    void createHDR(VkExtent2D ext);
    void recreateSwapchain();
    void destroySwapchainResources();

    GLFWwindow* m_win = nullptr;
    VkInstance m_inst = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_qf = 0;
    float m_qprio = 1.0f;
    VkDeviceQueueCreateInfo m_qci{};
    std::vector<const char*> m_devExt;
    VkDeviceCreateInfo m_dci{};
    VkSwapchainKHR m_sc = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    VkRenderPass m_rpScene = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_fbs;
    VkFramebuffer m_sceneFb = VK_NULL_HANDLE;
    VkImage m_depth = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMem = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    VkImage m_hdr = VK_NULL_HANDLE;
    VkDeviceMemory m_hdrMem = VK_NULL_HANDLE;
    VkImageView m_hdrView = VK_NULL_HANDLE;
    VkSampler m_hdrSampler = VK_NULL_HANDLE;
    VkFormat m_hdrFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat m_scfmt = VK_FORMAT_UNDEFINED;
    VkSurfaceFormatKHR m_fmt{};
    VkExtent2D m_extent{};
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
};
