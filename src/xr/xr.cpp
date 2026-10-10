#include "xr/xr.hpp"

#include "vk/vkapp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

bool xrok(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r))
        return true;
    std::fprintf(stderr, "xr %s: error %d\n", what, (int)r);
    return false;
}

// mirrored to a file so it stays readable when the app is launched via Steam (console hidden)
constexpr const char* kXrDiagPath = "/tmp/3dsim_xr_diag.log";

void diagReset() {
    if (FILE* f = std::fopen(kXrDiagPath, "w"))
        std::fclose(f);
}

// append one line to both stdout and the diagnostic file
void diagAppend(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::printf("%s", buf);
    if (FILE* f = std::fopen(kXrDiagPath, "a")) {
        std::fprintf(f, "%s", buf);
        std::fclose(f);
    }
}

void xrafail(XrResult r, const char* what) {
    if (!xrok(r, what))
        std::abort();
}

// prefer plain UNORM (the tonemap pass writes sRGB-encoded values)
VkFormat pickFormat(int64_t* fmts, uint32_t n) {
    const int64_t want[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM,
                            VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
    for (int64_t w : want)
        for (uint32_t i = 0; i < n; ++i)
            if (fmts[i] == w)
                return (VkFormat)w;
    return n > 0 ? (VkFormat)fmts[0] : VK_FORMAT_UNDEFINED;
}

} // namespace

bool Xr::createDevice(VkApp& app) {
    if (!m_loader.load()) {
        std::printf("xr: vendored loader not found (lib/openxr/lib/libopenxr.so.1)\n");
        return false;
    }
    if (!createInstance())
        return false;
    createShadowInstance();
    queryRuntimeVulkan(app);
    return createVulkanDevice(app);
}

bool Xr::createInstance() {
    XrApplicationInfo ai{};
    std::snprintf(ai.applicationName, sizeof(ai.applicationName), "3dsim");
    ai.applicationVersion = 1;
    std::snprintf(ai.engineName, sizeof(ai.engineName), "3dsim");
    ai.engineVersion = 1;
    // SteamVR's OpenXR runtime (2.18.x) rejects apiVersion 1.1; request 1.0
    ai.apiVersion = XR_API_VERSION_1_0;
    static const char* kExts[] = {"XR_KHR_vulkan_enable2"};
    XrInstanceCreateInfo ici{};
    ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ici.applicationInfo = ai;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = kExts;
    if (!xrok(m_loader.xrCreateInstance(&ici, &m_inst), "createInstance"))
        return false;

    XrSystemGetInfo sg{};
    sg.type = XR_TYPE_SYSTEM_GET_INFO;
    sg.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(m_loader.xrGetSystem(m_inst, &sg, &m_system)) || m_system == XR_NULL_SYSTEM_ID) {
        std::printf("xr: no head-mounted display\n");
        return false;
    }
    XrSystemProperties sp{};
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (XR_SUCCEEDED(m_loader.xrGetSystemProperties(m_inst, m_system, &sp)))
        std::printf("xr: %s (vendor %u)\n", sp.systemName, sp.vendorId);
    return true;
}

void Xr::createShadowInstance() {
    // SteamVR refuses xrCreateSession unless the vulkan-enable2 requirements/device
    // queries happened first (mirrors the sequence a conforming app performs). The
    // device query needs our vkGetInstanceProcAddr, which the runtime only gets if an
    // instance was created through xrCreateVulkanInstanceKHR, so make a shadow one.
    PFN_xrCreateVulkanInstanceKHR createVkJ = nullptr;
    m_loader.xrGetInstanceProcAddr(m_inst, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction*)&createVkJ);
    if (createVkJ) {
        VkApplicationInfo vapp{};
        vapp.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        vapp.pApplicationName = "3dsim";
        vapp.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo vic{};
        vic.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        vic.pApplicationInfo = &vapp;
        XrVulkanInstanceCreateInfoKHR xv{};
        xv.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
        xv.systemId = m_system;
        xv.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xv.vulkanCreateInfo = &vic;
        VkResult vkr = VK_SUCCESS;
        if (XR_FAILED(createVkJ(m_inst, &xv, &m_shadowInst, &vkr)) || vkr != VK_SUCCESS) {
            std::printf("xr: xrCreateVulkanInstanceKHR failed (vk %d)\n", (int)vkr);
            m_shadowInst = VK_NULL_HANDLE;
        }
    } else {
        std::printf("xr: xrCreateVulkanInstanceKHR not available\n");
    }
}

