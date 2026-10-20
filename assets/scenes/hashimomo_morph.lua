-- Hashimomo morph test: one slider drives one shape key through the skeleton.
-- The write fans out to every mesh that has that key; a mesh without it is a no-op.
local M = {}
local SHAPES = { "FHMHashimoto", "FBMThin", "Youth Weight", "Kim Head" }
local last = { i = 0, w = -1 }
local ch = nil

local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function M._on_ready()
    ch = scene.entity("char")
    scene.replace(ch, "Transform", { pos = { 0, 0, 0 } })
    render.gltf(ch, "local/hashimoto.glb")

    local mat = scene.entity("mat_ground")
    scene.replace(mat, "Material", { baseColor = { 0.35, 0.35, 0.38 }, metallic = 0, roughness = 0.95 })
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 20, 1, 20 })) })
    scene.replace(ground, "MaterialRef", { material = mat })

    -- the "ui_state" entity is a named, unrendered bind target: the sliders write to
    -- its Material fields, which _animate reads back with scene.get
    local ui = scene.entity("ui_state")
    scene.emplace(ui, "Material", { metallic = 1, roughness = 0.85 })
    local win = scene.ui_window("morph", 300, 110)
    scene.ui_text(win, "shape key -> all meshes that have it")
    scene.ui_slider_f(win, "shape #", "ui_state.Material.metallic", 1, #SHAPES, "%.0f")
    scene.ui_slider_f(win, "weight", "ui_state.Material.roughness", 0, 1, "%.2f")
end

function M._animate()
    local i = math.floor(scene.get("ui_state.Material.metallic"))
    i = math.max(1, math.min(#SHAPES, i))
    local w = scene.get("ui_state.Material.roughness")
    if i == last.i and w == last.w then
        return
    end
    last.i, last.w = i, w
    scene.set_morph(ch, SHAPES[i], w)
    print("morph: " .. SHAPES[i] .. " = " .. string.format("%.3f", w))
end

return M
