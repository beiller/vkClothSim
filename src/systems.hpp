#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "ui.hpp"
#include "engine.hpp"
#include <imgui.h>
#include <vector>

entt::entity findActiveCamera(const Engine& w);
CameraView cameraView(Engine& w, entt::entity cam, float aspect);

void stepRigid(Engine& w);
void resolveWorldTransforms(Engine& w);
void stepSoft(Engine& w, VkCommandBuffer cmd);
void draw(Engine& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui);

void stepXr(Engine& w, const XrFrame& fr);
entt::entity spawnVrRoot(Engine& w);
void spawnControllers(Engine& w);
void stepControllers(Engine& w, const XrFrame& fr);
void stepMouse(Engine& w);
void stepKeyboard(Engine& w);
void spawnInputEntities(Engine& w);
