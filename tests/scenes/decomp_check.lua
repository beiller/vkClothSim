local tick = 0
local lastY = nil

function _on_ready(self)
    -- ground (top at y=0)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    -- load a glTF (entity form) and decompose one of its meshes with CoACD
    local root = scene.entity("model")
    scene.replace(root, "Transform", { pos = { 0, 0, 0 } })
    scene.spawn(graphics.gltf("sword_collider.glb"), root)
    local meshes = scene.find_components("Geometry", root)
    t.check("mesh found under model", #meshes > 0)
    if #meshes == 0 then
        t.done()
        return
    end
    -- some meshes in the glb are not 2-manifold (CoACD rejects them): try each until one decomposes
    local hulls = nil
    for i = 1, #meshes do
        local ok, h = pcall(physics.convex_decompose, meshes[i], 24, 64, 0.05)
        if ok and type(h) == "table" and #h > 0 then
            hulls = h
            print(string.format("DECOMP decomposed mesh %d of %d", i, #meshes))
            break
        end
    end
    t.check("hulls produced", hulls ~= nil, string.format("no mesh of %d decomposed", #meshes))
    if not hulls then
        t.done()
        return
    end
    print(string.format("DECOMP hulls=%d", #hulls))
    for i = 1, math.min(#hulls, 8) do
        print(string.format("DECOMP hull %d: %d verts", i, #hulls[i]))
    end

    -- build a RigidBody from the decomposed convex hulls (mesh-local space)
    local e = scene.entity("decompBody")
    scene.replace(e, "Transform", { pos = { 0, 30, 0 } })
    local cols = {}
    for _, h in ipairs(hulls) do
        local s = scene.entity()
        scene.replace(s, "ConvexHullShape", { points = h })
        local c = scene.entity()
        scene.replace(c, "Collider", { shape = s })
        table.insert(cols, c)
    end
    scene.replace(e, "RigidBody", { colliders = cols })
end

function _physics(self, dt)
    tick = tick + 1
    if tick % 30 == 0 then
        local y = scene.get_pose("decompBody").pos[2]
        print(string.format("DECOMP t=%d bodyY=%.3f", tick, y))
        lastY = y
    end
    if tick == 600 then -- lastY is the value 30 ticks earlier
        local y = scene.get_pose("decompBody").pos[2]
        t.check("body resting above ground", lastY and y > 0 and math.abs(y - lastY) < 0.05,
                string.format("y=%.3f lastY=%s", y, tostring(lastY)))
        t.done()
    end
end

