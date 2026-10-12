#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "ui.hpp"
#include "world.hpp"
#include <imgui.h>
#include <vector>

entt::entity spawnCapsule(World& w, const RigidBody& body, int geom);
entt::entity spawnSoftBody(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, int pinned);
// static render-only mesh (no physics) from an uploaded geom
entt::entity spawnStaticGeom(World& w, int geom, const Transform& t);
// shared material entity (Material, optional Name); meshes reference it via MaterialRef
entt::entity spawnMaterial(World& w, const V3& baseColor, float metallic, float roughness, const char* name = nullptr);
entt::entity spawnLight(World& w, const V3& pos, const PointLight& p, const char* name);
// a plain camera entity (Transform + Camera). active = the window renders it; one by convention
entt::entity spawnCamera(World& w, const V3& pos, const V4& quat, float fovDeg, bool active = true);
// first entity with a matching Name (entt::null if none)
entt::entity findNamedEntity(const World& w, const std::string& name);
// dynamic rigid capsule sharing one material entity; t = initial pose, shape = Jolt collider
entt::entity spawnDynamicCapsule(World& w, int geom, const CapsuleParams& shape, const Transform& t, entt::entity mat);
// render-only capsule (no physics), optionally parented; for kinematic hierarchies
entt::entity spawnVisualCapsule(World& w, int geom, const Transform& t, entt::entity mat, entt::entity parent);
// soft body from script-provided mesh + constraints; pinned for pinSecs when > 0
entt::entity spawnSoftCloth(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, entt::entity mat,
                            float pinSecs);
// soft sphere; mass > 0 overrides the default
entt::entity spawnSoftBall(World& w, const Mesh& mesh, const std::vector<sim::Constraint>& cons, entt::entity mat,
                           float mass);
// texture from assets/ (or assets/ relative to the exe); renderer texture id or -1
int loadAssetTexture(World& w, const std::string& path);
// the active camera (entt::null if none)
entt::entity findActiveCamera(const World& w);
// compose an entity's Transform through its Parent chain (world space); valid before/after resolve
Transform worldTransform(entt::registry& reg, entt::entity e);
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
void draw(World& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui);

// mouse/keyboard move/look on the active camera (fly camera, or the virtual head when the XR
// session is not running); skipped while a headset pose is active
void stepFlyCamera(World& w, float dt);
// headset pose -> active camera's LOCAL Transform (the camera is a child of the VR root, so its
// world = walkPos + head); synthesize per-eye poses (headset eyes + walkPos, or the camera's world
// pose with virtual lens FOVs) -> setVrEyes
void stepXr(World& w);
// head-independent VR locomotion anchor: a root the head + grips are parented under. Its world
// Transform is walkPos (stick-driven, never the head), so world = walkPos + pose -> no drift
entt::entity spawnVrRoot(World& w, entt::entity head);
// right-stick smooth locomotion: integrate walkPos along the head's horizontal facing
void stepLocomotion(World& w, float dt);
// create a tracked controller entity per hand, parented to the VR root (grip pose -> LOCAL
// Transform, aim pose + trigger -> ControllerInput, Grab on the right hand); a resting pose
// shows until a pose arrives
void spawnControllers(World& w);
// headset controller poses -> controller entities' local Transform + ControllerInput (ref space)
void stepControllers(World& w);
// right-controller trigger: aim-ray grab of a dynamic rigid body, release to throw
void stepGrab(World& w, float dt);
