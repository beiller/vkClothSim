-- shadow test: a lit ball on a ground plane (env, lights, camera and UI live in shadow.json)
local M = {}

function M._on_ready(self)
    scene.static(scene.geometry(mesh.ground({ size = 40 })), "white", nil, nil, "ground")
    scene.static(scene.geometry(mesh.sphere({ subdiv = 4, radius = 2 })), "plaid", { 0, 3, 0 }, nil, "ball")
end

return M
