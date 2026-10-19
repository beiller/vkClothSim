#pragma once
#include "ecs.hpp"
#include "url.hpp"
#include "engine.hpp"

class VkApp;

// UI windows/widgets are entities (see ecs.hpp); draws every UIWindow with its widget children
void drawUi(Engine& w);
// spawn a UI window entity; order auto-assigned from Engine::uiSeq
entt::entity spawnUiWindow(Engine& w, const UIWindow& cfg);
// spawn the editor tab's inspector (one named-field widget per entity, sorted by entity)
void spawnEditorUi(Engine& w, entt::entity tab);
// spawn a widget entity parented to win/section; order auto-assigned
template <typename C> entt::entity spawnWidget(Engine& w, entt::entity parent, C c) {
    entt::entity e = w.reg.create();
    UIWidget mw;
    mw.order = w.uiSeq++;
    w.reg.emplace<UIWidget>(e, mw);
    w.reg.emplace<C>(e, c);
    w.reg.emplace<Parent>(e, parent);
    return e;
}
// spawn a widget from a reflect field (type/range from the table); used by the editor tab
void spawnFieldWidget(Engine& w, entt::entity parent, const char* label, const FieldDesc* f, void* base);
// app-level fps HUD, top right
void spawnFpsHud(Engine& w);
// ImGui context + vulkan/glfw backends
void initImGui(VkApp& app);
void shutdownImGui();
// start an ImGui frame (backend NewFrame trio)
void imguiNewFrame();
