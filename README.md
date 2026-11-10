# 3dsim

A data-driven 3D engine: ENTT ECS scene graph, Jolt rigid-body physics, GPU soft-body (PBD) and dual-quaternion skinning, a Vulkan renderer, OpenXR support, and Lua-scripted scenes. Two decoupled servers — `PhysicsServer` (CPU, self-paced 120 Hz) and `GraphicsServer` (Vulkan + GPU sim, self-paced 60 Hz) — exchange state only through ECS components; settings are data too (components on a marker entity, bindable by URL).

![3dsim](screenshot1.jpg)

## Features

### Full GLTF support
Load GLTF files with full support for skeleton, shape keys, object higherachy, full shader support including SSS + refraction.

### GPU soft-body cloth simulation
Cloth and soft bodies run entirely on the GPU as position-based-dynamics compute shaders (`shaders/softbody.comp`). Each body is a mesh plus flat constraint quads `{a, b, rest, k}` solved in substeps with per-body gravity, damping, friction, tension and stiffness, against a floor and up to `kMaxCapsules` dynamic capsule colliders. 

### Dual-quaternion skeleton skinning
Meshes deform on the GPU with dual-quaternion skinning plus shape keys

### PBR renderer
Forward Vulkan renderer implements metallic-roughness PBR with glTF texture conventions (G = roughness, B = metallic), emissive, normal scaling, opaque/transparent passes with alpha cutoff/blend modes, and **glass refraction**: a `refraction`/`ior` pair re-samples the blurred scene color and depth buffers for refracted views. A dedicated subsurface-scattering pass weighted per color channel.

### Image-based lighting and shadow mapping
Environment maps are generated on the GPU from equirectangular textures with PCF-style texel filtering; light falloff is inverse-square with a hard front-facing gate.

### Jolt rigid-body physics
`PhysicsServer` wraps the Jolt engine on the CPU with its own fixed-step accumulator at 120 Hz — it steps independently of frame rate and exposes per-step hooks (`_physics` Lua callbacks) for data-driven behaviour. Scenes declare dynamic, kinematic and static bodies with box/capsule colliders and joints; `collision_shape_solver_test` covers shape fitting (capsules to arbitrary meshes).

### OpenXR / VR
Full stereo OpenXR path: per-eye views from the runtime pose, controller tracking, and a clean camera hand-off — while an XR pose exists all scene cameras are deactivated and the VR camera drives rendering.

### Data-driven scenes and URL binding
Everything is ECS data: scenes are JSON (objects → named entities with components), settings are components on a marker entity, and any field of any component is addressable by URL (`name.Component.field`). UI widgets, scene `Bind` entries and script variables all read/write through the same resolver — so a slider can drive `Material.metallic`, a `Transform.pos`, or a Lua script variable with identical code.

### Lua scripting
Scenes attach behaviour via `"script": "scenes/<name>"` (or `scene.attach_script(e, file, params)`). Scripts get lifecycle hooks (`_on_ready`, `_animate`, `_physics`), a `self.vars` table whose entries are bindable by URL (`name.LuaScript.var` — floats, bools, strings, vec3/quat tables with dotted sub-paths), and seven API tables: `scene` (entity/component CRUD, poses, glTF, textures), `math` (vec/quaternion ops on top of the Lua stdlib), `physics` (raycast), `soft`, `graphics`, `ui`, and `input`. A `mesh` standard library generates capsule/sphere/box/plane geometry. The full reference is generated from source annotations: [docs/LUA_API.md](docs/LUA_API.md).

### Immersive UI and scene editor
A PBR-lit UI: windows, tabs, text, sliders (f/i) and buttons are ECS widgets (entities with components) rendered via an ImGui Vulkan backend onto a lit canvas quad — so they respond to the scene's lights. Sliders bind to any URL (component field or script variable). `ui.editor` embeds a full scene editor tab for inspecting and editing entities at runtime.

## Hello World Scene

[assets/scenes/hello_world.lua](assets/scenes/hello_world.lua)

