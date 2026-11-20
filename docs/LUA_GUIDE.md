# Lua scripting guide

Hand-written companion to the auto-generated [`LUA_API.md`](LUA_API.md) (the per-function reference,
regenerated from the C++ annotations). This guide covers the conventions that cut across the API:
how data is shaped, how entities/names/refs/URLs relate, and what a scene script looks like.

## Quick start

A scene is a `.json` in `assets/scenes/` whose objects can carry a `"script"` field pointing at a Lua module
(`{ "name": "hello", "script": "scenes/hello_world" }`); the engine attaches it to that object's entity, and
the script builds and drives the scene. Everything below comes from `hello_world.lua`:

```lua
local uib = require("ui")

spin_speed = 1.0   -- top-level vars are bindable via "hello.LuaScript.spin_speed"

function _on_ready(self)                          -- once, when the script is ready
    local camera = scene.entity("cam")
    scene.emplace(camera, "Transform", { pos = { 0, 1.2, 4 } })
    scene.emplace(camera, "Camera", { fovDeg = 50, active = true })
    scene.attach_script(camera, "scenes/camera")   -- fly camera (look + wasd/qe)

    local tex = graphics.load_texture("cloth_albedo.png", "cube_tex")
    local mat = scene.entity("mat_cube")
    scene.emplace(mat, "Material", { baseColor = { 1, 1, 1 }, metallic = 0.1, roughness = 0.4, tex = tex })

    self.cube = scene.entity("cube")               -- per-instance state lives on self
    scene.emplace(self.cube, "Transform", { pos = { 0, 0, 0 } })
    local g = scene.entity()
    scene.emplace(g, "Geometry", mesh.box({ 0.5, 0.5, 0.5 }))
    scene.emplace(self.cube, "Renderable", { geom = g })
    scene.emplace(self.cube, "MaterialRef", { material = mat })

    local win = uib.make_window("hello world", 320, 200, { 0, 1.5, 2.8 })
    uib.bind_slider(uib.tab(win, "scene"), "spin speed (rad/s)", "hello.LuaScript.spin_speed", 0, 10.0)
end

function _animate(self, dt)                        -- every graphics frame
    local pose = scene.get_pose(self.cube)
    scene.set_pose(self.cube, pose.pos, math.qmul(math.qaxis({ 0.5, 0.5, 0 }, spin_speed * dt), pose.quat))
end

function _physics(self, dt) end                    -- every frame, before the Jolt step
```

## Type conventions

| type | Lua shape | notes |
|---|---|---|
| `entity` | integer id | opaque; compare with `==`, pass around freely |
| `vec3` / color | `{x, y, z}` | flat 1-based array: `v[1]` = x, `v[2]` = y, `v[3]` = z |
| `quat` | `{x, y, z, w}` | **w is last**: identity = `{0, 0, 0, 1}` |
| `number[]` (Geometry, fit) | flat interleaved | `{x,y,z, x,y,z, ...}`; indices 0-based in Geometry |
| `entity-ref` (component fields) | entity **or** world-name string | both accepted everywhere a ref is read |

Rules of thumb:

- Positions/rotations you *read* come back as `{pos = {x,y,z}, quat = {x,y,z,w}}` tables
  (`scene.get_pose`, `scene.world_pose`); positions you *write* are bare arrays.
- A component field that is a vec3 reads through a live view as `{x,y,z}` — index it, don't destructure:
  `local x = t.pos[1]`.
- Vertex data (`Geometry.pos/nrm/uv`, `physics.fit_capsule`) is flat xyz-interleaved; `idx` is 0-based.

## Entities, names, refs, URLs

- **Entity** — an integer id from `scene.entity()` / `scene.spawn()`. Plain container: nothing renders or
  simulates until components are emplaced on it.
- **World name** — optional unique string attached at creation (`scene.entity("cam")`) or by spawn key.
  Looked up with `scene.get_entity(name)`; released by `scene.destroy`.
- **Ref fields** — component fields of kind `ref`/`ent_arr` (e.g. `Collider.shape`, `Renderable.geom`,
  `RigidBody.colliders`) accept an entity id or a world-name string. Inside one `scene.spawn` call, names
  resolve against the spawn's own keys first, then the world — so sibling nodes can reference each other:

  ```lua
  local e = scene.spawn({
      col  = { CapsuleShape = { halfLen = 0.5, radius = 0.3 } },
      body = { Transform = { pos = {0, 1, 0} },
               RigidBody = { mass = 10, colliders = { "col" } } },   -- resolves to e.col
  })
  ```

