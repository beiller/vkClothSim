# 3dsim

A data-driven 3D engine: ENTT ECS scene graph, Jolt rigid-body physics, GPU soft-body (PBD) and dual-quaternion skinning, a Vulkan renderer, OpenXR support, and Lua-scripted scenes. Two decoupled servers — `PhysicsServer` (CPU, self-paced 120 Hz) and `GraphicsServer` (Vulkan + GPU sim, self-paced 60 Hz) — exchange state only through ECS components; settings are data too (components on a marker entity, bindable by URL).

![3dsim](screenshot1.jpg)

## Hello World Scene

[assets/scenes/hello_world.lua](assets/scenes/hello_world.lua)

```lua
-- hello world: the scene json is blank (one object with this script attached); the camera,
-- light, textured spinning cube and UI are all created here in lua.
local M = {}

-- script variables, bindable via "hello.LuaScript.<var>"
M.vars = { 
    spin_speed = 1.0 
}

-- executed once, when the script is ready
function M._on_ready(self)
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
    ui.slider_f(tab, "spin speed (rad/s)", "hello.LuaScript.spin_speed", 0, 10.0, "%.2f")
    ui.slider_f(tab, "cube metallic", "mat_cube.Material.metallic", 0, 1.0, "%.2f")
    ui.slider_f(tab, "cube refraction", "mat_cube.Material.refraction", 0, 1.0, "%.2f")
    ui.slider_f(tab, "cube ior", "mat_cube.Material.ior", 1.0, 3.0, "%.2f")
    ui.slider_f(tab, "cube roughness", "mat_cube.Material.roughness", 0, 1.0, "%.2f")
    ui.editor(ui.tab(win, "editor"))
end

-- runs every graphics frame
function M._animate(self, dt)
    local speed = self.vars.spin_speed
    local pose = scene.get_pose(self.cube)
    scene.set_pose(self.cube, pose.pos, math.qmul(math.qaxis({ 0.5, 0.5, 0 }, speed * dt), pose.quat))
end

-- runs every physics step
function M._physics(self, dt)
    
end

return M
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

## Lua scripting

Scenes are JSON + Lua: objects in JSON, behaviour attached via `"script": "scenes/<name>"` or `scene.attach_script(e, file, params)`.

## Dependencies

Vulkan SDK (`glslc`), `xxd`, and system Lua 5.4. ENTT, GLFW, GLM, ImGui, Jolt, OpenXR and the small helpers (json, miniz, stb, tinyexr, tinygltf) are bundled under `lib/`.