void Xr::queryRuntimeVulkan(VkApp& app) {
    PFN_xrGetVulkanGraphicsRequirements2KHR getReqs = nullptr;
    m_loader.xrGetInstanceProcAddr(m_inst, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction*)&getReqs);
    if (getReqs) {
        XrGraphicsRequirementsVulkan2KHR reqs{};
        reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
        if (XR_SUCCEEDED(getReqs(m_inst, m_system, &reqs)))
            std::printf("xr: vulkan reqs %u..%u\n", reqs.minApiVersionSupported, reqs.maxApiVersionSupported);
    } else {
        std::printf("xr: xrGetVulkanGraphicsRequirements2KHR not available\n");
    }
    PFN_xrGetVulkanGraphicsDevice2KHR getDev = nullptr;
    m_loader.xrGetInstanceProcAddr(m_inst, "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction*)&getDev);
    if (getDev) {
        XrVulkanGraphicsDeviceGetInfoKHR gi{};
        gi.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
        gi.systemId = m_system;
        gi.vulkanInstance = m_shadowInst != VK_NULL_HANDLE ? m_shadowInst : app.instance();
        VkPhysicalDevice pdev = VK_NULL_HANDLE;
        if (XR_FAILED(getDev(m_inst, &gi, &pdev)))
            std::printf("xr: xrGetVulkanGraphicsDevice2KHR failed\n");
    } else {
        std::printf("xr: xrGetVulkanGraphicsDevice2KHR not available\n");
    }
}

bool Xr::createVulkanDevice(VkApp& app) {
    // Create the device through the runtime so the compositor registers our pfn+device and
    // resolves 1.1 core fns (vkGet*MemoryRequirements2KHR) via instance-proc instead of
    // device-proc (which returns NULL for the KHR alias and crashes the compositor).
    PFN_xrCreateVulkanDeviceKHR createDevJ = nullptr;
    m_loader.xrGetInstanceProcAddr(m_inst, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction*)&createDevJ);
    if (!createDevJ) {
        std::printf("xr: xrCreateVulkanDeviceKHR not available\n");
        return false;
    }
    {
        VkPhysicalDeviceProperties pp{};
        vkGetPhysicalDeviceProperties(app.pdev(), &pp);
        std::printf("xr: device on '%s' (api %u.%u)\n", pp.deviceName, VK_VERSION_MAJOR(pp.apiVersion),
                    VK_VERSION_MINOR(pp.apiVersion));
    }
    XrVulkanDeviceCreateInfoKHR xd{};
    xd.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
    xd.systemId = m_system;
    xd.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xd.vulkanPhysicalDevice = app.pdev();
    xd.vulkanCreateInfo = &app.deviceCreateInfo();
    VkResult vdr = VK_SUCCESS;
    if (XR_FAILED(createDevJ(m_inst, &xd, &m_dev, &vdr)) || vdr != VK_SUCCESS || m_dev == VK_NULL_HANDLE) {
        std::printf("xr: xrCreateVulkanDeviceKHR failed (vk %d)\n", (int)vdr);
        return false;
    }
    return true;
}

