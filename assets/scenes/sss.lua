-- sss test: a subsurface-scattering sphere; the SSS pass blurs only the pre-scatter half of
-- the diffuse term, so the light's specular highlight must stay sharp at any ssssWeight (the
-- 0.35 roughness + the bright point light make a distinct highlight to check against). The
-- patterned sphere checks the pre/post split: freckles/edges stay sharp at low sssSplit and
-- smear at high. The small sphere in front of the big one is the silhouette-bleed test; the
-- back light tests transmission (thin regions glow red when backlit)

local uib = require("ui")

local function put(geom, matRef, pos)
    local g = scene.entity()
    scene.replace(g, "Geometry", geom)
    local e = scene.entity()
    scene.replace(e, "Transform", { pos = pos })
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = matRef })
end

local function mat(name, comp)
    scene.replace(scene.entity(name), "Material", comp)
end

function _on_ready(self)
    mat("mat_ground", { baseColor = {0.25, 0.25, 0.28}, metallic = 0, roughness = 0.95 })

    put(mesh.box({ 20, 1, 20 }), scene.get_entity("mat_ground"), { 0, -2.5, 0 })
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.sphere({ subdiv = 4, radius = 1.5 }))
    local e = scene.entity("sphere")
    scene.replace(e, "Transform", {})
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = scene.get_entity("mat_sss") })

    -- patterned SSS sphere: checkerboard + freckle albedo (the split sharpness test)
    local tex = graphics.load_texture("sss_pattern.png", "sss_pat")
    -- reuse a pre-existing mat_pat (e.g. from JSON, to set ssssSplit) or create it
    local matPat = scene.get_entity("mat_pat") or scene.entity("mat_pat")
    scene.replace(matPat, "Material", { baseColor = { 1, 1, 1 }, metallic = 0, roughness = 0.35,
                                       ssssWeight = { 1, 1, 1 }, tex = tex })
    put(mesh.sphere({ subdiv = 4, radius = 1.0 }), matPat, {-2.6, -0.4, 0.8})

    -- silhouette-bleed test: a small SSS sphere in front of the big one (edge-on)
    put(mesh.sphere({ subdiv = 4, radius = 0.55 }), scene.get_entity("mat_sss"), { 1.2, -0.6, 1.9 })

    -- back light: transmission test (thin regions glow red when backlit)
    local bl = scene.entity()
    scene.replace(bl, "Transform", { pos = {-1.5, 0.5, -3} })
    scene.replace(bl, "PointLight", { color = { 1, 0.6, 0.4 }, intensity = 30, radius = 0.1 })

    self.metricsText = uib.text(uib.make_window("metrics", 160, 180, { 0.45, 1.72, 2.8 }), "")
end

-- refresh the metrics window from the engine's performance string every frame
function _animate(self, dt)
    local t = scene.component(self.metricsText, "UIText")
    if t then
        t.label = engine.performance_metrics()
    end
end