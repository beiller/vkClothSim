# 3dsim

DO NOT ALTER THIS FILE via coding agent (unless explicit permission is granted)

Bias towards action, use minimal thinking!

Rigid-body sim (Jolt) with a GPU soft-body layer (cloth + ball), rendered with Vulkan
(PBR/IBL, PCSS shadows, OpenXR VR). ECS (entt) is the single source of truth. Scene content
lives in `assets/scenes/` (JSON + per-entity Lua) — the engine holds no scene content and no
mesh generators.

## Code Style

- minimal comments, do not document history of changes etc. USE COMMENTS SPARINGLY!
  - Basically use almost NO comments unless it is a magic constant value that needs documenting!
- Push if statements towards the top of functions. For loops are below.
- Make function names and parameters fully descriptive
- Keep functions as short as possible.
- Use a functional style where possible.

## Layers (keep loosely coupled)

- rendering: `src/vk` + `shaders/`
- soft-body sim: `src/sim` (GPU compute)
- rigid/Jolt sim: `src/app`
- glue: `src/main.cpp` (app loop only; design for future command queues / separate threads)

## Debugging Tips:

UBOs cause lots of problems in the past - ensure they are aligned correctly!
 UBO verification command: spirv-dis build/gen/mesh_frag.spv then check OpMemberDecorate ... Offset lines.

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

- descriptive function /variable names, concise output
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
./tests/run_tests.sh           # full suite: build + C++ unit binaries + headless scene tests (tex_check needs a display)
```

## Lua API docs (auto-generated)

`docs/LUA_API.md` is generated — never edit it by hand:

```sh
python3 tools/gen_lua_api.py   # re-run after any change to src/script/api_*.cpp, lua_host.cpp, or reflect.hpp
```

Every exposed function MUST carry a one-line doc string directly above its handler (or reg() call):

```cpp
/// table.name(arg1: type, arg2?: type) -> ret | what it does, arg semantics, error cases
int lFoo(lua_State* L) { ... }
```

`?` marks optional args; omit `-> ret` for void functions. The generator exits non-zero if any function lacks a valid annotation (or if the annotation's table.name doesn't match its registration), so it doubles as an API lint.

The "magic" script-module hooks are documented the same way, in `lua_host.cpp` above `activateScripts()`:

```cpp
/// script-instance | <what the inst table is and its fields>
/// script-hook _on_ready(inst) | <when it is called>
```

Emplace-able components are documented with a one-line `/// Type | description` directly above each `FieldDesc` array in `src/core/reflect.hpp`; field names/types/ranges in the generated docs come from the descriptors themselves (single source of truth — they can never drift).

The generator lists hooks from the `grab("_name", ...)` call sites (machine truth) and fails if a looked-up hook has no doc, or warns on a doc for a hook that no longer exists.

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
