#include "vk/vkapp.hpp"

#include "vk/vkutil.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

bool VkApp::initInstance(int width, int height, const char* title) {
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
    std::vector<const char*> exts(req, req + n);
    uint32_t ne = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ne, nullptr);
    std::vector<VkExtensionProperties> iexts(ne);
    if (ne > 0)
        vkEnumerateInstanceExtensionProperties(nullptr, &ne, iexts.data());
    const char* want[] = {"VK_KHR_external_memory_capabilities", "VK_KHR_external_semaphore_capabilities",
                          "VK_KHR_portability_enumeration"};
    for (const char* w : want)
        if (std::any_of(iexts.begin(), iexts.end(),
                        [&](const VkExtensionProperties& p) { return std::string_view(p.extensionName) == w; }))
            exts.push_back(w);
    std::vector<const char*> layers;
    if (std::getenv("VK_VALIDATE")) {
        uint32_t nl = 0;
        vkEnumerateInstanceLayerProperties(&nl, nullptr);
        std::vector<VkLayerProperties> lprops(nl);
        vkEnumerateInstanceLayerProperties(&nl, lprops.data());
        for (const auto& p : lprops)
            if (std::string_view(p.layerName) == "VK_LAYER_KHRONOS_validation")
                layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = title;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{};
    ic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ic.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = (uint32_t)exts.size();
    ic.ppEnabledExtensionNames = exts.data();
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
    {
        VkPhysicalDeviceProperties pp{};
        vkGetPhysicalDeviceProperties(m_pdev, &pp);
        std::printf("3dsim: gpu %s (%s)\n", pp.deviceName,
                    pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete" : "integrated");
    }

    m_qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    m_qci.queueFamilyIndex = m_qf;
    m_qci.queueCount = 1;
    m_qci.pQueuePriorities = &m_qprio;

    uint32_t nde = 0;
    vkEnumerateDeviceExtensionProperties(m_pdev, nullptr, &nde, nullptr);
    std::vector<VkExtensionProperties> deexts(nde);
    if (nde > 0)
        vkEnumerateDeviceExtensionProperties(m_pdev, nullptr, &nde, deexts.data());
    const char* dewant[] = {"VK_KHR_swapchain", "VK_KHR_external_memory_fd", "VK_KHR_external_semaphore_fd"};
    for (const char* w : dewant)
        if (std::any_of(deexts.begin(), deexts.end(),
                        [&](const VkExtensionProperties& p) { return std::string_view(p.extensionName) == w; }))
            m_devExt.push_back(w);

    m_dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    m_dci.queueCreateInfoCount = 1;
    m_dci.pQueueCreateInfos = &m_qci;
    m_dci.enabledExtensionCount = (uint32_t)m_devExt.size();
    m_dci.ppEnabledExtensionNames = m_devExt.data();
    return true;
}

bool VkApp::initHeadless() {
    uint32_t ne = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ne, nullptr);
    std::vector<VkExtensionProperties> iexts(ne);
    if (ne > 0)
        vkEnumerateInstanceExtensionProperties(nullptr, &ne, iexts.data());
    const char* want[] = {"VK_KHR_external_memory_capabilities", "VK_KHR_external_semaphore_capabilities"};
    std::vector<const char*> exts;
    for (const char* w : want)
        if (std::any_of(iexts.begin(), iexts.end(),
                        [&](const VkExtensionProperties& p) { return std::string_view(p.extensionName) == w; }))
            exts.push_back(w);
    std::vector<const char*> layers;
    if (std::getenv("VK_VALIDATE")) {
        uint32_t nl = 0;
        vkEnumerateInstanceLayerProperties(&nl, nullptr);
        std::vector<VkLayerProperties> lprops(nl);
        vkEnumerateInstanceLayerProperties(&nl, lprops.data());
        for (const auto& p : lprops)
            if (std::string_view(p.layerName) == "VK_LAYER_KHRONOS_validation")
                layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "3dsim";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{};
    ic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = (uint32_t)exts.size();
    ic.ppEnabledExtensionNames = exts.data();
    ic.enabledLayerCount = (uint32_t)layers.size();
    ic.ppEnabledLayerNames = layers.data();
    VK(vkCreateInstance(&ic, nullptr, &m_inst));

    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(m_inst, &nd, nullptr);
    std::vector<VkPhysicalDevice> devs(nd);
    vkEnumeratePhysicalDevices(m_inst, &nd, devs.data());
    for (auto* d : devs) {
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qps(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qps.data());
        for (uint32_t i = 0; i < qc; ++i)
            if (qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_pdev = d;
                m_qf = i;
                break;
            }
        if (m_pdev)
            break;
    }
    if (!m_pdev) {
        std::fprintf(stderr, "no graphics device\\n");
        vkDestroyInstance(m_inst, nullptr);
        return false;
    }
    {
        VkPhysicalDeviceProperties pp{};
        vkGetPhysicalDeviceProperties(m_pdev, &pp);
        std::printf("3dsim: gpu %s (%s)\\n", pp.deviceName,
                    pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete" : "integrated");
    }

    m_qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    m_qci.queueFamilyIndex = m_qf;
    m_qci.queueCount = 1;
    m_qci.pQueuePriorities = &m_qprio;

    uint32_t nde = 0;
    vkEnumerateDeviceExtensionProperties(m_pdev, nullptr, &nde, nullptr);
    std::vector<VkExtensionProperties> deexts(nde);
    if (nde > 0)
        vkEnumerateDeviceExtensionProperties(m_pdev, nullptr, &nde, deexts.data());
    const char* dewant[] = {"VK_KHR_external_memory_fd", "VK_KHR_external_semaphore_fd"};
    for (const char* w : dewant)
        if (std::any_of(deexts.begin(), deexts.end(),
                        [&](const VkExtensionProperties& p) { return std::string_view(p.extensionName) == w; }))
            m_devExt.push_back(w);

    m_dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    m_dci.queueCreateInfoCount = 1;
    m_dci.pQueueCreateInfos = &m_qci;
    m_dci.enabledExtensionCount = (uint32_t)m_devExt.size();
    m_dci.ppEnabledExtensionNames = m_devExt.data();
    makeDevice();

    vkGetDeviceQueue(m_dev, m_qf, 0, &m_queue);
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

VkDevice VkApp::makeDevice() {
    VkPhysicalDeviceFeatures feat{};
    feat.imageCubeArray = VK_TRUE;
    feat.samplerAnisotropy = VK_TRUE;
    feat.vertexPipelineStoresAndAtomics = VK_TRUE;
    VkPhysicalDeviceMaintenance8FeaturesKHR m8{};
    m8.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR;
    m8.maintenance8 = VK_TRUE;
    m_dci.pNext = &m8;
    m_dci.pEnabledFeatures = &feat;
    VK(vkCreateDevice(m_pdev, &m_dci, nullptr, &m_dev));
    return m_dev;
}

bool VkApp::initDevice(VkDevice dev) {
    m_dev = dev;
    vkGetDeviceQueue(m_dev, m_qf, 0, &m_queue);

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_pdev, m_surface, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(nf);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_pdev, m_surface, &nf, fmts.data());
    // format list order is driver-specific (nvidia UNORM first, radv SRGB first); the tonemap encodes sRGB itself, an sRGB swapchain would double-encode
    m_fmt = fmts[0];
    for (const auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
            m_fmt = f;
    m_scfmt = m_fmt.format;

    VkAttachmentDescription satt[4]{};
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
    satt[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // STORE: the noClear (glass/blend) pass LOADs this depth
    satt[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    satt[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    satt[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    satt[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    satt[2].format = m_hdrFmt;
    satt[2].samples = VK_SAMPLE_COUNT_1_BIT;
    satt[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    satt[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    satt[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    satt[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    satt[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    satt[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    satt[3].format = m_hdrFmt;
    satt[3].samples = VK_SAMPLE_COUNT_1_BIT;
    satt[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    satt[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    satt[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    satt[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    satt[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    satt[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference scolor[3] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                      {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                      {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    const VkAttachmentReference sdepth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription ssp{};
    ssp.colorAttachmentCount = 3;
    ssp.pColorAttachments = scolor;
    ssp.pDepthStencilAttachment = &sdepth;
    VkRenderPassCreateInfo srpc{};
    srpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    srpc.attachmentCount = 4;
    srpc.pAttachments = satt;
    srpc.subpassCount = 1;
    srpc.pSubpasses = &ssp;
    VK(vkCreateRenderPass(m_dev, &srpc, nullptr, &m_rpScene));

    VkAttachmentDescription natt[4]{};
    natt[0].format = m_hdrFmt;
    natt[0].samples = VK_SAMPLE_COUNT_1_BIT;
    natt[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    natt[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    natt[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    natt[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    natt[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    natt[1].format = VK_FORMAT_D32_SFLOAT;
    natt[1].samples = VK_SAMPLE_COUNT_1_BIT;
    natt[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    natt[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    natt[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    natt[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    natt[2].format = m_hdrFmt;
    natt[2].samples = VK_SAMPLE_COUNT_1_BIT;
    natt[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    natt[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    natt[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // SSS-blurred to GENERAL, or untouched (color-attachment)
    natt[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    natt[3].format = m_hdrFmt;
    natt[3].samples = VK_SAMPLE_COUNT_1_BIT;
    natt[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    natt[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    natt[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    natt[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // SSS-blurred to GENERAL, or untouched (color-attachment)
    natt[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ncolor[3] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                      {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                      {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    const VkAttachmentReference ndepth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription nss{};
    nss.colorAttachmentCount = 3;
    nss.pColorAttachments = ncolor;
    nss.pDepthStencilAttachment = &ndepth;
    VkRenderPassCreateInfo nrpc{};
    nrpc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    nrpc.attachmentCount = 4;
    nrpc.pAttachments = natt;
    nrpc.subpassCount = 1;
    nrpc.pSubpasses = &nss;
    VK(vkCreateRenderPass(m_dev, &nrpc, nullptr, &m_rpSceneNoClear));

    VkAttachmentDescription batt[1]{};
    batt[0].format = m_fmt.format;
    batt[0].samples = VK_SAMPLE_COUNT_1_BIT;
    batt[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    batt[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    batt[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    batt[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    batt[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    batt[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    m_rp = makeSimpleRenderPass(m_dev, batt[0]);

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
    VK(vkCreateFence(m_dev, &fci, nullptr, &m_acqFence));
    return true;
}

void VkApp::shutdown() {
    VK(vkDeviceWaitIdle(m_dev));
    destroySwapchainResources();
    vkDestroyRenderPass(m_dev, m_rp, nullptr);
    vkDestroyRenderPass(m_dev, m_rpScene, nullptr);
    vkDestroyRenderPass(m_dev, m_rpSceneNoClear, nullptr);
    vkDestroySampler(m_dev, m_hdrSampler, nullptr);
    vkDestroyFence(m_dev, m_acqFence, nullptr);
    vkDestroyFence(m_dev, m_fence, nullptr);
    vkDestroyCommandPool(m_dev, m_pool, nullptr);
    vkDestroyDevice(m_dev, nullptr);
    if (m_surface)
        vkDestroySurfaceKHR(m_inst, m_surface, nullptr);
    vkDestroyInstance(m_inst, nullptr);
    if (m_win)
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
    if (w > 0 && h > 0)
        m_pendingExtent = {(uint32_t)w, (uint32_t)h};
}

void VkApp::resizeBuffers() {
    if (m_pendingExtent.width != m_extent.width || m_pendingExtent.height != m_extent.height)
        recreateSwapchain();
}

uint32_t VkApp::acquireNextImage() {
    for (;;) {
        uint32_t idx = 0;
        // finite timeout: a stuck acquire (e.g. mid-resize) must not block forever
        const VkResult r = vkAcquireNextImageKHR(m_dev, m_sc, 1000000000ull, VK_NULL_HANDLE, m_acqFence, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_TIMEOUT) {
            // rebuild before the fence wait: a failed acquire never signaled that fence
            recreateSwapchain();
            continue;
        }
        vkCheck(r, "acquire");
        VK(vkWaitForFences(m_dev, 1, &m_acqFence, VK_TRUE, 30000000000ull));
        VK(vkResetFences(m_dev, 1, &m_acqFence));
        return idx;
    }
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

void VkApp::present(uint32_t idx, bool wait) {
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 0;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_sc;
    pi.pImageIndices = &idx;
    vkCheck(vkQueuePresentKHR(m_queue, &pi), "present");
    if (wait)
        VK(vkQueueWaitIdle(m_queue));
}

void VkApp::recreateSwapchain() {
    VK(vkDeviceWaitIdle(m_dev));
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
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (std::getenv("NO_VSYNC")) {
        uint32_t nModes = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_pdev, m_surface, &nModes, nullptr);
        std::vector<VkPresentModeKHR> modes(nModes);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_pdev, m_surface, &nModes, modes.data());
        const VkPresentModeKHR prefs[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
                                          VK_PRESENT_MODE_FIFO_RELAXED_KHR};
        for (VkPresentModeKHR p : prefs)
            if (std::find(modes.begin(), modes.end(), p) != modes.end()) {
                presentMode = p;
                break;
            }
        std::printf("3dsim: NO_VSYNC -> present mode %d (%d supported)\n", (int)presentMode, (int)nModes);
    }
    scc.presentMode = presentMode;
    scc.clipped = VK_TRUE;
    VK(vkCreateSwapchainKHR(m_dev, &scc, nullptr, &m_sc));
    vkGetSwapchainImagesKHR(m_dev, m_sc, &icnt, nullptr);
    m_images.resize(icnt);
    vkGetSwapchainImagesKHR(m_dev, m_sc, &icnt, m_images.data());

    uint32_t mips = 1; // mip chain for the glass roughness blur (mip level = rough * 8)
    for (uint32_t s = std::max(m_extent.width, m_extent.height); s > 1; s >>= 1)
        ++mips;
    m_sceneColorMips = mips;

    createAttachment(m_extent, VK_FORMAT_D32_SFLOAT,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, m_depth, m_depthMem, m_depthView);
    createAttachment(m_extent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, m_depthCopy, m_depthCopyMem, m_depthCopyView);
    createAttachment(m_extent, m_hdrFmt,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, m_sceneColor, m_sceneColorMem, m_sceneColorView, mips);
    m_sceneColorAttView = makeImageView(m_dev, m_sceneColor, m_hdrFmt);
    createAttachment(m_extent, m_hdrFmt, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, m_sceneColorTemp, m_sceneColorTempMem, m_sceneColorTempView);
    createAttachment(m_extent, m_hdrFmt,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, m_sssDiffuse, m_sssDiffuseMem, m_sssDiffuseView);
    createAttachment(m_extent, m_hdrFmt,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, m_sssWeight, m_sssWeightMem, m_sssWeightView);
    createAttachment(m_extent, m_hdrFmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, m_hdr, m_hdrMem, m_hdrView);

    VkImageView satts[4] = {m_sceneColorAttView, m_depthView, m_sssDiffuseView, m_sssWeightView};
    makeFramebuffer(m_dev, m_rpScene, m_extent.width, m_extent.height, satts, 4, m_sceneFb);

    VkImageView natts[4] = {m_hdrView, m_depthView, m_sssDiffuseView, m_sssWeightView};
    makeFramebuffer(m_dev, m_rpSceneNoClear, m_extent.width, m_extent.height, natts, 4, m_sceneFbNoClear);

    m_views.resize(icnt);
    m_fbs.resize(icnt);
    for (uint32_t i = 0; i < icnt; ++i) {
        m_views[i] = makeImageView(m_dev, m_images[i], m_fmt.format);
        makeFramebuffer(m_dev, m_rp, m_extent.width, m_extent.height, &m_views[i], 1, m_fbs[i]);
    }
}

void VkApp::destroySwapchainResources() {
    for (auto* fb : m_fbs)
        vkDestroyFramebuffer(m_dev, fb, nullptr);
    vkFree(m_dev, &m_sceneFb, vkDestroyFramebuffer);
    for (auto* v : m_views)
        vkDestroyImageView(m_dev, v, nullptr);
    vkFree(m_dev, &m_sc, vkDestroySwapchainKHR);
    vkFree(m_dev, &m_sceneFbNoClear, vkDestroyFramebuffer);
    vkDestroyImageView(m_dev, m_depthView, nullptr);
    vkDestroyImage(m_dev, m_depth, nullptr);
    vkFreeMemory(m_dev, m_depthMem, nullptr);
    vkDestroyImageView(m_dev, m_depthCopyView, nullptr);
    vkDestroyImage(m_dev, m_depthCopy, nullptr);
    vkFreeMemory(m_dev, m_depthCopyMem, nullptr);
    vkDestroyImageView(m_dev, m_sceneColorAttView, nullptr);
    vkDestroyImageView(m_dev, m_sceneColorView, nullptr);
    vkDestroyImage(m_dev, m_sceneColor, nullptr);
    vkFreeMemory(m_dev, m_sceneColorMem, nullptr);
    vkDestroyImageView(m_dev, m_sceneColorTempView, nullptr);
    vkDestroyImage(m_dev, m_sceneColorTemp, nullptr);
    vkFreeMemory(m_dev, m_sceneColorTempMem, nullptr);
    vkDestroyImageView(m_dev, m_sssDiffuseView, nullptr);
    vkDestroyImage(m_dev, m_sssDiffuse, nullptr);
    vkFreeMemory(m_dev, m_sssDiffuseMem, nullptr);
    vkDestroyImageView(m_dev, m_sssWeightView, nullptr);
    vkDestroyImage(m_dev, m_sssWeight, nullptr);
    vkFreeMemory(m_dev, m_sssWeightMem, nullptr);
    vkDestroyImageView(m_dev, m_hdrView, nullptr);
    vkDestroyImage(m_dev, m_hdr, nullptr);
    vkFreeMemory(m_dev, m_hdrMem, nullptr);
    m_fbs.clear();
    m_views.clear();
    m_sc = VK_NULL_HANDLE;
    m_depth = VK_NULL_HANDLE;
    m_depthMem = VK_NULL_HANDLE;
    m_depthView = VK_NULL_HANDLE;
    m_depthCopy = VK_NULL_HANDLE;
    m_depthCopyMem = VK_NULL_HANDLE;
    m_depthCopyView = VK_NULL_HANDLE;
    m_sceneFb = VK_NULL_HANDLE;
    m_sceneFbNoClear = VK_NULL_HANDLE;
    m_sceneColor = VK_NULL_HANDLE;
    m_sceneColorMem = VK_NULL_HANDLE;
    m_sceneColorAttView = VK_NULL_HANDLE;
    m_sceneColorView = VK_NULL_HANDLE;
    m_sceneColorTemp = VK_NULL_HANDLE;
    m_sceneColorTempMem = VK_NULL_HANDLE;
    m_sceneColorTempView = VK_NULL_HANDLE;
    m_sssDiffuse = VK_NULL_HANDLE;
    m_sssDiffuseMem = VK_NULL_HANDLE;
    m_sssDiffuseView = VK_NULL_HANDLE;
    m_sssWeight = VK_NULL_HANDLE;
    m_sssWeightMem = VK_NULL_HANDLE;
    m_sssWeightView = VK_NULL_HANDLE;
    m_hdr = VK_NULL_HANDLE;
    m_hdrMem = VK_NULL_HANDLE;
    m_hdrView = VK_NULL_HANDLE;
}

void VkApp::createAttachment(VkExtent2D ext, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlagBits aspect,
                             VkImage& img, VkDeviceMemory& mem, VkImageView& view, uint32_t mipLevels) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent = {ext.width, ext.height, 1};
    ici.mipLevels = mipLevels;
    ici.arrayLayers = 1;
    ici.format = fmt;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = usage;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK(vkCreateImage(m_dev, &ici, nullptr, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_dev, img, &mr);
    VkMemoryAllocateInfo maa{};
    maa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    maa.allocationSize = mr.size;
    maa.memoryTypeIndex = vkFindMemoryType(m_pdev, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(m_dev, &maa, nullptr, &mem));
    VK(vkBindImageMemory(m_dev, img, mem, 0));
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = {aspect, 0, mipLevels, 0, 1};
    VK(vkCreateImageView(m_dev, &vci, nullptr, &view));
}
