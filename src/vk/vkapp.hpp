#pragma once
#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <vector>

class VkApp {
public:
    bool initInstance(int width, int height, const char* title);
    bool initHeadless();
    bool initDevice(VkDevice dev);
    VkDevice makeDevice();
    const VkDeviceCreateInfo& deviceCreateInfo() const { return m_dci; }
    void shutdown();

    bool windowShouldClose() const;
    void pollEvents();
    void resizeBuffers(); // rebuild the swapchain if the framebuffer size changed, before the acquire

    GLFWwindow* glfwWindow() const { return m_win; }
    VkInstance instance() const { return m_inst; }
    VkPhysicalDevice pdev() const { return m_pdev; }
    VkDevice device() const { return m_dev; }
    VkQueue queue() const { return m_queue; }
    uint32_t queueFamily() const { return m_qf; }
    VkRenderPass renderPass() const { return m_rp; }
    VkRenderPass sceneRenderPass() const { return m_rpScene; }
    VkRenderPass sceneNoClearRenderPass() const { return m_rpSceneNoClear; }
    VkFramebuffer frameBuffer(uint32_t i) const { return m_fbs[i]; }
    VkImageView view(uint32_t i) const { return m_views[i]; }
    VkFramebuffer sceneFramebuffer() const { return m_sceneFb; }
    VkFramebuffer sceneNoClearFramebuffer() const { return m_sceneFbNoClear; }
    VkImageView depthView() const { return m_depthView; }
    VkImageView sceneColorView() const { return m_sceneColorView; }
    VkImageView sceneColorTempView() const { return m_sceneColorTempView; }
    VkImage sssDiffuseImage() const { return m_sssDiffuse; }
    VkImageView sssDiffuseView() const { return m_sssDiffuseView; }
    VkImage sssWeightImage() const { return m_sssWeight; }
    VkImageView sssWeightView() const { return m_sssWeightView; }
    VkImage sssPostImage() const { return m_sssPost; }
    VkImageView sssPostView() const { return m_sssPostView; }
    uint32_t sceneColorMips() const { return m_sceneColorMips; }
    VkImageView depthCopyView() const { return m_depthCopyView; }
    VkImage depthImage() const { return m_depth; }
    VkImage sceneColorImage() const { return m_sceneColor; }
    VkImage sceneColorTempImage() const { return m_sceneColorTemp; }
    VkImage depthCopyImage() const { return m_depthCopy; }
    VkImageView hdrView() const { return m_hdrView; }
    VkSampler hdrSampler() const { return m_hdrSampler; }
    VkImage hdrImage() const { return m_hdr; }
    VkFormat swapchainFormat() const { return m_scfmt; }
    VkExtent2D extent() const { return m_extent; }
    VkExtent2D renderExtent() const { return m_renderExtent; } // offscreen scene buffers (scaled display size)
    void setRenderExtent(VkExtent2D e);                        // pending until a resizeBuffers applies it
    uint32_t imageCount() const { return (uint32_t)m_images.size(); }
    bool vsynced() const { return m_presentMode == VK_PRESENT_MODE_FIFO_KHR; } // FIFO present blocks on the display refresh

    uint32_t acquireNextImage();
    VkCommandBuffer beginCommands();
    void submit(VkCommandBuffer cmd);
    void present(uint32_t idx, bool wait);

