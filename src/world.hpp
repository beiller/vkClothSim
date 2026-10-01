#pragma once
#include "app/rigid.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include "xr_backend.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <memory>

class VkApp;

struct UIState {
    int joltIters = 10;
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    float exposure = 1.0f;
    bool showDemo = false;
    float envIntensity = 1.0f;
};

struct World {
    entt::registry reg;
    VkApp* app = nullptr;
    RigidScene rigid;
    SoftSim sim;
    Renderer renderer;
    std::unique_ptr<IXrBackend> xr;
    XrFrame xrFrame;
    entt::entity cam = entt::null;       // fly camera (Transform + Camera component)
    entt::entity vrCam = entt::null;     // head (VrCamera): tracks the XR head pose
    entt::entity playerRig = entt::null; // X/Y/Z player anchor (translation-only)
    entt::entity eyeRig = entt::null;    // eye-height empty (child of playerRig)
    UIState ui;
    std::function<void(World&)> drawUi;
};
