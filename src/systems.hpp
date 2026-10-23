#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "ui.hpp"
#include "engine.hpp"
#include <imgui.h>
#include <vector>

template <typename Marker>
inline entt::entity findMarker(const entt::registry& reg) {
    for (auto e : reg.view<Marker>())
        return e;
    return entt::null;
}

entt::entity findActiveCamera(const entt::registry& reg);
CameraView cameraView(const entt::registry& reg, entt::entity cam, float aspect);

void stepRigid(RigidScene& rigid, entt::registry& reg);
void resolveWorldTransforms(entt::registry& reg);
void stepSoft(Engine& w, VkCommandBuffer cmd);
void draw(Engine& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui);

void stepXr(entt::registry& reg, const XrFrame& fr);
entt::entity spawnVrRoot(entt::registry& reg);
void spawnControllers(Engine& w);
void stepControllers(entt::registry& reg, const XrFrame& fr);
void stepMouse(Engine& w);
void stepKeyboard(Engine& w);
void spawnInputEntities(Engine& w);
