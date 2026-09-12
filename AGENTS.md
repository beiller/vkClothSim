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
