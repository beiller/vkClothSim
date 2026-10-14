#pragma once
#include "app/rigid.hpp"
#include "scene_script.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include "xr_backend.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <utility>

class VkApp;

struct PhaseMs {
    float poll = 0.0f;   // glfw events + xrWaitFrame (frame pacing)
    float sim = 0.0f;    // input + scripts + fixed-step physics
    float render = 0.0f; // imgui + swapchain acquire + scene draw
    float xrEnd = 0.0f;  // xrEndFrame (runtime present)
    float idle = 0.0f;   // waiting for the display to present (window: vsync; VR: ~0, pacing is in poll)
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
    entt::entity cam = entt::null;    // the scene's fly camera (script-driven; current when not presenting)
    entt::entity vrCam = entt::null;  // the VR head camera (C++-driven from the XR pose, under the vr root)
    entt::entity vrRoot = entt::null; // head-independent locomotion anchor (world pos = walkPos)
    SceneScript script;               // Lua layer: std geometry library + optional scene behavior
    float fps = 0.0f;
    PhaseMs phase; // per-section frame time, shown in the fps HUD
    bool showDemo = false;
    int uiSeq = 0; // auto-assigned draw order for UI windows/widgets
};
