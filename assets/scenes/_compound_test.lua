-- throwaway: exercises compound + offset + rotated colliders
local M = {}

function M._on_ready(self)
    local g = scene.entity("ground")
    render.mesh(g, render.geometry(mesh.box({ 20, 1, 20 })), "mat_g", { 0, -1, 0 })
    physics.body(g, { { box = {20, 1, 20} } }, { static = 1 })
    soft.floor(0.0001)

    -- compound: capsule + box on top
    local a = scene.entity("a")
    render.mesh(a, render.geometry(mesh.capsule({ halfLen = 0.5, radius = 0.3 })), "mat_g", { -3, 2, 0 })
    physics.body(a, {
        { capsule = {0.5, 0.3} },
        { box = {0.4, 0.3, 0.4}, at = {0, 1.0, 0} },
    })

    -- single offset + rotated capsule (compound in Jolt)
    local b = scene.entity("b")
    render.mesh(b, render.geometry(mesh.capsule({ halfLen = 0.5, radius = 0.3 })), "mat_g", { 3, 3, 0 })
    physics.body(b, { { capsule = {0.5, 0.3}, at = {0, 0.6, 0}, rot = scene.qaxis({0, 0, 1}, math.pi / 2) } })

    -- soft ball above a: must rest on the compound's box
    local bg = mesh.sphere({ subdiv = 4, radius = 0.5 })
    local ball = scene.entity("ball")
    scene.emplace(ball, "Transform", { pos = {-3, 8, 0} })
    soft.body(ball, bg, mesh.ballCons(bg), "mat_g", 2.0)
end

return M
