-- Joint inspector: puts a small sphere on every joint. Markers are children of the joint
-- entities, so they follow the bones when posed. (The character mesh stays visible; to hide a
-- prim, zero its Renderable's visible field.)
local M = {}

-- stdlib geometry tables are the Geometry struct itself: stash one in a geometry entity
local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function M._on_ready(self)
    local matGround = scene.entity("mat_ground")
    scene.replace(matGround, "Material", { baseColor = {0.35, 0.35, 0.38}, metallic = 0, roughness = 0.95 })
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 20, 1, 20 })) })
    scene.replace(ground, "MaterialRef", { material = matGround })

    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/hashimoto.glb")

    local matJoint = scene.entity("mat_joint")
    scene.replace(matJoint, "Material", { baseColor = {1, 0.15, 0.15}, metallic = 0, roughness = 0.4,
                                          emissive = {1, 0.15, 0.15} })
    local g = geom(mesh.sphere({ subdiv = 2, radius = 0.015 }))
    local skel = scene.find_components("Skeleton", ch)[1]
    for _, j in ipairs(scene.component(skel, "Skeleton").joints) do
        local m = scene.entity()
        scene.replace(m, "Transform", {})
        scene.replace(m, "Renderable", { geom = g, noShadow = true })
        scene.replace(m, "MaterialRef", { material = matJoint })
        scene.add_child(j, m)
    end
end
return M
