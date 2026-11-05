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
    makeCube("cube_png", { -3, 1, 0 }, graphics.load_texture("cloth_albedo.png", "tex_png"))
    makeCube("cube_rt", { 3, 1, 0 }, graphics.render_texture(256, 256, "rt"))
    local tmp = graphics.load_texture("cloth_albedo.png")
    scene.destroy(tmp)
    scene.replace(scene.get_entity("env"), "Environment", { tex = graphics.load_texture("fly-studio-03_1K.exr", "env2") })

    -- minimal verdict: the texture ops above must have produced live entities (requires graphics)
    local tp = scene.component(scene.get_entity("tex_png"), "Texture")
    t.check("png tex loaded", tp and tp.path == "cloth_albedo.png", tp and tp.path or "nil")
    -- GPUTexture is not Lua-visible; render_texture errors on failure, so the named entity existing is the check
    t.check("rt created", scene.get_entity("rt"))
    t.done()
end

return M
