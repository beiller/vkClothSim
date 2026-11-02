# 3dsim

A data-driven 3D engine: ENTT ECS scene graph, Jolt rigid-body physics, GPU soft-body (PBD) and dual-quaternion skinning, a Vulkan renderer, OpenXR support, and Lua-scripted scenes. Two decoupled servers — `PhysicsServer` (CPU, self-paced 120 Hz) and `GraphicsServer` (Vulkan + GPU sim, self-paced 60 Hz) — exchange state only through ECS components; settings are data too (components on a marker entity, bindable by URL).

![3dsim](screenshot1.jpg)

## Build & run

```sh
cmake -S . -B build -Wno-dev
cmake --build build -j
./build/vksim                          # interactive (default scene)
./build/vksim --scene assets/scenes/hello_world.json
./build/vksim --headless --frames 120 # GPU sim, no window
```

Flags: `--scene <path>`, `--no-vr`, `--no-graphics` (CPU physics only), `--headless` (GPU compute, no window), `--frames <n>` (exit after n frames).

Re-run the `cmake -S .` step after editing a shader — SPIR-V is baked in at configure time.

## Lua scripting

Scenes are JSON + Lua: objects in JSON, behaviour attached via `"script": "scenes/<name>"` or `scene.attach_script(e, file, params)`. Modules expose `_on_ready(self)` / `_animate(self, dt)` / `_physics(self, dt)` hooks; `self.vars` values are mirrored to C++ each frame and bindable through `name.LuaScript.<var>` URLs (UI widgets included).

Full API reference (auto-generated, do not edit by hand): [docs/LUA_API.md](docs/LUA_API.md) — regenerate with `python3 tools/gen_lua_api.py`.

## Dependencies

Vulkan SDK (`glslc`), `xxd`, and system Lua 5.4. ENTT, GLFW, GLM, ImGui, Jolt, OpenXR and the small helpers (json, miniz, stb, tinyexr, tinygltf) are bundled under `lib/`.
