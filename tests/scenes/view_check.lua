-- Live component view semantics: nil when absent, explicit emplace, live re-read
-- through the same table, vector/quat/array round-trips, error paths.

function _on_ready()
    local g = scene.entity()

    -- the view of an absent component is nil; emplace creates it
    t.check("nil when absent", scene.component(g, "RigidBody") == nil)
    scene.emplace(g, "RigidBody")
    local v = scene.component(g, "RigidBody")
    t.check("view after emplace", v ~= nil)
    v.mass = 7.5
    t.eq("read back mass", v.mass, 7.5)

    -- the same view sees a write made through scene.replace (not a snapshot)
    scene.replace(g, "RigidBody", { mass = 3.25 })
    t.eq("live after replace", v.mass, 3.25)

    local e2 = scene.entity()
    t.check("absent view nil", scene.component(e2, "RigidBody") == nil)

    -- vector + quat round-trip through the view
    scene.emplace(e2, "Transform")
    local tp = scene.component(e2, "Transform")
    tp.pos = { 1, 2, 3 }
    t.eq("pos x", tp.pos[1], 1)
    t.eq("pos y", tp.pos[2], 2)
    t.eq("pos z", tp.pos[3], 3)
    tp.quat = { 0.7071, 0, 0, 0.7071 }
    t.eq("quat x", tp.quat[1], 0.7071)
    t.eq("quat w", tp.quat[4], 0.7071)

    -- checkbox + entity array through the view
    v.forceSleep = true
    t.check("checkbox write", v.forceSleep == true)
    t.eq("colliders count", #v.colliders, 0)

    -- string array round-trip (joints left empty keeps bindSkeleton happy)
    local skel = scene.entity()
    scene.emplace(skel, "Skeleton")
    local s = scene.component(skel, "Skeleton")
    s.boneNames = { "A", "B" }
    t.eq("boneNames[1]", s.boneNames[1], "A")
    t.eq("boneNames count", #s.boneNames, 2)

    -- error paths
    t.check("unknown type errors", not pcall(scene.component, g, "Bogus"))
    local e3 = scene.entity()
    local w = scene.component(e3, "Transform")
    scene.destroy(e3)
    t.check("write to removed entity errors", not pcall(function() w.pos = { 0, 0, 0 } end))
    scene.remove(g, "RigidBody")
    t.check("write after remove errors", not pcall(function() v.mass = 1.0 end))

    t.done()
end
