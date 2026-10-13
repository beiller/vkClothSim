-- demo behavior: scattered rigid capsules + cloth/ball soft bodies + ground
-- (mesh is the engine-loaded geometry stdlib global; lights/materials/UI live in demo.json)

local M = {}

local clothH
local pinTime = 0.0
local released = false
local PIN_SECS = 3.0

function M._on_ready(self)
    -- ground: static visual box + physics floor (rigid box collider + soft-body floor at the box top)
    render.static(render.geometry(mesh.box({ 55, 1, 55 })), "mat_ground", { 0, -1, 0 }, nil, "ground")
    physics.box({ 40, 1, 40 }, { 0, -1, 0 })

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
        physics.body(capShape, capGeom, "mat_capsule", {px, py, pz}, q)
    end

    -- soft bodies; the cloth is held (pinned) for PIN_SECS then released
    local cloth = mesh.plane({ cw = 64, ch = 64, span = 8.0, y0 = 10.0 })
    clothH = soft.body(cloth, mesh.planeCons({ cw = 64, ch = 64, span = 8.0 }), "mat_cloth", nil, "cloth")
    scene.set(clothH, "pinned", 1)
    local ball = mesh.sphere({ subdiv = 2, radius = 1.5, center = {0, 12, 0} })
    soft.body(ball, mesh.ballCons(ball), "mat_ball", 2.0, "ball")
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
