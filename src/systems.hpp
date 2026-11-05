#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "ui.hpp"
#include "app/engine.hpp"
#include <vector>

template <typename Marker>
inline entt::entity findMarker(const entt::registry& reg) {
    for (auto e : reg.view<Marker>())
        return e;
    return entt::null;
}

entt::entity findActiveCamera(const entt::registry& reg);
CameraView cameraView(const entt::registry& reg, entt::entity cam, float aspect);

void resolveWorldTransforms(entt::registry& reg);

void stepXr(entt::registry& reg, const XrFrame& fr);
entt::entity spawnVrRoot(entt::registry& reg);
void spawnControllers(Engine& w);
void stepControllers(entt::registry& reg, const XrFrame& fr);

bool uiPanelUv(const entt::registry& reg, entt::entity canvas, const V3& hitPos, V2& uv);
void stepMouse(Engine& w);
void stepKeyboard(Engine& w);
void spawnInputEntities(Engine& w);

void stepBinds(entt::registry& reg);
