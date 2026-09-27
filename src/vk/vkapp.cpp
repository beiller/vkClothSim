#include "vk/vkapp.hpp"

#include "vk/vkutil.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

void checkResult(VkResult r, const char* what) {
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    std::fprintf(stderr, "vk %s error %d @%s:%d\n", what, (int)r, __FILE__, __LINE__);
    std::abort();
}

} // namespace

bool VkApp::init(int width, int height, const char* title) {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
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

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_pdev, m_surface, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(nf);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_pdev, m_surface, &nf, fmts.data());
    m_fmt = fmts[0];
    m_scfmt = m_fmt.format;

    VkAttachmentDescription satt[2]{};
    satt[0].format = m_hdrFmt;
    satt[0].samples = VK_SAMPLE_COUNT_1_BIT;
    satt[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    satt[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    satt[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    satt[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    satt[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    satt[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    satt[1].format = VK_FORMAT_D32_SFLOAT;
    satt[1].samples = VK_SAMPLE_COUNT_1_BIT;
    satt[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    satt[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    satt[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    satt[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    satt[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    satt[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference scr[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                    {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription ssp{};
    ssp.colorAttachmentCount = 1;
    ssp.pColorAttachments = &scr[0];
    ssp.pDepthStencilAttachment = &scr[1];
    VkRenderPassCreateInfo srpc{};
    srpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    srpc.attachmentCount = 2;
    srpc.pAttachments = satt;
    srpc.subpassCount = 1;
    srpc.pSubpasses = &ssp;
    VK(vkCreateRenderPass(m_dev, &srpc, nullptr, &m_rpScene));

    VkAttachmentDescription batt[1]{};
    batt[0].format = m_fmt.format;
    batt[0].samples = VK_SAMPLE_COUNT_1_BIT;
    batt[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    batt[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    batt[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    batt[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    batt[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    batt[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference bcr[1] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription bsp{};
    bsp.colorAttachmentCount = 1;
    bsp.pColorAttachments = &bcr[0];
    VkRenderPassCreateInfo brpc{};
    brpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    brpc.attachmentCount = 1;
    brpc.pAttachments = batt;
    brpc.subpassCount = 1;
    brpc.pSubpasses = &bsp;
    VK(vkCreateRenderPass(m_dev, &brpc, nullptr, &m_rp));

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipLodBias = 0.0f;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    sci.compareEnable = VK_FALSE;
    sci.compareOp = VK_COMPARE_OP_ALWAYS;
    sci.minLod = 0.0f;
    sci.maxLod = 0.0f;
    sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sci.unnormalizedCoordinates = VK_FALSE;
    VK(vkCreateSampler(m_dev, &sci, nullptr, &m_hdrSampler));

    recreateSwapchain();

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
    destroySwapchainResources();
    vkDestroyRenderPass(m_dev, m_rp, nullptr);
    vkDestroyRenderPass(m_dev, m_rpScene, nullptr);
    vkDestroySampler(m_dev, m_hdrSampler, nullptr);
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

void VkApp::pollEvents() {
    glfwPollEvents();
    int w, h;
    glfwGetFramebufferSize(m_win, &w, &h);
    if (w > 0 && h > 0 && ((uint32_t)w != m_extent.width || (uint32_t)h != m_extent.height))
        recreateSwapchain();
}

bool VkApp::keyIsDown(int key) const {
    return glfwGetKey(m_win, key) == GLFW_PRESS;
}

uint32_t VkApp::acquireNextImage() {
    uint32_t idx = 0;
    const VkResult r = vkAcquireNextImageKHR(m_dev, m_sc, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &idx);
    checkResult(r, "acquire");
    return idx;
}

VkCommandBuffer VkApp::beginCommands() {
    VK(vkResetCommandBuffer(m_cmd, 0));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK(vkBeginCommandBuffer(m_cmd, &cbi));
    return m_cmd;
}

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
    checkResult(vkQueuePresentKHR(m_queue, &pi), "present");
}

void VkApp::recreateSwapchain() {
    VK(vkDeviceWaitIdle(m_dev));
    uint32_t nImg = (uint32_t)m_images.size();
    uint32_t drain;
    for (uint32_t i = 0; i < nImg; ++i)
        vkAcquireNextImageKHR(m_dev, m_sc, 1000000000ull, VK_NULL_HANDLE, VK_NULL_HANDLE, &drain);
    destroySwapchainResources();

    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_pdev, m_surface, &caps));
    m_extent = caps.currentExtent;
    if (m_extent.width == 0xFFFFFFFF) {
        int w, h;
        glfwGetFramebufferSize(m_win, &w, &h);
        m_extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
    if (m_extent.width == 0)
        m_extent.width = 1;
    if (m_extent.height == 0)
        m_extent.height = 1;

    uint32_t icnt = caps.minImageCount + 1;
    if (caps.maxImageCount && icnt > caps.maxImageCount)
        icnt = caps.maxImageCount;
    VkSwapchainCreateInfoKHR scc{};
    scc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scc.surface = m_surface;
    scc.minImageCount = icnt;
    scc.imageFormat = m_fmt.format;
    scc.imageColorSpace = m_fmt.colorSpace;
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

    createDepth(m_extent);
    createHDR(m_extent);

    VkImageView satts[2] = {m_hdrView, m_depthView};
    VkFramebufferCreateInfo sfci{};
    sfci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    sfci.renderPass = m_rpScene;
    sfci.attachmentCount = 2;
    sfci.pAttachments = satts;
    sfci.width = m_extent.width;
    sfci.height = m_extent.height;
    sfci.layers = 1;
    VK(vkCreateFramebuffer(m_dev, &sfci, nullptr, &m_sceneFb));

    m_views.resize(icnt);
    m_fbs.resize(icnt);
    for (uint32_t i = 0; i < icnt; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = m_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = m_fmt.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(m_dev, &vci, nullptr, &m_views[i]));
        VkImageView fatts[1] = {m_views[i]};
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_rp;
        fci.attachmentCount = 1;
        fci.pAttachments = fatts;
        fci.width = m_extent.width;
        fci.height = m_extent.height;
        fci.layers = 1;
        VK(vkCreateFramebuffer(m_dev, &fci, nullptr, &m_fbs[i]));
    }
}

void VkApp::destroySwapchainResources() {
    for (auto* fb : m_fbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    if (m_sceneFb)
        vkDestroyFramebuffer(m_dev, m_sceneFb, nullptr);
    for (auto* v : m_views)
        vkDestroyImageView(m_dev, v, nullptr);
    if (m_sc)
        vkDestroySwapchainKHR(m_dev, m_sc, nullptr);
    vkDestroyImageView(m_dev, m_depthView, nullptr);
    vkDestroyImage(m_dev, m_depth, nullptr);
    vkFreeMemory(m_dev, m_depthMem, nullptr);
    vkDestroyImageView(m_dev, m_hdrView, nullptr);
    vkDestroyImage(m_dev, m_hdr, nullptr);
    vkFreeMemory(m_dev, m_hdrMem, nullptr);
    m_fbs.clear();
    m_views.clear();
    m_sc = VK_NULL_HANDLE;
    m_depth = VK_NULL_HANDLE;
    m_depthMem = VK_NULL_HANDLE;
    m_depthView = VK_NULL_HANDLE;
    m_sceneFb = VK_NULL_HANDLE;
    m_hdr = VK_NULL_HANDLE;
    m_hdrMem = VK_NULL_HANDLE;
    m_hdrView = VK_NULL_HANDLE;
}

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

void VkApp::createHDR(VkExtent2D ext) {
    VkImageCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    dci.imageType = VK_IMAGE_TYPE_2D;
    dci.extent = {ext.width, ext.height, 1};
    dci.mipLevels = 1;
    dci.arrayLayers = 1;
    dci.format = m_hdrFmt;
    dci.tiling = VK_IMAGE_TILING_OPTIMAL;
    dci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    dci.samples = VK_SAMPLE_COUNT_1_BIT;
    dci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK(vkCreateImage(m_dev, &dci, nullptr, &m_hdr));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_dev, m_hdr, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(m_pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(m_dev, &maa, nullptr, &m_hdrMem));
    VK(vkBindImageMemory(m_dev, m_hdr, m_hdrMem, 0));
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = m_hdr;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = m_hdrFmt;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(m_dev, &vci, nullptr, &m_hdrView));
}