bool Xr::init(VkApp& app) {
    if (m_inst == XR_NULL_HANDLE || m_dev == VK_NULL_HANDLE)
        return false;
    XrGraphicsBindingVulkanKHR vk{};
    vk.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
    vk.instance = m_shadowInst != VK_NULL_HANDLE ? m_shadowInst : app.instance();
    vk.physicalDevice = app.pdev();
    vk.device = m_dev;
    vk.queueFamilyIndex = app.queueFamily();
    vk.queueIndex = 0;
    XrSessionCreateInfo sc{};
    sc.type = XR_TYPE_SESSION_CREATE_INFO;
    sc.next = &vk;
    sc.systemId = m_system;
    if (!xrok(m_loader.xrCreateSession(m_inst, &sc, &m_session), "createSession"))
        return false;

    XrEnvironmentBlendMode modes[8];
    uint32_t nm = 0;
    if (XR_SUCCEEDED(m_loader.xrEnumerateEnvironmentBlendModes(
            m_inst, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &nm, modes)) &&
        nm > 0)
        m_blend = modes[0];

    uint32_t nv = 0;
    if (XR_FAILED(m_loader.xrEnumerateViewConfigurationViews(
            m_inst, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, nullptr)) ||
        nv == 0) {
        std::printf("xr: no stereo views\n");
        return false;
    }
    std::vector<XrViewConfigurationView> vcs(nv);
    for (auto& v : vcs) {
        v.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        v.next = nullptr;
    }
    xrafail(m_loader.xrEnumerateViewConfigurationViews(m_inst, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nv,
                                                       &nv, vcs.data()),
            "enumerateViews");
    m_eyeCount = (int)std::min<uint32_t>(nv, 2);

    int64_t fmts[256];
    uint32_t nf = 0;
    xrafail(m_loader.xrEnumerateSwapchainFormats(m_session, 256, &nf, fmts), "enumerateFormats");
    m_fmt = pickFormat(fmts, nf);
    if (m_fmt == VK_FORMAT_UNDEFINED) {
        std::printf("xr: no swapchain format\n");
        return false;
    }

    for (int i = 0; i < m_eyeCount; ++i) {
        Swap& s = m_swaps[i];
        s.extent = {vcs[i].recommendedImageRectWidth, vcs[i].recommendedImageRectHeight};
        XrSwapchainCreateInfo c{};
        c.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        c.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        c.format = (int64_t)m_fmt;
        c.sampleCount = 1;
        c.width = s.extent.width;
        c.height = s.extent.height;
        c.faceCount = 1;
        c.arraySize = 1;
        c.mipCount = 1;
        if (!xrok(m_loader.xrCreateSwapchain(m_session, &c, &s.sc), "createSwapchain"))
            return false;
        uint32_t ni = 0;
        xrafail(m_loader.xrEnumerateSwapchainImages(s.sc, 0, &ni, nullptr), "enumerateImages");
        // the runtime writes the full Vulkan image struct (24B), not the 16B base header
        std::vector<XrSwapchainImageVulkanKHR> imgs(ni);
        for (auto& v : imgs)
            v.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
        xrafail(m_loader.xrEnumerateSwapchainImages(s.sc, ni, &ni,
                                                    reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data())),
                "enumerateImages");
        s.images.resize(ni);
        for (uint32_t k = 0; k < ni; ++k)
            s.images[k] = imgs[k].image;
        m_rects[i] = {0, 0, (int32_t)s.extent.width, (int32_t)s.extent.height};
    }

    XrReferenceSpaceCreateInfo rs{};
    rs.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    // STAGE: origin at the floor, centered on the SteamVR play space (a configured room-scale
    // play area is required). Gives a floor + room origin so the user stands on the floor.
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    rs.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};
    rs.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    const XrResult res = m_loader.xrCreateReferenceSpace(m_session, &rs, &m_refSpace);
    if (!xrok(res, "createReferenceSpace(STAGE)")) {
        std::fprintf(stderr, "xr: STAGE needs a SteamVR play space (SteamVR > Play Area). Relaunch.\n");
        return false;
    }

    initActions();

    std::printf("xr: %d eyes %ux%u format=%d blend=%d\n", m_eyeCount, m_swaps[0].extent.width, m_swaps[0].extent.height,
                (int)m_fmt, (int)m_blend);
    return true;
}

