# Architecture

Two servers + world, all data-driven through the ENTT ECS. The registry is the single
source of truth; servers read/write components and never talk to each other directly.

## System map

```
+--------------------------------------------------------------------------------------+
| main.cpp   (glue only: args, server enablement, frame loop, pacing, profiling)        |
+-----------=-----------------------------=---------------------------------------------+
            |                               |
  owns + steps                          owns + steps
            v                               v
+-----------------------+      +------------------------+
| PhysicsServer         |      | GraphicsServer         |
| (src/physics/, Jolt)  |      | (src/vk/ + src/sim/)   |
| CPU rigid bodies      |      | Vulkan renderer        |
| update(reg, dt):      |      | GPU soft PBD sim       |
|  120 Hz accumulator   |      | GPU DQ skinning        |
|  hook -> project ->   |      | canvas targets (UI FBO)|
|  kinematic -> step -> |      | updateSim(reg, dt):    |
|  pose writeback       |      |  skin every frame,     |
| raycast/pose queries  |      |  soft 60 Hz accumulator|
+-----------------------+      +------------------------+
            |                               |
            |  reads/writes                |  reads/writes
            v                               v
+--------------------------------------------------------------------------------------+
| World: entt::registry (src/core/)                                                   |
| Transform Parent WorldTransform  RigidBody KinematicBody Collider *Shape             |
| Joint SixDofJoint  SoftBodyData Skeleton Geometry Renderable Material Texture       |
| GPUTexture Environment PointLight Camera SceneCamera VrCamera VrRoot                |
| SettingsRoot RenderSettings PhysicsSettings  LuaScript UI* Bind Name                |
+--------------------------------------------------------------------------------------+
            |
   also in the world (glue systems, src/systems.cpp):
     resolveWorldTransforms  stepXr/stepControllers  stepMouse/stepKeyboard  stepBinds
            |
            +-- Xr (OpenXR session/views) -> ControllerInput / camera Transform
            +-- Ui (ImGui, one context per canvas) <- canvas FBOs from GraphicsServer
            +-- SceneScript (Lua 5.4 host) -> _on_ready/_animate/_physics hooks
            +-- SceneLoader (JSON -> ECS; spawns the settings entity)
```

Cross-server seams (the only ones):
- `IRaycast` — UI picking + Lua `physics.raycast` call `PhysicsServer::raycast`
- canvas targets — UI draws into `GraphicsServer::uiTargetPass/Fb`, reads `canvasImage`
- drawable vertex buffers — soft/skin adopt renderer-owned buffers via `requestVerts`
  (in-server; headless mode: GraphicsServer allocates them itself)

## Server enablement

| Flag | window | device | physics | soft/skin | draw | UI | XR |
|------|--------|--------|---------|-----------|------|----|----|
| (default) | yes | yes | on | on | on | on | auto |
| `--no-graphics` | no | **no** | on | off | off | off | off |
| `--headless` | no | yes (device-only) | on | on | off | off | off |

Optional `--frames N` exits after N frames (CI). Serverless/headless use a fixed
`kFrameDt` per frame for determinism; windowed mode uses the measured frame dt.

## Per-frame pipeline (windowed mode)

1. poll/resize; `xr.waitFrame/poll` -> XrFrame
2. `graphics.flushFrees`; pending scene swap -> `loadScene`
3. `stepMouse/stepKeyboard` (GLFW -> MouseInput/KeyboardInput)
4. `script.activateScripts + animateAll(dt)` (`_animate` hooks)
5. `stepXr + stepControllers` (when an XR session runs)
6. `physics.update(reg, dt)` — 120 Hz accumulator; per internal step: `_physics` hook ->
   Jolt project/kinematic/step -> pose writeback to Transform
7. `resolveWorldTransforms` — Transform + Parent -> WorldTransform (hierarchy)
8. `graphics.updateSim(reg, dt)` — skin DQ step (own submit); soft PBD on its 60 Hz
   accumulator (own submit; verlet integrate -> graph-colored distance constraints ->
   capsule collision -> normal recompute)
9. camera -> `setViewProj`; UI canvas picking (raycast vs canvas colliders -> uv)
10. `uiFrame` (ImGui draw into canvas FBOs); `stepBinds` (Bind component sync)
11. acquire -> `draw`/`drawVr` (PBR + IBL, PCSS shadows, SSS/glass, tonemap, ImGui overlay)
    -> present

## Scene content

- `assets/scenes/*.json` — objects with components; one camera object required; optional
  `"set"` block of short URLs (`render.exposure`, `rigid.velocitySteps`) resolved against
  the settings entity.
- per-entity `.lua` behavior scripts; hooks `_on_ready` (once), `_animate` (per render
  frame), `_physics` (per fixed sim step). All geometry is generated in Lua
  (`assets/scenes/mesh.lua` is always loaded as the global `mesh`).

## Lua API (one table per subsystem)

| Table | Host | Functions |
|-------|------|-----------|
| `scene.*` | world / loader | entity emplace replace component remove set_pose get_pose world_pose set_morph shapes link_mesh get_entity find_components attach_script destroy add_child time button scenes request_scene last_error set get |
| `math.*` | — | vadd vsub vscale vlen vnorm vcross vdot qmul qinv qaxis qrot qfromto |
| `physics.*` | PhysicsServer | raycast |
| `soft.*` | GraphicsServer | set_floor |
| `graphics.*` | GraphicsServer | gltf load_texture render_texture |
| `ui.*` | UI | window tab editor button text section slider_f slider_i checkbox color drag_v3 fire_button |
| `input.*` | input systems | key_down |
| `mesh.*` | — (geometry stdlib) | box sphere capsule plane tapered ball + custom generators |

`scene.set/get` take a URL: `name.Component.field`, or the short forms `render.<field>` /
`rigid.<field>` which resolve against the settings entity's RenderSettings /
PhysicsSettings components.

## Build layout (CMake)

- `3dsim_core` — components, reflection, URL resolution, math/mesh types
- `3dsim_io` — gltf + asset loaders
- `3dsim_solvers` — tapered-capsule shape fitter
- `3dsim_physics` — RigidScene (Jolt) + PhysicsServer
- `3dsim_graphics` — VkApp, Renderer, soft/skin GPU sim + GraphicsServer
- `3dsim_xr` — OpenXR backend
- `3dsim_ui` — ImGui host (one context per canvas)
- `3dsim_script` — Lua 5.4 host + per-domain API modules
- `vksim` — main.cpp + systems.cpp + scene.cpp (glue only)

CPU self-tests (no GPU): `dualquat_test`, `view_proj_test`, `collision_shape_solver_test`.

## Notes

- sim runs at a fixed timestep (120 Hz rigid / 60 Hz soft), decoupled from the render
  rate by per-server accumulators; NO_VSYNC pacing in main is pure render pacing.
- the soft sim and renderer never exchange vertex data on the CPU — sim writes into
  renderer-owned buffers (or GraphicsServer-owned in headless mode).
- Jolt capsules are the only bridge between the rigid and soft worlds
  (WorldTransform -> GPU capsule buffer each soft step).
