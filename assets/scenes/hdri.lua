-- hdri sphere test: a single reflective sphere (env, lights, camera and UI live in hdri.json)
local M = {}

function M._on_ready(self)
    local e = scene.entity("sphere")
    render.mesh(e, render.geometry(mesh.sphere({ subdiv = 4, radius = 2 })), "mat_sphere")
end

return M
