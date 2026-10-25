-- sss test: a subsurface-scattering sphere; the SSS pass blurs only the diffuse term, so the
-- light's specular highlight must stay sharp at any ssssWeight (the 0.35 roughness + the
-- bright point light make a distinct highlight to check against)
local M = {}

local function put(geom, matName, pos)
    local g = scene.entity()
    scene.replace(g, "Geometry", geom)
    local e = scene.entity()
    scene.replace(e, "Transform", { pos = pos })
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = scene.get_entity(matName) })
end

local function mat(name, comp)
    scene.replace(scene.entity(name), "Material", comp)
end

function M._on_ready(self)
    mat("mat_ground", { baseColor = {0.25, 0.25, 0.28}, metallic = 0, roughness = 0.95 })

    put(mesh.box({ 20, 1, 20 }), "mat_ground", { 0, -2.5, 0 })
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.sphere({ subdiv = 4, radius = 1.5 }))
    local e = scene.entity("sphere")
    scene.replace(e, "Transform", {})
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = scene.get_entity("mat_sss") })
end

return M
