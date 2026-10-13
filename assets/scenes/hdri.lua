-- hdri sphere test: a single reflective sphere (env, lights, camera and UI live in hdri.json)
local M = {}

function M._on_ready(self)
    render.static(render.geometry(mesh.sphere({ subdiv = 4, radius = 2 })), "mat_sphere", nil, nil, "sphere")
end

return M