```lua
-- hello world: the scene json is blank (one object with this script attached); the camera,
-- light, textured spinning cube and UI are all created here in lua.

-- script variables, bindable via "hello.LuaScript.<var>"
-- "hello" is the name of this script entity (specified in hello_world.json)
spin_speed = 1.0

-- local vars are not exposed
local my_local_var = nil

-- executed once, when the script is ready
function _on_ready(self)
    -- the fly camera: active=true is what makes it THE camera
    local camera = scene.entity("cam")
    scene.emplace(camera, "Transform", { pos = { 0, 1.2, 4 } })
    scene.emplace(camera, "Camera", { fovDeg = 50, active = true })
    scene.attach_script(camera, "scenes/camera") -- attaches the "fly camera movement" script which gives "look" and "wasd + qe" kb movement

    local light = scene.entity("light")
    scene.emplace(light, "Transform", { pos = { 0, 3, 4 } })
    scene.emplace(light, "PointLight", { color = { 1, 1, 1 }, intensity = 60, radius = 0.5 })

    -- the textured cube: geometry entity -> renderable ref, texture entity -> material ref
    local tex = graphics.load_texture("cloth_albedo.png", "cube_tex")
    local mat = scene.entity("mat_cube")
    scene.emplace(mat, "Material", { baseColor = { 1, 1, 1 }, metallic = 0.1, roughness = 0.4, tex = tex })

    self.cube = scene.entity("cube")
    scene.emplace(self.cube, "Transform", { pos = { 0, 0, 0 } })
    local g = scene.entity()
    scene.emplace(g, "Geometry", mesh.box({ 0.5, 0.5, 0.5 }))
    scene.emplace(self.cube, "Renderable", { geom = g })
    scene.emplace(self.cube, "MaterialRef", { material = mat })

    -- the ui: one window, a text line, a spin-speed slider, and the scene editor
    local win = ui.window("hello world", 320, 200)
    local tab = ui.tab(win, "scene")
    ui.text(tab, "a spinning cube - everything created in lua")

    -- hook up a slider to this scrip't spin_speed variable
    ui.slider_f(tab, "spin speed (rad/s)", "hello.LuaScript.spin_speed", 0, 10.0, "%.2f")
    ui.slider_f(tab, "cube metallic", "mat_cube.Material.metallic", 0, 1.0, "%.2f")
    ui.slider_f(tab, "cube refraction", "mat_cube.Material.refraction", 0, 1.0, "%.2f")
    ui.slider_f(tab, "cube ior", "mat_cube.Material.ior", 1.0, 3.0, "%.2f")
    ui.slider_f(tab, "cube roughness", "mat_cube.Material.roughness", 0, 1.0, "%.2f")
    ui.editor(ui.tab(win, "editor"))
end

-- runs every graphics frame
function _animate(self, dt)
    local speed = spin_speed
    local pose = scene.get_pose(self.cube)
    scene.set_pose(self.cube, pose.pos, math.qmul(math.qaxis({ 0.5, 0.5, 0 }, speed * dt), pose.quat))
end

-- runs every physics step
function _physics(self, dt)
    
end


```

[assets/scenes/hello_world.json](assets/scenes/hello_world.json)

```json
{
    "name": "hello world",
    "objects": [
        { "name": "hello", "script": "scenes/hello_world" }
    ]
}
```

## Full API reference 

[docs/LUA_API.md](docs/LUA_API.md)

## Build & run

```sh
cmake -S . -B build -Wno-dev
cmake --build build -j
./build/vksim                          # interactive (default menu scene)
./build/vksim --scene assets/scenes/hello_world.json --no-vr  # load a scene, disable VR
```

Flags: `--scene <path>`, `--no-vr`, `--no-graphics` (CPU physics only), `--headless` (GPU compute, no window), `--frames <n>` (exit after n frames).

Re-run the `cmake -S .` step after editing a shader — SPIR-V is baked in at configure time.

## Dependencies

Vulkan SDK (`glslc`), `xxd`, and system Lua 5.4. ENTT, GLFW, GLM, ImGui, Jolt, OpenXR and the small helpers (json, miniz, stb, tinyexr, tinygltf) are bundled under `lib/`.