void Xr::initActions() {
    if (m_session == XR_NULL_HANDLE)
        return;
    diagReset();
    if (XR_FAILED(m_loader.xrStringToPath(m_inst, "/user/hand/left", &m_hand[0])) ||
        XR_FAILED(m_loader.xrStringToPath(m_inst, "/user/hand/right", &m_hand[1]))) {
        std::printf("xr: stringToPath hand failed\n");
        return;
    }
    XrActionSetCreateInfo asec{};
    asec.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    std::snprintf(asec.actionSetName, sizeof(asec.actionSetName), "main");
    std::snprintf(asec.localizedActionSetName, sizeof(asec.localizedActionSetName), "main");
    asec.priority = 0;
    if (XR_FAILED(m_loader.xrCreateActionSet(m_inst, &asec, &m_actionSet))) {
        std::printf("xr: createActionSet failed\n");
        return;
    }
    auto mk = [&](XrAction& a, const char* name, XrActionType t) {
        XrActionCreateInfo ci{};
        ci.type = XR_TYPE_ACTION_CREATE_INFO;
        std::snprintf(ci.actionName, sizeof(ci.actionName), "%s", name);
        std::snprintf(ci.localizedActionName, sizeof(ci.localizedActionName), "%s", name);
        ci.actionType = t;
        ci.countSubactionPaths = 2;
        ci.subactionPaths = m_hand;
        return m_loader.xrCreateAction(m_actionSet, &ci, &a);
    };
    if (XR_FAILED(mk(m_gripPose, "grip", XR_ACTION_TYPE_POSE_INPUT)) ||
        XR_FAILED(mk(m_aimPose, "aim", XR_ACTION_TYPE_POSE_INPUT)) ||
        XR_FAILED(mk(m_trigger, "trigger", XR_ACTION_TYPE_FLOAT_INPUT)) ||
        XR_FAILED(mk(m_stick, "stick", XR_ACTION_TYPE_VECTOR2F_INPUT))) {
        std::printf("xr: createAction failed\n");
        return;
    }
    // Suggest bindings per interaction profile; each list only contains paths valid for that
    // profile (a single unsupported path rejects the whole suggestion). SteamVR matches the
    // connected hardware to whichever profile it supports and skips the rest.
    // IMPORTANT: this must run before attaching the action set, or the runtime rejects it with
    // XR_ERROR_ACTIONSETS_ALREADY_ATTACHED.
    struct Bind {
        XrAction action;
        const char* leaf;
    };
    auto suggest = [&](const char* profile, const std::initializer_list<Bind>& binds) {
        XrPath p;
        if (XR_FAILED(m_loader.xrStringToPath(m_inst, profile, &p)))
            return;
        std::vector<XrActionSuggestedBinding> sb;
        sb.reserve(binds.size() * 2);
        for (const Bind& b : binds)
            for (int h = 0; h < 2; ++h) {
                char full[128];
                std::snprintf(full, sizeof(full), "/user/hand/%s%s", h == 0 ? "left" : "right", b.leaf);
                XrActionSuggestedBinding e{};
                if (XR_SUCCEEDED(m_loader.xrStringToPath(m_inst, full, &e.binding))) {
                    e.action = b.action;
                    sb.push_back(e);
                }
            }
        if (sb.empty())
            return;
        XrInteractionProfileSuggestedBinding sip{};
        sip.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sip.interactionProfile = p;
        sip.countSuggestedBindings = (uint32_t)sb.size();
        sip.suggestedBindings = sb.data();
        const XrResult r = m_loader.xrSuggestInteractionProfileBindings(m_inst, &sip);
        if (r == XR_ERROR_PATH_UNSUPPORTED)
            diagAppend("xr: profile %s unsupported here (skipped)\n", profile);
        else if (XR_FAILED(r))
            diagAppend("xr: suggest %s failed %d\n", profile, (int)r);
        else
            diagAppend("xr: bound %s (%zu inputs)\n", profile, sb.size());
    };
    // ONE suggestion call per profile: a second call for the same profile replaced the first on
    // this runtime (only the stick got bound, grip/aim lost their sources). Stick paths below are
    // the ones this runtime accepted (throttle is NOT supported on simple, so simple stays grip/aim).
    const char* grip = "/input/grip/pose";
    const char* aim = "/input/aim/pose";
    const char* trig = "/input/trigger/value";
    const char* stick = "/input/thumbstick";
    suggest("/interaction_profiles/khr/simple_controller", {{m_gripPose, grip}, {m_aimPose, aim}});
    suggest("/interaction_profiles/htc/vive_controller",
            {{m_gripPose, grip}, {m_aimPose, aim}, {m_trigger, trig}, {m_stick, "/input/trackpad"}});
    suggest("/interaction_profiles/oculus/touch_controller",
            {{m_gripPose, grip}, {m_aimPose, aim}, {m_trigger, trig}, {m_stick, stick}});
    suggest("/interaction_profiles/valve/index_controller",
            {{m_gripPose, grip}, {m_aimPose, aim}, {m_trigger, trig}, {m_stick, stick}});
    suggest("/interaction_profiles/valve/knuckles_controller",
            {{m_gripPose, grip}, {m_aimPose, aim}, {m_trigger, trig}});
    suggest("/interaction_profiles/windows/mixed_reality_controller",
            {{m_gripPose, grip}, {m_aimPose, aim}, {m_trigger, trig}});

    // attach after the suggestions so the runtime can bind the suggested sources to our actions
    XrSessionActionSetsAttachInfo att{};
    att.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    att.countActionSets = 1;
    XrActionSet set = m_actionSet;
    att.actionSets = &set;
    xrafail(m_loader.xrAttachSessionActionSets(m_session, &att), "attachActionSets");
}

