# 3dsim

GPU physics-based character simulator. Pure **Vulkan** renderer + **Jolt** (C++)
rigid-body physics + a **CPU soft-body simulation**. `raylib` is used only for
`raymath` (header-only C); its GL renderer is NOT used. **Dear ImGui** (Vulkan backend)
provides the UI. GLFW (vendored) provides the window + input.

**Separation of concerns** — the soft-body sim is **decoupled from the renderer**:
- `src/sim/sim.hpp` — a **universal** position-based (Verlet + sequential Gauss-Seidel)
  solver. It takes a plain **vertex list** + a list of **two-point distance constraints**,
  integrates (Verlet + gravity) and solves the constraints, and returns the vertices. It
  knows **nothing** about meshes, triangles, pressure, capsules, or the ground — those are
  supplied by the caller. The **same solver** governs every soft body (cloth, ball, ...).
- `src/vk_main.cpp` — the **app**: builds each body's vertices + constraints, steps the
  solver, then applies the **app-level** extras around it (one-way **collision** against the
  Jolt capsules + ground, and the ball's **pressure** via a PBD volume constraint that uses
  the ball's triangles), then uploads the vertices to the GPU and renders.

## The goal (current task)
A Vulkan window showing:
- **Jolt** with a **static ground plane** + **16 dynamic capsules** that fall and **clump
  into a pile** (mostly-horizontal, friction 0.7, low restitution). The capsules are the
  obstacles the cloth drapes over.
- A **high-res cloth** (64x64 = 4096 pts) governed by the universal solver (Verlet +
  constraints + damping, 3 sub-steps per step). It starts **held flat above the pile**
  (`pinned`), then **unpins** (after `CLOTH_HOLD=150` frames / `R` resets) and **falls onto
  the clumped pile**, draping over it. The cloth is pushed out of the capsules (capsule SDF)
  AND the **ground plane** (y=0) — **obstacles affect the cloth, the cloth does NOT affect
  them** (one-way coupling).
- A **soft-body ball** (UV-sphere, 242 pts) governed by the **same** solver. It **falls**
  (gravity), **lands** on the ground (one-way collision), and is **expand/deflated** by a
  **pressure** slider (a PBD volume constraint applied in the app layer).
- **ImGui** overlay (HUD + controls) drawn on top in the same swapchain — zero CPU pixel
  copy per frame.

**The soft-body sim runs on the CPU** (the universal solver in `sim/sim.hpp`), NOT on the
GPU. A parallel GPU constraint (read/write race on neighbor points) pumped energy and made
the 64x64 sheet explode; the CPU uses **sequential Gauss-Seidel** (no race) so it is
deterministic and stable. The solver stores 3 floats/vertex; the app packs them to the
GPU's 16-byte `vec3` stride on upload (`uploadVerts`). ~1 ms/frame at 3 sub-steps x 4
iterations.

## Data flow (per frame)
```
RigidScene (Jolt, CPU): step the dynamic capsules on the ground (fall + clump)
sim (CPU):  for each soft body (cloth, ball):
      -> SoftBody.step(): Verlet + sequential Gauss-Seidel constraints
      -> applyPressure (ball): PBD volume to the target
      -> applyCollision: one-way push-out against RigidScene.colliders() + the ground
Renderer (Vulkan, GPU):
  -> upload the bodies' vertices + the capsule transforms (RigidScene.capsuleGPU()) to the GPU
  -> render pass: draw scene bg + cloth + ball + instanced capsules
  -> ImGui overlay (app::drawOverlay, same swapchain image)
  -> present
```

## Build & run
```sh
cmake -S . -B build -Wno-dev
cmake --build build -j
./build/vksim              # the Vulkan + Jolt + cloth app
./build/vksim --shot s.ppm # render one frame, write PPM, exit (headless check)
```
Targets: `vksim` (main, pure Vulkan+Jolt+cloth), `caps3d` (raylib/GL capsule viewer, reference),
`sim3d` (headless Vulkan SDF smoke test, optional). Needs Vulkan SDK, `glslc`, `xxd`.

## Layout
Concerns are split so each is independently testable + has no cross-dependencies:
- `src/sim/sim.hpp`    – **simulator**: the universal soft-body solver (`SoftBody`: Verlet +
  sequential Gauss-Seidel) + the app-level physics helpers (`applyCollision`, `applyPressure`,
  `bodyVolume`) + the body builders (`makeCloth`, `makeBall`). No Vulkan/Jolt/ImGui. A body is
  just (n vertices, distance constraints, a param set); the SAME solver governs every body.
- `src/app/rigid.{hpp,cpp}` – **rigid-body physics** (Jolt): `RigidScene` (ground + 16 clumping
  capsules). Exposes the capsules' transforms as plain data: `colliders()` (sim::Collider
  segments for the one-way soft-body collision) + `capsuleGPU()` (per-instance render data).
