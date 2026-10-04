#pragma once
#include "app/rigid.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include "xr_backend.hpp"
#include <entt/entt.hpp>
#include <memory>

class VkApp;

// renderer-side state the UI binds to (not scene-graph data, so plain state, not an entity)
struct RenderSettings {
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    float exposure = 1.0f;
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
    RenderSettings render;
    float fps = 0.0f;
    bool showDemo = false;
    int uiSeq = 0; // auto-assigned draw order for UI windows/widgets
};
