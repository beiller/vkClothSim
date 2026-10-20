#pragma once
#include <vulkan/vulkan.h>

#include "ecs.hpp"
#include "mesh.hpp"
#include "sim/sim.hpp"
#include "ui.hpp"
#include "engine.hpp"
#include <imgui.h>
#include <vector>

// add a visual: Renderable(geom) + MaterialRef + optional Parent (noShadow skips the shadow
// pass); ensures a Transform (identity if absent). geom = the geometry entity (vertex data)
void addMesh(Engine& w, entt::entity e, entt::entity geom, entt::entity mat, entt::entity parent, bool noShadow = false);
// shared material entity (Material, optional Name); meshes reference it via MaterialRef
entt::entity spawnMaterial(Engine& w, const V3& baseColor, float metallic, float roughness, const V3& emissive,
                           const char* name = nullptr);

// shared texture entity from decoded RGBA8 bytes (glTF image): deduped by id (doubles as the
// Name); upload is eager because the bytes exist only at import time
entt::entity textureBytes(Engine& w, const std::string& id, const uint8_t* rgba, int wpx, int hpx);
// the active camera (entt::null if none)
entt::entity findActiveCamera(const Engine& w);
// the camera's full view data (the SSS/glass passes need the view matrix + frustum, not just
// the combined viewProj): the signed tangents match the XR View convention
CameraView cameraView(Engine& w, entt::entity cam, float aspect);

// project new RigidBody/KinematicBody/Joint components to Jolt, step, copy dynamic poses back
// into the ECS (kinematic poses are script/XR-owned, never copied back)
void stepRigid(Engine& w);
// compose each entity's Transform through its Parent chain into a WorldTransform
void resolveWorldTransforms(Engine& w);
// first-sight projection of new SoftBodyData components to GPU bodies, then record the sim
void stepSoft(Engine& w, VkCommandBuffer cmd);
// query the ECS and render the scene (mono) to the swapchain image
void draw(Engine& w, VkCommandBuffer cmd, uint32_t fb, ImDrawData* imgui);

// headset pose -> vr cam's LOCAL Transform (child of the VR root: world = walkPos + head);
// synthesize per-eye views (headset eyes + walkPos, or fly camera + virtual lens tangents)
void stepXr(Engine& w, const XrFrame& fr);
// head-independent VR locomotion anchor (VR head camera + grips parented under it); also spawns
// the "vr cam"
entt::entity spawnVrRoot(Engine& w);
// a tracked controller entity per hand, parented to the VR root; physics bodies are a
// scene-script choice (a KinematicBody component)
void spawnControllers(Engine& w);
// headset controller poses -> controller entities' local Transform + ControllerInput
void stepControllers(Engine& w, const XrFrame& fr);
// GLFW -> "mouse" entity (position + buttons)
void stepMouse(Engine& w);
// GLFW -> "keyboard" entity (key states)
void stepKeyboard(Engine& w);
// create the "mouse" and "keyboard" entities (call once at startup)
void spawnInputEntities(Engine& w);
