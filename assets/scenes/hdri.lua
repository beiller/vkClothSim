-- hdri sphere test: a single reflective sphere (env, lights, camera and UI live in hdri.json)

function _on_ready(self)
    local g = scene.entity()
    scene.replace(g, "Geometry", mesh.sphere({ subdiv = 4, radius = 2 }))
    local e = scene.entity("sphere")
    scene.replace(e, "Transform", {})
    scene.replace(e, "Renderable", { geom = g })
    scene.replace(e, "MaterialRef", { material = scene.get_entity("mat_sphere") })
end

