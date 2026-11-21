-- collision object layers: default is 1, UI windows are on 2, kLayerOff (7) pairs collide with nothing;
-- physics.raycast takes a layer filter
local OFF = 7 -- kLayerOff
local uib = require("ui")

local function box(name, pos, half, comp, fields)
    local shape = scene.entity()
    scene.replace(shape, "BoxShape", { half = half })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })
    local e = scene.entity(name)
    scene.replace(e, "Transform", { pos = pos })
    fields.colliders = { col }
    scene.replace(e, comp, fields)
    return e
end

local tick = 0

function _on_ready(self)
    -- ray paths: A (layer 0) below B (layer 1) at x=0; default-layer box D at x=-3;
    -- UI window (layer 2) at x=8; physics rig off to the side at x=5
    box("boxA", { 0, 2, 0 }, { 0.5, 0.5, 0.5 }, "KinematicBody", { layer = 0 })
    box("boxB", { 0, 4, 0 }, { 0.5, 0.5, 0.5 }, "KinematicBody", { layer = 1 })
    box("boxD", { -3, 2, 0 }, { 0.5, 0.5, 0.5 }, "KinematicBody", {}) -- no layer field: default
    uib.make_window("t", 200, 200, { 8, 2, 0 }) -- canvas collider lands on layer 2 (0.44 m slab)
    box("ground", { 5, -0.5, 0 }, { 2, 0.5, 2 }, "KinematicBody", {})
    box("drop", { 5, 2, 0 }, { 0.5, 0.5, 0.5 }, "RigidBody", { mass = 1 })
    box("excGround", { -6, -0.5, 0 }, { 2, 0.5, 2 }, "KinematicBody", {})
    box("excDrop", { -6, 2, 0 }, { 0.5, 0.5, 0.5 }, "RigidBody", { mass = 1 })
end

local function rayAt(x, layer)
    local h = physics.raycast({ x, 10, 0 }, { 0, -1, 0 }, 20, nil, layer)
    return h and h.pos[2] or nil
end

function _physics(self, dt)
    tick = tick + 1
    if tick == 2 then -- bodies exist after the first project()
        t.check("unfiltered ray hits topmost box B", math.abs(rayAt(0, nil) - 4.5) < 0.01, tostring(rayAt(0, nil)))
        t.eq("layer=0 ray hits box A", rayAt(0, 0), 2.5, 1e-3)
        t.eq("layer=1 ray hits box B", rayAt(0, 1), 4.5, 1e-3)
        t.check("off-layer (7) ray hits nothing", rayAt(0, OFF) == nil)
        t.eq("default body is on layer 1", rayAt(-3, 1), 2.5, 1e-3)
        t.check("layer=0 ray misses the default body", rayAt(-3, 0) == nil)
        t.eq("UI window is on layer 2", rayAt(8, 2), 2.22, 1e-3)
        t.eq("unfiltered ray hits the UI window too", rayAt(8, nil), 2.22, 1e-3)
        physics.collision_exception(scene.get_entity("excDrop"), scene.get_entity("excGround"))
    end
    if tick == 220 then -- 240 Hz ticks: fall from y=2 settles by ~tick 150
        local y = scene.get_pose("drop").pos[2]
        t.check("dropper rests on ground (y~0.5)", math.abs(y - 0.5) < 0.15, tostring(y))
        scene.set("drop.RigidBody.layer", OFF) -- switch the dropper off its collision layer
    end
    if tick == 400 then
        local y = scene.get_pose("drop").pos[2]
        t.check("off-layer dropper fell through ground (y < -1)", y < -1, tostring(y))
        local ey = scene.get_pose("excDrop").pos[2]
        t.check("collision_exception pair fell through (y < -1)", ey < -1, tostring(ey))
        t.done()
    end
end
