#include "xr/xr.hpp"

#include "vk/vkapp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

bool xrok(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r))
        return true;
    std::fprintf(stderr, "xr %s: error %d\n", what, (int)r);
    return false;
}

void xrafail(XrResult r, const char* what) {
    if (!xrok(r, what))
        std::abort();
}

// prefer plain UNORM (the tonemap pass writes sRGB-encoded values)
VkFormat pickFormat(int64_t* fmts, uint32_t n) {
    const int64_t want[] = {VK_FORMAT_B8G8R8A8_UNORM,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_FORMAT_B8G8R8_UNORM,
                            VK_FORMAT_R8G8B8_UNORM,
                            VK_FORMAT_R16G16B16A16_SFLOAT};
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
    XrApplicationInfo ai{};
    std::snprintf(ai.applicationName, sizeof(ai.applicationName), "3dsim");
    ai.applicationVersion = 1;
    std::snprintf(ai.engineName, sizeof(ai.engineName), "3dsim");
    ai.engineVersion = 1;
    // SteamVR's OpenXR runtime (2.18.x) rejects apiVersion 1.1; request 1.0
    ai.apiVersion = XR_API_VERSION_1_0;
    static const char* kExts[] = {"XR_KHR_vulkan_enable2", "XR_EXT_hand_tracking"};
    XrInstanceCreateInfo ici{};
    ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ici.applicationInfo = ai;
    ici.enabledExtensionCount = 2;
    ici.enabledExtensionNames = kExts;
    if (!xrok(m_loader.xrCreateInstance(&ici, &m_inst), "createInstance"))
        return false;

    // diagnose: which instance extensions does the runtime support? (esp. hand_tracking)
    {
        uint32_t n = 0;
        if (XR_SUCCEEDED(m_loader.xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr)) && n > 0) {
            std::vector<XrExtensionProperties> exts(n);
            for (auto& e : exts)
                e.type = XR_TYPE_EXTENSION_PROPERTIES;
            uint32_t got = 0;
            if (XR_SUCCEEDED(m_loader.xrEnumerateInstanceExtensionProperties(nullptr, n, &got, exts.data()))) {
                std::fprintf(stderr, "xr: %u extensions: ", got);
                for (uint32_t i = 0; i < got; ++i)
                    std::fprintf(stderr, "%s%s", exts[i].extensionName, i + 1 < got ? ", " : "\n");
            }
        }
    }

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
    if (XR_SUCCEEDED(m_loader.xrEnumerateEnvironmentBlendModes(m_inst, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8,
                                                               &nm, modes)) &&
        nm > 0)
        m_blend = modes[0];

    uint32_t nv = 0;
    if (XR_FAILED(m_loader.xrEnumerateViewConfigurationViews(m_inst, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv,
                                                             nullptr)) ||
        nv == 0) {
        std::printf("xr: no stereo views\n");
        return false;
    }
    std::vector<XrViewConfigurationView> vcs(nv);
    for (auto& v : vcs) {
        v.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        v.next = nullptr;
    }
    xrafail(m_loader.xrEnumerateViewConfigurationViews(m_inst, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nv, &nv,
                                                        vcs.data()),
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
    // LOCAL_FLOOR (1000426000 in this header) is rejected here; LOCAL is always supported
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};
    rs.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!xrok(m_loader.xrCreateReferenceSpace(m_session, &rs, &m_refSpace), "createReferenceSpace"))
        return false;

    // diagnose: what interaction profile is the runtime using for the right hand?
    {
        XrPath rh;
        if (m_loader.xrStringToPath(m_inst, "/user/hand/right", &rh) == XR_SUCCESS && m_loader.xrGetCurrentInteractionProfile) {
            XrInteractionProfileState ps{};
            ps.type = XR_TYPE_INTERACTION_PROFILE_STATE;
            XrResult pr = m_loader.xrGetCurrentInteractionProfile(m_session, rh, &ps);
            if (XR_SUCCEEDED(pr)) {
                char buf[256] = {};
                uint32_t blen = sizeof(buf);
                if (XR_SUCCEEDED(m_loader.xrPathToString(m_inst, ps.interactionProfile, sizeof(buf), &blen, buf)))
                    std::fprintf(stderr, "xr: right-hand profile: %s\n", buf);
            } else
                std::fprintf(stderr, "xr: right-hand profile query res=%d\n", (int)pr);
        }
    }

    // right-controller pose via the actions system (non-fatal: VR still works if it fails)
    {
        XrPath rightSub, profile, gripPose, stickSrc;
        const bool okPaths =
            m_loader.xrStringToPath(m_inst, "/user/hand/right", &rightSub) == XR_SUCCESS &&
            m_loader.xrStringToPath(m_inst, "/interaction_profiles/oculus/touch_controller", &profile) == XR_SUCCESS &&
            m_loader.xrStringToPath(m_inst, "/user/hand/right/input/grip/pose", &gripPose) == XR_SUCCESS &&
            m_loader.xrStringToPath(m_inst, "/user/hand/right/input/thumbstick", &stickSrc) == XR_SUCCESS;
        if (!okPaths) {
            std::fprintf(stderr, "xr: right-hand paths unavailable\n");
        } else {
            XrActionSetCreateInfo asc{};
            asc.type = XR_TYPE_ACTION_SET_CREATE_INFO;
            std::snprintf(asc.actionSetName, sizeof(asc.actionSetName), "3dsim");
            std::snprintf(asc.localizedActionSetName, sizeof(asc.localizedActionSetName), "3dsim");
            if (xrok(m_loader.xrCreateActionSet(m_inst, &asc, &m_actionSet), "createActionSet")) {
                if (std::getenv("XR_DIAG")) {
                    // diagnose: which toplevel paths does the runtime accept for a POSE action?
                    const char* cands[] = {"/user/controller", "/user/hand/right", "/user/hand/left", "/user/target/rigid"};
                    for (const char* c : cands) {
                        XrPath p;
                        if (m_loader.xrStringToPath(m_inst, c, &p) != XR_SUCCESS) {
                            std::fprintf(stderr, "xr: toplevel %-18s -> pathParse FAIL\n", c);
                            continue;
                        }
                        XrActionCreateInfo tc{};
                        tc.type = XR_TYPE_ACTION_CREATE_INFO;
                        std::snprintf(tc.actionName, sizeof(tc.actionName), "diag");
                        std::snprintf(tc.localizedActionName, sizeof(tc.localizedActionName), "diag");
                        tc.actionType = XR_ACTION_TYPE_POSE_INPUT;
                        tc.countSubactionPaths = 1;
                        tc.subactionPaths = &p;
                        XrAction ta = XR_NULL_HANDLE;
                        XrResult r = m_loader.xrCreateAction(m_actionSet, &tc, &ta);
                        std::fprintf(stderr, "xr: toplevel %-18s -> createAction %d\n", c, (int)r);
                        if (XR_SUCCEEDED(r) && ta != XR_NULL_HANDLE)
                            m_loader.xrDestroyAction(ta);
                    }
                }
                // NOTE: action names must be short and plain here. This SteamVR/Moonlight runtime
                // rejects "rightHandPose" from createAction with -21 (PATH_FORMAT_INVALID) even though
                // the subaction path is valid (an identical path with name "diag" returns 0). Short
                // names like "pose"/"stick" (matching Godot's action-map resource names) work.
                XrActionCreateInfo ac{};
                ac.type = XR_TYPE_ACTION_CREATE_INFO;
                std::snprintf(ac.actionName, sizeof(ac.actionName), "pose");
                std::snprintf(ac.localizedActionName, sizeof(ac.localizedActionName), "pose");
                ac.actionType = XR_ACTION_TYPE_POSE_INPUT;
                ac.countSubactionPaths = 1;
                ac.subactionPaths = &rightSub;
                if (xrok(m_loader.xrCreateAction(m_actionSet, &ac, &m_poseAction), "createAction")) {
                    // thumbstick (vector2) for locomotion
                    XrActionCreateInfo sc{};
                    sc.type = XR_TYPE_ACTION_CREATE_INFO;
                    std::snprintf(sc.actionName, sizeof(sc.actionName), "stick");
                    std::snprintf(sc.localizedActionName, sizeof(sc.localizedActionName), "stick");
                    sc.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
                    sc.countSubactionPaths = 1;
                    sc.subactionPaths = &rightSub;
                    xrok(m_loader.xrCreateAction(m_actionSet, &sc, &m_stickAction), "createStickAction");
                    XrActionSuggestedBinding sb[2]{{m_poseAction, gripPose}, {m_stickAction, stickSrc}};
                    XrInteractionProfileSuggestedBinding sbs{};
                    sbs.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
                    sbs.interactionProfile = profile;
                    sbs.countSuggestedBindings = (m_stickAction != XR_NULL_HANDLE) ? 2 : 1;
                    sbs.suggestedBindings = sb;
                    xrok(m_loader.xrSuggestInteractionProfileBindings(m_inst, &sbs), "suggestBindings");
                    // attach BEFORE creating the action space: the runtime requires the action
                    // set to be attached, otherwise createActionSpace fails with -46 (not attached).
                    XrSessionActionSetsAttachInfo attach{};
                    attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
                    attach.countActionSets = 1;
                    attach.actionSets = &m_actionSet;
                    if (xrok(m_loader.xrAttachSessionActionSets(m_session, &attach), "attachActionSets")) {
                        XrActionSpaceCreateInfo asp{};
                        asp.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
                        asp.action = m_poseAction;
                        asp.subactionPath = rightSub;
                        asp.poseInActionSpace.position = {0.0f, 0.0f, 0.0f};
                        asp.poseInActionSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
                        if (xrok(m_loader.xrCreateActionSpace(m_session, &asp, &m_rightHandSpace), "createActionSpace"))
                            m_rightSubaction = rightSub;
                    }
                }
            }
        }
    }

    m_ready = true;
    std::printf("xr: %d eyes %ux%u format=%d blend=%d\n", m_eyeCount, m_swaps[0].extent.width, m_swaps[0].extent.height,
                (int)m_fmt, (int)m_blend);
    return true;
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
                        m_focused = false;
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
                m_focused = (m_state == XR_SESSION_STATE_FOCUSED);
            }
        }
        buf.type = XR_TYPE_EVENT_DATA_BUFFER;
        res = m_loader.xrPollEvent(m_inst, &buf);
    }
}

