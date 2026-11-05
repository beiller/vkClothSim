-- Tapered-capsule check: component round-trip + validation, and dropped tapered capsules
-- settling at the analytically expected height on a static ground. Jolt's shape is a
-- frustum with sphere caps (radius bottomRadius at -Y*halfHeight, topRadius at +Y*halfHeight),
-- so an upright body rests at y = halfHeight + bottomRadius and an inverted one at
-- y = halfHeight + topRadius. A 45-deg-tilted drop rolls over to its side (min caliper
-- ~2*bottomRadius), so it is only checked to rest on the ground without sinking or floating.
local M = {}
local tick = 0

local kTapers = {
    { halfHeight = 0.5, bottomRadius = 0.25, topRadius = 0.15 }, -- taper to top
    { halfHeight = 0.4, bottomRadius = 0.3, topRadius = 0.3 }, -- uniform (plain capsule)
    { halfHeight = 0.7, bottomRadius = 0.15, topRadius = 0.45 }, -- taper to bottom
}

local bodies = {}
local y280 = {}

function M._on_ready()
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    local gs = scene.entity()
    scene.replace(gs, "BoxShape", { half = { 40, 1, 40 } })
    local gc = scene.entity()
    scene.replace(gc, "Collider", { shape = gs })
    scene.replace(ground, "KinematicBody", { colliders = { gc } })

    -- component round-trip + validation
    local rt = scene.entity("tcap_rt")
    local ok, err = pcall(scene.replace, rt, "TaperedCapsule", kTapers[1])
    t.check("replace ok", ok, err)
    local c = scene.component(rt, "TaperedCapsule")
    t.eq("halfHeight readback", c.halfHeight, 0.5)
    t.eq("bottomRadius readback", c.bottomRadius, 0.25)
    t.eq("topRadius readback", c.topRadius, 0.15)
    local bad = pcall(scene.replace, scene.entity(), "TaperedCapsule",
                     { halfHeight = 0.5, bottomRadius = 0, topRadius = 0.2 })
    t.check("zero radius rejected", not bad)

    -- physics: one upright body per param set + inverted + 45-deg tilt, all dropped from y=3
    local function drop(name, tp, quat, x)
        local shape = scene.entity()
        scene.replace(shape, "TaperedCapsule", tp)
        local col = scene.entity()
        scene.replace(col, "Collider", { shape = shape })
        local b = scene.entity(name)
        scene.replace(b, "Transform", { pos = { x, 3, 0 }, quat = quat })
        scene.replace(b, "RigidBody", { colliders = { col } })
        bodies[name] = b
    end
    drop("up1", kTapers[1], { 0, 0, 0, 1 }, -2.5) -- rests at h + rb = 0.75
    drop("up2", kTapers[2], { 0, 0, 0, 1 }, 0) -- rests at h + r = 0.70
    drop("up3", kTapers[3], { 0, 0, 0, 1 }, 2.5) -- rests at h + rb = 0.85
    drop("inv", kTapers[1], math.qaxis({ 1, 0, 0 }, math.pi), 7) -- top down: h + rt = 0.65
    drop("tilt", kTapers[1], math.qaxis({ 1, 0, 0 }, math.pi / 4), -7) -- rolls to a side; loose band
end

function M._physics(self, dt)
    tick = tick + 1
    if tick == 280 then
        for n, b in pairs(bodies) do y280[n] = scene.world_pose(b).pos[2] end
    elseif tick == 300 then
        local maxD = 0
        for n, b in pairs(bodies) do
            local y = scene.world_pose(b).pos[2]
            maxD = math.max(maxD, math.abs(y - y280[n]))
            print(string.format("TCAP %s y=%.3f", n, y))
        end
        t.eq("up1 rest h+rb", scene.world_pose(bodies.up1).pos[2], 0.75, 0.05)
        t.eq("up2 rest h+r", scene.world_pose(bodies.up2).pos[2], 0.70, 0.05)
        t.eq("up3 rest h+rb", scene.world_pose(bodies.up3).pos[2], 0.85, 0.05)
        t.eq("inv rest h+rt", scene.world_pose(bodies.inv).pos[2], 0.65, 0.05)
        local ty = scene.world_pose(bodies.tilt).pos[2]
        t.check("tilt fell and rests on ground (0.05..2.0)", ty > 0.05 and ty < 2.0, string.format("y=%.3f", ty))
        t.check("all settled (280->300 drift < 0.01)", maxD < 0.01, string.format("max drift %.4f", maxD))
        t.done()
    end
end

return M