    // Async pipeline (FRAME_PACING.md phase 6): up to kSlots frames in flight. beginFrame picks this frame's
    // slot and waits+resets that slot's fence — guaranteeing the last GPU user of this slot's buffers/sets is
    // done before we overwrite them (a no-op in steady state, since the pacing sleep gives the GPU the whole
    // rest of the previous frame to finish). submit() then just batches command buffers; endFrame submits the
    // batch with the frame's fence. setAsync(false) restores the legacy blocking submit (--sync bisection).
    static constexpr int kSlots = 3;
    void setAsync(bool on);
    int beginFrame();
    void endFrame();
    // Submit the current async batch right now with the slot fence (no wait). For VR: the eye passes are
    // submitted blocking, so everything recorded before them must already be queued. The slot fence is
    // resubmitted by this call; endFrame skips its own submit when the batch is then empty.
    void flushBatch();
    // While true, submit() executes immediately (legacy blocking semantics) instead of batching. VR renders
    // both eyes back-to-back while CPU-writing per-eye UBOs between them, so the eyes must each execute on
    // the queue before the next eye's UBO is written -- batching them into one submit would make both eyes
    // read the last eye's data.
    void setBlocking(bool on);
    // Wait for every in-flight frame to finish. For rare descriptor updates that touch all ring slots at once
    // (texture uploads, posPrev rebinding): those sets may be in use by up to kSlots-1 older frames.
    void waitAll();
    // Like waitAll but waits on ALL slots — for teardown/scene-switch, when no frame is being recorded and the
    // last submitted frame's fence (the current slot) must drain too.
    void drain();
    bool async() const { return m_async; }

private:
    void createAttachment(VkExtent2D ext, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlagBits aspect,
                          VkImage& img, VkDeviceMemory& mem, VkImageView& view, uint32_t mipLevels = 1);
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
    VkRenderPass m_rpSceneNoClear = VK_NULL_HANDLE;
    VkFramebuffer m_sceneFbNoClear = VK_NULL_HANDLE;
    VkImage m_depth = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMem = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    VkImage m_depthCopy = VK_NULL_HANDLE;
    VkDeviceMemory m_depthCopyMem = VK_NULL_HANDLE;
    VkImageView m_depthCopyView = VK_NULL_HANDLE;
    VkImage m_sceneColor = VK_NULL_HANDLE;
    VkDeviceMemory m_sceneColorMem = VK_NULL_HANDLE;
    VkImageView m_sceneColorView = VK_NULL_HANDLE;
    VkImageView m_sceneColorAttView = VK_NULL_HANDLE;
    VkImage m_sceneColorTemp = VK_NULL_HANDLE;
    VkDeviceMemory m_sceneColorTempMem = VK_NULL_HANDLE;
    VkImageView m_sceneColorTempView = VK_NULL_HANDLE;
    VkImage m_sssDiffuse = VK_NULL_HANDLE;
    VkDeviceMemory m_sssDiffuseMem = VK_NULL_HANDLE;
    VkImageView m_sssDiffuseView = VK_NULL_HANDLE;
    VkImage m_sssWeight = VK_NULL_HANDLE;
    VkDeviceMemory m_sssWeightMem = VK_NULL_HANDLE;
    VkImageView m_sssWeightView = VK_NULL_HANDLE;
    VkImage m_sssPost = VK_NULL_HANDLE;
    VkDeviceMemory m_sssPostMem = VK_NULL_HANDLE;
    VkImageView m_sssPostView = VK_NULL_HANDLE;
    uint32_t m_sceneColorMips = 1;
    VkImage m_hdr = VK_NULL_HANDLE;
    VkDeviceMemory m_hdrMem = VK_NULL_HANDLE;
    VkImageView m_hdrView = VK_NULL_HANDLE;
    VkSampler m_hdrSampler = VK_NULL_HANDLE;
    VkFormat m_hdrFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat m_scfmt = VK_FORMAT_UNDEFINED;
    VkSurfaceFormatKHR m_fmt{};
    VkExtent2D m_extent{};
    VkExtent2D m_pendingExtent{};
    VkExtent2D m_renderExtent{};
    VkExtent2D m_pendingRenderExtent{};
    VkCommandPool m_pool = VK_NULL_HANDLE;
    // Command-buffer pool: a frame records several stages (soft per tick, skin, shadow, env, render...), each in
    // its own buffer. A buffer is safe to reset once the fence of the frame it was submitted with has been
    // waited on — tracked per buffer via m_cmdFrame (see beginCommands).
    static constexpr int kCmdPool = 48;
    VkCommandBuffer m_cmds[kCmdPool] = {};
    int64_t m_cmdFrame[kCmdPool] = {}; // frame count the buffer was last recorded for (-1 = never)
    int m_nextCmd = 0;
    VkFence m_fence = VK_NULL_HANDLE;  // sync-mode blocking submit
    VkFence m_fences[kSlots] = {};     // async pipeline, one per slot
    uint64_t m_frameCount = 0;
    int m_curSlot = 0;
    bool m_async = true;
    bool m_blocking = false;   // submit() executes immediately while true (VR eye passes)
    bool m_flushed = false;    // flushBatch resubmitted the slot fence this frame
    std::vector<VkCommandBuffer> m_batch;
    VkFence m_acqFence = VK_NULL_HANDLE;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
};
