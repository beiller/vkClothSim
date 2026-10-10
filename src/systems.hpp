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

// UI windows/widgets are entities (see ecs.hpp); draws every UIWindow with its widget children
void drawUi(World& w);
// spawn a UI window entity; order auto-assigned from World::uiSeq
inline entt::entity spawnUiWindow(World& w, const UIWindow& cfg) {
    entt::entity e = w.reg.create();
    UIWindow c = cfg;
    c.order = w.uiSeq++;
    w.reg.emplace<UIWindow>(e, c);
    return e;
}
// spawn a widget entity parented to win/section; order auto-assigned
template <typename C>
entt::entity spawnWidget(World& w, entt::entity parent, C c) {
    entt::entity e = w.reg.create();
    UIWidget mw;
    mw.order = w.uiSeq++;
    w.reg.emplace<UIWidget>(e, mw);
    w.reg.emplace<C>(e, c);
    w.reg.emplace<Parent>(e, parent);
    return e;
}
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
// Transform, aim pose + trigger -> ControllerInput); a resting pose shows until a pose arrives
void spawnControllers(World& w, entt::entity head);
// headset controller poses -> controller entities' local Transform + ControllerInput (ref space)
void stepControllers(World& w);
// right-controller trigger: aim-ray grab of a dynamic rigid body, release to throw
void stepGrab(World& w, float dt);