void Xr::pollEvents() {
    if (m_inst == XR_NULL_HANDLE)
        return;
    XrEventDataBuffer buf{};
    buf.type = XR_TYPE_EVENT_DATA_BUFFER;
    XrResult res = m_loader.xrPollEvent(m_inst, &buf);
    while (res == XR_SUCCESS) {
        if (buf.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* ev = (XrEventDataSessionStateChanged*)&buf;
            if (ev->session == m_session) {
                m_state = ev->state;
                switch (m_state) {
                case XR_SESSION_STATE_READY:
                    if (!m_running) {
                        XrSessionBeginInfo b{};
                        b.type = XR_TYPE_SESSION_BEGIN_INFO;
                        b.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        if (xrok(m_loader.xrBeginSession(m_session, &b), "beginSession")) {
                            m_running = true;
                            std::printf("xr: session started\n");
                        }
                    }
                    break;
                case XR_SESSION_STATE_STOPPING:
                    if (m_running) {
                        m_running = false;
                        xrafail(m_loader.xrEndSession(m_session), "endSession");
                        std::printf("xr: session ended\n");
                    }
                    break;
                case XR_SESSION_STATE_LOSS_PENDING:
                    m_loader.xrRequestExitSession(m_session);
                    break;
                default:
                    break;
                }
            }
        }
        buf.type = XR_TYPE_EVENT_DATA_BUFFER;
        res = m_loader.xrPollEvent(m_inst, &buf);
    }
}

XrFrame Xr::poll() {
    pollEvents();
    XrFrame fr;
    fr.running = m_running;
    if (!m_running)
        return fr;

    XrFrameWaitInfo wi{};
    wi.type = XR_TYPE_FRAME_WAIT_INFO;
    xrafail(m_loader.xrWaitFrame(m_session, &wi, &m_fs), "waitFrame");
    XrFrameBeginInfo bi{};
    bi.type = XR_TYPE_FRAME_BEGIN_INFO;
    xrafail(m_loader.xrBeginFrame(m_session, &bi), "beginFrame");
    fr.shouldRender = m_fs.shouldRender != 0;

    XrViewLocateInfo li{};
    li.type = XR_TYPE_VIEW_LOCATE_INFO;
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = m_fs.predictedDisplayTime;
    li.space = m_refSpace;
    XrViewState vs{};
    vs.type = XR_TYPE_VIEW_STATE;
    for (int i = 0; i < 2; ++i)
        m_views[i].type = XR_TYPE_VIEW;
    uint32_t n = 0;
    xrafail(m_loader.xrLocateViews(m_session, &li, &vs, 2, &n, m_views), "locateViews");
    const int ne = (int)std::min<uint32_t>(n, 2);
    fr.havePose = true;
    fr.nEyes = ne;
    for (int i = 0; i < ne; ++i) {
        const XrView& v = m_views[i];
        XrEyeData& e = fr.eye[i];
        e.pos = {v.pose.position.x, v.pose.position.y, v.pose.position.z};
        e.quat = {v.pose.orientation.x, v.pose.orientation.y, v.pose.orientation.z, v.pose.orientation.w};
        e.tanL = std::tan(v.fov.angleLeft);
        e.tanR = std::tan(v.fov.angleRight);
        e.tanU = std::tan(v.fov.angleUp);
        e.tanD = std::tan(v.fov.angleDown);
    }
    pollControllers(fr);
    return fr;
}

