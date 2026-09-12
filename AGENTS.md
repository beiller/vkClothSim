# 3dsim

A Jolt physics simulation, with a GPU soft-body simulation bolted on top that runs on the
GPU and uses capsules as colliders. The soft bodies (the cloth + the ball) both run on the
GPU. The compute shader is passed just two things per body — its **vertices** and its
**joints** (two-point distance constraints) — plus the Jolt capsules (as colliders). The
capsules from Jolt are passed to the sim, which runs on the GPU.

## Files

- `src/main.cpp` — the app: window + frame loop + `--shot`.
- `src/vk/vkapp.{hpp,cpp}` — GLFW window + Vulkan core (instance/device/swapchain/render pass/command buffer/submit, synchronous per frame) + PPM readback.
- `src/vk/vkutil.hpp` — Vulkan helpers: `VK()` macro, memory-type lookup, buffer creation.
- `src/vk/renderer.{hpp,cpp}` — the GPU: pipelines + buffers, the **soft-body compute sim** (per-frame dispatch for the cloth + ball), the per-frame uploads, and the draws.
- `src/app/rigid.{hpp,cpp}` — Jolt: static ground + 50 dynamic capsules that fall and clump; exposes their transforms as plain data (colliders for the GPU sim + per-instance render data).
- `src/app/scene.{hpp,cpp}` — the simulation side: steps Jolt and supplies each soft body's initial vertices + joints + sim params to the GPU sim.
- `src/app/params.hpp` — `SimParams` (the user-tunable soft-body params).
- `src/app/ui.{hpp,cpp}` — the Dear ImGui HUD + controls.
- `src/sim/sim.hpp` — the body builders (`makeCloth`, `makeBall`: vertices + joints) + the CPU soft-body solver (used only by the headless `simtest`).
- `src/math.hpp` — minimal column-major Mat4/V3.
- `src/simtest.cpp` — headless CPU soft-body test (`simtest`).
- `src/penprobe.cpp` — headless penetration probe (`penprobe`; CPU bodies only — no Vulkan).
- `src/smoke.cpp` — headless Vulkan SDF smoke test (`sim3d`).
- `src/caps3d.cpp` — raylib/GL capsule viewer (reference, off by default).
- `shaders/` — GLSL, compiled to SPIR-V at configure time (glslc + xxd):
  - `softbody.comp` — the **GPU soft-body sim**: Verlet + joint (distance-constraint) relaxation + one-way capsule/ground collision. Double-buffered, race-free. Steps both the cloth and the ball.
  - `cloth_vert/frag` — cloth mesh render (reads the pos SSBO).
  - `ball_vert/frag` — ball mesh render. `caps_vert/frag` — instanced capsule render. `vk_bg_*` — background.
- `lib/` — vendored Jolt, Dear ImGui, GLFW, raylib.
- `build/gen/*_spv.hpp` — SPIR-V baked to C arrays at configure time (do not edit).

## Build & run

```sh
cmake -S . -B build -Wno-dev        # re-run after editing any shader (SPIR-V is baked here)
cmake --build build -j
./build/vksim                        # the app
./build/vksim --shot s.ppm [--steps N]   # settle N steps, render one frame, write PPM, exit
./build/simtest 120                  # headless CPU soft-body test
```

## Notes

- The soft-body sim (cloth **and** ball) runs on the **GPU** (`shaders/softbody.comp`): one-way
  collision against the Jolt capsules + the ground. The capsules never feel the soft bodies
  (one-way). The two soft bodies do not collide with each other.
- The compute shader is passed each body's **vertices** + **joints** (an explicit list of
  two-point distance constraints) + the capsules. The joint list is what lets the ONE shader
  step both the grid cloth and the sphere ball. There is **no pressure** on the ball (it is a
  plain soft body held by its joints).
- The sim is **double-buffered** (read buffer A, write buffer B, swap) so no thread ever reads
  a slot another thread is writing — a read/write race on shared vertices is what made the first
  GPU cloth explode. **Every** dispatch must write **every** slot to the other buffer (even an
  unchanged vertex) — a conditional write leaves the other buffer stale.
- SPIR-V is baked at **configure** time — re-run `cmake -S . -B build` after editing a shader.

## Status

DONE. The GPU soft-body sim (cloth **and** ball) is stable and verified:
- `--shot` (default 450 steps) settles to a **drape** (cloth y-range ~4 m, ~8 m wide, **0
  penetration**, the ball lands on the pile). The PPM is **byte-identical** across runs
  (deterministic); no `VK_ERROR_DEVICE_LOST` over 60 s of live mode (36 compute dispatches ×
  3 barriers per frame).
- `--shot` diagnostics: cloth x/z + y range, pile extent + centroid, cloth contact, and a
  CPU-side **penetration** check on the read-back GPU positions (the key correctness metric).

## Gotchas (learned the hard way)
- The shared `Phys` SSBO descriptor range **must** equal the C++ struct size. It was left at 24
  bytes while the struct grew (28, then 32), so `tension`/`relaxScale` read **out of bounds →
  garbage** → the rest length was garbage → the relax exploded the cloth to 17 km. The fields
  before the gap (`nCaps..skin`) were fine, which is why the fall looked OK until the impact.
- `Entry.j` (the neighbor index) is an **int** in memory and read as an **int** in GLSL
  (`struct Entry { int j; float rest; float k; int pad; }`, 16 B). Storing it as float bits and
  reading it as int gives an out-of-bounds neighbor → the mesh collapses to the origin.
- The **Jacobi** relax (race-free, parallel) is less energy-stable than the CPU's **Gauss-Seidel**.
  Impacting the pile pumps energy that accumulates and **explodes** unless the per-substep
  **damping** is low enough to bleed it off. `damping=0.98` explodes ~300 frames; `0.96` is
  stable for 600+ frames (the default). This also slows the fall — the default `--shot` uses
  450 steps so the cloth has time to land + drape.
- The pile is **sparse** (50 long capsules spread over ~15 m), so a too-rigid cloth slips through
  the gaps instead of draping; the Jacobi (looser than GS) + enough damping is what lets it
  bridge them. The one-way collision is correct (0 penetration) — the drape is a
  stiffness/damping balance, not a collision bug.
