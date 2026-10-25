-- glass test: a refractive sphere in front of colored content (env, light, camera in glass.json)
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
    mat("mat_ground", { baseColor = {0.4, 0.4, 0.45}, metallic = 0, roughness = 0.95 })
    mat("mat_red", { baseColor = {1, 0.1, 0.1}, metallic = 0, roughness = 0.6 })
    mat("mat_blue", { baseColor = {0.1, 0.2, 1}, metallic = 0, roughness = 0.6 })
    mat("mat_yellow", { baseColor = {1, 0.9, 0.1}, metallic = 0, roughness = 0.6 })

    put(mesh.box({ 20, 1, 20 }), "mat_ground", { 0, -2.7, 0 })
    put(mesh.box({ 0.6, 4, 0.6 }), "mat_red", { 1.5, 0, -4 })
    put(mesh.box({ 0.6, 4, 0.6 }), "mat_blue", { -1.5, 0, -4 })
    put(mesh.sphere({ subdiv = 3, radius = 0.8 }), "mat_yellow", { 0, 1.2, -5 })

    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.sphere({ subdiv = 4, radius = 2 }))
    local e = scene.entity("sphere")
    scene.replace(e, "Transform", {})
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = scene.get_entity("mat_glass") })
end

return M
