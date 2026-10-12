# 3dsim

DO NOT ALTER THIS FILE via coding agent (unless explicit permission is granted)

Bias towards action, use minimal thinking!

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

- `src/main.cpp`: app loop, ImGui, scene selection (`--hdri` / `--shadow` / `--hier` / `--scene file.json`; `--no-vr` = plain window, else VR auto-detects)
- `src/world.hpp`: shared `World` state
- `src/scene.cpp`: scene loader — builds the world from `assets/scenes/*.json`; objects bind `.lua` behavior scripts via a `"script"` field; all geometry generated in Lua
- `src/scene_script.cpp`: embedded Lua 5.4 host; scripts see only the `scene` table (no io/os/debug/package)
- `src/systems.cpp`: sim step, collider sync, spawn verbs (capsule/cloth/ball, textures); consumes Lua-generated geometry only
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
- `assets/scenes/`: scene definitions — `.json` data + per-entity `.lua` behavior scripts

## Scenes

Scenes are `.json` (declarative data) + per-entity `.lua` behavior scripts in `assets/scenes/`; the engine contains no scene content and no mesh generators.

- Loader order (`createSceneWorld`): std-lib load (`mesh` global) -> JSON build (lights, materials, objects) -> `activateScripts()` runs each script entity's `_on_ready` to fixpoint -> `set` block -> `sim.build()` -> env -> UI.
- All geometry is generated in Lua by `assets/scenes/mesh.lua` (`mesh.capsule` / `mesh.sphere` / `mesh.ground` / `mesh.cloth`, plus `mesh.clothCons` / `mesh.ballCons`); the engine always loads it as the global `mesh` before any scene content (scripts use the global directly; it also backs declarative meshes and the VR controller grips).
- Geometry tables: `{pos, nrm?, uv?, idx}` of flat floats; vertex indices are 1-based (the engine normalizes to 0-based). Constraints: flat `{a, b, rest, k, ...}` with 1-based vertex indices.
- Script binding: a JSON object with a `"script"` field becomes a no-mesh "behavior container" entity (`Name` + identity `Transform` + `LuaScript`). `"script"` is a module path (same convention as the `attach_script` verb), e.g. `{"name": "demo", "script": "scenes/demo"}`. No auto-discovery; `attach_script` is still used for runtime-spawned entities (e.g. tree branches).
- Scripts get a `scene` table: `geometry(geom) -> geomId` (shared for rigid/static meshes), spawn verbs `static(geom, mat, pos?, quat?, name?)` / `capsule(geom, halfLen, radius, ...)` / `cloth(geom, cons, ...)` / `ball(geom, cons, ...)`, `material`, `set`/`get` through URL indirection, `attach_script` / `destroy` / `add_child` / hierarchy queries, `set_pose`, and a vec/quat math kit (vectors/quats as Lua arrays). Behavior is defined by module-level hooks: `_on_ready(self)` (once), `_animate(self, dt)` (per render frame), `_physics(self, dt)` (per fixed step). The full list is in `src/scene_script.cpp`.
- Scene JSON objects: a declarative mesh `{"name", "mesh": {"type": "ground", "size": 55}, "material", "pos?", "quat?"}` (`type` selects the `mesh.<type>` generator; the other numeric keys become its params) or a script entity `{"name", "script": "scenes/demo"}`. The built-in scenes use script entities for all their geometry.
- Error model: load errors abort; a hook error prints one traceback — an `_on_ready` failure drops all of that entity's hooks (it becomes inert), an `_animate`/`_physics` failure drops just that hook.
- Scene JSON `ui.widgets` bind sliders/checkboxes/buttons to entity fields via `bind` URLs; script `button` actions are validated at load.

## HDRI process

- Load `.exr` on CPU with TinyEXR into a 32-bit float equirect buffer.
- Upload as a 2D image with row-pitch-aware copies.
- `env_cube.comp`: equirect -> cubemap mip 0.
- `env_prefilter.comp`: GGX-filtered specular mips for roughness.
- `env_irradiance.comp`: cosine-weighted diffuse irradiance cubemap.
- `mesh_frag.frag`: split-sum IBL using irradiance for diffuse and prefiltered cubemap for specular.

## Render loop

Per frame, `main.cpp` drives the layers in order:
- XR (when active): pose read, controller/grab steps.
- Scripts: `activateScripts()` activates any script entities spawned since last frame (to fixpoint), then `animateAll(dt)` runs each script's `_animate` (render-rate dt).
- Fixed sim step (decoupled at `kFrameDt`, 0..n times per frame): `physicsAll(kFrameDt)` runs each script's `_physics`, `stepRigid()` advances rigid bodies and writes poses back to ECS `Transform`, `resolveWorldTransforms()` flattens the hierarchy, `syncColliders()` copies capsule poses into the GPU capsule buffer, and `stepSoft()` records/submits `softbody.comp` substeps (updating the GPU position/normal buffers the renderer reads).
- Camera: `renderer.setViewProj()` updates the view/projection and camera-position UBO.
- Scene render: `Renderer::draw()` (or `drawVr()` per-eye in VR) updates per-instance model/material UBOs, draws meshes into an HDR scene target with depth, tonemaps to the swapchain image, and renders ImGui on top.
- Present: the swapchain image is presented.

## Build & run

Requires system Lua 5.4 (headers + liblua5.4, found via CMake).

```sh
cmake -S . -B build -Wno-dev        # re-run after editing any shader (SPIR-V is baked here)
cmake --build build -j
./build/vksim                        # default demo scene
./build/vksim --hdri                 # HDRI sphere test
./build/vksim --shadow               # shadow test
./build/vksim --hier                 # fractal tree (json + lua)
./build/vksim --scene file.json      # any scene json
./build/vksim --no-vr                # plain window (no OpenXR session)
DRI_PRIME=1 ./build/vksim            # force AMD iGPU (RADV) instead of the 4090
```

## Screenshot

Capture the app window (needs `import`/ImageMagick + `xprop`).

```sh
setsid ./build/vksim --no-vr >/tmp/vksim.log 2>&1 < /dev/null & disown
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


## Canonical Reference

Godot is a canonical reference as it uses many similar technologies to this project
- Vulkan rendering
- OpenXR VR integration
- Lighting and shadow techniques

The source code is available here: ~/src/godot4
EG: openXR code is here: ~/src/godot4/modules/openxr
