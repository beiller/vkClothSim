local M = {}
local tick = 0
local lastY = nil

function M._on_ready(self)
    -- ground (top at y=0)
    local ground = scene.entity("ground")
    scene.replace(ground, "Transform", { pos = { 0, -1, 0 } })
    local gShape = scene.entity()
    scene.replace(gShape, "BoxShape", { half = { 55, 1, 55 } })
    local gCol = scene.entity()
    scene.replace(gCol, "Collider", { shape = gShape })
    scene.replace(ground, "KinematicBody", { colliders = { gCol } })

    -- load a glTF and decompose its first mesh with CoACD
    local root = scene.entity("model")
    scene.replace(root, "Transform", { pos = { 0, 0, 0 } })
    scene.spawn(graphics.gltf("local/basegen8.glb"), root)
    local meshes = scene.find_components("Geometry", root)
    if #meshes == 0 then
        print("DECOMP FAIL: no Geometry mesh under model")
        return
    end
    local hulls = physics.convex_decompose(meshes[1], 24, 64, 0.05)
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

function M._physics(self, dt)
    tick = tick + 1
    if tick % 30 == 0 then
        local y = scene.get_pose("decompBody").pos[2]
        print(string.format("DECOMP t=%d bodyY=%.3f", tick, y))
        if lastY and math.abs(y - lastY) < 0.05 and y > 0 then
            print("DECOMP RESTING (stable above ground)")
        end
        lastY = y
    end
end

return M
