#pragma once
#include "app/rigid.hpp"
#include "camera.hpp"
#include "sim/softsim.hpp"
#include "vk/renderer.hpp"
#include <entt/entt.hpp>
#include <functional>

class VkApp;

struct UIState {
    int joltIters = 10;
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    bool showDemo = false;
};

struct World {
    entt::registry reg;
    VkApp* app = nullptr;
    RigidScene rigid;
    SoftSim sim;
    Renderer renderer;
    Camera camera;
    UIState ui;
    std::function<void(World&)> drawUi;
};
