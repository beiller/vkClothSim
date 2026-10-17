#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "ui.hpp"
#include "engine.hpp"
#include <imgui.h>
#include <vector>

// add a visual to an entity: Renderable(geom) + MaterialRef + optional Parent (noShadow skips the
// shadow pass); ensures a Transform (identity if absent) since a mesh without a pose sits at the
// origin. geom = the geometry entity holding the vertex data (a Geometry component)
void addMesh(Engine& w, entt::entity e, entt::entity geom, entt::entity mat, entt::entity parent, bool noShadow = false);
// shared material entity (Material, optional Name); meshes reference it via MaterialRef
entt::entity spawnMaterial(Engine& w, const V3& baseColor, float metallic, float roughness, const V3& emissive,
                           const char* name = nullptr);
// first entity with a matching Name (entt::null if none)
entt::entity findNamedEntity(const Engine& w, const std::string& name);
// shared texture entity from decoded RGBA8 bytes (a glTF image, embedded or external): deduped
// by id, which doubles as the Name ("file.glb#img0" or a resolved file path); the upload is
// eager because the bytes exist only at import time
entt::entity textureBytes(Engine& w, const std::string& id, const uint8_t* rgba, int wpx, int hpx);
// the active camera (entt::null if none)
entt::entity findActiveCamera(const Engine& w);
// compose an entity's Transform through its Parent chain (world space); valid before/after resolve
Transform worldTransform(entt::registry& reg, entt::entity e);
Mat4 cameraViewProj(Engine& w, entt::entity cam, float aspect);

// project new RigidBody/KinematicBody/Joint components to Jolt, pull each kinematic body's
// world Transform into Jolt, step the sim, then copy the dynamic bodies' poses back into the
// ECS (kinematic poses are script/XR-owned, never copied back)
void stepRigid(Engine& w);
// compose each entity's Transform through its Parent chain into a WorldTransform
void resolveWorldTransforms(Engine& w);
void syncColliders(Engine& w);
// first-sight projection of new SoftBodyData components to GPU bodies, then record the sim
void stepSoft(Engine& w, VkCommandBuffer cmd);
// query the ECS and render the scene (mono) to the swapchain image
void draw(Engine& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui);

// current camera: the VR head camera while presenting, the scene's fly camera otherwise;
// headset pose -> vr cam's LOCAL Transform (a child of the VR root, so its world = walkPos +
// head); synthesize the camera's per-eye views (headset eyes + walkPos, or the fly camera's
// world pose with virtual lens tangents) -> the Camera component's views
void stepXr(Engine& w, const XrFrame& fr);
// head-independent VR locomotion anchor: a root the VR head camera + grips are parented under;
// also spawns the "vr cam" (the VR head camera)
entt::entity spawnVrRoot(Engine& w);
// create a tracked controller entity per hand, parented to the VR root (grip visual + input
// state); physics bodies are a scene-script choice (a KinematicBody component)
void spawnControllers(Engine& w);
// headset controller poses -> controller entities' local Transform + ControllerInput
void stepControllers(Engine& w, const XrFrame& fr);
// GLFW -> "mouse" entity (position + buttons)
void stepMouse(Engine& w);
// GLFW -> "keyboard" entity (key states)
void stepKeyboard(Engine& w);
// create the "mouse" and "keyboard" entities (call once at startup)
void spawnInputEntities(Engine& w);
