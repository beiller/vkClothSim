-- demo behavior: scattered rigid capsules + cloth/ball soft bodies + ground
-- (mesh is the engine-loaded geometry stdlib global; lights/materials/UI live in demo.json)

local M = {}

local clothH
local pinTime = 0.0
local released = false
local PIN_SECS = 3.0

local world_scale = 0.666

function M._on_ready(self)
    -- ground: one entity with a visual box + static box collider; the soft sim's floor is explicit
    local ground = scene.entity("ground")
    render.mesh(ground, render.geometry(mesh.box({ 55, 1, 55 })), "mat_ground", { 0, -1, 0 })
    physics.body(ground, { {shape = physics.box(40, 1, 40)} }, { static = 1 })
    soft.floor(0.0001)

    -- 50 scattered dynamic capsules, all sharing one shape, one material, one geometry
    local cap_length = 0.9 * world_scale
    local cap_r = 0.5 * world_scale
    local cap = physics.capsule(cap_length, cap_r)
    local capGeom = render.geometry(mesh.capsule({ halfLen = cap_length, radius = cap_r }))
    math.randomseed(12345)
    for i = 1, 50 do
        local px = (math.random() * 2 - 1) * 2 * world_scale
        local py = 4.5 + 3.5 * math.random()
        local pz = (math.random() * 2 - 1) * 2 * world_scale
        local q = scene.qmul(scene.qaxis({0, 1, 0}, math.random() * 2 * math.pi),
                             scene.qaxis({1, 0, 0}, (math.random() * 2 - 1) * 0.9))
        local e = scene.entity()
        render.mesh(e, capGeom, "mat_capsule", {px, py, pz}, q)
        physics.body(e, { {shape = cap} })
    end

    -- soft bodies; the entity's Transform is the pinned (spawn) pose, the cloth is released after PIN_SECS
    local clothSpan = 8.0 * world_scale
    local clothDim = 128
    local clothGeom = mesh.plane({ cw = clothDim, ch = clothDim, span = clothSpan })
    clothH = scene.entity("cloth")
    scene.emplace(clothH, "Transform", { pos = {0, 10, 0} })
    soft.body(clothH, clothGeom, mesh.planeCons({ cw = clothDim, ch = clothDim, span = clothSpan }), "mat_cloth", 1.0)
    scene.set(clothH, "pinned", 1)

    local ballGeom = mesh.sphere({ subdiv = 4, radius = 1.5 * world_scale })
    local ball = scene.entity("ball")
    scene.emplace(ball, "Transform", { pos = {0, 12, 0} })
    soft.body(ball, ballGeom, mesh.ballCons(ballGeom), "mat_ball", 2.0)
end

function M._physics(self, dt)
    if released then
        return
    end
    pinTime = pinTime + dt
    if pinTime >= PIN_SECS then
        scene.set(clothH, "pinned", 0)
        released = true
    end
end

return M
