# 3dsim

DO NOT ALTER THIS FILE via coding agent (unless explicit permission is granted)

Jolt rigid-body sim with a GPU soft-body layer (cloth + ball) using Jolt capsules as colliders.
Soft-body bodies are passed to the GPU as vertices + two-point joints; Jolt capsules are passed as colliders.

Keep the three layers loosely coupled:
- rendering: `src/vk`
- soft-body sim: `src/sim`, `shaders/softbody.comp`
- rigid/Jolt sim: `src/app`, `src/systems.cpp`

`src/main.cpp` is glue only. Design for future command queues / separate threads.

Conventions:
- minimal comments
- descriptive names
- concise output
- reconfigure CMake after shader edits

## Current layout

- `src/main.cpp`: app loop, ImGui, scene selection (`--hdri` or default demo)
- `src/world.hpp`: shared `World` state
- `src/demo.cpp`: default cloth/ball/capsule demo
- `src/hdri.cpp`: HDRI sphere test scene
- `src/systems.cpp`: sim step, collider sync, resets
- `src/app/rigid.cpp`: Jolt rigid step
- `src/sim/softsim.cpp`: GPU soft-body dispatch
- `src/vk/vkapp.cpp`: GLFW/Vulkan app, swapchain, render pass
- `src/vk/renderer.cpp`: scene rendering, environment/IBL, UI
- `src/vk/vkutil.hpp`: Vulkan helpers; equirect upload must honor `rowPitch`
- `shaders/`:
  - `mesh_*`: PBR/IBL mesh shading
  - `softbody.comp`: GPU soft-body integration/constraints
  - `env_cube.comp`: equirect -> cubemap
  - `env_prefilter.comp`: rough specular prefilter
  - `env_irradiance.comp`: irradiance / ambient diffuse
  - `env_brdf.comp`: BRDF LUT pass, currently unused by `mesh_frag.frag`
  - `tonemap_*`: HDR tonemap / sRGB post pass
- `assets/fly-studio-03_1K.exr`: HDRI used by `--hdri` and demo

## HDRI process

- Load `.exr` on CPU with TinyEXR into a 32-bit float equirect buffer.
- Upload as a 2D image with row-pitch-aware copies.
- `env_cube.comp`: equirect -> cubemap mip 0.
- `env_prefilter.comp`: GGX-filtered specular mips for roughness.
- `env_irradiance.comp`: cosine-weighted diffuse irradiance cubemap.
- `mesh_frag.frag`: split-sum IBL using irradiance for diffuse and prefiltered cubemap for specular.

## Render loop

Per frame, `main.cpp` drives the layers in order:
- Jolt: `stepRigid()` advances rigid bodies at fixed `kFrameDt` and writes poses back to ECS `Transform`.
- Collider sync: `syncColliders()` copies capsule poses into the GPU capsule buffer used by the soft-body sim.
- Soft body: `stepSoft()` records and submits `softbody.comp` substeps, updating the same GPU position/normal buffers later read by the mesh renderer.
- Camera: `renderer.setViewProj()` updates the view/projection and camera-position UBO.
- Scene render: `Renderer::draw()` updates per-instance model/material UBOs, draws meshes into an HDR scene target with depth, tonemaps to the swapchain image, and renders ImGui on top.
- Present: the swapchain image is presented.

## Build & run

```sh
cmake -S . -B build -Wno-dev        # re-run after editing any shader (SPIR-V is baked here)
cmake --build build -j
./build/vksim                        # default demo
./build/vksim --hdri                 # HDRI sphere test
DRI_PRIME=1 ./build/vksim            # force AMD iGPU (RADV) instead of the 4090
```

## Screenshot

Capture the app window (needs `import`/ImageMagick + `xprop`).

```sh
setsid ./build/vksim >/tmp/vksim.log 2>&1 < /dev/null & disown
sleep 6
for id in $(xprop -root _NET_CLIENT_LIST | sed 's/.*# //' | tr -d ','); do
    n=$(xprop -id "$id" _NET_WM_NAME 2>/dev/null | sed 's/.*= //; s/"//g')
    [ "$n" = "3dsim" ] && import -window "$id" /tmp/shot.png
done
pkill -x vksim
```

## Desired sim direction

- Position-based Verlet / PBD / XPBD style
- graph-colored constraint groups; one dispatch per color
- prefer substeps over high iteration counts