XrFrameData Xr::poll() {
    pollEvents();
    XrFrameData fr;
    fr.running = m_running;
    fr.focused = m_focused;
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
    if (m_running) {
        if (m_rightHandDbg == 0)
            std::fprintf(stderr, "xr: right-hand setup: actionSet=%s space=%s locate=%s subaction=%llu\n",
                         m_actionSet != XR_NULL_HANDLE ? "ok" : "NULL", m_rightHandSpace != XR_NULL_HANDLE ? "ok" : "NULL",
                         m_loader.xrLocateSpace ? "ok" : "NULL", (unsigned long long)m_rightSubaction);
        if (m_rightHandSpace != XR_NULL_HANDLE) {
            XrActiveActionSet active{m_actionSet, m_rightSubaction};
            XrActionsSyncInfo sync{};
            sync.type = XR_TYPE_ACTIONS_SYNC_INFO;
            sync.countActiveActionSets = 1;
            sync.activeActionSets = &active;
            m_loader.xrSyncActions(m_session, &sync);
            XrSpaceLocation loc{};
            loc.type = XR_TYPE_SPACE_LOCATION;
            const XrResult lr = m_loader.xrLocateSpace(m_rightHandSpace, m_refSpace, m_fs.predictedDisplayTime, &loc);
            const XrSpaceLocationFlags want = XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if (XR_SUCCEEDED(lr) && (loc.locationFlags & want) == want) {
                fr.haveRightHand = true;
                fr.rightHandPos = {loc.pose.position.x, loc.pose.position.y, loc.pose.position.z};
                fr.rightHandQuat = {loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w};
                if (m_rightHandDbg < 5)
                    std::fprintf(stderr, "xr: right-hand OK #%d pos=%.2f %.2f %.2f flags=%x\n", m_rightHandDbg, fr.rightHandPos.x,
                                 fr.rightHandPos.y, fr.rightHandPos.z, (unsigned)loc.locationFlags);
            } else if (m_rightHandDbg < 30) {
                std::fprintf(stderr, "xr: right-hand miss #%d res=%d flags=%x (want %x)\n", m_rightHandDbg, (int)lr,
                             (unsigned)loc.locationFlags, (unsigned)want);
            }
        }
        // read the right-hand thumbstick (normalized -1..1) for locomotion
        if (m_stickAction != XR_NULL_HANDLE && m_loader.xrGetActionStateVector2f) {
            XrActionStateGetInfo si{};
            si.type = XR_TYPE_ACTION_STATE_GET_INFO;
            si.action = m_stickAction;
            si.subactionPath = m_rightSubaction;
            XrActionStateVector2f ss{};
            ss.type = XR_TYPE_ACTION_STATE_VECTOR2F;
            if (XR_SUCCEEDED(m_loader.xrGetActionStateVector2f(m_session, &si, &ss))) {
                fr.haveRightStick = true;
                fr.rightStickX = ss.currentState.x;
                fr.rightStickY = ss.currentState.y;
                if (m_rightHandDbg < 30 && (std::abs(ss.currentState.x) > 0.05f || std::abs(ss.currentState.y) > 0.05f))
                    std::fprintf(stderr, "xr: stick #%d x=%.2f y=%.2f\n", m_rightHandDbg, ss.currentState.x, ss.currentState.y);
            }
        }
        ++m_rightHandDbg;
    }
    return fr;
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

int Xr::imageIndex(int eye) const {
    return m_swaps[eye].cur;
}

void Xr::shutdown() {
    if (m_session) {
        if (m_running) {
            xrafail(m_loader.xrEndSession(m_session), "endSession");
            m_running = false;
        }
        for (auto& s : m_swaps)
            if (s.sc != XR_NULL_HANDLE)
                m_loader.xrDestroySwapchain(s.sc);
        if (m_refSpace != XR_NULL_HANDLE)
            m_loader.xrDestroySpace(m_refSpace);
        if (m_rightHandSpace != XR_NULL_HANDLE)
            m_loader.xrDestroySpace(m_rightHandSpace);
        if (m_actionSet != XR_NULL_HANDLE)
            m_loader.xrDestroyActionSet(m_actionSet);
        m_loader.xrDestroySession(m_session);
    }
    if (m_shadowInst != VK_NULL_HANDLE)
        vkDestroyInstance(m_shadowInst, nullptr);
    if (m_inst != XR_NULL_HANDLE)
        m_loader.xrDestroyInstance(m_inst);
}
