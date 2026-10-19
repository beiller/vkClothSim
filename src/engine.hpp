#pragma once
#include "app/rigid.hpp"
#include "scene_script.hpp"
#include "sim/gpusim.hpp"
#include "vk/renderer.hpp"
#include "xr_backend.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <utility>

class VkApp;

struct PhaseMs {
    float poll = 0.0f;   // glfw events + XR pose/action queries + free flushes
    float sim = 0.0f;    // input + scripts + fixed-step physics
    float render = 0.0f; // imgui + swapchain acquire + scene draw
    float xrEnd = 0.0f;  // xrEndFrame (runtime present)
    float idle = 0.0f;   // HMD pacing wait (VR)
};

struct Engine {
    entt::registry reg;
    VkApp* app = nullptr;
    RigidScene rigid;
    GpuSim gpuSim; // GPU simulation: skeleton skinning + soft bodies (discover skeletons via the ECS)
    Renderer renderer;
    std::unique_ptr<IXrBackend> xr;
    RenderSettings render;
    entt::entity cam = entt::null;    // the scene's fly camera (script-driven; current when not presenting)
    entt::entity vrCam = entt::null;  // the VR head camera (C++-driven from the XR pose, under the vr root)
    entt::entity vrRoot = entt::null; // head-independent locomotion anchor (world pos = walkPos)
    SceneScript script;               // Lua layer: std geometry library + optional scene behavior
    float fps = 0.0f;
    PhaseMs phase; // per-section frame time, shown in the fps HUD
    bool showDemo = false;
    int uiSeq = 0; // auto-assigned draw order for UI windows/widgets
    bool xrSession = false; // a live OpenXR session (the vr root + grips exist only then)
    std::string pendingScene; // a requested scene switch (scene.request_scene); applied at the top of the next frame
    std::string scenePath;    // the last successfully loaded scene (for the menu fallback)
    std::string loadError;    // the last scene load failure, shown in the menu
};
