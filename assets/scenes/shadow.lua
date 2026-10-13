-- shadow test: a lit ball on a ground plane (env, lights, camera and UI live in shadow.json)
local M = {}

function M._on_ready(self)
    local ground = scene.entity("ground")
    render.mesh(ground, render.geometry(mesh.box({ 40, 1, 40 })), "white", { 0, -1, 0 })
    local ball = scene.entity("ball")
    render.mesh(ball, render.geometry(mesh.sphere({ subdiv = 4, radius = 2 })), "plaid", { 0, 3, 0 })
end

return M
