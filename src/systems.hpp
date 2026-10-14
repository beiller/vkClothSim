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
entt::entity spawnMaterial(World& w, const V3& baseColor, float metallic, float roughness, const char* name = nullptr);
// first entity with a matching Name (entt::null if none)
entt::entity findNamedEntity(const World& w, const std::string& name);
// add a rigid body to an entity (needs a Transform): Jolt body from a shared shape + soft-sim
// capsule collider (capsule shapes); isStatic = fixed collider (ground) vs integrated
void addRigidBody(World& w, entt::entity e, int shapeId, bool isStatic);
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
