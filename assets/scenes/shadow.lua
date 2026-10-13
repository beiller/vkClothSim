-- shadow test: a lit ball on a ground plane (env, lights, camera and UI live in shadow.json)
local M = {}

function M._on_ready(self)
    render.static(render.geometry(mesh.box({ 40, 1, 40 })), "white", { 0, -1, 0 }, nil, "ground")
    render.static(render.geometry(mesh.sphere({ subdiv = 4, radius = 2 })), "plaid", { 0, 3, 0 }, nil, "ball")
end

return M
