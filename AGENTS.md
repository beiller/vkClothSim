# 3dsim

GPU physics-based character simulator. Rigid-body capsules per body part (from
Daz weights/skeleton) collide on the CPU; a Vulkan **compute shader** drives the
soft skin mesh from the capsule poses.

## Build & run
```sh
cmake -S . -B build -Wno-dev
cmake --build build -j
./build/sim3d        # headless smoke test: capsule SDF on GPU, prints PASS
```
Needs Vulkan SDK + `glslc` + `xxd` + a compute-capable GPU (picks the discrete GPU).

## Layout
- `src/main.cpp`   – minimal Vulkan compute harness (instance→device→dispatch→readback)
- `shaders/*.comp` – compute shaders; `smoke.comp` = point→capsule signed distance
- `build/gen/*_spv.hpp` – SPIR-V baked to C arrays at build time (do not edit)

## Conventions
- C++23, minimal, no dependencies beyond the Vulkan loader.
- GLSL struct layout in `shaders/` must be mirrored exactly in `main.cpp` (keep `static_assert` sizes in sync). SSBO `vec3`/`vec4` arrays are 16-byte strided.
- One file per concern as it grows; keep the harness single-shot and dependency-free.