- **URLs** — dot-paths used by `scene.get`/`scene.set`, UI binding (`Bind.url`), and callbacks:
  - `"Name.Component.field"` — a reflected field (e.g. `"mat_cube.Material.metallic"`). Names may contain
    dots; the last two segments are always `Component.field`.
  - `"name.LuaScript.var"` — a top-level variable of an attached script (string get/set).
  - `"name.LuaScript.fn"` — a function to call on click/change (no argument, except the entity for buttons).

## Components and live views

- Create with `scene.emplace(e, "Type", fields)`, rewrite with `scene.replace` (must exist), remove with
  `scene.remove`. Valid types/fields: see the **components** section of [LUA_API.md](LUA_API.md); omitted
  fields take their C++ defaults.
- `scene.component(e, "Type")` returns a **live view**: every read resolves against the current component,
  every write goes straight in. It is not a copy — don't store it across destroys, and writes require the
  component to exist already.
- The usual animation loop: snapshot → mutate → write back (`get_pose`/`set_pose`), or drive fields through
  `scene.animate` (keyframes) / UI `Bind`s.

## Script modules

A file attached with `scene.attach_script(e, file, params)` must **return a table**. The engine calls:

| hook | when | use for |
|---|---|---|
| `_on_ready(inst)` | once after load | build entities/UI; `inst.params` = the `params` arg |
| `_animate(inst, dt)` | every graphics frame | visual motion, UI polling |
| `_physics(inst, dt)` | every frame before the Jolt step | kinematic poses, forces, impulses |

`inst` is a per-attach table: `inst.entity` is the attached entity; its `__index` is the module, so module
functions act as methods and you can keep per-instance state on it (`self.cube = ...`). Top-level variables
in the module are exposed as `"name.LuaScript.<var>"` URLs — that's how UI sliders bind to script state.

Scripts reach each other's instances directly: `scene.attach_script` activates immediately and returns the
fresh `inst`, and `scene.get_script(name|entity)` fetches another entity's `inst` as a plain table — read or
write its per-instance fields, call its methods with its own self (`other:fn()`). Fetch fresh; the table goes
stale when the entity is destroyed or re-attached.

Shared modules (e.g. `char_ctrl.lua`) return a plain table of functions and are used with `require`; only
*attached* scripts get hooks.

## The `ui` stdlib (`require("ui")`)

Every widget is plain components (UIWidget marker + Parent + one widget component + optional Bind), so you
can also build them by hand. Helpers:

```lua
local uib = require("ui")
local win  = uib.make_window("title", 320, 200, { 0, 1.5, 2.8 })  -- pos required (world space)
local tab  = uib.tab(win, "scene")                                -- a UITab under the window
uib.text(tab, "a line of text")
-- bind to "name.Component.field" or "name.LuaScript.var"; lo/hi default to the field's own range
uib.bind_slider(tab, "spin (rad/s)", "hello.LuaScript.spin_speed", 0, 10.0, "%.2f")
uib.editor(uib.tab(win, "editor"))   -- auto-generated inspector for every named entity
```

`uib.build(parent, spec)` interprets a widget spec array (`section`, `slider-f`, `slider-i`, `checkbox`,
`color`, `drag-v3`, `text`, `button`) — the same vocabulary scene jsons use. Buttons take
`action = "name.LuaScript.fn"`; value widgets take an optional `change` callback URL.

## The `mesh` stdlib (global)

Geometry builders returning a Geometry field table (`{pos, nrm, uv, idx}`, 0-based indices):

| builder | params |
|---|---|
| `mesh.box({hx, hy, hz})` | half extents |
| `mesh.sphere(p)` | `{radius = 1, subdiv = 4}` |
| `mesh.capsule(p)` | `{radius, halfLen, S?, M?}` |
| `mesh.taperedCapsule(p)` | `{halfHeight, bottomRadius, topRadius, S?, M?}` |
| `mesh.cone(p)` | `{radius, halfLen, S?, M?}` |
| `mesh.arrow(p)` | `{len = 1, radius?, headLen?, headRadius?}` |
| `mesh.plane(p)` / `mesh.quad(p)` | `{cw, ch, span}` / `{w, h}` |
| `mesh.planeCons(p)` / `mesh.ballCons(p)` | constraint-solver variants (soft bodies) |
| `mesh.createAxis(opts)` | spawns a 3-axis gizmo entity: `{pos?, quat?, len?}` |

Usage: `scene.emplace(g, "Geometry", mesh.box({0.5, 0.5, 0.5}))`, then point a `Renderable.geom` at it.

## Where to look next

- Per-function signatures, return shapes and examples: [LUA_API.md](LUA_API.md) (auto-generated).
- Component field tables (types + ranges): the **components** section of LUA_API.md, generated from
  `src/core/reflect.hpp`.
- Real-world usage: `assets/scenes/hello_world.lua` (building a scene), `char_ctrl.lua` (rigid-body character
  controller with joints), `caps_fit.lua` (fit_capsule workflow).
