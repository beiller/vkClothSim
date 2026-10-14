#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "ui.hpp"
#include "world.hpp"
#include <imgui.h>
#include <vector>

// add a visual to an entity: Renderable + MaterialRef + optional Parent; ensures the entity has
// a Transform (identity if absent) since a mesh without a pose sits at the origin
void addMesh(World& w, entt::entity e, int geom, entt::entity mat, entt::entity parent);
// add a soft body to an entity: SoftBodyData + Renderable + MaterialRef; mass > 0 overrides the default
void addSoftBody(World& w, entt::entity e, const Mesh& mesh, const std::vector<sim::Constraint>& cons, entt::entity mat,
                 float mass);
// shared material entity (Material, optional Name); meshes reference it via MaterialRef
entt::entity spawnMaterial(World& w, const V3& baseColor, float metallic, float roughness, const V3& emissive,
                           const char* name = nullptr);
// first entity with a matching Name (entt::null if none)
entt::entity findNamedEntity(const World& w, const std::string& name);
// one collider of a body: a shared shape entity + pose in the body's frame
struct ColliderSpec {
    entt::entity shape = entt::null;
    Transform local{};
};
// add a rigid body to an entity (needs a Transform): a Jolt body built from the colliders' shared
// shapes + one collider child entity per collider; isStatic = fixed collider (ground) vs integrated
void addRigidBody(World& w, entt::entity e, const std::vector<ColliderSpec>& colliders, bool isStatic);
// kinematic body: same collider children; the Jolt body follows the entity's pose each step
void addKinematicBody(World& w, entt::entity e, const std::vector<ColliderSpec>& colliders);
// one collider child entity: local Transform + Collider(shape) + Parent -> the body
entt::entity addCollider(World& w, entt::entity body, entt::entity shape, const Transform& local);
// texture from assets/ (or assets/ relative to the exe); renderer texture id or -1
int loadAssetTexture(World& w, const std::string& path);
// the active camera (entt::null if none)
entt::entity findActiveCamera(const World& w);
// compose an entity's Transform through its Parent chain (world space); valid before/after resolve
Transform worldTransform(entt::registry& reg, entt::entity e);
Mat4 cameraViewProj(World& w, entt::entity cam, float aspect);

void stepRigid(World& w);
// compose each entity's Transform through its Parent chain into a WorldTransform
void resolveWorldTransforms(World& w);
void syncColliders(World& w);
void stepSoft(World& w, VkCommandBuffer cmd);
// query the ECS and render the scene (mono) to the swapchain image
void draw(World& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui);

// current camera: the VR head camera while presenting, the scene's fly camera otherwise;
// headset pose -> vr cam's LOCAL Transform (a child of the VR root, so its world = walkPos +
// head); synthesize per-eye poses (headset eyes + walkPos, or the fly camera's world pose with
// virtual lens FOVs) -> setVrEyes
void stepXr(World& w);
// head-independent VR locomotion anchor: a root the VR head camera + grips are parented under;
// also spawns the "vr cam" (the VR head camera)
entt::entity spawnVrRoot(World& w);
// create a tracked controller entity per hand, parented to the VR root; right hand gets a
// kinematic Jolt body (hand anchor for joints)
void spawnControllers(World& w);
// headset controller poses -> controller entities' local Transform + ControllerInput + drive hand body
void stepControllers(World& w);
// GLFW -> "mouse" entity (position + buttons)
void stepMouse(World& w);
// GLFW -> "keyboard" entity (key states)
void stepKeyboard(World& w);
// create the "mouse" and "keyboard" entities (call once at startup)
void spawnInputEntities(World& w);
