#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "world.hpp"
#include <imgui.h>
#include <vector>

entt::entity spawnCapsule(World& w, const RigidBody& body, int geom);
entt::entity spawnStaticBody(World& w, const RigidBody& body, int geom);
entt::entity spawnSoftBody(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, int pinned);
entt::entity spawnStaticMesh(World& w, const Mesh& mesh, const Transform& t);
entt::entity spawnLight(World& w, const V3& pos, const PointLight& p, const char* name);
entt::entity spawnVrCamera(World& w, const char* name);
entt::entity spawnCamera(World& w, const V3& pos, const V4& quat, float fovDeg);
Mat4 cameraViewProj(World& w, entt::entity cam, float aspect);

void stepPinHolds(World& w, float dt);
void resetSofts(World& w);
void stepRigid(World& w);
// push each RigidStatic body's ECS Transform into its Jolt body (Transform is the source of truth)
void stepStaticRigid(World& w);
// run the scene's per-frame animation callback (local Transform writes)
void stepAnimation(World& w, float dt);
// compose each entity's Transform through its Parent chain into a WorldTransform
void resolveWorldTransforms(World& w);
void syncColliders(World& w);
void stepSoft(World& w, VkCommandBuffer cmd);
// copy ECS transforms/materials/lights/env into the renderer; call before any render
void syncSceneToRenderer(World& w);
void draw(World& w, VkCommandBuffer cmd, uint32_t fb, const float bg[3], ImDrawData* imgui);
// mouse/keyboard move/look on the camera entity (fly camera, or the virtual head when the XR
// session is not running); skipped while a headset pose is active
void stepFlyCamera(World& w, float dt);
// XR head pose -> VrCamera Transform; then synthesize the per-eye poses (headset pose + eye-rig
// placement, or the camera entity with virtual lens FOVs) -> setVrEyes
void stepXr(World& w);
