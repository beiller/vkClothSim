#pragma once
#include "app/rigid.hpp"
#include "camera.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include <entt/entt.hpp>
#include <functional>

#ifdef WITH_OPENXR
#include "xr/xr.hpp"
#endif

class VkApp;

struct VirtHead {
    V3 pos{0, 0, 0};
    V4 quat{0, 0, 0, 1};
    bool init = false;
};

struct UIState {
    int joltIters = 10;
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    float exposure = 1.0f;
    bool showDemo = false;
    float envIntensity = 1.0f;
    float vrRigOffset[3] = {0.0f, 0.0f, 0.0f};
    bool vrSceneCamPose = false;
    VirtHead virtHead;
};

struct World {
    entt::registry reg;
    VkApp* app = nullptr;
    RigidScene rigid;
    SoftSim sim;
    Renderer renderer;
    Camera camera;
#ifdef WITH_OPENXR
    Xr xr;
    entt::entity vrCam = entt::null;     // head (VrCamera): tracks the OpenXR pose
    entt::entity playerRig = entt::null; // X/Y/Z player anchor (translation-only)
    entt::entity eyeRig = entt::null;    // eye-height empty (child of playerRig)
#endif
    UIState ui;
    std::function<void(World&)> drawUi;
};
