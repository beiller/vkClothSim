-- SixDofJoint test: a box welded to the world (all axes fixed) must not fall;
-- a Y-pinned box floats but is free in X/Z and rotation; a control box falls.
local M = {}

local kFree = 1e30

local function boxBody(name, px, py, pz, mat)
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.box({ 0.5, 0.5, 0.5 }))
    local shape = scene.entity()
    scene.replace(shape, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })
    local e = scene.entity(name)
    scene.replace(e, "Transform", { pos = { px, py, pz } })
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = mat })
    scene.replace(e, "RigidBody", { colliders = { col } })
    return e
end

local function y(e)
    return scene.get_pose(e)[1][2]
end

local welded, slider, control, lim, motored
local n = 0

function M._on_ready(self)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = (function()
        local g = scene.entity()
        scene.replace(g, "Geometry", mesh.box({ 55, 1, 55 }))
        return g
    end)() })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("mat_ground") })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 40, 1, 40 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    welded = boxBody("weld", 0, 1.0, 0, scene.get_entity("mat_red"))
    local jw = scene.entity()
    scene.replace(jw, "SixDofJoint", {
        bodyA = welded, grabPos = { 0, 1.0, 0 },
        limOn = 1,
        limTransMin = { 0, 0, 0 }, limTransMax = { 0, 0, 0 },
        limAngMin = { 0, 0, 0 }, limAngMax = { 0, 0, 0 } })

    slider = boxBody("slide", 2, 1.0, 0, scene.get_entity("mat_blue"))
    local js = scene.entity()
    scene.replace(js, "SixDofJoint", {
        bodyA = slider, grabPos = { 2, 1.0, 0 },
        limOn = 1,
        limTransMin = { -kFree, 0, -kFree }, limTransMax = { kFree, 0, kFree },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree } })

    control = boxBody("free", -2, 1.0, 0, scene.get_entity("mat_ground"))

    -- finite Y limit only (no motor): must hold the box at y = 1.0
    lim = boxBody("lim", 6, 1.0, 0, scene.get_entity("mat_blue"))
    local jl = scene.entity()
    scene.replace(jl, "SixDofJoint", {
        bodyA = lim, grabPos = { 6, 1.0, 0 },
        limOn = 1,
        limTransMin = { -1, 0, -1 }, limTransMax = { 1, 1.1, 1 },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree } })

    -- position motor on TranslationY drives the box to y = 1.0 + 0.5
    motored = boxBody("motor", 4, 1.0, 0, scene.get_entity("mat_orange"))
    local jm = scene.entity()
    scene.replace(jm, "SixDofJoint", {
        bodyA = motored, grabPos = { 4, 1.0, 0 },
        limOn = 1,
        limTransMin = { -1, 0, -1 }, limTransMax = { 1, 1.1, 1 },
        limAngMin = { -kFree, -kFree, -kFree }, limAngMax = { kFree, kFree, kFree },
        motorState = { 0, 2, 0, 0, 0, 0 },
        motorFreq = { 0, 8, 0 }, motorDamp = { 0, 6, 0 },
        motorTargetPos = { 0, 0.5, 0 } })
end

function M._animate(self, dt)
    n = n + 1
    if n % 30 == 0 then
        print(string.format("sixdof: weld=%.3f slide=%.3f free=%.3f lim=%.3f (want 1.000) mot=%.3f (want 1.500)",
                          y(welded), y(slider), y(control), y(lim), y(motored)))
    end
end

return M
