// vkapp.cpp
#include "vk/vkapp.hpp"

#include "vk/vkutil.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

bool VkApp::init(int width, int height, const char* title) {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    m_win = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_win) {
        std::fprintf(stderr, "window creation failed\n");
        glfwTerminate();
        return false;
    }

    uint32_t n = 0;
    const char** req = glfwGetRequiredInstanceExtensions(&n);
    std::vector<const char*> layers;
    if (std::getenv("VK_VALIDATE")) {
        uint32_t nl = 0;
        vkEnumerateInstanceLayerProperties(&nl, nullptr);
        std::vector<VkLayerProperties> lprops(nl);
        vkEnumerateInstanceLayerProperties(&nl, lprops.data());
        for (const auto& p : lprops)
            if (std::string_view(p.layerName) == "VK_LAYER_KHRONOS_validation")
                layers.push_back(p.layerName);
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = title;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{};
    ic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = n;
    ic.ppEnabledExtensionNames = req;
    ic.enabledLayerCount = (uint32_t)layers.size();
    ic.ppEnabledLayerNames = layers.data();
    VK(vkCreateInstance(&ic, nullptr, &m_inst));

    VK(glfwCreateWindowSurface(m_inst, m_win, nullptr, &m_surface));

    // the first physical device with a graphics + present queue
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(m_inst, &nd, nullptr);
    std::vector<VkPhysicalDevice> devs(nd);
    vkEnumeratePhysicalDevices(m_inst, &nd, devs.data());
    for (auto* d : devs) {
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qps(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qps.data());
        for (uint32_t i = 0; i < qc; ++i) {
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &present);
            if ((qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                m_pdev = d;
                m_qf = i;
                break;
            }
        }
        if (m_pdev)
            break;
    }
    if (!m_pdev) {
        std::fprintf(stderr, "no graphics+present device\n");
        vkDestroySurfaceKHR(m_inst, m_surface, nullptr);
        vkDestroyInstance(m_inst, nullptr);
        glfwDestroyWindow(m_win);
        glfwTerminate();
        return false;
    }

    const char* devExt = "VK_KHR_swapchain";
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_qf;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = &devExt;
    VK(vkCreateDevice(m_pdev, &dci, nullptr, &m_dev));
    vkGetDeviceQueue(m_dev, m_qf, 0, &m_queue);

    // swapchain (first surface format, current extent, min+1 images)
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_pdev, m_surface, &caps));
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_pdev, m_surface, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(nf);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_pdev, m_surface, &nf, fmts.data());
    VkSurfaceFormatKHR fmt = fmts[0];
    m_scfmt = fmt.format;
    m_extent = caps.currentExtent;
    if (m_extent.width == 0xFFFFFFFF) {
        int w, h;
        glfwGetFramebufferSize(m_win, &w, &h);
        m_extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
    uint32_t icnt = caps.minImageCount + 1;
    if (caps.maxImageCount && icnt > caps.maxImageCount)
        icnt = caps.maxImageCount;
    VkSwapchainCreateInfoKHR scc{};
    scc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scc.surface = m_surface;
    scc.minImageCount = icnt;
    scc.imageFormat = fmt.format;
    scc.imageColorSpace = fmt.colorSpace;
    scc.imageExtent = m_extent;
    scc.imageArrayLayers = 1;
    scc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scc.preTransform = caps.currentTransform;
    scc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scc.clipped = VK_TRUE;
    VK(vkCreateSwapchainKHR(m_dev, &scc, nullptr, &m_sc));
    vkGetSwapchainImagesKHR(m_dev, m_sc, &icnt, nullptr);
    m_images.resize(icnt);
    vkGetSwapchainImagesKHR(m_dev, m_sc, &icnt, m_images.data());

    // depth (shared by all framebuffers)
    createDepth(m_extent);

    // render pass (color + depth)
    VkAttachmentDescription att[2]{};
    att[0].format = fmt.format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cr[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                   {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription sp{};
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr[0];
    sp.pDepthStencilAttachment = &cr[1];
    VkRenderPassCreateInfo rpc{};
    rpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpc.attachmentCount = 2;
    rpc.pAttachments = att;
    rpc.subpassCount = 1;
    rpc.pSubpasses = &sp;
    VK(vkCreateRenderPass(m_dev, &rpc, nullptr, &m_rp));

    // image views + framebuffers
    m_views.resize(icnt);
    m_fbs.resize(icnt);
    for (uint32_t i = 0; i < icnt; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = m_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(m_dev, &vci, nullptr, &m_views[i]));
        VkImageView fatts[2] = {m_views[i], m_depthView};
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_rp;
        fci.attachmentCount = 2;
        fci.pAttachments = fatts;
        fci.width = m_extent.width;
        fci.height = m_extent.height;
        fci.layers = 1;
        VK(vkCreateFramebuffer(m_dev, &fci, nullptr, &m_fbs[i]));
    }

    // one reusable command buffer + one fence (per-frame sync)
    VkCommandPoolCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpc.queueFamilyIndex = m_qf;
    VK(vkCreateCommandPool(m_dev, &cpc, nullptr, &m_pool));
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(m_dev, &cai, &m_cmd));
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK(vkCreateFence(m_dev, &fci, nullptr, &m_fence));
    return true;
}

