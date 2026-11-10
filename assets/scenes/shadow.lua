-- shadow test: a lit ball on a ground plane (env, lights, camera and UI live in shadow.json)

-- stdlib geometry tables are the Geometry struct itself: stash one in a geometry entity
local function geom(t)
    local g = scene.entity()
    scene.replace(g, "Geometry", t)
    return g
end

function _on_ready(self)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    scene.replace(ground, "Renderable", { geom = geom(mesh.box({ 40, 1, 40 })) })
    scene.replace(ground, "MaterialRef", { material = scene.get_entity("white") })
    local ball = scene.entity("ball")
    scene.replace(ball, "Transform", { pos = { 0, 3, 0 } })
    scene.replace(ball, "Renderable", { geom = geom(mesh.sphere({ subdiv = 4, radius = 2 })) })
    scene.replace(ball, "MaterialRef", { material = scene.get_entity("plaid") })
end

