local M = {}
function M._on_ready(self)
    render.material("ground", {0.35, 0.35, 0.38}, 0, 0.95)
    render.mesh(scene.entity("ground"), render.geometry(mesh.box({ 20, 1, 20 })), "ground", { 0, -1, 0 })
    local ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/basegen8.glb")
end
return M
