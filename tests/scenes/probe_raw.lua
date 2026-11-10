function _on_ready()
    local e = scene.entity()
    local rb = scene.component(e, "RigidBody")
    t.check("auto created with defaults", scene.has(e, "RigidBody"))
    rb.mass = 5.0
    t.eq("held ref reads back", rb.mass, 5.0)
    t.done()
end
