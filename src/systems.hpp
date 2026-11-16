#pragma once
#include <vulkan/vulkan.h>

#include "app/engine.hpp"
#include "ecs.hpp"
#include "mesh.hpp"
#include "ui.hpp"
#include <vector>

template <typename Marker> inline entt::entity findMarker(const entt::registry& reg) {
    for (auto e : reg.view<Marker>())
        return e;
    return entt::null;
}

entt::entity findActiveCamera(const entt::registry& reg);
CameraView cameraView(const entt::registry& reg, entt::entity cam, float aspect);

void resolveWorldTransforms(entt::registry& reg);

void stepAnimations(entt::registry& reg, float dt);

// Fixed-timestep bone interpolation (FRAME_PACING.md): snapshotBones captures each skeleton joint's
// local transform at the end of a tick; interpolateBones writes lerp(prev,curr,alpha) per frame so
// skinning (which reads bone WorldTransforms) renders between ticks. watchSystems cleans up on destroy.
void snapshotBones(const entt::registry& reg);
void interpolateBones(entt::registry& reg, float alpha);
void watchSystems(entt::registry& reg);

void stepXr(entt::registry& reg, const XrFrame& fr);
entt::entity spawnVrRoot(entt::registry& reg);
void spawnControllers(Engine& w);
void stepControllers(entt::registry& reg, const XrFrame& fr);

bool uiPanelUv(const entt::registry& reg, entt::entity canvas, const V3& hitPos, V2& uv);
void stepMouse(Engine& w);
void stepKeyboard(Engine& w);
void spawnInputEntities(Engine& w);

void stepBinds(entt::registry& reg);
