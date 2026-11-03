local M = {}
local tick = 0

local function qp(q)
    return string.format("{%.3f,%.3f,%.3f,%.3f}", q[1], q[2], q[3], q[4])
end

local Qh, Qo
local bA, bB, bC

function M._on_ready(self)
    Qh = math.qaxis({ 0, 1, 0 }, 0.5)
    Qo = math.qaxis({ 1, 0, 0 }, 0.9)
    local function dyn(name, px, quat)
        local s = scene.entity()
        scene.replace(s, "BoxShape", { half = { 0.5, 0.5, 0.5 } })
        local c = scene.entity()
        scene.replace(c, "Collider", { shape = s })
        local e = scene.entity(name)
        scene.replace(e, "Transform", { pos = { px, 5, 0 }, quat = quat })
        scene.replace(e, "RigidBody", { colliders = { c } })
        return e
    end
    bA = dyn("bA", 0, Qh)
    bB = dyn("bB", 3, Qo)
    bC = dyn("bC", 6, { 1, 0, 0, 0 })
end

function M._physics(self, dt)
    tick = tick + 1
    if tick == 10 or tick == 40 then
        print(string.format("DYNQ t=%d bA ecs=%s world=%s (set Qh=%s)", tick,
            qp(scene.component(bA, "Transform").quat), qp(scene.world_pose(bA).quat), qp(Qh)))
        print(string.format("DYNQ t=%d bB ecs=%s world=%s (set Qo=%s)", tick,
            qp(scene.component(bB, "Transform").quat), qp(scene.world_pose(bB).quat), qp(Qo)))
        print(string.format("DYNQ t=%d bC ecs=%s world=%s (set I={1,0,0,0})", tick,
            qp(scene.component(bC, "Transform").quat), qp(scene.world_pose(bC).quat)))
    end
end

return M
