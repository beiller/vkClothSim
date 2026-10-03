# Architecture

Jolt rigid-body sim with a GPU soft-body layer (cloth + ball) using Jolt capsules as colliders.
Soft-body bodies are passed to the GPU as vertices + two-point joints; Jolt capsules are passed as colliders.

## System map

```
+--------------------------------------------------------------------------------+
| main.cpp   (glue only)                                                         |
| args -> VR auto-detect -> scene pick -> ImGui init -> frame loop -> present    |
+-----------=-------------------------------=------------------------------------+
            |                               |
            |  selects                      |
            |            new frame + draw   |
            v                               v
+-----------------------+      +------------------------+
| scenes/ (src/scenes/) |      | ImGui + UIState ui     |
| demo hdri hierarchy   |      | joltIters, exposure,   |
| shadowtest            |      | envIntensity, bgColor, |
| spawn*() -> ECS       |      | drawUi(World&) fn      |
| (systems.cpp)         |      +------------------------+
+-----------------------+
            |                               |
            |  spawn*() (entities + comps)  |
            |           ui.* read by loop   |
            v                               v
      +-----=-------------------------------=--------+
      | World  (world.hpp)   --   shared state       |
      | entt::registry reg   (ecs.hpp)               |
      | Transform  Parent  WorldTransform  Animation |
      | RigidDynamics  RigidStatic  CapsuleCollider  |
      | SoftBodyData  PinHold  Camera  Renderable    |
      | Material  PointLight  Name                   |
      +----------------------------------------------+
                              |
                              |  owns (member objects)
          |===================|===|=======================|====================|
          v                       v                       v                    v
  +-----=---------+   +---------=------------+   +------=---------+   +------=----------+
  | RigidScene    |   | SoftSim              |   | Renderer       |   | Xr (OpenXR)     |
  | (Jolt, CPU)   |   | (GPU compute)        |   | (Vulkan)       |   | session, views, |
  | app/rigid.cpp |   | sim/ + softbody.comp |   | vk/ + shaders/ |   | swapchains      |
  +---------------+   +----------------------+   +----------------+   +-----------------+
                                  |                       |
                                  |   record + submit cmd buffers (per frame)
                                  |=======================|
                                              v
       +--------------------------------------=-------------------------------------+
       | VkApp  (vk/)   --   GLFW + Vulkan foundation                               |
       | window, instance/device/queue, swapchain, HDR target, depth, cmd pools     |
       +----------------------------------------------------------------------------+
```

## Interactions

1. **scenes/ -> ECS registry** — `spawn*()` (systems.cpp) creates entities + components once at startup (capsules, soft bodies, static meshes, lights, cameras, VR rig)
2. **frame loop <-> UIState ui** — loop reads `ui.joltIters`/`exposure`/`envIntensity`; scene `drawUi()` writes them (ImGui widgets)
3. **ECS <-> RigidScene** — Jolt poses -> `Transform` (RigidDynamics); `Transform` -> static pose (RigidStatic, user-driven bodies, `stepStaticRigid`)
4. **ECS -> SoftSim** — capsule poses: `WorldTransform` -> `sim.setCapsulePose` (GPU capsule buffer); `SoftBodyData` -> pinned/params/steps
5. **SoftSim <-> Renderer** — shared pos/nrm VkBuffers = soft-body mesh verts (`renderer.addMesh` hands the same buffers to `sim.addSoftBody`)
6. **Xr -> ECS** — head pose -> active camera `Transform` (`stepXr`); fly camera is skipped while a session is running
7. **Xr -> Renderer** — per-eye pose + lens tangents -> `setVrEyes()`; swapchain images for direct headset render
8. **ECS -> Renderer** — `syncSceneToRenderer`: Renderable+WorldTransform -> model matrices, Material -> per-instance PBR params, PointLight -> lights UBO
9. **SoftSim, Renderer -> VkApp** — record into VkApp command pools, submit per frame (soft sim uses its own simCmd)
10. **assets.cpp (TinyEXR) -> Renderer** — equirect HDR -> `env_cube.comp` -> `env_prefilter.comp` (spec mips) + `env_irradiance.comp`, used as split-sum IBL
11. **RigidScene internals** — Jolt PhysicsSystem + JobSystemThreadPool + TempAllocator

## Per-frame pipeline

Order in `main.cpp`, fixed dt = 1/60:

1. R key — `resetSofts`: SoftBodyData -> `sim.resetSoft` (GPU pos/prev/nrm <- rest pose)
2. `stepPinHolds` — PinHold + SoftBodyData -> `sb.pinned` (unpin after holdTime)
3. `stepRigid` — Jolt `Update(kFrameDt)`; poses -> ECS Transform
4. `stepAnimation` — Animation fn mutates its entity's Transform
5. `pollEvents`; `xr->poll` -> XrFrame (eye poses)
6. `stepFlyCamera` — mouse/WASD/QE -> camera Transform (only when no XR session)
7. `stepXr` — head -> camera Transform; eyes -> `renderer.setVrEyes`
8. `resolveWorldTransforms` — Transform + Parent -> WorldTransform (hierarchy)
9. `syncColliders` — CapsuleCollider + WorldTransform -> `sim.setCapsulePose`
10. `stepSoft` — SoftBodyData -> `sim.setPinned/setParams`; `sim.record(simCmd)`:
    - per substep: mode 0 verlet integrate (damping/gravity/clamp) -> mode 1 distance constraints x passes x graph-color groups (one dispatch per color) -> mode 2 capsule collision + velocity clamp
    - after all substeps: mode 3 recompute normals
    - `app.submit(simCmd)`
11. `stepStaticRigid` — Transform -> Jolt static body pose
12. `setViewProj` — camera WorldTransform -> view/proj + camPos UBO
13. `draw` / `drawVr` — `syncSceneToRenderer` -> `renderShadowCubes` (per-light cube) -> `renderScenePass` (PBR + IBL + shadows -> HDR target) -> tonemap -> window and/or per-VR-eye targets -> ImGui overlay
14. `present`

## Notes

- The ECS `World` is the hub: the four sub-systems never talk to each other directly, everything goes through entities/components.
- The soft sim and renderer are decoupled purely by the shared GPU vertex buffers (no CPU readback of cloth verts).
- Jolt capsules are the only bridge between the rigid and soft worlds (pos -> GPU capsule buffer, step 9).
