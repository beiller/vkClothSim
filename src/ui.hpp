#pragma once
#include "ecs.hpp"
#include "url.hpp"
#include "engine.hpp"

class VkApp;

void drawUi(Engine& w);
entt::entity spawnUiWindow(Engine& w, const UIWindow& cfg);
void spawnEditorUi(Engine& w, entt::entity tab);
template <typename C> entt::entity spawnWidget(Engine& w, entt::entity parent, C c) {
    entt::entity e = w.reg.create();
    UIWidget mw;
    mw.order = w.uiSeq++;
    w.reg.emplace<UIWidget>(e, mw);
    w.reg.emplace<C>(e, c);
    w.reg.emplace<Parent>(e, parent);
    return e;
}
void spawnFieldWidget(Engine& w, entt::entity parent, const char* label, const FieldDesc* f, void* base);
void spawnFpsHud(Engine& w);
void initImGui(VkApp& app);
void shutdownImGui();
void imguiNewFrame();