void Xr::pollControllers(XrFrame& fr) {
    if (m_actionSet == XR_NULL_HANDLE)
        return;
    // sync once for the active set across both hands
    XrActionSet set = m_actionSet;
    XrActiveActionSet active[2] = {{set, m_hand[0]}, {set, m_hand[1]}};
    XrActionsSyncInfo si{};
    si.type = XR_TYPE_ACTIONS_SYNC_INFO;
    si.countActiveActionSets = 2;
    si.activeActionSets = active;
    m_loader.xrSyncActions(m_session, &si);
    ++m_diagFrame; // throttles the pose failure logs below

    for (int h = 0; h < 2; ++h) {
        XrController& c = fr.controller[h];
        c = XrController{};

        XrActionStateGetInfo gi{};
        gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
        gi.subactionPath = m_hand[h];
        gi.action = m_trigger;
        XrActionStateFloat tf{};
        tf.type = XR_TYPE_ACTION_STATE_FLOAT;
        if (XR_SUCCEEDED(m_loader.xrGetActionStateFloat(m_session, &gi, &tf)) && tf.isActive)
            c.trigger = tf.currentState;
        gi.action = m_stick;
        XrActionStateVector2f tv{};
        tv.type = XR_TYPE_ACTION_STATE_VECTOR2F;
        if (XR_SUCCEEDED(m_loader.xrGetActionStateVector2f(m_session, &gi, &tv)) && tv.isActive) {
            c.stickOn = true;
            // standard stick: x=left/right, y=up/down; keep the contract x=right, y=forward
            c.stick = {tv.currentState.x, tv.currentState.y};
        }
        // pose via a lazily-created action space, located against the reference space
        auto poseOf = [&](XrAction action, XrSpace* space, const char* name, V3& outP, V4& outQ) -> bool {
            if (*space == XR_NULL_HANDLE) {
                XrActionSpaceCreateInfo asi{};
                asi.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
                asi.action = action;
                asi.subactionPath = m_hand[h];
                asi.poseInActionSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
                asi.poseInActionSpace.position = {0.0f, 0.0f, 0.0f};
                const XrResult cr = m_loader.xrCreateActionSpace(m_session, &asi, space);
                if (XR_FAILED(cr)) {
                    diagAppend("xr: hand[%d] createSpace %s failed=%d\n", h, name, (int)cr);
                    return false;
                }
            }
            XrSpaceLocation loc{};
            loc.type = XR_TYPE_SPACE_LOCATION;
            const XrResult lr = m_loader.xrLocateSpace(*space, m_refSpace, m_fs.predictedDisplayTime, &loc);
            const bool ok = XR_SUCCEEDED(lr) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
            if (!ok && m_diagFrame % 60 == 0)
                diagAppend("xr: hand[%d] locate %s result=%d flags=0x%x\n", h, name, (int)lr,
                           (unsigned)loc.locationFlags);
            if (!ok)
                return false;
            outP = {loc.pose.position.x, loc.pose.position.y, loc.pose.position.z};
            outQ = {loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w};
            return true;
        };
        const bool g = poseOf(m_gripPose, &m_gripSpace[h], "grip", c.pos, c.quat);
        const bool a = poseOf(m_aimPose, &m_aimSpace[h], "aim", c.aimPos, c.aimQuat);
        c.active = g || a;
    }
}

