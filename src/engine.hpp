#pragma once
#include "physics/physics_server.hpp"
#include "script/scene_script.hpp"
#include "vk/graphics_server.hpp"
#include "xr_backend.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <string>

class VkApp;

struct PhaseMs {
    float poll = 0.0f;
    float cpu = 0.0f;
    float jolt = 0.0f;
    float skel = 0.0f;
    float sim = 0.0f; // cloth (soft) GPU time only
    float render = 0.0f;
    float xrEnd = 0.0f;
    float idle = 0.0f;
};

struct Engine {
    entt::registry reg;
    VkApp* app = nullptr;
    PhysicsServer physics;
    GraphicsServer graphics;
    std::unique_ptr<IXrBackend> xr;
    SceneScript script;
    bool hasDevice = false; // graphics server initialized (Vulkan device present)
    bool hasWindow = false; // window + renderer draw path active
    float fps = 0.0f;
    PhaseMs phase;
    bool showDemo = false;
    int uiSeq = 0;
    bool xrSession = false;
    std::string pendingScene;
    std::string scenePath;
    std::string loadError;
};