- `src/app/ui.{hpp,cpp}`     – **UI** (Dear ImGui): `UIState` (the user-tunable params) +
  `drawOverlay` (the HUD + controls). No Vulkan/Jolt/sim.
- `src/vk_main.cpp`        – **renderer** (Vulkan) + the main loop (integration): the swapchain,
  the per-mesh pipelines (scene / capsules / cloth / ball — `makeGraphicsPipeline` shares the
  pipeline state), the vertex upload, the frame loop (step rigid + step soft-body + render + UI).
- `src/main.cpp`           – headless Vulkan SDF smoke test
- `src/caps3d.cpp`         – raylib/GL 3D capsule viewer (reference)
- `shaders/`               – GLSL (compiled to SPIR-V at build time via glslc+xxd):
  - `vk_bg_vert/frag` – full-screen triangle (scene bg, color from a uniform)
  - `cloth.comp`      – (UNUSED by the sim; the cloth sim moved to the CPU. Kept for reference.)
  - `cloth_vert/frag` – cloth mesh render (derivative normals, reads the pos SSBO)
  - `ball_vert/frag`  – ball mesh render (outward normal via centroid, reads the pos SSBO)
  - `caps_vert/frag`  – instanced capsule render (per-instance center/quat/radius/halfLen)
- `lib/jolt`         – vendored Jolt Physics (C++)
- `lib/imgui`        – vendored Dear ImGui (Vulkan + GLFW backends)
- `lib/glfw`         – vendored GLFW (window + VkSurface)
- `lib/raylib`       – vendored raylib (for `raymath` only)
- `build/gen/*_spv.hpp` – SPIR-V baked to C arrays at build time (do not edit)

## Conventions
- C++23, minimal, vendored deps (no system installs beyond the Vulkan SDK + glslc/xxd).
- Vulkan: one render pass into the swapchain image; scene + cloth + capsules + ImGui all draw
  into it. Fences for per-frame sync.
- GLSL struct layout in `shaders/` must be mirrored exactly in C++ (`static_assert` sizes).
- Capsule = segment [a,b] + radius. Cloth collision uses the point→capsule SDF (one-way push-out).
- One-way coupling is deliberate: the cloth reads capsule transforms, never writes back.

## Status
DONE. All phases complete and verified (`--shot` pixel analysis + live-mode stability).
1. Pure-Vulkan + GLFW + ImGui window (HUD, `--shot` PPM readback).
2. Jolt (vendored v5.6.0, static `libJolt.a`) built + linked.
3. Jolt scene: static ground + 16 **dynamic** capsules (fall + clump); 3D camera + depth;
   instanced capsule/ground render.
4. **CPU cloth** (`stepClothCPU`): Verlet + sequential Gauss-Seidel constraints + one-way
   capsule/ground SDF push-out → positions uploaded to a GPU SSBO → cloth mesh render.

## Toolchain gotchas (Vulkan 1.4 / SDK 1.4.350, glslc=ANGLE)
- `VkBufferMemoryBarrier`/`VkImageMemoryBarrier` have **no** `srcStageMask`/`dstStageMask`
  fields; the pipeline stages go in the `vkCmdPipelineBarrier(...)` args, not the barrier structs.
- `VK_ACCESS_STORAGE_BUFFER_READ/WRITE_BIT` do **not** exist in this SDK; use the generic
  `VK_ACCESS_SHADER_READ_BIT` / `VK_ACCESS_SHADER_WRITE_BIT` for storage-buffer barriers.
