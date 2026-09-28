# OpenXR Plan

Render the 3dsim scene (Jolt capsules + GPU cloth/ball) in stereo under SteamVR on
Linux, with controller "hand" capsules that push the soft bodies. Windowed mode (GLFW)
stays the default; `--xr` launches OpenXR.

## Decisions

| Topic        | Decision                                                    |
|--------------|-------------------------------------------------------------|
| Runtime      | SteamVR (Linux)                                             |
| Modes        | windowed default, `--xr` for OpenXR                         |
| Interaction  | controller hand capsules on day one                         |
| Scene        | rescaled (~0.25x) layout for XR                             |
| Tonemap      | single stereo pass (2 color attachments), not per-eye       |
| Camera       | ECS entity; movement is a system                            |

## Current state

- Frame loop (`main.cpp`): dt -> pin holds -> Jolt step (fixed 1/60, CPU) -> capsule pose
  sync -> sim recorded into its own cmd buffer (submit + fence wait) -> ImGui -> acquire ->
  draw (scene -> HDR, tonemap -> swapchain image + ImGui, submit + fence wait) -> present.
  One fence, fully synchronous, one command buffer per pass group.
- `VkApp` fuses core Vulkan + GLFW window + swapchain + depth + HDR + render passes +
  framebuffers.
- `Renderer` owns geometry/instances, the scene pipeline (per-instance model UBO) and the
  tonemap pipeline; `draw()` records both passes and submits internally.
- `Camera` is a plain struct in `World`: fov/near/far sliders, position/rotation set once
  at startup. **No movement.**
- `SoftSim` / `RigidScene` are already presentation-agnostic (record into / step with a
  command buffer).
- No OpenXR runtime or headers on the system; third-party libs are vendored under `lib/`.
- README's `--shot` mode does not exist in the code (no argv parsing) - fix while
  touching `main.cpp`.

## Target architecture

```
main.cpp            loop: presenter.beginFrame -> sim steps -> renderer.draw(frame) -> presenter.endFrame
+- VkApp            core Vulkan: instance, device, queue, cmd pool, fence
+- present/presenter.hpp      interface: beginFrame(Frame&), endFrame(Frame&), format/extent
|  +- glfw_presenter.*        today's window/swapchain/depth/HDR/render-pass/framebuffer code
|  +- xr_presenter.*          XR instance/system/session, 2 stereo swapchains,
|                             xrWaitFrame, xrLocateView, controller spaces
+- Renderer         per-view HDR/depth/scene-FB; scene pass per eye; single stereo tonemap; UI per eye
+- SoftSim          unchanged
+- RigidScene       unchanged
```

`Frame` (produced by the presenter each frame):

- `views[]`: `{ finalImageView, extent, proj, viewPos, viewQuat }` (1 view windowed, 2 XR)
- XR: wait/signal semaphores, session state
- input: key/mouse state (windowed), controller poses (XR)

Per frame (XR): one command buffer = sim + scene L + scene R + stereo tonemap + UI L +
UI R, submitted once (waits on XR semaphores, our fence, fence-wait), then
`xrEndSession` (signals XR semaphores). Keeps the existing one-fence synchronous style;
in-flight frames are a later optimization.

## Render passes

Windowed (unchanged):

1. scene -> HDR + depth
2. final (swapchain image): tonemap + ImGui

XR:

1. scene L -> HDR_L + depth_L   (viewProj = P_L x V_L, per eye)
2. scene R -> HDR_R + depth_R
3. tonemap - single pass: attachments [swapL, swapR], one fullscreen draw, dual outputs
   `out0 = tonemap(sample(HDR_L, uv))`, `out1 = tonemap(sample(HDR_R, uv))`
4. UI L: ImGui into swapL (load, not clear)
5. UI R: ImGui into swapR

Scene stays per-eye (projections differ -> parallax). Tonemap merges because Vulkan
allows two color attachments in one subpass; the only per-eye input is the two HDR
sources. Caveat: the ImGui Vulkan backend hardcodes a 1-attachment pipeline, so UI gets
its own per-eye pass; a 2-attachment UI+tonemap pass (backend patch) is polish.
Stereo tonemap is a small new shader (dual output); windowed keeps the current one.

## Phases

### P0 - deps

- `lib/openxr/`: `openxr.h` (Khronos) + small `dlopen("libopenxr.so.1")` loader
  (repo convention: vendor; SteamVR ships the runtime library).
- CMake: `option(WITH_OPENXR ...)`, new sources.
- Dev box: Steam + SteamVR running.

### P1 - refactors (windowed behavior unchanged)

- Split `VkApp` -> core Vulkan; move surface/swapchain/depth/HDR/render-pass/framebuffer
  code into `present/glfw_presenter.*` behind the `presenter.hpp` interface.
