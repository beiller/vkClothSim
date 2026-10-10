#pragma once
#include "ecs.hpp"
#include "world.hpp"

class VkApp;

// UI windows/widgets are entities (see ecs.hpp); draws every UIWindow with its widget children
void drawUi(World& w);
// spawn a UI window entity; order auto-assigned from World::uiSeq
entt::entity spawnUiWindow(World& w, const UIWindow& cfg);
// spawn a widget entity parented to win/section; order auto-assigned
template <typename C> entt::entity spawnWidget(World& w, entt::entity parent, C c) {
    entt::entity e = w.reg.create();
    UIWidget mw;
    mw.order = w.uiSeq++;
    w.reg.emplace<UIWidget>(e, mw);
    w.reg.emplace<C>(e, c);
    w.reg.emplace<Parent>(e, parent);
    return e;
}
// app-level fps HUD, top right
void spawnFpsHud(World& w);
// ImGui context + vulkan/glfw backends
void initImGui(VkApp& app);
void shutdownImGui();
// start an ImGui frame (backend NewFrame trio)
void imguiNewFrame();