- `vkGetPhysicalDevice*` query entrypoints return `void`.
- glslc: cannot variable-index a buffer-block array when the block has **two** unknown-size
  arrays → split into separate single-array blocks (cloth `Pos`/`Prev`).
- GLSL: declare `struct` at global scope (not nested in a buffer block); buffer-block members
  accessed via the instance (`cb.pos[i]`); fragment `in` vars need `layout(location=N)`;
  use `int(x)` not C-cast `(int)x`; `vec3(a[0],a[1],a[2])` not `vec3(arr)`.
- Params: use a **storage buffer** (`layout(binding=3) buffer Params{...} p;`, std430), NOT a
  `uniform` block. glslc/ANGLE lays out a std140 `float arr[3]` with **16-byte base
  alignment** (first element at offset 16, not 4), shifting later fields past the C++ struct
  size so the GPU reads them as 0. std430 flat-scalar layout matches the C++ struct exactly.
- **Do NOT insert a COMPUTE→COMPUTE barrier between cloth dispatches, repeated every frame.**
  On the NVIDIA driver this resets the device (`VK_ERROR_DEVICE_LOST`, code −4) within ~60
  frames. Instead run all Verlet **sub-steps inside one dispatch** (`p.substeps` loop in
  `cloth.comp`) so there are no inter-dispatch barriers.

## Status / next steps
Current scene: **16 dynamic capsules** (fall + clump into a pile) + high-res **cloth**
(64x64, CPU Verlet + sequential Gauss-Seidel, 3 sub-steps, 4 constraint iters, damping 0.999)
that **unpins** (after `CLOTH_HOLD=150` frames) and drapes over the clumped pile. `R` resets
(re-pins + restarts). `--shot out.ppm [--steps N]` (default 150) renders the settled drape.
All verified (live + shot, deterministic, ~60fps, no device loss).

**Stability notes** (why the cloth moved to the CPU): the original **GPU** constraint applied
distance relaxation in parallel across a 64x64 grid. The cross-thread race on neighbor points
(both the Gauss-Seidel write-race and a Jacobi read-race) pumped energy into the sheet — it
buckled and then **exploded** (points flew to y=400s / NaN) as Verlet inherited the spurious
displacement as velocity. A parallel race can't be made deterministic. The sim now runs on the
**CPU** with **sequential** Gauss-Seidel (no race) → deterministic + stable; the collision
response still removes **normal velocity** so impact doesn't inject energy.

**Performance**: the CPU cloth sim is ~1 ms/frame (3 sub-steps x 4 iters x 4096 pts); frame rate
is dominated by the render pass / vsync and by **any other GPU process** sharing the device
(check `nvidia-smi` — a large Python GPU process will cap the fps). The per-frame CPU→GPU copy
is tiny (~64 KB cloth positions + 1 KB capsule transforms/params); it is **not** the vertex
count or a per-vertex upload.

**Physical units**: the scene is in **meters** (cloth 8 m wide, capsules ~0.5 m radius), so
`gravity = 9.81 m/s^2` and `dt = (1/60)/3 = 1/180 s` are realistic. Damping is applied **per
sub-step**, so it compounds 180x/s — it must be near-1 to feel natural. `0.985` over-damped the
fall to ~2.6x slower than free-fall (sluggish); `0.999` gives ~66% of free-fall speed, a
realistic cloth-in-air feel.

**Cloth sim parameters** (real-time via the ImGui window, struct `ClothSim g_cloth`):
- `mass` (0.1–10, def 1.0) — scales gravity (`9.81*mass`); heavier = falls faster, drapes more.
- `damping` (0.90–1.00, def 0.999) — velocity damping per sub-step (low = stiffer, high = fluid).
- `stiffness` (1–16, def 4) — Gauss-Seidel constraint iterations (higher = more rigid).
- `tension` (0.5–1.5, def 1.0) — rest-length scale (<1 loose/wrinkly, >1 taut).
All read by `stepClothCPU` each step (no GPU round-trip); a "reset params" button restores defaults.

Next (optional polish): two-way coupling, per-frame shot (video), camera framing, per-point
mass / friction. SPIR-V is baked at **configure** time — re-run `cmake -S . -B build` after
editing any shader.
