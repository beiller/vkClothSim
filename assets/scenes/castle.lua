local M = {}
function M._on_ready(self)
    local mat = scene.entity("mat_ground")
    scene.replace(mat, "Material", { baseColor = {0.35, 0.35, 0.38}, metallic = 0, roughness = 0.95 })
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.box({ 20, 1, 20 }))
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = g })
    scene.replace(ground, "MaterialRef", { material = mat })
    local ch = scene.entity("castle")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/castle/source/castle.glb")
    scene.set_pose("cam", { 0, 1.7, 7 }, { 0, 0, 0, 1 })
end
return M