void VkApp::shutdown() {
    VK(vkDeviceWaitIdle(m_dev));
    for (auto* fb : m_fbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    for (auto* v : m_views)
        vkDestroyImageView(m_dev, v, nullptr);
    vkDestroyRenderPass(m_dev, m_rp, nullptr);
    vkDestroySwapchainKHR(m_dev, m_sc, nullptr);
    vkDestroyImageView(m_dev, m_depthView, nullptr);
    vkDestroyImage(m_dev, m_depth, nullptr);
    vkFreeMemory(m_dev, m_depthMem, nullptr);
    vkDestroyFence(m_dev, m_fence, nullptr);
    vkDestroyCommandPool(m_dev, m_pool, nullptr);
    vkDestroyDevice(m_dev, nullptr);
    vkDestroySurfaceKHR(m_inst, m_surface, nullptr);
    vkDestroyInstance(m_inst, nullptr);
    glfwDestroyWindow(m_win);
    glfwTerminate();
}

bool VkApp::windowShouldClose() const {
    return glfwWindowShouldClose(m_win) != 0;
}

void VkApp::pollEvents() { // NOLINT(readability-convert-member-functions-to-static)
    glfwPollEvents();
}

bool VkApp::keyIsDown(int key) const {
    return glfwGetKey(m_win, key) == GLFW_PRESS;
}

// Acquire the next image. No fence/semaphore: `vkAcquireNextImageKHR` blocks until an image is
// available, so the image is safe to use when this returns. This app is fully synchronous (the
// submit fence is waited on before present), so a separate acquire fence is not needed — and
// reusing/resetting one across frames was the source of the hangs.
uint32_t VkApp::acquireNextImage() {
    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(m_dev, m_sc, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &idx);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "vk error %d @%s:%d\n", (int)r, __FILE__, __LINE__);
        std::abort();
    }
    return idx;
}

// Reset + begin the reusable command buffer. The previous frame's submit fence was already
// waited on in `submit()` before present, so the command buffer is free to reuse.
VkCommandBuffer VkApp::beginCommands() {
    VK(vkResetCommandBuffer(m_cmd, 0));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK(vkBeginCommandBuffer(m_cmd, &cbi));
    return m_cmd;
}

// End the command buffer, submit, wait on the fence (per-stage sync), then reset the fence so
// it can be reused by a later submit in the same frame (the sim and the render are submitted
// separately so the compute->render handoff is a clean queue boundary).
void VkApp::submit(VkCommandBuffer cmd) {
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK(vkQueueSubmit(m_queue, 1, &si, m_fence));
    VK(vkWaitForFences(m_dev, 1, &m_fence, VK_TRUE, 30000000000ull));
    VK(vkResetFences(m_dev, 1, &m_fence));
}

void VkApp::present(uint32_t idx) {
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 0;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_sc;
    pi.pImageIndices = &idx;
    VK(vkQueuePresentKHR(m_queue, &pi));
}

// Create the shared depth image + view (D32).
void VkApp::createDepth(VkExtent2D ext) {
    VkImageCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    dci.imageType = VK_IMAGE_TYPE_2D;
    dci.extent = {ext.width, ext.height, 1};
    dci.mipLevels = 1;
    dci.arrayLayers = 1;
    dci.format = VK_FORMAT_D32_SFLOAT;
    dci.tiling = VK_IMAGE_TILING_OPTIMAL;
    dci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dci.samples = VK_SAMPLE_COUNT_1_BIT;
    dci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK(vkCreateImage(m_dev, &dci, nullptr, &m_depth));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_dev, m_depth, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(m_pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(m_dev, &maa, nullptr, &m_depthMem));
    VK(vkBindImageMemory(m_dev, m_depth, m_depthMem, 0));
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = m_depth;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_D32_SFLOAT;
    vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(m_dev, &vci, nullptr, &m_depthView));
}
