-- runtime texture path test: load_texture (file), render_texture (standalone target),
-- destroy (free), and an environment swap (forces a full env rebuild)
local M = {}

local function makeCube(name, pos, tex)
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.box({ 1, 1, 1 }))
    local e = scene.entity(name)
    scene.replace(e, "Transform", { pos = pos })
    scene.replace(e, "Renderable", { geom = g })
    local m = scene.entity()
    scene.replace(m, "Material", { baseColor = { 1, 1, 1 }, roughness = 1.0, tex = tex })
    scene.replace(e, "MaterialRef", { material = m })
end

function M._on_ready(self)
    makeCube("cube_png", { -3, 1, 0 }, scene.load_texture("cloth_albedo.png", "tex_png"))
    makeCube("cube_rt", { 3, 1, 0 }, scene.render_texture(256, 256, "rt"))
    local tmp = scene.load_texture("cloth_albedo.png")
    scene.destroy(tmp)
    scene.replace(scene.find("env"), "Environment", { tex = scene.load_texture("fly-studio-03_1K.exr", "env2") })
end

return M
