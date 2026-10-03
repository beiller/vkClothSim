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
// a plain camera entity (Transform + Camera). active = the window renders it; one by convention
entt::entity spawnCamera(World& w, const V3& pos, const V4& quat, float fovDeg, bool active = true);
// the active camera (entt::null if none)
entt::entity findActiveCamera(const World& w);
// compose an entity's Transform through its Parent chain (world space); valid before/after resolve
Transform worldTransform(entt::registry& reg, entt::entity e);
Mat4 cameraViewProj(World& w, entt::entity cam, float aspect);
// player + eye-height rig under the head: the active camera is re-parented as the head and its
// local pose becomes the headset pose (or the mouse/keyboard virtual head when no session runs)
void spawnVrRig(World& w, entt::entity head);

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
// mouse/keyboard move/look on the active camera (fly camera, or the virtual head when the XR
// session is not running); skipped while a headset pose is active
void stepFlyCamera(World& w, float dt);
// headset pose -> active camera's local Transform; then synthesize the per-eye poses (headset
// eyes + rig placement, or the active camera's world pose with virtual lens FOVs) -> setVrEyes
void stepXr(World& w);
