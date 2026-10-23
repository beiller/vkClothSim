#pragma once
#include "app/rigid.hpp"
#include "core/reflect.hpp"
#include "script/scene_script.hpp"
#include "sim/gpusim.hpp"
#include "vk/renderer.hpp"
#include "xr_backend.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <utility>
#include <vector>

class VkApp;

struct PhaseMs {
    float poll = 0.0f;
    float sim = 0.0f;
    float render = 0.0f;
    float xrEnd = 0.0f;
    float idle = 0.0f;
};

struct Engine {
    entt::registry reg;
    VkApp* app = nullptr;
    RigidScene rigid;
    GpuSim gpuSim;
    Renderer renderer;
    std::unique_ptr<IXrBackend> xr;
    RenderSettings render;
    SceneScript script;
    float fps = 0.0f;
    PhaseMs phase;
    bool showDemo = false;
    int uiSeq = 0;
    bool xrSession = false;
    std::string pendingScene;
    std::string scenePath;
    std::string loadError;
};

// the non-entity reflection roots (editor / url binding); only glue code builds this
inline std::vector<refl::RootRef> engineRoots(Engine& w) {
    return {{refl::findType("RenderSettings"), &w.render}, {refl::findType("RigidScene"), &w.rigid}};
}
