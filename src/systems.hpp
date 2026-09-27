#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "world.hpp"
#include <imgui.h>
#include <vector>

entt::entity spawnCapsule(World& w, const RigidBody& body, int geom);
entt::entity spawnSoftBody(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, int pinned);
entt::entity spawnStaticMesh(World& w, const Mesh& mesh, const Transform& t);

void stepPinHolds(World& w, float dt);
void resetSofts(World& w);
void stepRigid(World& w);
void syncColliders(World& w);
void stepSoft(World& w, VkCommandBuffer cmd);
void draw(World& w, VkCommandBuffer cmd, uint32_t fb, const float bg[3], ImDrawData* imgui);