void Xr::endFrame(bool render) {
    if (!m_running)
        return;
    XrCompositionLayerProjection layer{};
    layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    if (m_blend != XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layer.space = m_refSpace;
    layer.viewCount = (uint32_t)m_eyeCount;
    for (int i = 0; i < m_eyeCount; ++i) {
        m_proj[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        m_proj[i].pose = m_views[i].pose;
        m_proj[i].fov = m_views[i].fov;
        m_proj[i].subImage.swapchain = m_swaps[i].sc;
        m_proj[i].subImage.imageRect = m_rects[i];
        m_proj[i].subImage.imageArrayIndex = 0;
    }
    layer.views = m_proj;
    XrCompositionLayerBaseHeader* lh = (XrCompositionLayerBaseHeader*)&layer;
    XrFrameEndInfo fei{};
    fei.type = XR_TYPE_FRAME_END_INFO;
    fei.displayTime = m_fs.predictedDisplayTime;
    fei.environmentBlendMode = m_blend;
    fei.layerCount = render ? 1 : 0;
    fei.layers = &lh;
    xrafail(m_loader.xrEndFrame(m_session, &fei), "endFrame");
}

void Xr::acquireImage(int eye) {
    m_swaps[eye].acquired = false;
    if (m_swaps[eye].sc == XR_NULL_HANDLE)
        return;
    XrSwapchainImageAcquireInfo ai{};
    ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    uint32_t idx = 0;
    XrResult r = m_loader.xrAcquireSwapchainImage(m_swaps[eye].sc, &ai, &idx);
    if (r != XR_SUCCESS) {
        std::fprintf(stderr, "xr acquireImage: eye=%d res=%d (skipping)\n", eye, (int)r);
        return;
    }
    XrSwapchainImageWaitInfo wi{};
    wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wi.timeout = 1000000000; // 1s
    XrResult wr = m_loader.xrWaitSwapchainImage(m_swaps[eye].sc, &wi);
    if (wr != XR_SUCCESS) {
        std::fprintf(stderr, "xr waitSwapchainImage: eye=%d res=%d (skipping)\n", eye, (int)wr);
        return;
    }
    m_swaps[eye].cur = (int)idx;
    m_swaps[eye].acquired = true;
}

void Xr::releaseImage(int eye) {
    if (!m_swaps[eye].acquired)
        return;
    XrSwapchainImageReleaseInfo ri{};
    ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    xrafail(m_loader.xrReleaseSwapchainImage(m_swaps[eye].sc, &ri), "releaseImage");
    m_swaps[eye].acquired = false;
}

void Xr::destroyActions() {
    for (auto& s : m_gripSpace)
        if (s != XR_NULL_HANDLE)
            m_loader.xrDestroySpace(s);
    for (auto& s : m_aimSpace)
        if (s != XR_NULL_HANDLE)
            m_loader.xrDestroySpace(s);
    for (auto& a : {m_gripPose, m_aimPose, m_trigger, m_stick})
        if (a != XR_NULL_HANDLE)
            m_loader.xrDestroyAction(a);
    if (m_actionSet != XR_NULL_HANDLE)
        m_loader.xrDestroyActionSet(m_actionSet);
    m_gripSpace[0] = m_gripSpace[1] = m_aimSpace[0] = m_aimSpace[1] = XR_NULL_HANDLE;
    m_gripPose = m_aimPose = m_trigger = m_stick = XR_NULL_HANDLE;
    m_actionSet = XR_NULL_HANDLE;
}

void Xr::shutdown() {
    // idempotent: ~Xr calls this after main's explicit shutdown()
    if (m_session != XR_NULL_HANDLE) {
        if (m_running) {
            xrafail(m_loader.xrEndSession(m_session), "endSession");
            m_running = false;
        }
        for (auto& s : m_swaps) {
            if (s.sc != XR_NULL_HANDLE) {
                m_loader.xrDestroySwapchain(s.sc);
                s.sc = XR_NULL_HANDLE;
            }
        }
        if (m_refSpace != XR_NULL_HANDLE)
            m_loader.xrDestroySpace(m_refSpace);
        destroyActions();
        m_loader.xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
        m_refSpace = XR_NULL_HANDLE;
    }
    if (m_shadowInst != VK_NULL_HANDLE) {
        vkDestroyInstance(m_shadowInst, nullptr);
        m_shadowInst = VK_NULL_HANDLE;
    }
    if (m_inst != XR_NULL_HANDLE) {
        m_loader.xrDestroyInstance(m_inst);
        m_inst = XR_NULL_HANDLE;
    }
}
