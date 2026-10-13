-- demo behavior: scattered rigid capsules + cloth/ball soft bodies + ground
-- (mesh is the engine-loaded geometry stdlib global; lights/materials/UI live in demo.json)

local M = {}

local clothH
local pinTime = 0.0
local released = false
local PIN_SECS = 3.0

function M._on_ready(self)
    -- ground: one entity with a visual box + static box collider + soft-body floor
    local ground = scene.entity("ground")
    render.mesh(ground, render.geometry(mesh.box({ 55, 1, 55 })), "mat_ground", { 0, -1, 0 })
    physics.box(ground, { 40, 1, 40 })

    -- 50 scattered dynamic capsules, all sharing one shape, one material, one geometry
    local capShape = physics.shapeCapsule(0.9, 0.5)
    local capGeom = render.geometry(mesh.capsule({ halfLen = 0.9, radius = 0.5 }))
    math.randomseed(12345)
    for i = 1, 50 do
        local px = (math.random() * 2 - 1) * 2
        local py = 4.5 + 3.5 * math.random()
        local pz = (math.random() * 2 - 1) * 2
        local q = scene.qmul(scene.qaxis({0, 1, 0}, math.random() * 2 * math.pi),
                             scene.qaxis({1, 0, 0}, (math.random() * 2 - 1) * 0.9))
        local e = scene.entity()
        render.mesh(e, capGeom, "mat_capsule", {px, py, pz}, q)
        physics.body(e, capShape)
    end

    -- soft bodies; the entity's Transform is the pinned (spawn) pose, the cloth is released after PIN_SECS
    local clothGeom = mesh.plane({ cw = 64, ch = 64, span = 8.0 })
    clothH = scene.entity("cloth")
    scene.set(clothH, "pos", {0, 10, 0})
    soft.body(clothH, clothGeom, mesh.planeCons({ cw = 64, ch = 64, span = 8.0 }), "mat_cloth")
    scene.set(clothH, "pinned", 1)
    local ballGeom = mesh.sphere({ subdiv = 2, radius = 1.5 })
    local ball = scene.entity("ball")
    scene.set(ball, "pos", {0, 12, 0})
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
