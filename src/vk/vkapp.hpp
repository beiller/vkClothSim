// vkapp.hpp
// The window + the Vulkan core. A GLFW window (GLFW_NO_API), a VkInstance/VkDevice with a
// single graphics+present queue, a swapchain (+ views + framebuffers) into one render
// pass (color + D32 depth), and a fence-guarded command buffer for per-frame recording.
// Knows nothing about the scene, the sim, or the UI — the renderer records the draws.
#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

class VkApp {
public:
    // Create the window + all the Vulkan objects. Returns false (message on stderr) on
    // failure.
    bool init(int width, int height, const char* title);
    void shutdown();

    // the window
    bool windowShouldClose() const;
    void pollEvents();
    bool keyIsDown(int key) const;

    // handles (for the renderer + the ImGui backends)
    GLFWwindow* glfwWindow() const { return m_win; }
    VkInstance instance() const { return m_inst; }
    VkPhysicalDevice pdev() const { return m_pdev; }
    VkDevice device() const { return m_dev; }
    VkQueue queue() const { return m_queue; }
    uint32_t queueFamily() const { return m_qf; }
    VkRenderPass renderPass() const { return m_rp; }
    VkSwapchainKHR swapchain() const { return m_sc; }
    VkFramebuffer frameBuffer(uint32_t i) const { return m_fbs[i]; }
    VkFormat swapchainFormat() const { return m_scfmt; }
    VkExtent2D extent() const { return m_extent; }
    uint32_t imageCount() const { return (uint32_t)m_images.size(); }

    // per-frame: acquire the next image, record commands, submit + wait
    uint32_t acquireNextImage();
    VkCommandBuffer beginCommands();   // fence wait + reset + begin (one reusable cmd buffer)
    void submit(VkCommandBuffer cmd);  // end + submit + fence wait
    void present(uint32_t idx);

    // one-shot: read swapchain image `idx` back to a PPM (headless verification)
    void readbackPPM(uint32_t idx, const std::string& path);

private:
    void createDepth(VkExtent2D ext);

    GLFWwindow* m_win = nullptr;
    VkInstance m_inst = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_qf = 0;
    VkSwapchainKHR m_sc = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    VkRenderPass m_rp = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_fbs;
    VkImage m_depth = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMem = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    VkFormat m_scfmt = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
};
