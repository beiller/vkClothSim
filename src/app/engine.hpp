#pragma once
#include "physics/physics_server.hpp"
#include "script/scene_script.hpp"
#include "vk/graphics_server.hpp"
#include "xr/xr_backend.hpp"
#include <entt/entt.hpp>
#include <cstdio>
#include <memory>
#include <string>

class VkApp;

struct PhaseMs {
    float poll = 0.0f;
    float cpu = 0.0f;
    float jolt = 0.0f;
    float skel = 0.0f;
    float sim = 0.0f;
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
    bool hasDevice = false;
    bool hasWindow = false;
    float fps = 0.0f;
    PhaseMs phase;
    bool showDemo = false;
    int uiSeq = 0;
    bool xrSession = false;
    std::string pendingScene;
    std::string scenePath;
    std::string loadError;
    std::string perfText;
};

inline void writePerfText(Engine& w) {
    char buf[256];
    const float fps = w.fps > 1.0f ? w.fps : 1.0f;
    std::snprintf(buf, sizeof(buf), "%.1f fps  %.1f ms\n"
                                  "poll  %6.1f\n"
                                  "cpu   %6.1f\n"
                                  "jolt  %6.1f\n"
                                  "skel  %6.1f\n"
                                  "sim   %6.1f\n"
                                  "rend  %6.1f\n"
                                  "xrend %6.1f\n"
                                  "idle  %6.1f",
                  w.fps, 1000.0f / fps, w.phase.poll, w.phase.cpu, w.phase.jolt,
                  w.phase.skel, w.phase.sim, w.phase.render, w.phase.xrEnd, w.phase.idle);
    w.perfText = buf;
}