- Merge the sim cmd buffer into the render cmd buffer (one submit per frame; SoftSim's
  first barrier already uses `ALL_COMMANDS`).
- `Renderer::draw()` takes a `Frame` (per-view targets); `setViewProj` moves into the
  per-view scene pass; submit moves to `presenter.endFrame`.
- Camera into the ECS:
  - `Camera` becomes an EnTT component `{fovDeg, nearP, farP}`; the camera is an entity
    with `Transform` + `Camera`; `World::camera` goes away.
  - New `stepCamera(w, dt)` system: windowed - WASD/QE move + mouse-drag look written
    into the Transform; XR (P2) - copies the head pose into the Transform.
  - viewProj is computed from the entity (Transform + Camera + aspect); the UBO `camPos`
    is the entity position in both modes.
  - UI camera sliders edit the component.
  - Input plumbing: the presenter polls GLFW each frame and exposes keys + mouse delta.
- `main.cpp`: argv parsing (`--xr`); fix or drop the README's `--shot` claim.
- Acceptance: `./lint.sh` green, windowed output unchanged, camera moves with WASD/mouse.

### P2 - XrPresenter + stereo render

- `present/xr_presenter.*`: instance -> system -> device (existing selection logic,
  validated against `XrSystemGraphicsProperties`) -> session ->
  `xrEnumerateSwapchainFormats` -> 2 stereo swapchains (opaque, B8G8R8A8) ->
  `xrBeginSession`.
- `beginFrame`: `xrWaitFrame` -> `xrLocateView` per view (projection + pose ->
  `Frame.views`); acquire both swapchain images; build/rebuild per-view HDR/depth/scene
  framebuffers on extent change.
- `endFrame`: submit (wait on XR semaphores + our fence, fence-wait) then `xrEndSession`
  (signal XR semaphores).
- Renderer: per-view scene passes; single stereo tonemap pass (2 color attachments,
  dual-output shader); per-eye UI passes.
- `stepCamera` XR branch: view 0 pose -> camera entity Transform.
- Acceptance: `--xr` shows the scene in stereo in the SteamVR desktop window, then in
  the HMD; head movement drives the view; windowed still works.

### P3 - hand capsules (day-one interaction)

- `createDemoWorld(w, layout)`: XR layout adds 2 hand collider slots
  (`w.sim.addCapsule(handParams)`) **before** `w.sim.build()` (the capsule GPU buffer is
  sized at build time).
- One hand geometry `makeCapsuleMesh(handParams, 1)` (the mesh fn already takes
  `CapsuleParams`), two renderer instances; per frame set model from controller pose.
- Each frame: `xrLocateSpace` on the two controller spaces (input sources via
  `XR_STANDARD_controller`, SteamVR's standard mapping) -> pose ->
  `renderer.setModel` + `sim.setCapsulePose(slot, pose)`. The sim capsule axis is the
  shader's quat-Y, so align Y with the controller's forward.
- Hands are sim-only colliders (not Jolt bodies): they push cloth + ball without
  disturbing the Jolt capsules, matching the existing one-way design.
- Hand size ~ `{halfLen 0.15, radius 0.05}` at 0.25x world scale - tune.

### P4 - frame pacing

- XR runs at 72/90/120 Hz; sim steps are fixed 1/60. Accumulate the frame delta and run
  `n = clamp(floor(acc / kFrameDt), 0, 2)` sim steps (Jolt + soft, recorded n times into
  the same cmd buffer) per XR frame. Pin holds use real dt.

### P5 - VR layout + tuning

- `createDemoWorld(w, layout)`: XR layout ~0.25x scale - cloth span 2 m at y ~= 1.6,
  ball y ~= 2.5, capsules y 1-2, ground ~30 m, user at origin on the existing y=0
  floor. Windowed keeps today's numbers.
- Cloth fall speed relative to its size: tune via existing per-body `params.mass`
  (gravity term is `kGravity x mass` in the shader) - no code change.
- Polish (optional): foveation (`XR_EXT_foveation`), 2-attachment UI+tonemap pass
  (imgui vulkan backend patch), in-flight frames (fence + UBO ring; current CPU UBO
  writes assume one in-flight frame), drop the hidden GLFW window (UI buttons cover
  reset).

## Risks / notes

- SteamVR on Linux is the least-trodden runtime; first P2 milestone is stereo in the
  SteamVR desktop window before trusting the HMD.
- CPU UBO writes (viewProj, model UBOs, sim params) are safe only because every submit
  fence-waits - keep that invariant until an in-flight design exists.
- XR swapchain format is chosen via `xrEnumerateSwapchainFormats`; render passes are
  built from it (both eyes share one format, so one final render pass serves both).
- Keep `./lint.sh` green at the end of every phase.
