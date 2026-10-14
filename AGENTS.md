# 3dsim

DO NOT ALTER THIS FILE via coding agent (unless explicit permission is granted)

Bias towards action, use minimal thinking!

Rigid-body sim (Jolt) with a GPU soft-body layer (cloth + ball), rendered with Vulkan
(PBR/IBL, PCSS shadows, OpenXR VR). ECS (entt) is the single source of truth. Scene content
lives in `assets/scenes/` (JSON + per-entity Lua) — the engine holds no scene content and no
mesh generators.

## Code Style

Push if statements towards the top of functions. For loops are below.
Make function names and parameters fully descriptive
Keep functions as short as possible.
Use a functional style where possible.

## Layers (keep loosely coupled)

- rendering: `src/vk` + `shaders/`
- soft-body sim: `src/sim` (GPU compute)
- rigid/Jolt sim: `src/app`
- glue: `src/main.cpp` (app loop only; design for future command queues / separate threads)

## Systems

- **World/ECS** — shared `World` state; entities carry components (transforms, renderables,
  materials, cameras, lights, rigid/soft bodies, scripts). Hierarchy flattened to world
  transforms each step.
- **Scene loader** — builds the world from `assets/scenes/*.json`; objects bind per-entity
  `.lua` behavior scripts. All geometry is generated in Lua; `assets/scenes/mesh.lua` is
  always loaded as the global `mesh` before any scene content.
- **Lua host** — embedded Lua 5.4, sandboxed (no io/os/debug/package); scripts see only the
  `scene` table. Hooks: `_on_ready` (once), `_animate` (per render frame), `_physics` (per
  fixed sim step). Load errors abort; a hook error prints one traceback and drops that hook.
- **Rigid sim** — Jolt; reusable collision shapes (capsule, box); dynamic and static bodies;
  poses written back to the ECS each step.
- **Soft sim** — GPU compute: vertices + two-point constraints, capsule colliders + floor
  plane; position-based, substeps over iteration counts.
- **Renderer** — Vulkan; PBR with split-sum IBL from an equirect HDRI (cubemap ->
  prefilter/irradiance mips), PCSS point-light shadows, HDR tonemap post pass, ImGui UI.
  A camera is a general view device; the VR head is a separate view; one is "current" per frame.
- **Input/VR** — GLFW window or OpenXR (auto-detect, `--no-vr` = plain window); controller
  input is exposed to Lua.

## Conventions

- minimal comments, descriptive names, concise output
- reconfigure CMake after shader edits (SPIR-V is baked at configure time)
- sim runs at a fixed timestep, decoupled from the render rate

## Build & run

Requires system Lua 5.4.

```sh
cmake -S . -B build -Wno-dev
cmake --build build -j
./build/vksim --no-vr          # plain window (else OpenXR auto-detects)
./build/vksim --scene file.json
DRI_PRIME=1 ./build/vksim      # force AMD iGPU (RADV) instead of the 4090
```

## Screenshot

Needs `import`/ImageMagick + `xprop`; the window title is always `3dsim`.
`pkill` kills ALL vksim instances — never run this while the user has a live VR session.

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

Godot is a canonical reference (Vulkan rendering, OpenXR, lighting/shadows): `~/src/godot4`
(e.g. OpenXR: `~/src/godot4/modules/openxr`)
